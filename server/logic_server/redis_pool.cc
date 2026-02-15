#include "redis_pool.h"
#include <spdlog/spdlog.h>
#include <chrono>

RedisPool::RedisPool(const std::string& host, int port, const std::string& password, size_t min_pool, size_t max_pool)
    : host_(host), port_(port), password_(password), min_pool_(min_pool), max_pool_(max_pool) {
    for (size_t i = 0; i < min_pool_; ++i) {
        redisContext* c = CreateConn();
        if (c) idle_.push_back(c);
    }
    total_created_ = idle_.size();
}

RedisPool::~RedisPool() {
    std::unique_lock lk(mu_);
    for (auto c : idle_) {
        if (c) redisFree(c);
    }
    idle_.clear();
    total_created_ = 0;
}

RedisPool::ConnGuard::~ConnGuard() {
    if (pool && ctx) pool->Release(ctx);
}

redisContext* RedisPool::CreateConn() {
    redisContext* ctx = redisConnect(host_.c_str(), port_);
    if (!ctx || ctx->err) {
        if (ctx) {
            spdlog::error("RedisPool: connect error: {}", ctx->errstr);
            redisFree(ctx);
        } else {
            spdlog::error("RedisPool: redisConnect returned nullptr");
        }
        return nullptr;
    }
    if (!password_.empty()) {
        redisReply* r = (redisReply*)redisCommand(ctx, "AUTH %s", password_.c_str());
        if (!r || r->type == REDIS_REPLY_ERROR) {
            spdlog::error("RedisPool: AUTH failed: {}", r ? r->str : "null");
            if (r) freeReplyObject(r);
            redisFree(ctx);
            return nullptr;
        }
        freeReplyObject(r);
    }
    return ctx;
}

bool RedisPool::IsHealthy(redisContext* ctx) {
    return ctx && ctx->err == 0;
}

std::unique_ptr<RedisPool::ConnGuard> RedisPool::Acquire(int timeout_ms) {
    using namespace std::chrono;
    auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    std::unique_lock lk(mu_);
    while (true) {
        if (!idle_.empty()) {
            redisContext* c = idle_.front();
            idle_.pop_front();
            if (!IsHealthy(c)) {
                redisFree(c);
                --total_created_;
                continue;
            }
            auto g = std::make_unique<ConnGuard>();
            g->ctx = c;
            g->pool = this;
            return g;
        }
        if (total_created_ < max_pool_) {
            lk.unlock();
            redisContext* c = CreateConn();
            lk.lock();
            if (c) {
                ++total_created_;
                auto g = std::make_unique<ConnGuard>();
                g->ctx = c;
                g->pool = this;
                return g;
            }
        }
        if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
            return nullptr;
        }
    }
}

void RedisPool::Release(redisContext* ctx) {
    std::unique_lock lk(mu_);
    if (!IsHealthy(ctx)) {
        redisFree(ctx);
        if (total_created_ > 0) --total_created_;
    } else {
        idle_.push_back(ctx);
    }
    lk.unlock();
    cv_.notify_one();
}