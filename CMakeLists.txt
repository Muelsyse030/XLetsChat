cmake_minimum_required(VERSION 3.20)
project(min-qq VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- 依赖查找 ---
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)

# [修正点] 首字母大写
find_package(Workflow REQUIRED CONFIG) 

find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)

# --- 包含子项目 ---
add_subdirectory(server/gateway)
add_subdirectory(server/logic_server)