#include "pool_redis_client.h"
#include <spdlog/spdlog.h>
#include <hiredis/hiredis.h>

bool PooledRedisClient::Set(const std::string& key, const std::string& value) {
    auto g = pool_->Acquire();
    if (!g || !g->ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(g->ctx, "SET %s %s", key.c_str(), value.c_str());
    if (!reply) {
        spdlog::error("Redis SET reply null");
        return false;
    }
    bool ok = true;
    if (reply->type == REDIS_REPLY_ERROR) {
        spdlog::error("Redis SET error: {}", reply->str);
        ok = false;
    }
    freeReplyObject(reply);
    return ok;
}

std::optional<std::string> PooledRedisClient::Get(const std::string& key) {
    auto g = pool_->Acquire();
    if (!g || !g->ctx) return std::nullopt;
    redisReply* reply = (redisReply*)redisCommand(g->ctx, "GET %s", key.c_str());
    if (!reply) return std::nullopt;
    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}