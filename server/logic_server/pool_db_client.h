#pragma once
#include "db_pool.h"
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include "im.pb.h"

class PooledDbClient {
public:

    explicit PooledDbClient(DbPool* pool) : pool_(pool) {}
    bool Execute(const std::string& sql);
    std::string GetUserPassword(int64_t uid);
    bool SaveMessage(const std::string& msg_id, int64_t from_uid, int64_t to_uid, const std::string& content);
    std::vector<im::ChatMsg> GetofflineMsgs(int64_t uid, int64_t last_msg_id);
    int64_t CreateUser(const std::string& username, const std::string& password, const std::string& email);
    bool CheckUserByEmail(const std::string& email, const std::string& password, im::HttpLoginRes& user_info);
    bool CreateFriendRequest(int64_t from_uid, int64_t to_uid, const std::string& reason, int64_t& out_req_id);
    std::vector<im::FriendRequest> GetFriendRequestsForUser(int64_t uid);
    bool AcceptFriendRequest(int64_t req_id);
    bool AreFriends(int64_t uid1, int64_t uid2);
    std::vector<int64_t> ListFriends(int64_t uid);
    bool SaveP2PMessage(const std::string& msg_id , int64_t from_uid , int64_t to_uid , const std::string& content);
    std::vector<im::ChatMsg> GetP2PMsgs(int64_t uid , int64_t last_msg_id);
    bool SaveGroupMessage(const std::string& msg_id , int64_t group_id , int64_t from_uid , const std::string& content);
    std::vector<im::ChatMsg> GetGroupMsgs(int64_t group_id , int64_t last_msg_id);
    int64_t CreateGroup(int64_t owber_uid , const std::string& group_name);
    bool AddGroupMember(int64_t group_id , int64_t member_uid);
    bool RemoveGroupMember(int64_t group_id , int64_t member_uid);
    std::vector<int64_t> GetGroupMembers(int64_t group_id);
    std::vector<int64_t> GetUserGroups(int64_t uid);
    bool IsGroupMember(int64_t group_id , int64_t uid);

private:

    DbPool* pool_;
    
};