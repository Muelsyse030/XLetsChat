#pragma once
#include "redis_pool.h"
#include <optional>
#include <string>

class PooledRedisClient {
public:
    explicit PooledRedisClient(RedisPool* pool) : pool_(pool) {}
    bool Set(const std::string& key, const std::string& value);
    std::optional<std::string> Get(const std::string& key);
private:
    RedisPool* pool_;
};