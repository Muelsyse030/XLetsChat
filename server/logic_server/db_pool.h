#pragma once
#include <libpq-fe.h>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include <string>

class DbPool {
public:
    DbPool(const std::string& conninfo, size_t min_pool = 2, size_t max_pool = 20);
    ~DbPool();

    struct ConnGuard {
        PGconn* conn = nullptr;
        DbPool* pool = nullptr;
        ~ConnGuard();
    };

    // 返回 nullptr 表示超时或失败
    std::unique_ptr<ConnGuard> Acquire(int timeout_ms = 5000);

private:
    PGconn* CreateConn();
    void Release(PGconn* conn);
    bool IsHealthy(PGconn* conn);

    std::string conninfo_;
    size_t min_pool_;
    size_t max_pool_;
    std::deque<PGconn*> idle_;
    size_t total_created_ = 0;
    std::mutex mu_;
    std::condition_variable cv_;
};