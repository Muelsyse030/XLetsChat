LetsChat IM 服务端开发文档

版本: v0.1.0 (Alpha)
状态: 核心链路跑通 (登录/单聊/推送/离线漫游)
1. 项目概述 (Overview)

LetsChat 是一个基于 C++ 开发的高性能、分布式即时通讯(IM)后端系统。采用微服务架构设计，将接入层与逻辑层分离，支持水平扩展。
1.1 技术栈 (Tech Stack)

    开发语言: C++17

    构建工具: CMake

    接入网关: uWebSockets (高性能 WebSocket 库)

    RPC 框架: gRPC (Protobuf)

    数据库: PostgreSQL (持久化存储)

    缓存: Redis (会话管理 & 路由)

    日志: spdlog

1.2 系统架构 (Architecture)

系统采用典型的分层架构，主要包含以下组件：

    Gateway Server (接入层)

        监听 WebSocket 端口 (8000)。

        维护与客户端的长连接 (Session)。

        负责协议的解包/封包。

        内部启动 gRPC Server (50052)，接收 Logic 层的反向推送请求。

        无状态: 仅在内存维护 uid -> socket 映射，不操作 DB。

    Logic Server (逻辑层)

        提供 gRPC Service (50051)。

        处理核心业务：登录鉴权、消息落地、消息同步。

        无状态: 通过 Redis 共享用户状态，可水平扩展。

    Data Layer (数据层)

        Redis: 存储 UserID -> GatewayAddress 的路由信息。

        PostgreSQL: 存储用户信息 (t_user) 和 历史消息 (t_chat_msg)。

2. 通信协议 (Protocol)

所有 WebSocket 通信采用二进制流格式，严格遵循 LTV (Length-Type-Value) 模型以解决粘包问题。
2.1 封包结构 (Packet Header)

每个数据包头部固定 12 字节：
字段	长度	类型	描述
Length	4 Bytes	uint32	包总长度 (Header + Body)，网络字节序 (Big-Endian)
Version	2 Bytes	uint16	协议版本 (如 1)
CmdId	2 Bytes	uint16	命令字，区分业务类型
SeqId	4 Bytes	uint32	序列号，用于请求/响应匹配
Body	N Bytes	bytes	Protobuf 序列化后的数据
2.2 命令字定义 (Command IDs)

基于 proto/im.proto 定义：
Cmd ID	描述	Protobuf Message	方向	备注
0x1001	登录请求	LoginReq	Client -> Server	携带 token
0x1002	登录响应	LoginRes	Server -> Client	返回 SessionID
0x1003	发送消息	MsgSendReq	Client -> Server	上行消息
0x1004	发送响应	MsgSendRes	Server -> Client	服务器 ACK (送达回执)
0x1005	消息推送	MsgPush	Server -> Client	下行通知 (别人发给我的)
0x1006	离线同步请求	SyncMsgReq	Client -> Server	拉取历史消息
0x1007	离线同步响应	SyncMsgRes	Server -> Client	返回消息列表
3. 数据库设计 (Database Schema)
3.1 PostgreSQL (持久化)

运行在 Docker 容器 minqq_pg 中。

表 1: 用户表 (t_user)
SQL

CREATE TABLE t_user (
    id BIGSERIAL PRIMARY KEY,           -- 用户 UID
    username VARCHAR(50) UNIQUE NOT NULL,
    password VARCHAR(50) NOT NULL,      -- 密码 (生产环境需加盐 Hash)
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

表 2: 消息表 (t_chat_msg)
SQL

CREATE TABLE t_chat_msg (
    id BIGSERIAL PRIMARY KEY,           -- 自增主键
    msg_id VARCHAR(64) NOT NULL,        -- 全局唯一消息 ID (应用层生成)
    from_uid BIGINT NOT NULL,           -- 发送者
    to_uid BIGINT NOT NULL,             -- 接收者
    content TEXT,                       -- 消息内容
    msg_type INT DEFAULT 1,             -- 1:文本, 2:图片
    create_time BIGINT NOT NULL         -- 发送时间戳
);

3.2 Redis (缓存与路由)

运行在 Docker 容器 LetsChat_redis 中。

    Key: IM:USER:SESS:{uid}

    Value: {gateway_ip}:{grpc_port} (例如 127.0.0.1:50052)

    作用: Logic Server 通过此 Key 查找目标用户连接在哪个 Gateway 上。

4. 内部 RPC 接口 (Microservices)

基于 proto/im_service.proto 定义。
4.1 LogicService (Gateway -> Logic)

监听端口: 50051

    Login(LoginReq): 校验 DB 密码，写入 Redis Session。

    SendMsg(MsgSendReq): 消息入库 (Postgres)，查询路由 (Redis)，发起推送。

    SyncMsg(SyncMsgReq): 查询 Postgres 历史消息表，返回未读列表。

4.2 GatewayService (Logic -> Gateway)

监听端口: 50052

    PushMsg(PushMsgReq): 接收 Logic 发来的二进制包，通过 WebSocket 推送给指定 UID 的客户端。

5. 快速开始 (Getting Started)
5.1 环境准备

    Linux / WSL2 / macOS

    Docker & Docker Compose

    CMake >= 3.20

    g++ (支持 C++17)

5.2 启动基础组件
Bash

cd docker
docker compose up -d
# 确保 postgres 和 redis 容器已启动

5.3 数据库初始化 (首次运行)
Bash

# 进入 PG 容器
docker exec -it minqq_pg psql -U admin -d LetsChat

# 粘贴上文 "3.1 PostgreSQL" 中的 SQL 建表语句
# 并插入测试用户:
INSERT INTO t_user (username, password) VALUES ('user1', '123456');
INSERT INTO t_user (username, password) VALUES ('user2', '123456');

5.4 编译与运行
Bash

# 回到项目根目录
mkdir build && cd build
cmake ..
make -j4

# 启动 Logic Server (先)
./bin/logic_server

# 启动 Gateway Server (后)
./bin/gateway_server

6. 待办事项 (TODO)

    [ ] 群聊功能: 新增群成员关系表，实现消息扩散。

    [ ] 心跳保活: 实现 Heartbeat (0x0001) 处理，超时断开连接。

    [ ] MinIO 集成: 支持图片/文件上传。

    [ ] 连接池: 优化 DB 和 Redis 的连接管理。

    [ ] 配置加载: 将硬编码的 IP/Port 移至 YAML 配置文件。