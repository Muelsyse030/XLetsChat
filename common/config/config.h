#pragma once
#include <string>
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>

class Config {
public:
    struct RedisConfig {
        std::string host;
        int port;
        std::string password;
    };

    struct PostgresConfig {
        std::string host;
        int port;
        std::string user;
        std::string password;
        std::string dbname;
    };

    struct GrpcConfig {
        std::string logic_server_addr;
        std::string gateway_server_addr;
        std::string logic_server_listen_addr;
        std::string gateway_server_listen_addr;
    };

    struct SeaweedFSConfig {
        std::string master_endpoint;
        std::string public_endpoint;
    };

    struct ServerConfig {
        RedisConfig redis;
        PostgresConfig postgres;
        GrpcConfig grpc;
        SeaweedFSConfig seaweedfs;
    };

    static Config& Instance() {
        static Config instance;
        return instance;
    }

    bool Load(const std::string& config_file = "config.yaml") {
        try {
            YAML::Node config = YAML::LoadFile(config_file);

            // Redis
            config_.redis.host = config["redis"]["host"].as<std::string>("0.0.0.0");
            config_.redis.port = config["redis"]["port"].as<int>(6379);
            config_.redis.password = config["redis"]["password"].as<std::string>("redis_pwd_123");

            // PostgreSQL
            config_.postgres.host = config["postgres"]["host"].as<std::string>("127.0.0.1");
            config_.postgres.port = config["postgres"]["port"].as<int>(5432);
            config_.postgres.user = config["postgres"]["user"].as<std::string>("admin");
            config_.postgres.password = config["postgres"]["password"].as<std::string>("admin_pwd");
            config_.postgres.dbname = config["postgres"]["dbname"].as<std::string>("LetsChat");

            // gRPC
            config_.grpc.logic_server_addr = config["grpc"]["logic_server_addr"].as<std::string>("0.0.0.0:50051");
            config_.grpc.gateway_server_addr = config["grpc"]["gateway_server_addr"].as<std::string>("0.0.0.0:50052");
            config_.grpc.logic_server_listen_addr = config["grpc"]["logic_server_listen_addr"].as<std::string>("0.0.0.0:50051");
            config_.grpc.gateway_server_listen_addr = config["grpc"]["gateway_server_listen_addr"].as<std::string>("0.0.0.0:50052");

            // SeaweedFS
            config_.seaweedfs.master_endpoint = config["seaweedfs"]["master_endpoint"].as<std::string>("http://127.0.0.1:9333");
            config_.seaweedfs.public_endpoint = config["seaweedfs"]["public_endpoint"].as<std::string>("http://127.0.0.1:8080");

            spdlog::info("Configuration loaded successfully");
            return true;
        } catch (const std::exception& e) {
            spdlog::error("Failed to load config: {}", e.what());
            return false;
        }
    }

    const ServerConfig& GetConfig() const {
        return config_;
    }

    const RedisConfig& GetRedisConfig() const {
        return config_.redis;
    }

    const PostgresConfig& GetPostgresConfig() const {
        return config_.postgres;
    }

    const GrpcConfig& GetGrpcConfig() const {
        return config_.grpc;
    }

    const SeaweedFSConfig& GetSeaweedFSConfig() const {
        return config_.seaweedfs;
    }

private:
    Config() = default;
    ServerConfig config_;
};
