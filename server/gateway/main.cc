#include <uwebsockets/App.h>
#include <spdlog/spdlog.h>
#include "packet.h"
#include "im.pb.h"
#include "im_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>

std::unique_ptr<im::LogicService::Stub> logic_stub;

// 定义每个连接的私有数据 (Session)
struct PerSocketData {
    int64_t uid = 0; // 登录后记录 UID
};

int main() {

    spdlog::set_pattern("[%H:%M:%S %z] [%^%L%$] [Gateway-uWS] %v");
    spdlog::info("Starting uWebSockets Gateway on port 8000...");
    //初始化gRpc Channel
    std::string logic_server_address = "0.0.0.0:50051";
    auto channel = grpc::CreateChannel(logic_server_address , grpc::InsecureChannelCredentials());
    logic_stub = im::LogicService::NewStub(channel);
    spdlog::info("Connected to Logic Server at {}", logic_server_address);

    uWS::App()
        .ws<PerSocketData>("/ws", {
            // 1. 配置项
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 16 * 1024, // 最大包体 16KB
            .idleTimeout = 60,             // 60秒无心跳断开

            // 2. 连接建立 (Open)
            .open = [](auto *ws) {
                spdlog::info("New Connection!");
            },

            // 3. 收到消息 (Message) - 核心逻辑
            .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
                // 必须是二进制消息
                if (opCode != uWS::OpCode::BINARY) {
                    spdlog::warn("Drop non-binary message");
                    return;
                }

                // 长度校验
                if (message.length() < HEADER_LEN) {
                    spdlog::warn("Packet too short: {}", message.length());
                    return;
                }
                // 解析 Header (注意：uWS 的 message 是 string_view，可以直接转 const uint8_t*)
                const uint8_t* buffer = reinterpret_cast<const uint8_t*>(message.data());
                PacketHeader header = PacketHelper::DecodeHeader(buffer);
                //处理登录请求(0x1001)
                if(header.cmd_id == 0x1001){
                    im::LoginReq req;
                    if(req.ParseFromArray(buffer + HEADER_LEN , header.length - HEADER_LEN)){
                        spdlog::info(">>recv logincReq: uid = {}" , req.uid());
                        //调用logic server的Login接口
                        im::LoginRes res;
                        grpc::ClientContext context;
                        grpc::Status status = logic_stub->Login(&context , req , &res);
                        if(status.ok()){
                            if(res.err_code() == im::ErrorCode::ERR_SUCCESS){
                                spdlog::info("<<< rpc login success ! Session_id = {}" , res.session_id());
                                ws->getUserData()->uid = req.uid();
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
            },
            .close = [](auto *ws, int code, std::string_view message) {
                spdlog::info("Connection closed. UID={}", ws->getUserData()->uid);
            }
        })
        // 监听端口
        .listen(8000, [](auto *listen_socket) {
            if (listen_socket) {
                spdlog::info("Listening on port 8000 successfully");
            } else {
                spdlog::error("Failed to listen on port 8000");
                exit(-1);
            }
        })
        // 启动事件循环 (阻塞)
        .run();
    return 0;
}