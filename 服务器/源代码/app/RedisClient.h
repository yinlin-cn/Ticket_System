#pragma once

#include <cstdint>
#include <string>

enum class RedisReserveStatus {
    Acquired,
    SoldOut,
    Duplicate,
    NotInitialized,
    Error,
};

struct RedisReserveResult {
    RedisReserveStatus status = RedisReserveStatus::Error;
    long long remaining = 0;
    std::string order_no;
    std::string error;
};

class RedisClient {
public:
    RedisClient() = default;
    RedisClient(std::string host, int port, int timeout_ms);

    RedisReserveResult reserve(const std::string& ticket_type_id,
                               uint64_t user_id,
                               const std::string& idempotency_key,
                               int quantity,
                               const std::string& order_no);

    bool initialize_stock_if_absent(const std::string& ticket_type_id,
                                    long long remaining);
    // 启动时用数据库真值覆盖 Redis 库存：进程重启后数据库才是权威。
    bool set_stock(const std::string& ticket_type_id, long long remaining);
    // 读当前准入额度（库存键）。键不存在或 Redis 不可用返回 false。
    bool get_stock(const std::string& ticket_type_id, long long& remaining);
    // 退款成功时只增加准入额度；键不存在时不凭空创建，交回数据库真值兜底。
    bool release_stock(const std::string& ticket_type_id, int quantity);
    bool compensate(const std::string& ticket_type_id,
                    uint64_t user_id,
                    const std::string& idempotency_key,
                    int quantity,
                    const std::string& order_no);
    bool mark_sold_out(const std::string& ticket_type_id,
                       uint64_t user_id,
                       const std::string& idempotency_key);

private:
    std::string host_ = "127.0.0.1";
    int port_ = 6379;
    int timeout_ms_ = 1000;
};
