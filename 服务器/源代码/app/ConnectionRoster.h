#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Internalconnection.h"
#include "connect_book.h"

// 从名册里取出的身份视图。
struct RosterIdentity {
    uint64_t identity_id = 0;   // 业务层身份 ID（账号级）
    uint64_t user_id = 0;       // 数据库用户主键，查库用
    uint64_t virtual_fd = 0;    // 框架当前虚拟连接 FD
    std::string username;
};

// 连接身份名册：业务层维护的 虚拟连接 FD -> 业务身份 ID 映射。
//
// 框架的 connect_book 只登记"有哪些连接、怎么投递"，不关心连接背后是谁；
// 这张表补的正是身份那一层，因此两者是并列的，不是同一张表。
//
// 维护的不变量：
//   1. 一个业务身份 ID 同一时刻最多对应一条在线连接（账号级一对一）。
//   2. 表里每条记录都指向框架名册中仍活着的连接；判定死活一律用
//      连接对象本身去比对，不看连接号，避免 socket 号复用造成误判。
class ConnectionRoster {
public:
    using Conn = std::shared_ptr<Internalconnection>;

    enum class LoginResult {
        Ok,
        AlreadyOnline,   // 同一身份已有在线连接，调用方应拒绝后来者
        Invalid,         // 连接号或身份非法
    };

    // 原子完成"查重 + 写入"。existing_fd 传出已在线的 virtual_fd。
    LoginResult login(uint64_t virtual_fd, std::weak_ptr<Internalconnection> conn,
                      uint64_t identity_id, uint64_t user_id,
                      const std::string& username, uint64_t& existing_fd);

    // 业务请求的鉴权入口：按当前连接查身份。
    // conn 传入发起请求的连接对象，用于确认这个连接号没被复用。
    bool resolve(uint64_t virtual_fd, const Conn& conn,
                 RosterIdentity& identity);

    // 主动登出：清掉这条连接的身份。
    bool logout(uint64_t virtual_fd);

    // 按业务身份投递消息。供框架外的任意线程调用（异步结果、通知）。
    bool send_to_identity(uint64_t identity_id, const std::string& message);

    // 异步结果投递：优先送回发起请求的那条连接（连接号与连接对象都要对得上），
    // 这条连接已经断开时才退回到该身份当前所在的连接。
    bool deliver(uint64_t virtual_fd,
                 const std::weak_ptr<Internalconnection>& conn,
                 uint64_t identity_id, const std::string& message);

    // 消费 connect_book 的变更，清理断开的连接。
    void apply_changes(const std::vector<net_changer>& changes);

    // 全量对账兜底：清掉框架名册里已经不存在的条目，返回清理数量。
    size_t reconcile();

    size_t online_count() const;
    std::vector<RosterIdentity> online_snapshot() const;

private:
    struct Entry {
        uint64_t identity_id = 0;
        uint64_t user_id = 0;
        uint64_t virtual_fd = 0;
        std::string username;
        std::weak_ptr<Internalconnection> conn;
    };

    // 以下两个 _locked 版本要求调用方已持有 mutex_。
    bool alive_locked(const Entry& entry) const;
    void erase_locked(uint64_t virtual_fd);

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, Entry> by_vfd_;
    std::unordered_map<uint64_t, uint64_t> by_identity_;
};
