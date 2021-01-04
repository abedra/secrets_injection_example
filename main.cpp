#include <filesystem>
#include <fstream>
#include <iostream>
#include <pqxx/pqxx>

#include "VaultClient.h"
#include "lib/json.hpp"

struct DatabaseConfig {
  int port;
  std::string host;
  std::string database;
  std::string username;
  std::string password;

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