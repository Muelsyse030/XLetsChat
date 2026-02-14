#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>
#include <cstring>

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "im.pb.h"
#include "im_service.grpc.pb.h"
#include "redis_client.h"
#include "db_client.h"
#include <mutex>
#include <unordered_map>
#include "s3_client.h"
#include "../../common/config/config.h"

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
    LogicServiceImpl(RedisClient* redis , DbClient* db , S3Client* s3) : redis_(redis),db_(db),s3_(s3){}

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
        if(!db_->AreFriends(msg.from_uid() , msg.to_uid())){
            reply->set_err_code(im::ErrorCode::ERR_NOT_FRIEND);
            reply->set_err_msg("You must be friend to send message");
            spdlog::info("->SendMsg blocked : {} -> {} not friends" , msg.from_uid() , msg.to_uid());
            return Status::OK;
        }
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
    Status RegisterUser(ServerContext* context , const im::RegisterReq* request , im::RegisterRes* reply) override{
        spdlog::info("RPC Register : emial={} nick={}",request->email() , request->nickname());

        int64_t uid = db_->CreateUser(request->nickname() , request->password() , request->email());
        if(uid > 0){
            reply->set_err_code(0);
            reply->set_uid(uid);
            spdlog::info("User created: uid={}" , uid);
        }else{
            reply->set_err_code(1);
            reply->set_err_msg("create user failed");
        }
        return Status::OK;
    }
    Status HttpLogin(ServerContext* context , const im::HttpLoginReq* request , im::HttpLoginRes* reply) override {
        spdlog::info("RPC Httplogin: emial={}",request->email());
        if(db_->CheckUserByEmail(request->email() , request->password() , *reply)){
            reply->set_err_code(0);
            spdlog::info("User login sucess : uid={}",reply->uid());
            redis_->Set("IM:USER:TOKEN"+std::to_string(reply->uid()) , request->password());
        }else{
            reply->set_err_code(1);
            reply->set_err_msg("Invaild email or passwrod");
            spdlog::warn("User login failed : email={}",request->email());
        }
        return Status::OK;
    }
    Status GetUploadUrl(ServerContext* context , const im::GetUploadUrlReq* request, im::GetUploadUrlRes* reply){
        spdlog::info("RPC GetUploadUrl UID={} File={}" , request->uid() , request->file_name());
        int64_t now = std::chrono::system_clock::now().time_since_epoch().count();
        std::string object_key = std::to_string(request->uid()) + "/" + std::to_string(now) + "_" + request->file_name();
        std::string upload_url = s3_->GetPresignedPutUrl(object_key);
        std::string download_url = s3_->GetDownloadUrl(object_key);

        if (upload_url.empty() || download_url.empty()) {
            reply->set_err_code(im::ERR_SYS_ERROR);
            reply->set_err_msg("SeaweedFS unavailable");
            spdlog::error("-> Failed to generate upload/download URL for {}", object_key);
            return Status::OK;
        }

        reply->set_err_code(im::ERR_SUCCESS);
        reply->set_upload_url(upload_url);
        reply->set_download_url(download_url);

        spdlog::info("-> Generated Upload URL for {}", object_key);
        return Status::OK;
    }
    Status SendFriendRequest(ServerContext* context , const im::SendFriendReq* req , im::SendFriendRes* reply) override {
        spdlog::info("PRC SendFriendRequest : from={} to={} reason={}",req->from_uid() , req->to_uid() , req->reason());
        int64_t req_id = 0;
        if(db_->CreateFriendRequest(req->from_uid() , req->to_uid() , req->reason() , req_id)){
            reply->set_err_code(im::ErrorCode::ERR_SUCCESS);
            reply->set_req_id(req_id);
            spdlog::info("->Friend request created: id={}" , req_id);
        }else{
            reply->set_err_code(im::ErrorCode::ERR_SYS_ERROR);
            reply->set_err_msg("Failed to create friend request");
            spdlog::error("->Failed to create friend request from {} to {}" , req->from_uid() , req->to_uid());
        }
        return Status::OK;
    }
    Status RespondFriendRequest(ServerContext* context , const im::RespondFriendReq* req , im::RespondFriendRes* reply) override{
        spdlog::info("RPC RespondFriendRequest: req_id={} accept={} " , req->req_id() , req->accept());
        if(req->accept()){
            if(db_->AcceptFriendRequest(req->req_id())){
                reply->set_err_code(im::ErrorCode::ERR_SUCCESS);
                spdlog::info("Friend request accepted : req_id = {}" , req->req_id());
            }else{
                reply->set_err_code(im::ErrorCode::ERR_SYS_ERROR);
                reply->set_err_msg("Failed to accept friend request");
            }
        }else{
            std::string sql = "UPDATE t_friend_request SET status=2 WHERE id=" + std::to_string(req->req_id());
            db_->Execute(sql);
            reply->set_err_code(im::ERR_SUCCESS);
        }
        return Status::OK;
    }

    Status ListFriends(ServerContext* context, const im::FriendListReq* request, im::FriendListRes* reply) override {
    auto list = db_->ListFriends(request->uid());
    reply->set_err_code(im::ERR_SUCCESS);
    for(auto f : list) reply->add_friend_uids(f);
    return Status::OK;
    }

    Status GetFriendRequests(ServerContext* context, const im::GetFriendReqsReq* request, im::GetFriendReqsRes* reply) override {
    auto reqs = db_->GetFriendRequestsForUser(request->uid());
    reply->set_err_code(im::ERR_SUCCESS);
    for(const auto& r : reqs){
        *reply->add_requests() = r;
    }
    return Status::OK;
}
    private:
        RedisClient* redis_;
        DbClient* db_;
        S3Client* s3_;
};

std::string GetEnvOrDefault(const char* key, const std::string& default_value) {
    const char* value = std::getenv(key);
    if (value == nullptr || std::strlen(value) == 0) {
        return default_value;
    }
    return value;
}

int RunServer(){
    constexpr int kStorageConnectErrorCode = 42;
    
    // Load configuration
    Config& config = Config::Instance();
    if (!config.Load("../config.yaml")) {
        spdlog::error("Failed to load configuration. Exiting.");
        return 1;
    }

    // Get config values
    const auto& redis_cfg = config.GetRedisConfig();
    const auto& db_cfg = config.GetPostgresConfig();
    const auto& grpc_cfg = config.GetGrpcConfig();
    const auto& seaweedfs_cfg = config.GetSeaweedFSConfig();

    std::string server_address = grpc_cfg.logic_server_listen_addr;
    
    RedisClient redis;
    if(!redis.connect(redis_cfg.host, redis_cfg.port, redis_cfg.password)){
        spdlog::error("Failed to connect to Redis. Exiting.");
        return 1;
    }
    
    DbClient db;
    const std::string seaweed_master_endpoint = seaweedfs_cfg.master_endpoint;
    const std::string seaweed_public_endpoint = seaweedfs_cfg.public_endpoint;
    const std::string seaweed_bucket = GetEnvOrDefault("SEAWEED_BUCKET", "letschat");
    const std::string seaweed_access_key = GetEnvOrDefault("SEAWEED_ACCESS_KEY", "");
    const std::string seaweed_secret_key = GetEnvOrDefault("SEAWEED_SECRET_KEY", "");

    S3Client s3(seaweed_master_endpoint,
                seaweed_public_endpoint,
                seaweed_bucket,
                seaweed_access_key,
                seaweed_secret_key);

    if (!s3.CheckConnectivity()) {
        spdlog::error("SeaweedFS unavailable, logic server exit with code {}", kStorageConnectErrorCode);
        return kStorageConnectErrorCode;
    }

    std::string db_conn_str = "dbname=" + db_cfg.dbname + 
                              " user=" + db_cfg.user + 
                              " password=" + db_cfg.password + 
                              " hostaddr=" + db_cfg.host + 
                              " port=" + std::to_string(db_cfg.port);
    if(!db.Connect(db_conn_str)){
        spdlog::error("failed to connect to database , server exit");
        return 2;
    }
    LogicServiceImpl service(&redis, &db ,&s3);

    ServerBuilder builder;
    builder.AddListeningPort(server_address , grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    spdlog::info("logic Server is listening on {}", server_address);

    server->Wait();
    return 0;
}

int main(){
    spdlog::set_pattern("[%H:%H:%S%z][%^%L%$][Logic]%v");

    return RunServer();
}
