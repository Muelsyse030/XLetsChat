#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "im.pb.h"
#include "im_service.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using im::LogicService;
using im::LoginReq;
using im::LoginRes;

class LogicServiceImpl final : public LogicService::Service{
    Status Login(ServerContext* context , const LoginReq* request , LoginRes* reply) override {
        spdlog::info("PRC Login Request: Uid= {} , token = {} , device = {}" , request->uid() , request->token() , request->device_id());

        if(request->token() == "123456"){
            //登录成功
            reply->set_err_code(im::ErrorCode::ERR_SUCCESS);
            reply->set_session_id("sess_" + std::to_string(request->uid())); //生成一个假session
            reply->set_server_time(time(nullptr));
            spdlog::info("->Login Success: uid = {}" , request->uid());
        }
        else{
            reply->set_err_code(im::ErrorCode::ERR_AUTH_FAIL);
            reply->set_err_msg("Invalid token");
            spdlog::info("->Login Failed: uid = {}" , request->uid());
        }
        return Status::OK;
    }
};

void RunServer(){
    std::string server_address("0.0.0.0:50051");
    LogicServiceImpl service;

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