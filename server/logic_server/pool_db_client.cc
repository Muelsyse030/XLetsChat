#include "pool_db_client.h"
#include <libpq-fe.h>
#include <sstream>

bool PooledDbClient::Execute(const std::string& sql) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return false;
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("SQL Execute failed: {} | Error: {}", sql, PQerrorMessage(g->conn));
        PQclear(res);
        return false;
    }
    PQclear(res);
    return true;
}

std::string PooledDbClient::GetUserPassword(int64_t uid) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return "";
    std::string sql = "SELECT password FROM t_user WHERE id=" + std::to_string(uid);
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return "";
    }
    std::string password = "";
    if (PQntuples(res) > 0) password = PQgetvalue(res, 0, 0);
    PQclear(res);
    return password;
}

bool PooledDbClient::SaveMessage(const std::string& msg_id, int64_t from_uid, int64_t to_uid, const std::string& content) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return false;
    long now = time(nullptr);
    std::ostringstream oss;
    oss << "INSERT INTO t_chat_msg (msg_id, from_uid, to_uid, content, create_time) VALUES ('"
        << msg_id << "', " << from_uid << ", " << to_uid << ", '" << content << "', " << now << ")";
    PGresult* res = PQexec(g->conn, oss.str().c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("insert Msg failed:{} | Error:{}", oss.str(), PQerrorMessage(g->conn));
        PQclear(res);
        return false;
    }
    PQclear(res);
    return true;
}

std::vector<im::ChatMsg> PooledDbClient::GetofflineMsgs(int64_t uid, int64_t last_msg_id) {
    std::vector<im::ChatMsg> msgs;
    auto g = pool_->Acquire();
    if (!g || !g->conn) return msgs;
    std::string sql = "SELECT msg_id, from_uid, to_uid, content, create_time FROM t_chat_msg WHERE to_uid=" + std::to_string(uid)
                      + " AND id > " + std::to_string(last_msg_id) + " ORDER BY id ASC LIMIT 100";
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i) {
            im::ChatMsg msg;
            msg.set_from_uid(std::stoll(PQgetvalue(res, i, 1)));
            msg.set_to_uid(std::stoll(PQgetvalue(res, i, 2)));
            msg.set_content(PQgetvalue(res, i, 3));
            msg.set_create_time(std::stoll(PQgetvalue(res, i, 4)));
            msgs.push_back(msg);
        }
    }
    if (res) PQclear(res);
    return msgs;
}

int64_t PooledDbClient::CreateUser(const std::string& username, const std::string& password, const std::string& email) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return -1;
    std::string sql = "INSERT INTO t_user (username, password, email) VALUES ('" + username + "', '" + password + "', '" + email + "') RETURNING id";
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return -1;
    }
    int64_t uid = std::stoll(PQgetvalue(res, 0, 0));
    PQclear(res);
    return uid;
}

bool PooledDbClient::CheckUserByEmail(const std::string& email, const std::string& password, im::HttpLoginRes& user_info) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return false;
    std::string sql = "SELECT id, username, password FROM t_user WHERE email='" + email + "'";
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return false;
    }
    std::string db_pass = PQgetvalue(res, 0, 2);
    if (db_pass != password) {
        PQclear(res);
        return false;
    }
    user_info.set_uid(std::stoll(PQgetvalue(res, 0, 0)));
    user_info.set_nickname(PQgetvalue(res, 0, 1));
    user_info.set_token(db_pass);
    PQclear(res);
    return true;
}

bool PooledDbClient::CreateFriendRequest(int64_t from_uid, int64_t to_uid, const std::string& reason, int64_t& out_req_id) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return false;
    long now = time(nullptr);
    std::string sql = "INSERT INTO t_friend_request (from_uid, to_uid, reason, create_time) VALUES (" + std::to_string(from_uid)
        + ", " + std::to_string(to_uid) + ", '" + reason + "', " + std::to_string(now) + ") RETURNING id";
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        if (res) PQclear(res);
        return false;
    }
    out_req_id = std::stoll(PQgetvalue(res, 0, 0));
    PQclear(res);
    return true;
}

std::vector<im::FriendRequest> PooledDbClient::GetFriendRequestsForUser(int64_t uid) {
    std::vector<im::FriendRequest> list;
    auto g = pool_->Acquire();
    if (!g || !g->conn) return list;
    std::string sql = "SELECT id, from_uid, to_uid, reason, create_time, status FROM t_friend_request WHERE to_uid=" + std::to_string(uid) + " AND status=0 ORDER BY id ASC";
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i) {
            im::FriendRequest fr;
            fr.set_req_id(std::stoll(PQgetvalue(res, i, 0)));
            fr.set_from_uid(std::stoll(PQgetvalue(res, i, 1)));
            fr.set_to_uid(std::stoll(PQgetvalue(res, i, 2)));
            fr.set_reason(PQgetvalue(res, i, 3));
            fr.set_create_time(std::stoll(PQgetvalue(res, i, 4)));
            fr.set_status(std::stoi(PQgetvalue(res, i, 5)));
            list.push_back(fr);
        }
    }
    if (res) PQclear(res);
    return list;
}

bool PooledDbClient::AcceptFriendRequest(int64_t req_id) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return false;
    std::string sql = "SELECT from_uid, to_uid FROM t_friend_request WHERE id=" + std::to_string(req_id) + " AND status=0";
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return false;
    }
    int64_t from_uid = std::stoll(PQgetvalue(res, 0, 0));
    int64_t to_uid = std::stoll(PQgetvalue(res, 0, 1));
    PQclear(res);
    long now = time(nullptr);
    std::string ins1 = "INSERT INTO t_friend (uid, friend_uid, create_time) VALUES (" + std::to_string(from_uid) + ", " + std::to_string(to_uid) + ", " + std::to_string(now) + ") ON CONFLICT DO NOTHING";
    std::string ins2 = "INSERT INTO t_friend (uid, friend_uid, create_time) VALUES (" + std::to_string(to_uid) + ", " + std::to_string(from_uid) + ", " + std::to_string(now) + ") ON CONFLICT DO NOTHING";
    PGresult* r1 = PQexec(g->conn, ins1.c_str());
    PGresult* r2 = PQexec(g->conn, ins2.c_str());
    if (PQresultStatus(r1) != PGRES_COMMAND_OK || PQresultStatus(r2) != PGRES_COMMAND_OK) {
        if (r1) PQclear(r1);
        if (r2) PQclear(r2);
        return false;
    }
    if (r1) PQclear(r1);
    if (r2) PQclear(r2);
    std::string upd = "UPDATE t_friend_request SET status=1 WHERE id=" + std::to_string(req_id);
    PGresult* ur = PQexec(g->conn, upd.c_str());
    if (ur) PQclear(ur);
    return true;
}

bool PooledDbClient::AreFriends(int64_t uid1, int64_t uid2) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return false;
    std::string sql = "SELECT 1 FROM t_friend WHERE uid=" + std::to_string(uid1) + " AND friend_uid=" + std::to_string(uid2) + " LIMIT 1";
    PGresult* res = PQexec(g->conn, sql.c_str());
    bool ok = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
    if (res) PQclear(res);
    return ok;
}

std::vector<int64_t> PooledDbClient::ListFriends(int64_t uid) {
    std::vector<int64_t> out;
    auto g = pool_->Acquire();
    if (!g || !g->conn) return out;
    std::string sql = "SELECT friend_uid FROM t_friend WHERE uid=" + std::to_string(uid);
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i) {
            out.push_back(std::stoll(PQgetvalue(res, i, 0)));
        }
    }
    if (res) PQclear(res);
    return out;
}

bool PooledDbClient::SaveGroupMessage(const std::string& msg_id, int64_t group_id, int64_t from_uid, const std::string& content) {
    auto g = pool_->Acquire();
    if (!g || !g->conn) return false;
    long now = time(nullptr);
    
    // 读扩散：消息只存一份
    std::ostringstream oss;
    oss << "INSERT INTO t_group_msg (msg_id, group_id, from_uid, content, create_time) VALUES ('"
        << msg_id << "', " << group_id << ", " << from_uid << ", '" << content << "', " << now << ")";
    
    PGresult* res = PQexec(g->conn, oss.str().c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Group insert failed: {} | Error: {}", oss.str(), PQerrorMessage(g->conn));
        PQclear(res);
        return false;
    }
    PQclear(res);
    return true;
}

std::vector<im::ChatMsg> PooledDbClient::GetGroupMsgs(int64_t group_id, int64_t last_msg_id) {
    std::vector<im::ChatMsg> msgs;
    auto g = pool_->Acquire();
    if (!g || !g->conn) return msgs;
    
    // 群聊查询：读扩散，只查一份数据
    std::string sql = "SELECT msg_id, group_id, from_uid, content, create_time FROM t_group_msg WHERE group_id=" + std::to_string(group_id)
                      + " AND id > " + std::to_string(last_msg_id) + " ORDER BY id ASC LIMIT 100";
    
    PGresult* res = PQexec(g->conn, sql.c_str());
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; ++i) {
            im::ChatMsg msg;
            msg.set_from_uid(std::stoll(PQgetvalue(res, i, 2)));
            msg.set_to_uid(std::stoll(PQgetvalue(res, i, 1))); // 群ID作为to_uid
            msg.set_content(PQgetvalue(res, i, 3));
            msg.set_create_time(std::stoll(PQgetvalue(res, i, 4)));
            msgs.push_back(msg);
        }
    }
    if (res) PQclear(res);
    return msgs;
}

