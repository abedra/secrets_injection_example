#include <iostream>
#include <fstream>
#include <filesystem>
#include <pqxx/pqxx>
#include "lib/json.hpp"
#include "VaultClient.h"

struct DatabaseConfig {
    int port;
    std::string host;
    std::string database;
    std::string username;
    std::string password;

    std::string connectionString() {
        std::stringstream ss;
        ss << "hostaddr=" << host << " "
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
    std::string unparsedJson(std::istreambuf_iterator<char>{inputStream}, {});

    return nlohmann::json::parse(unparsedJson)["database"];
}

int main(void) {
    std::filesystem::path configPath{"config.json"};
    DatabaseConfig databaseConfig = getDatabaseConfiguration(configPath);
    pqxx::connection databaseConnection{databaseConfig.connectionString()};

    if (databaseConnection.is_open()) {
        std::cout << "Connected" << std::endl;
    } else {
        std::cout << "Could not connect" << std::endl;
    }
}