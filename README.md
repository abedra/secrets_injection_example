# Secrets Management: Transparent Secret Injection

## Introduction

Making the change to proper secrets management in your software can be a daunting task. The associated lift can be enough to make a team postpone the choice indefinitely. This post provides some design ideas that shold help ease the burden. We will set the following goals:

* Seamlessly swap out real secret values with references to secrets in another system
* Allow real secrets to continue to be used in the case the secrets management system fails

Having the ability to "break glass in case of emergency" should be considered a design requirement. It's something that should only be used under dire circumstances, but it's always important to remember that availability lies at the core of security.

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

### Setting up The Key Value Store

### Adding Vault to our Program

## Introducing Secret References

### Updating the Configuration Loader

### Trying it Out

## Next Steps

### Fully Dynamic References

### Immutable Configuration

### Stronger Configuration Types

### Using Vault's Dynamic Secrets