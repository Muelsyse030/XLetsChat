#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "im.pb.h"
#include "im_service.grpc.pb.h"
#include "redis_client.h"
#include "db_client.h"
#include <mutex>
#include <unordered_map>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using im::LogicService;
using im::LoginReq;
using im::LoginRes;
using im::MsgSendReq; 
using im::MsgSendRes;

std::string PackPushMsg(const im::ChatMsg& chat_msg) {

    im::MsgPush push_pkg;
    *push_pkg.mutable_msg() = chat_msg;
    
    std::string body_bytes;
    push_pkg.SerializeToString(&body_bytes);

    uint32_t total_len = 12 + body_bytes.size();
    uint16_t version = 1;
    uint16_t cmd_id = 0x1005;
    uint32_t seq_id = 0;      

    uint32_t net_len = htonl(total_len);
    uint16_t net_ver = htons(version);
    uint16_t net_cmd = htons(cmd_id);
    uint32_t net_seq = htonl(seq_id);

    std::string packet;
    packet.append((char*)&net_len, 4);
    packet.append((char*)&net_ver, 2);
    packet.append((char*)&net_cmd, 2);
    packet.append((char*)&net_seq, 4);
    packet.append(body_bytes);

    return packet;
}

class LogicServiceImpl final : public LogicService::Service{
public:
    LogicServiceImpl(RedisClient* redis , DbClient* db) : redis_(redis),db_(db){}

    Status Login(ServerContext* context , const LoginReq* request , LoginRes* reply) override {
        spdlog::info("PRC Login Request: Uid= {} , token = {} , device = {}" , request->uid() , request->token() , request->device_id());
        std::string db_password = db_->GetUserPassword(request->uid());
        if(!db_password.empty() && db_password == request->token()){
            reply->set_err_code(im::ERR_SUCCESS);
            reply->set_session_id("sess_"+ std::to_string(request->uid()));

            std::string redis_key = "IM:USER:SESS" + std::to_string(request->uid());
            std::string gateway_addr = "0.0.0.0:50052";

            if(redis_->Set(redis_key , gateway_addr)){
                spdlog::info("->Login Success : Uid = {}", request->uid());
            }
        }
        else{
            reply->set_err_code(im::ErrorCode::ERR_AUTH_FAIL);
            reply->set_err_msg("Invalid token");
            spdlog::info("->Login Failed: uid = {}" , request->uid());
        }
        return Status::OK;
    }
    Status SendMsg(ServerContext* context , const MsgSendReq* request , MsgSendRes* reply) override {
        const auto& msg = request->msg();
        spdlog::info("RPC sendMsg: from={} to={} content={}" , msg.from_uid() , msg.to_uid() , msg.content());
        
        int64_t msg_id = std::chrono::system_clock::now().time_since_epoch().count();
       
        bool saved = db_->SaveMessage(std::to_string(msg_id) , msg.from_uid() , msg.to_uid() , msg.content());
        if(!saved){
            spdlog::error("failed to save messahe to DB");
        }

        std::string redis_key = "IM:USER:SESS:" + std::to_string(msg.to_uid());
        auto gateway_addr_opt = redis_->Get(redis_key);

        if(gateway_addr_opt.has_value()){
            std::string gateway_addr = gateway_addr_opt.value();
            spdlog::info("->Found target user {} at gateway[{}]" , msg.to_uid() , gateway_addr_opt.value());
            auto channel = grpc::CreateChannel(gateway_addr , grpc::InsecureChannelCredentials());
            auto stub = im::GatewayService::NewStub(channel);

            im::PushMsgReq push_req;
            push_req.set_to_uid(msg.to_uid());

            std::string raw_packet = PackPushMsg(msg);
            push_req.set_content(raw_packet);

            im::PushMsgRes push_res;
            grpc::ClientContext client_context;
            client_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));

            grpc::Status status = stub->PushMsg(&client_context , push_req , &push_res);
            
            if (status.ok() && push_res.err_code() == 0) {
                spdlog::info("--> Push to Gateway Success!");
            } else {
                spdlog::warn("--> Push to Gateway Failed: {} ({})", status.error_message(), push_res.err_msg());
            }
        }
        else{
            spdlog::warn("->Target user {} is offline(reids key not found)" , msg.to_uid());

        }
        reply->set_err_code(im::ErrorCode::ERR_SUCCESS);
        reply->set_msg_id(msg_id);
        reply->set_create_time(time(nullptr));

        return Status::OK;
    }
    Status SyncMsg(ServerContext* context , const im::SyncMsgReq* request , im::SyncMsgRes* reply) override{
        spdlog::info("RPC SyncMsg: Uid={} lastMsgID={}",request->uid() , request->last_msg_id());

        std::vector<im::ChatMsg> history_msgs = db_->GetofflineMsgs(request->uid() , request->last_msg_id());

        reply->set_err_code(im::ERR_SUCCESS);
        for(const auto& msg : history_msgs){
            *reply->add_msgs() = msg;
        }
        spdlog::info("->Synced {} message to UID={}" , history_msgs.size() , request->uid());
        return Status::OK;
    }
    private:
        RedisClient* redis_;
        DbClient* db_;
};

void RunServer(){
    std::string server_address("0.0.0.0:50051");
    RedisClient redis;
    if(!redis.connect("0.0.0.0" , 6379 , "redis_pwd_123")){
        spdlog::error("Failed to connect to Redis. Exiting.");
        return;
    }
    DbClient db;
    std::string db_conn_str = "dbname=LetsChat user=admin password=password123 hostaddr=127.0.0.1 port=5432";
    if(!db.Connect(db_conn_str)){
        spdlog::error("failed to connect to database , server exit");
        return;
    }
    LogicServiceImpl service(&redis,&db);

    ServerBuilder builder;
    builder.AddListeningPort(server_address , grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    spdlog::info("logic Server is listening on {}", server_address);

    server->Wait();
}

int main(){
    spdlog::set_pattern("[%H:%H%S %z][%^%L%$][Logic]%v");

    RunServer();

    return 0;
}