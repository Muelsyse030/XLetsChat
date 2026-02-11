#pragma once

#include <arpa/inet.h>
#include <cstring>
#include <mutex>
#include <netdb.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

struct ParsedEndpoint {
    std::string scheme;
    std::string host;
    int port;
};

inline std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t start = json.find(search);
    if (start == std::string::npos) {
        search = "\"" + key + "\":";
        start = json.find(search);
        if (start == std::string::npos) {
            return "";
        }
        start += search.length();
        size_t end = json.find_first_of(",}", start);
        return json.substr(start, end - start);
    }
    start += search.length();
    size_t end = json.find("\"", start);
    return json.substr(start, end - start);
}

inline ParsedEndpoint ParseEndpoint(const std::string& endpoint, const std::string& default_scheme, int default_port) {
    ParsedEndpoint parsed{default_scheme, endpoint, default_port};

    std::string remain = endpoint;
    size_t scheme_pos = endpoint.find("://");
    if (scheme_pos != std::string::npos) {
        parsed.scheme = endpoint.substr(0, scheme_pos);
        remain = endpoint.substr(scheme_pos + 3);
    }

    size_t slash_pos = remain.find('/');
    if (slash_pos != std::string::npos) {
        remain = remain.substr(0, slash_pos);
    }

    size_t port_pos = remain.rfind(':');
    if (port_pos != std::string::npos) {
        parsed.host = remain.substr(0, port_pos);
        parsed.port = std::stoi(remain.substr(port_pos + 1));
    } else {
        parsed.host = remain;
    }

    return parsed;
}

class S3Client {
public:
    S3Client(const std::string& master_endpoint,
             const std::string& public_endpoint,
             const std::string& bucket,
             const std::string& access_key,
             const std::string& secret_key)
        : master_(ParseEndpoint(master_endpoint, "http", 9333)),
          public_(ParseEndpoint(public_endpoint, "http", 8080)),
          bucket_(bucket),
          access_key_(access_key),
          secret_key_(secret_key) {}

    bool CheckConnectivity() {
        std::string response = HttpGet(master_.host, master_.port, "/dir/status");
        if (response.empty()) {
            spdlog::error("SeaweedFS connectivity check failed: endpoint={}:{}", master_.host, master_.port);
            return false;
        }
        spdlog::info("SeaweedFS connectivity check ok: endpoint={}:{}", master_.host, master_.port);
        return true;
    }

    std::string GetPresignedPutUrl(const std::string& object_name, int expires_in_seconds = 600) {
        (void)expires_in_seconds;
        std::string assign_path = "/dir/assign?count=1&collection=" + bucket_;
        std::string response = HttpGet(master_.host, master_.port, assign_path);

        if (response.empty()) {
            spdlog::error("SeaweedFS assign failed: empty response from {}:{}", master_.host, master_.port);
            return "";
        }

        std::string fid = ExtractJsonValue(response, "fid");
        if (fid.empty()) {
            spdlog::error("SeaweedFS assign failed: invalid response={} ", response);
            return "";
        }

        {
            std::lock_guard<std::mutex> lock(fid_map_mu_);
            object_fid_map_[object_name] = fid;
        }

        std::string final_url = BuildPublicBaseUrl() + "/" + fid + "?collection=" + bucket_ + "&filename=" + object_name;
        if (!access_key_.empty()) {
            final_url += "&accessKey=" + access_key_;
        }
        if (!secret_key_.empty()) {
            final_url += "&secretKey=" + secret_key_;
        }

        spdlog::info("SeaweedFS assign success: object={} fid={} upload_url={}", object_name, fid, final_url);
        return final_url;
    }

    std::string GetDownloadUrl(const std::string& object_name) {
        std::string fid;
        {
            std::lock_guard<std::mutex> lock(fid_map_mu_);
            auto it = object_fid_map_.find(object_name);
            if (it != object_fid_map_.end()) {
                fid = it->second;
            }
        }

        if (fid.empty()) {
            spdlog::warn("SeaweedFS download url fallback: missing fid for object={}.", object_name);
            return "";
        }

        return BuildPublicBaseUrl() + "/" + fid + "?collection=" + bucket_;
    }

private:
    ParsedEndpoint master_;
    ParsedEndpoint public_;
    std::string bucket_;
    std::string access_key_;
    std::string secret_key_;
    std::mutex fid_map_mu_;
    std::unordered_map<std::string, std::string> object_fid_map_;

    std::string BuildPublicBaseUrl() const {
        return public_.scheme + "://" + public_.host + ":" + std::to_string(public_.port);
    }

    std::string HttpGet(const std::string& host, int port, const std::string& path) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return "";
        }

        struct hostent* server = gethostbyname(host.c_str());
        if (server == NULL) {
            close(sock);
            return "";
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
            if (bytes <= 0) {
                break;
            }
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
