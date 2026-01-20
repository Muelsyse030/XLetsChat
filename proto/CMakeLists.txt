find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)


add_library(im_proto_lib 
    im.proto 
    im_service.proto
)

target_link_libraries(im_proto_lib PUBLIC 
    protobuf::libprotobuf 
    gRPC::grpc++
)


target_include_directories(im_proto_lib PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR} # 生成的代码在这里
    ${CMAKE_CURRENT_SOURCE_DIR} # 原始代码在这里
)

protobuf_generate(TARGET im_proto_lib)

protobuf_generate(
    TARGET im_proto_lib 
    LANGUAGE grpc 
    PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
)