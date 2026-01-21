#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <spdlog/spdlog.h>
#include <optional>

class RedisClient{
public:
    RedisClient() : context_(nullptr){}
    ~RedisClient(){
        if(context_){
            redisFree(context_);
        }
    }

    bool connect(const std::string& ip = "127.0.0.1" , int port = 6379 , const std::string& password = ""){
        context_ = redisConnect(ip.c_str() , port);
        if(context_ == nullptr || context_->err){
            if(context_){
                spdlog::error("Redis connection error : {} " , context_->errstr);
            }
            else{
                spdlog::error("Redis connection error : can't allocate redis context");
            }
            return false;
        }
        if (!password.empty()) {
            redisReply* reply = (redisReply*)redisCommand(context_, "AUTH %s", password.c_str());
            if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
                spdlog::error("Redis authentication failed: {}", reply ? reply->str : "Unknown error");
                if (reply) freeReplyObject(reply);
                redisFree(context_);
                context_ = nullptr;
                return false;
            }
            freeReplyObject(reply);
            spdlog::info("Redis authentication success");
        }
        
        spdlog::info("Connected to Redis at {}:{}",ip,port);
        return true;
    }
    bool Set(const std::string& key , const std::string& value){
        if(!context_) return false;
        redisReply* reply = (redisReply*)redisCommand(context_ , "SET %s %s" , key.c_str() , value.c_str());
        if(!reply){
            spdlog::error("Redis SET error field");
            return false;
        }
        bool success = true;
        if(reply->type == REDIS_REPLY_ERROR){
            spdlog::error("Redis SET error : {}",reply->str);
            success = false;
        }
        freeReplyObject(reply);
        return success;
    }

    std::optional<std::string> Get(const std::string& key){
        if(!context_) return std::nullopt;

        redisReply* reply = (redisReply*)redisCommand(context_ , "GET %s" , key.c_str());
        if(!reply)return std::nullopt;

        std::optional<std::string> result;
        if(reply->type == REDIS_REPLY_STRING){
            result = std::string(reply->str , reply->len);
        }
        freeReplyObject(reply);
        return result;
    }
private:
    redisContext* context_;
};