#pragma once
#include <string>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <sstream>
#include <spdlog/spdlog.h>

std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t start = json.find(search);
    if (start == std::string::npos) {
        search = "\"" + key + "\":";
        start = json.find(search);
        if (start == std::string::npos) return "";
        start += search.length();
        size_t end = json.find_first_of(",}", start);
        return json.substr(start, end - start);
    }
    start += search.length();
    size_t end = json.find("\"", start);
    return json.substr(start, end - start);
}

class S3Client {
public:
    S3Client(const std::string& endpoint, const std::string& access_key, const std::string& secret_key, const std::string& bucket)
        : master_host_("weed_master"), master_port_(9333) {
    }

    std::string GetPresignedPutUrl(const std::string& object_name, int expires_in_seconds = 600) {
        std::string response = HttpGet(master_host_, master_port_, "/dir/assign");
        
        if (response.empty()) {
            spdlog::error("SeaweedFS: Failed to connect to master");
            return "";
        }
        std::string fid = ExtractJsonValue(response, "fid");
        
        if (fid.empty()) {
            spdlog::error("SeaweedFS: Invalid response: {}", response);
            return "";
        }
        std::string final_url = "http://127.0.0.1:8080/" + fid;
        
        spdlog::info("SeaweedFS Assigned: fid={} final_url={}", fid, final_url);
        return final_url;
    }

    std::string GetDownloadUrl(const std::string& object_name) {
        return "http://127.0.0.1:8080/" + object_name;
    }

private:
    std::string master_host_;
    int master_port_;

    std::string HttpGet(const std::string& host, int port, const std::string& path) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";

        struct hostent* server = gethostbyname(host.c_str());
        if (server == NULL) {
             server = gethostbyname("127.0.0.1"); // Fallback
             if (server == NULL) {
                 close(sock);
                 return "";
             }
        }

        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        serv_addr.sin_port = htons(port);

        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            return "";
        }

        std::string request = "GET " + path + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Connection: close\r\n\r\n";

        send(sock, request.c_str(), request.length(), 0);

        std::string response;
        char buffer[4096];
        while (true) {
            int bytes = recv(sock, buffer, 4095, 0);
            if (bytes <= 0) break;
            buffer[bytes] = 0;
            response += buffer;
        }
        close(sock);

        size_t header_end = response.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            return response.substr(header_end + 4);
        }
        return response;
    }
};