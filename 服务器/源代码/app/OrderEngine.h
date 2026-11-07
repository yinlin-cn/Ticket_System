#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "CacheModule.h"
#include "Internalconnection.h"
#include "RedisClient.h"
#include "connect_pool.h"

// 待处理订单：设计图"待处理订单队列集合"里的一个元素。
//
// 能进到这里的订单一定已经在 Redis 里预扣过额度，因此写库不会再出现超卖，
// 批处理只负责"落库 + 回填缓存 + 回送结果"。
//
// 投递靠 identity_id（业务身份，账号级）走连接身份名册，不直接持有连接对象：
// 订单在队列里可能停留一段时间，期间客户端可能已经断开或重连。
struct PendingOrder {
    std::string order_no;
    uint64_t user_id = 0;        // 数据库用户主键
    uint64_t identity_id = 0;    // 业务身份 ID（账号级）
    uint64_t ticket_type_id = 0; // 商品（票种）ID
    int quantity = 0;
    std::string idempotency_key;
    uint64_t virtual_fd = 0;     // 提交请求时的框架连接号
    std::weak_ptr<Internalconnection> conn;
};

// 退款请求先不访问数据库，和购票请求一样由框架短任务投递到独立长线程。
// ticket_type_id 由客户端随订单信息带回，调度器因此能在同一票种内保证
// "先处理退款，再处理购票"。
struct PendingRefund {
    std::string refund_no;
    std::string order_no;
    uint64_t user_id = 0;
    uint64_t identity_id = 0;
    uint64_t ticket_type_id = 0;
    int quantity = 0;
    std::string idempotency_key;
    uint64_t virtual_fd = 0;
    std::weak_ptr<Internalconnection> conn;
    int attempts = 0;
};

// 订单处理模块：设计图里的"待处理订单队列集合 + 内部专有数据库模块 +
// 票务信息处理返回模块"三件套。
//
//   1. 每个 ticket_type 一条订单队列；某条队列有批次在写库（in-flight）时，
//      新订单照样入队，但要等这一批落库后才被取走。
//   2. 调度线程按 tick 扫描各队列，先从缓存模块读该商品的可用额度（额度就是
//      库里真实剩余量），再按额度取一批（不超过 batch_max）订单，把队列置为
//      不可访问并投给自己内部的长线程池。
//   3. 长线程用模块自建的 connect_pool 写库：一条 UPDATE（带 remaining>=n 的
//      条件，兜底防超卖）+ 一条多值 INSERT 放在同一个事务里，提交后把真实余量
//      回写缓存，再按停止原因处理队列，最后通过投递回调把结果送回客户端。
//
// 这条 DB 路径完全独立于框架的 DB 连接池/线程池，因此别的业务堵住不会连带
// 堵住订单落库；反过来订单落库也不吃框架的 DB 准入额度。
class OrderEngine {
public:
    struct Options {
        int db_connections = 8;        // 内部专有连接池大小
        int db_workers = 6;            // 内部线程池线程数
        std::size_t batch_max = 500;   // 单批最大订单数（默认订单读取数）
        int tick_ms = 2;               // 批处理循环线程的扫描周期
        int max_retry = 8;             // 同一批系统错误重试上限
        std::string host = "127.0.0.1";
        std::string user = "chat";
        std::string password = "chat123";
        std::string database = "chat";
        unsigned int port = 3306;
    };

    // 结果投递：把一条最终结果回送给对应身份。可跨线程调用。
    using Deliver = std::function<void(const PendingOrder&, const std::string&)>;
    using DeliverRefund =
        std::function<void(const PendingRefund&, const std::string&)>;

    OrderEngine(Options options, RedisClient* redis, CacheModule* cache);
    ~OrderEngine();

    OrderEngine(const OrderEngine&) = delete;
    OrderEngine& operator=(const OrderEngine&) = delete;

    void set_deliver(Deliver deliver);
    void set_refund_deliver(DeliverRefund deliver);

    // 先 set_deliver 再 start，保证工作线程起来时回调已经就位。
    void start();

    // 框架短任务的出口：入队即返回，不阻塞、不挂起协程。
    void enqueue(PendingOrder order);
    void enqueue_refund(PendingRefund refund);

    // Redis 里的库存被别人清掉时（比如运维 flush），让调度线程重新灌一次。
    void request_reseed();

    struct Stats {
        uint64_t accepted = 0;
        uint64_t batches = 0;
        uint64_t committed_orders = 0;
        uint64_t failed_orders = 0;
        uint64_t rejected_orders = 0;
        uint64_t delivered = 0;
        uint64_t queue_depth = 0;
        uint64_t retries = 0;
        uint64_t sold_out = 0;
        uint64_t accepted_refunds = 0;
        uint64_t committed_refunds = 0;
        uint64_t failed_refunds = 0;
        uint64_t refund_queue_depth = 0;
    };
    Stats stats() const;

    // 用数据库真值覆盖 Redis 库存与本地缓存额度。启动时跑一次，避免进程重启后
    // Redis 里残留上一轮扣减过的旧值。
    bool seed_redis_stock();

    // 订单状态变化（取消/退款）后把真实余量同步回缓存，供批处理额度使用。
    void refresh_quota_cache(uint64_t ticket_type_id, long long remaining);

private:
    struct TypeQueue {
        std::deque<PendingOrder> items;
        std::deque<PendingRefund> refunds;
        bool in_flight = false;   // true 时队列不可取，只能入队
        int attempts = 0;         // 连续系统错误重试次数
        std::chrono::steady_clock::time_point retry_after{};
    };

    struct BatchJob {
        uint64_t ticket_type_id = 0;
        std::vector<PendingOrder> orders;
        std::size_t queue_prefix = 0;  // 这批订单在队列头部占的条数
    };

    struct RefundJob {
        uint64_t ticket_type_id = 0;
        std::vector<PendingRefund> refunds;
    };

    struct WorkJob {
        enum class Kind { Reserve, Refund };
        Kind kind = Kind::Reserve;
        BatchJob reserve;
        RefundJob refund;
    };

    void scheduler_loop();
    void worker_loop();

    void run_batch(const BatchJob& job);
    void run_refund_batch(const RefundJob& job);

    enum class RefundOutcome {
        Committed,
        BusinessFailure,
        Retry,
    };
    struct RefundExecution {
        RefundOutcome outcome = RefundOutcome::Retry;
        long long remaining_after = 0;
        std::string status;
        std::string code;
        std::string detail;
    };
    RefundExecution process_refund(const PendingRefund& refund);

    void finish_success(const BatchJob& job, long long remaining_after,
                        long long price_cents);
    void finish_retry(const BatchJob& job, const std::string& reason);
    void finish_failure(const BatchJob& job, const std::string& code,
                        const std::string& reason);
    // 余量不足：本批 + 该商品队列里剩下的订单全部回失败，并同步真实余量。
    void finish_sold_out(const BatchJob& job, long long db_remaining);

    void mark_queue_free_locked(uint64_t ticket_type_id, bool reset_attempts);
    // 订单被最终处理掉（成功/失败/售罄）时，把数量从"已准入未落库"里减掉。
    void subtract_admitted_locked(uint64_t ticket_type_id, long long units);

    // 从缓存读额度；未命中则回源数据库并上拉。
    bool quota_of(uint64_t ticket_type_id, long long& quota);
    bool price_of(uint64_t ticket_type_id, long long& price_cents);
    bool query_quota(uint64_t ticket_type_id, long long& quota);
    bool query_price(uint64_t ticket_type_id, long long& price_cents);

    void deliver_one(const PendingOrder& order, bool ok,
                     const std::string& code, const std::string& detail,
                     long long amount_cents);
    void reject_one(const PendingOrder& order, const std::string& code,
                    const std::string& reason);
    void deliver_refund(const PendingRefund& refund, bool ok,
                        const std::string& code, const std::string& detail,
                        long long remaining_after);

    Options options_;
    RedisClient* redis_ = nullptr;
    CacheModule* cache_ = nullptr;
    Deliver deliver_;
    DeliverRefund refund_deliver_;

    std::unique_ptr<connect_pool> conn_pool_;

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, TypeQueue> queues_;
    // 每个票种"已准入但还没落库/还没判死"的票数之和。准入额度（Redis 里的库存）
    // 加上它就是库里真实剩余量，也就是这一批能取多少的预算。
    std::unordered_map<uint64_t, long long> admitted_units_;

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;

    std::deque<WorkJob> jobs_;
    std::mutex job_mutex_;
    std::condition_variable job_cv_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> reseed_requested_{false};
    bool started_ = false;
    std::thread scheduler_thread_;
    std::vector<std::thread> workers_;

    std::atomic<uint64_t> accepted_{0};
    std::atomic<uint64_t> batches_{0};
    std::atomic<uint64_t> committed_orders_{0};
    std::atomic<uint64_t> failed_orders_{0};
    std::atomic<uint64_t> rejected_orders_{0};
    std::atomic<uint64_t> delivered_{0};
    std::atomic<uint64_t> retries_{0};
    std::atomic<uint64_t> sold_out_{0};
    std::atomic<uint64_t> accepted_refunds_{0};
    std::atomic<uint64_t> committed_refunds_{0};
    std::atomic<uint64_t> failed_refunds_{0};
};
