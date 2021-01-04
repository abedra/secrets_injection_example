# Secrets Management: Transparent Secret Injection

## Introduction

Making the change to proper secrets management in your software can be a daunting task. The associated lift can be enough to make a team postpone the choice indefinitely. This post provides some design ideas that should help ease the burden. We will set the following goals:

* Seamlessly swap out real secret values with references to secrets in another system
* Allow real secrets to continue to be used in the case of secrets management system failure

Having the ability to "break glass in case of emergency" should be considered a design requirement. It's something that should only be used under dire circumstances, but it's always important to remember that availability rests at the core of security. The examples in this post can be referenced in their entirety at [https://github.com/abedra/secrets_injection_example](https://github.com/abedra/secrets_injection_example).

### Really, C++?

The language in this example is mostly irrelevant. The following ideas work in any language with a JSON library and a PostgreSQL driver. C++ is used here because I am the author of [libvault](https://github.com/abedra/libvault) and typically run through these ideas using this library to ensure it works as intended.

## Our Program

We have a small program. It connects to a PostgreSQL database and checks to verify the connection is open. For this example, that's all it needs to do since we're focusing on the secrets required to make the connetion. 

```cpp
int main(void) {
    std::filesystem::path configPath{"config.json"};

    try {
        DatabaseConfig databaseConfig = getDatabaseConfiguration(configPath);
        pqxx::connection databaseConnection{databaseConfig.connectionString()};
    
        if (databaseConnection.is_open()) {
            std::cout << "Connected" << std::endl;
        } else {
            std::cout << "Could not connect" << std::endl;
        }
    } catch(const std::exception &e) {
        std::cout << e.what() << std::endl;
    }
}
```

More could be done here to isolate the different types of exceptions and pull them out of `main`, but this is done for the sake of brevity. 

### Loading Configuration

The program loads a file by the name of `config.json`. We can assume it contains all of the relevant information required to make our database connection.

```json
{
  "database": {
    "host": "dynamic-secrets-postgres",
    "port": 5432,
    "database": "postgres",
    "username": "postgres",
    "password": "postgres"
  }
}
```

This example will use [nlohmann's json library](https://github.com/nlohmann/json) to deserialize the configuration. Let's start with our basic type:

```cpp
struct DatabaseConfig {
    int port;
    std::string host;
    std::string database;
    std::string username;
    std::string password;

    std::string connectionString() {
        std::stringstream ss;
        ss << "host=" << host << " "
           << "port=" << port << " "
           << "user=" << username << " "
           << "password=" << password << " "
           << "dbname=" << database;
        
        return ss.str();
    }
};
```

Once constructed, this will furnish a connection string usable by the `libpqxx` driver. To construct this we need to load our configuration file and deserialize it:

```cpp
void from_json(const nlohmann::json &j, DatabaseConfig &databaseConfig) {
    j.at("port").get_to(databaseConfig.port);
    j.at("host").get_to(databaseConfig.host);
    j.at("database").get_to(databaseConfig.database);
    j.at("username").get_to(databaseConfig.username);
    j.at("password").get_to(databaseConfig.password);
}

DatabaseConfig getDatabaseConfiguration(const std::filesystem::path &path) {
    std::ifstream inputStream(path.generic_string());
    std::string raw(std::istreambuf_iterator<char>{inputStream}, {});

    return nlohmann::json::parse(raw)["database"];
}
```

This simple custom deserializer is a quick and easy way to get there. We have now hit the point that secrets have entered our configuration. Not everything here should be considered a secret, but we should at the very least conceal the username and password.

## Introducing Vault

HashiCorp Vault is a tool for secrets management. While we are using it for our example, this post will skip an in depth explanation. More can be found on [HashiCorp's Product Page](https://www.hashicorp.com/products/vault). In order to use Vault we need to set it up and add our secrets. We can use a single script to get the vault binary and provide a ready to use environment

```shell
#!/usr/bin/env bash

set -e

VAULT_VERSION=1.6.1

if [[ ! -f "bin/vault" ]]; then
    mkdir -p bin

    pushd bin

    curl -O -L https://releases.hashicorp.com/vault/$VAULT_VERSION/vault_"$VAULT_VERSION"_linux_amd64.zip
    unzip vault_"$VAULT_VERSION"_linux_amd64.zip
    rm vault_"$VAULT_VERSION"_linux_amd64.zip

    popd
fi

export VAULT_ADDR=http://127.0.0.1:8200
VAULT=bin/vault

$VAULT login
$VAULT policy write example vault/example.hcl
$VAULT auth enable approle
$VAULT write auth/approle/role/client policies="example"
ROLE_ID=$($VAULT read auth/approle/role/client/role-id | grep role_id | awk '{print $2}')
SECRET_ID=$($VAULT write -f auth/approle/role/client/secret-id | grep -m1 secret_id | awk '{print $2}')
$VAULT kv put secret/database vault:dbuser=postgres vault:dbpass=postgres

rm -f .env

echo "APPROLE_ROLE_ID=$ROLE_ID" >> .env
echo "APPROLE_SECRET_ID=$SECRET_ID" >> .env
```

This will leave us with a `.env` file that we can use in our program to provide the Vault authentiation credentials

### Adding Vault to our Program

In order lookup our secrets, we need a way to interface with Vault. We will do this using [libvault](https://github.com/abedra/libvault). First, let's get an instance of `Vault::Client`

```cpp
Vault::Client getVaultClient() {
  char *roleId = std::getenv("APPROLE_ROLE_ID");
  char *secretId = std::getenv("APPROLE_SECRET_ID");

  if (!roleId && !secretId) {
    std::cout << "APPROLE_ROLE_ID and APPROLE_SECRET_ID environment variables must be set" << std::endl;
    exit(-1);
  }

  Vault::AppRoleStrategy appRoleStrategy{Vault::RoleId{roleId}, Vault::SecretId{secretId}};
  Vault::Config config = Vault::ConfigBuilder()
                             .withHost(Vault::Host{"dynamic-secrets-vault"})
                             .withTlsEnabled(false)
                             .build();

  return Vault::Client{config, appRoleStrategy};
}
```

This will pickup the `APPROLE_ROLE_ID` and `APPROLE_SECRET_ID` environment variables and use them to authenticate to Vault. These are automatically passed in in the example using the `--env-file` argument to Docker. The secret bootstrapping problem is something that deserves its own detailed discussion.

## Introducing Secret References

Now that we have the ability to communicate with Vault, we need to modify our configuration to reference the location in of the secret we wish to consume. 

```json
{
  "database": {
    "host": "dynamic-secrets-postgres",
    "port": 5432,
    "database": "postgres",
    "username": "vault:dbuser",
    "password": "vault:dbpass"
  }
}
```

We have swapped out our real secrets with the key used in Vault that idendifies the secret. Let's add a simple replacement mechanism into `DatabaseConfig`:

```cpp
  DatabaseConfig withSecrets(const Vault::Client &vaultClient) {
    Vault::KeyValue kv{vaultClient};
    auto databaseSecrets = kv.read(Vault::Path{"database"});
    if (databaseSecrets) {
      std::unordered_map<std::string, std::string> secrets =
          nlohmann::json::parse(databaseSecrets.value())["data"]["data"];
      auto maybeUsername = secrets.find(this->username);
      auto maybePassword = secrets.find(this->password);
      this->username = maybeUsername == secrets.end() 
        ? this->username
        : maybeUsername->second;
      this->password = maybePassword == secrets.end() 
        ? this->password
        : maybePassword->second;

      return *this;
    } else {
      return *this;
    }
  }
```

This will furnish an updated `DatabaseConfig`. If there was a value provided by vault, it will be used. If none was provided, it will continue using what was provided in the configuration. This ensures that the program will be able to move on and off of Vault with no program changes in the event of a secrets engine failure.

## Putting it All Together

Finally, let's update `main` to account for our changes:

```cpp
int main(void) {
  std::filesystem::path configPath{"config.json"};
  Vault::Client vaultClient = getVaultClient();

  if (vaultClient.is_authenticated()) {
    try {
      DatabaseConfig databaseConfig =
          getDatabaseConfiguration(configPath).withSecrets(vaultClient);
      pqxx::connection databaseConnection{databaseConfig.connectionString()};

      if (databaseConnection.is_open()) {
        std::cout << "Connected" << std::endl;
      } else {
        std::cout << "Could not connect" << std::endl;
      }
    } catch (const std::exception &e) {
      std::cout << e.what() << std::endl;
    }
  } else {
    std::cout << "Unable to authenticate to Vault" << std::endl;
  }
}
```

## Running the Example

This example is fully dockerized and uses three separate containers. One for PostgreSQL, one for Vault, and one for our program. The following commands will run the example.

In one terminal:

```shell
make docker-network
make postgres
```

In another terminal:
```shell
make vault
```

In a third terminal, copy the root token from the second terminal output after Vault has completed booting:
```shell
vault/setup # paste the root token value when prompted
make docker-run
```

You will see the output `Connected` in the terminal if successful.

## Wrap-Up

This example is meant to demonstrate that the lift into secrets management can be both simple and low effort. In order to make this more generic a move away from the json library custom deserializers will be necessary. This would allow us to take all values as a map and construct our configuration values using an initial map with all values potentially swapped out with vault supplied values if applicable. We would parse the initial configuration into a map and pass that to all of our various configuration types. It would be a good idea at this point to change the constructor to only offer one type of construction ala `std::unordered_map, Vault::Client, Vault::Path` that provides the configuration slice necessary, the Vault client, and the path to the secret mount that holds the values. The constructor can then iterate through its members, swapping out what is provided and constructing a real instance to use. 