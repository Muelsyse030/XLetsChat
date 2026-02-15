#include "db_pool.h"
#include <spdlog/spdlog.h>
#include <chrono>

DbPool::DbPool(const std::string& conninfo, size_t min_pool, size_t max_pool)
    : conninfo_(conninfo), min_pool_(min_pool), max_pool_(max_pool) {
    for (size_t i = 0; i < min_pool_; ++i) {
        PGconn* c = CreateConn();
        if (c) idle_.push_back(c);
    }
    total_created_ = idle_.size();
}

DbPool::~DbPool() {
    std::unique_lock lk(mu_);
    for (auto c : idle_) {
        if (c) PQfinish(c);
    }
    idle_.clear();
    total_created_ = 0;
}

DbPool::ConnGuard::~ConnGuard() {
    if (pool && conn) pool->Release(conn);
}

PGconn* DbPool::CreateConn() {
    PGconn* conn = PQconnectdb(conninfo_.c_str());
    if (conn == nullptr || PQstatus(conn) != CONNECTION_OK) {
        if (conn) {
            spdlog::error("DbPool: PQconnectdb failed: {}", PQerrorMessage(conn));
            PQfinish(conn);
        } else {
            spdlog::error("DbPool: PQconnectdb returned nullptr");
        }
        return nullptr;
    }
    return conn;
}

bool DbPool::IsHealthy(PGconn* conn) {
    return conn && PQstatus(conn) == CONNECTION_OK;
}

std::unique_ptr<DbPool::ConnGuard> DbPool::Acquire(int timeout_ms) {
    using namespace std::chrono;
    auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    std::unique_lock lk(mu_);
    while (true) {
        if (!idle_.empty()) {
            PGconn* c = idle_.front();
            idle_.pop_front();
            if (!IsHealthy(c)) {
                PQfinish(c);
                --total_created_;
                continue;
            }
            auto g = std::make_unique<ConnGuard>();
            g->conn = c;
            g->pool = this;
            return g;
        }
        if (total_created_ < max_pool_) {
            lk.unlock();
            PGconn* c = CreateConn();
            lk.lock();
            if (c) {
                ++total_created_;
                auto g = std::make_unique<ConnGuard>();
                g->conn = c;
                g->pool = this;
                return g;
            }
        }
        if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
            return nullptr;
        }
    }
}

void DbPool::Release(PGconn* conn) {
    std::unique_lock lk(mu_);
    if (!IsHealthy(conn)) {
        PQfinish(conn);
        if (total_created_ > 0) --total_created_;
    } else {
        idle_.push_back(conn);
    }
    lk.unlock();
    cv_.notify_one();
}