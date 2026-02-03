#pragma once
#include <libpq-fe.h>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

class DbClient{
    public:
    DbClient() : conn_(nullptr) {}
    ~DbClient(){
        if(conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }
    bool Connect(const std::string& conn_str){
        conn_ = PQconnectdb(conn_str.c_str());
        if(PQstatus(conn_) != CONNECTION_OK){
            spdlog::error("DB connection failed : {}" , PQerrorMessage(conn_));
            PQfinish(conn_);
            return false;
        }
        spdlog::info("Connected to PostgreSQL successfully");
        return true;
    }
    bool Execute(const std::string& sql){
        if(!conn_){
            return false;
        }
        PGresult* res = PQexec(conn_ , sql.c_str());
        if(PQresultStatus(res) != PGRES_COMMAND_OK){
            spdlog::error("SQL Execute faild: {} | Error: {}",sql,PQerrorMessage);
            PQclear(res);
            return false;
        }
        PQclear(res);
        return true;
    }
    std::string GetUserPassword(int64_t uid){
        if(!conn_) { return "";}
        std::string sql = "SELECT password FROM t_user WHERE id=" +std::to_string(uid);
        PGresult* res = PQexec(conn_ , sql.c_str());
        if(PQresultStatus(res) != PGRES_TUPLES_OK){
            spdlog::warn("Query User faild:{}" , PQerrorMessage(conn_));
            PQclear(res);
            return "";
        }
        std::string password = "";
        if(PQntuples(res) > 0){
            password = PQgetvalue(res , 0 , 0 );
        }
        PQclear(res);
        return password;
    }
    bool SaveMessage(const std::string& msg_id , int64_t from_uid , int64_t to_uid , const std::string& content){
        if(!conn_){
            return false;
        }
        long now = time(nullptr);
        std::string sql = "INSERT INTO t_chat_msg (msg_id, from_uid, to_uid, content, create_time) VALUES ('" + 
                          msg_id + "', " + 
                          std::to_string(from_uid) + ", " + 
                          std::to_string(to_uid) + ", '" + 
                          content + "', " + 
                          std::to_string(now) + ")";
        PGresult* res = PQexec(conn_ , sql.c_str());
        if(PQresultStatus(res) != PGRES_COMMAND_OK){
            spdlog::error("insert Msg failed:{} | Error:{}" , sql , PQerrorMessage);
            PQclear(res);
            return false;
        }
        PQclear(res);
        spdlog::info(">>> DB:saved Msg [{}] form {} to {}" , msg_id , from_uid , to_uid);
        return true;
    }
    std::vector<im::ChatMsg> GetofflineMsgs(int64_t uid , int64_t last_msg_id){
        std::vector<im::ChatMsg> msgs;
        if(!conn_) {return msgs;}

        std::string sql = "SELECT msg_id, from_uid, to_uid, content, create_time FROM t_chat_msg "
                          "WHERE to_uid=" + std::to_string(uid) + 
                          " AND id > " + std::to_string(last_msg_id) + 
                          " ORDER BY id ASC LIMIT 100";
        PGresult* res = PQexec(conn_ , sql.c_str());
        if(PQresultStatus(res) == PGRES_TUPLES_OK){
            int rows = PQntuples(res);
            for(int i = 0 ; i<rows ; i++){
                im::ChatMsg msg;

                msg.set_from_uid(std::stoll(PQgetvalue(res, i, 1)));
                msg.set_to_uid(std::stoll(PQgetvalue(res, i, 2)));
                msg.set_content(PQgetvalue(res, i, 3));
                msg.set_create_time(std::stoll(PQgetvalue(res, i, 4)));

                msgs.push_back(msg);
            }
        }else{
            spdlog::error("Query History failed:{}" , PQerrorMessage(conn_));
        }
        PQclear(res);
        return msgs;
    }
    int64_t CreateUser(const std::string& username , const std::string& password , const std::string& email){
        if(!conn_){
            return -1;
        }
        std::string sql = "INSERT INTO t_user (username, password, email) VALUES ('" + 
                      username + "', '" + 
                      password + "', '" + 
                      email + "') RETURNING id";

        PGresult* res = PQexec(conn_, sql.c_str());

        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Insert user failed: {}", PQerrorMessage(conn_));
        PQclear(res);
        return -1;
        }
        int64_t uid = std::stoll(PQgetvalue(res, 0, 0));
        PQclear(res);
        return uid;
    }
    bool CheckUserByEmail(const std::string& email , const std::string& password , im::HttpLoginRes& user_info){
        if(!conn_) return false;
        std::string sql = "SELECT id, nickname, password FROM t_user WHERE email='" + email + "'";
        PGresult* res = PQexec(conn_ , sql.c_str());
        if(PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0){
            PQclear(res);
            return false;
        }
        std::string db_pass = PQgetvalue(res , 0 ,2);
        if(db_pass != password){
            PQclear(res);
            return false;
        }
        user_info.set_uid(std::stoll(PQgetvalue(res , 0 , 0)));
        user_info.set_nickname(PQgetvalue(res , 0 , 1));
        user_info.set_token(db_pass);

        PQclear(res);
        return true;
    }
    private:
    PGconn* conn_;
};