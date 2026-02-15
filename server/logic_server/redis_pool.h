#pragma once
#include <hiredis/hiredis.h>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include <string>

class RedisPool {
public:
    RedisPool(const std::string& host, int port, const std::string& password = "", size_t min_pool = 2, size_t max_pool = 50);
    ~RedisPool();

    struct ConnGuard {
        redisContext* ctx = nullptr;
        RedisPool* pool = nullptr;
        ~ConnGuard();
    };

    std::unique_ptr<ConnGuard> Acquire(int timeout_ms = 2000);

private:
    redisContext* CreateConn();
    void Release(redisContext* ctx);
    bool IsHealthy(redisContext* ctx);

    std::string host_;
    int port_;
    std::string password_;
    size_t min_pool_;
    size_t max_pool_;
    std::deque<redisContext*> idle_;
    size_t total_created_ = 0;
    std::mutex mu_;
    std::condition_variable cv_;
};