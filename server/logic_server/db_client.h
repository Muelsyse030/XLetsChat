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
    private:
    PGconn* conn_;
};