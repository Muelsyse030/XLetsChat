#pragma once
#include <cstdint>
#include <vector>
#include <arpa/inet.h>
#include <string>
#include <cstring>

const size_t HEADER_LEN = 12;

struct PacketHeader {
    uint32_t length;
    uint16_t version;
    uint16_t cmd_id;
    uint32_t seq_id;
};

class PacketHelper{
    public:
        static void EncodeHeader(const PacketHeader& header , uint8_t*buffer){
            uint32_t net_len = htonl(header.length);
            uint16_t net_ver = htons(header.version);
            uint16_t net_cmd = htons(header.cmd_id);
            uint32_t net_seq = htonl(header.seq_id);

            memcpy(buffer , &net_len , sizeof(net_len));
            memcpy(buffer + 4 , &net_ver , sizeof(net_ver));
            memcpy(buffer + 6 , &net_cmd , sizeof(net_cmd));
            memcpy(buffer + 8 , &net_seq , sizeof(net_seq));
        }
        static PacketHeader DecodeHeader(const uint8_t* buffer){
            PacketHeader h;
            h.length = ntohl(*reinterpret_cast<const uint32_t*>(buffer));
            h.version = ntohs(*reinterpret_cast<const uint16_t*>(buffer + 4));
            h.cmd_id = ntohs(*reinterpret_cast<const uint16_t*>(buffer + 6));
            h.seq_id = ntohl(*reinterpret_cast<const uint32_t*>(buffer + 8));
            return h;
        }
};