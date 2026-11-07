#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "CacheModule.h"
#include "ConnectionRoster.h"
#include "EventTask.h"
#include "OrderEngine.h"
#include "RedisClient.h"

// 票务业务层入口：把设计图里的六个模块组装到一起。
//
//   登录模块             register_user / login
//   连接身份认证模块     authenticate（每个业务请求先按连接查身份）
//   连接身份名册模块     ConnectionRoster（账号级一对一 + 按身份投递）
//   商品浏览模块         catalog_list（缓存 + 查库，纯短任务，不占常驻线程）
//   缓存模块             CacheModule
//   订单处理模块         OrderEngine（独立长线程 + 内部专有数据库路径）
class TicketService {
public:
    TicketService();
    ~TicketService();

    EventTask handle(int request_id, std::string message);

private:
    struct Session {
        uint64_t user_id = 0;
        uint64_t identity_id = 0;
        std::string username;
    };

    EventTask register_user(std::string username, std::string password);
    EventTask login(std::string username, std::string password);
    EventTask login_challenge(std::string username);
    EventTask login_proof(std::string username, std::string proof);
    EventTask establish_session(std::string username, uint64_t user_id,
                                std::string response_command);
    EventTask catalog_list();
    EventTask reserve(std::string token, std::string ticket_type_id,
                      std::string quantity, std::string idempotency_key);
    EventTask refund(std::string token, std::string order_no,
                     std::string quantity_text, std::string idempotency_key,
                     std::string ticket_type_id);
    EventTask order_status(std::string token, std::string order_no);
    EventTask my_orders(std::string token);
    EventTask whoami();
    EventTask logout();
    EventTask notice_push(std::string target_identity, std::string message);

    bool authenticate(const std::string& token, Session& session);
    std::string new_order_no();
    std::string new_refund_no();

    // 名册维护线程：消费框架连接变更，并按周期全量对账。
    void roster_loop();

    std::mutex session_mutex_;
    std::unordered_map<std::string, Session> sessions_;
    std::atomic<uint64_t> order_sequence_{0};
    std::atomic<uint64_t> refund_sequence_{0};

    struct LoginChallenge {
        uint64_t virtual_fd = 0;
        std::weak_ptr<Internalconnection> conn;
        std::string username;
        std::string nonce;
        std::string salt;
        std::chrono::steady_clock::time_point expires_at;
    };
    std::mutex challenge_mutex_;
    std::unordered_map<uint64_t, LoginChallenge> challenges_;

    // 缓存模块：商品浏览与订单额度共用同一套键。
    CacheModule cache_;

    ConnectionRoster roster_;
    std::thread roster_thread_;
    std::atomic<bool> roster_stop_{false};

    // 兼容开关：旧的 token 鉴权路径。默认开，设 TICKET_TOKEN_FALLBACK=0
    // 可切成"只认连接身份"。
    const bool token_fallback_;

    // 准入开关：默认走 Redis 原子预扣闸门。设 TICKET_ADMISSION_REDIS=0 时
    // 完全不碰 Redis，请求直接入队，改由订单引擎的数据库条件更新兜底防超卖。
    const bool admission_redis_;

    RedisClient redis_;

    // 订单处理模块：最后声明，最先析构，先于名册停掉，避免投递时名册已销毁。
    std::unique_ptr<OrderEngine> order_engine_;
};
