#include <uwebsockets/App.h>
#include <spdlog/spdlog.h>
#include "packet.h"
#include "im.pb.h"
#include "im_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <thread>

std::unique_ptr<im::LogicService::Stub> logic_stub;


struct PerSocketData {
    int64_t uid = 0; 
};

class SessionManager{
    public:
    static SessionManager& GetInstance(){
        static SessionManager instance;
        return instance;
    }
    void AddSession(int64_t uid, uWS::WebSocket<false, true, PerSocketData>* ws){
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[uid] = ws;
    }

    void RemoveSession(int64_t uid){
        std::lock_guard<std::mutex> lock(mutex_);
        if(sessions_.find(uid) != sessions_.end()){
            sessions_.erase(uid);
        }
    }
    bool PushToUser(int64_t uid, const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(uid);
        if (it != sessions_.end()) {
            it->second->send(data, uWS::OpCode::BINARY);
            return true;
        }
        return false;
    }
    private:
    std::mutex mutex_;
    std::unordered_map<int64_t, uWS::WebSocket<false, true, PerSocketData>*> sessions_;
};
class GatewayServiceImpl final : public im::GatewayService::Service{
    grpc::Status PushMsg(grpc::ServerContext* context , const im::PushMsgReq* request , im::PushMsgRes* reply){
        spdlog::info("RPC PushMsg recv : ToUId = {} , len = {}",request->to_uid() , request->content());
        bool success = SessionManager::GetInstance().PushToUser(request->to_uid() , request->content());
        if(success){
            reply->set_err_code(0);
            spdlog::info("<<< pushed to client successfully");
        }
        else{
            reply->set_err_code(-1);
            reply->set_err_msg("User not found locally");
            spdlog::warn("<<<Push to User {} not found on this gateway" , request->to_uid());
        }
        return grpc::Status::OK;
    }
};

void RunGrpcServer(){
        std::string server_address = "0.0.0.0:50052";
        GatewayServiceImpl service;

         grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address , grpc::InsecureServerCredentials());
        builder.RegisterService(&service);

        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        spdlog::info("Gateway gRpc Server listening on {}", server_address);
        server->Wait();
    }

int main() {

    spdlog::set_pattern("[%H:%M:%S %z] [%^%L%$] [Gateway-uWS] %v");
    spdlog::info("Starting uWebSockets Gateway on port 8000...");

    std::thread grpc_thread(RunGrpcServer);
    grpc_thread.detach();

    //初始化gRpc Channel
    std::string logic_server_address = "0.0.0.0:50051";
    auto channel = grpc::CreateChannel(logic_server_address , grpc::InsecureChannelCredentials());
    logic_stub = im::LogicService::NewStub(channel);
    spdlog::info("Connected to Logic Server at {}", logic_server_address);

    uWS::App()
        .ws<PerSocketData>("/ws", {
            
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 16 * 1024, 
            .idleTimeout = 60,             

            .open = [](auto *ws) {
                spdlog::info("New Connection!");
            },
            .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
                if (opCode != uWS::OpCode::BINARY) {
                    spdlog::warn("Drop non-binary message");
                    return;
                }

                // 长度校验
                if (message.length() < HEADER_LEN) {
                    spdlog::warn("Packet too short: {}", message.length());
                    return;
                }
                const uint8_t* buffer = reinterpret_cast<const uint8_t*>(message.data());
                PacketHeader header = PacketHelper::DecodeHeader(buffer);
                
                if(header.cmd_id == 0x1001){
                    im::LoginReq req;
                    if(req.ParseFromArray(buffer + HEADER_LEN , header.length - HEADER_LEN)){
                        spdlog::info(">>recv logincReq: uid = {}" , req.uid());
                        im::LoginRes res;
                        grpc::ClientContext context;
                        grpc::Status status = logic_stub->Login(&context , req , &res);
                        if(status.ok()){
                            if(res.err_code() == im::ErrorCode::ERR_SUCCESS){
                                spdlog::info("<<< rpc login success ! Session_id = {}" , res.session_id());
                                ws->getUserData()->uid = req.uid();
                                SessionManager::GetInstance().AddSession(req.uid() , ws);
                            }
                            else{
                                spdlog::warn("<<< RPC Login Failed: {}", res.err_msg());
                            }
                            std::string res_body;
                            res.SerializeToString(&res_body);

                            PacketHeader res_header;
                            res_header.length = HEADER_LEN + res_body.size();
                            res_header.version = 0x1002;
                            res_header.seq_id = header.seq_id;

                            uint8_t head_buffer[HEADER_LEN];
                            PacketHelper::EncodeHeader(res_header , head_buffer);

                            std::string send_data;
                            send_data.append((char*)head_buffer , HEADER_LEN);
                            send_data.append(res_body);

                            ws->send(send_data , uWS::OpCode::BINARY);
                        }
                        else{
                            spdlog::error("RPC Call Failed: {} - {}", (int)status.error_code(), status.error_message());
                        }
                    }   
                }
                else if(header.cmd_id == 0x1003){
                    im::MsgSendReq req;
                    if((message.length() >= HEADER_LEN + (header.length - HEADER_LEN)) &&
                        req.ParseFromArray(buffer + HEADER_LEN , message.length() - HEADER_LEN)){
                            int64_t current_id = ws->getUserData()->uid;
                            if(current_id == 0){
                                spdlog::warn("User not logged in , frop msg");
                                return;
                            }
                            req.mutable_msg()->set_from_uid(current_id);
                            spdlog::info(">> Recv MsgSendReq: to {} content = {}" ,req.msg().to_uid() , req.msg().content());

                            im::MsgSendRes res;
                            grpc::ClientContext context;
                            grpc::Status status = logic_stub->SendMsg(&context , req , &res);
                            if(status.ok()){
                                std::string res_body;
                                res.SerializeToString(&res_body);

                                PacketHeader res_header;
                                res_header.length = HEADER_LEN + res_body.size();
                                res_header.version = header.version;
                                res_header.cmd_id = 0x1004;
                                res_header.seq_id = header.seq_id;

                                uint8_t head_buf[HEADER_LEN];
                                PacketHelper::EncodeHeader(res_header , head_buf);

                                std::string send_data;
                                send_data.append((char*)head_buf , HEADER_LEN);
                                send_data.append(res_body);

                                ws->send(send_data , uWS::OpCode::BINARY);
                                spdlog::info("<<< Reply MsgSendRes : OK");
                            }
                            else{
                                spdlog::error("RPC SendMsg Failed: {}" , status.error_message());
                            }
                        }
                }
            },
            .close = [](auto *ws, int code, std::string_view message) {
                SessionManager::GetInstance().RemoveSession(ws->getUserData()->uid);
                spdlog::info("Connection closed. UID={}", ws->getUserData()->uid);
            }
        })
        
        .listen(8000, [](auto *listen_socket) {
            if (listen_socket) {
                spdlog::info("Listening on port 8000 successfully");
            } else {
                spdlog::error("Failed to listen on port 8000");
                exit(-1);
            }
        })
        
        .run();
    return 0;
}