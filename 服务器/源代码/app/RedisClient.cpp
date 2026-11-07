#include "RedisClient.h"

#include <hiredis/hiredis.h>

#include <cstring>
#include <memory>
#include <sys/time.h>
#include <utility>

namespace {

constexpr const char* kReserveScript = R"lua(
local stock = redis.call('GET', KEYS[1])
if not stock then
    return {-1, 'STOCK_NOT_INITIALIZED'}
end

local existing = redis.call('GET', KEYS[2])
if existing then
    return {2, existing}
end

local quantity = tonumber(ARGV[1])
if not quantity or quantity <= 0 then
    return {-2, 'BAD_QUANTITY'}
end

local available = tonumber(stock)
if available < quantity then
    return {0, tostring(available)}
end

redis.call('DECRBY', KEYS[1], quantity)
redis.call('SET', KEYS[2], ARGV[2], 'EX', 86400)
return {1, tostring(redis.call('GET', KEYS[1]))}
)lua";

constexpr const char* kCompensateScript = R"lua(
local owner = redis.call('GET', KEYS[2])
if owner ~= ARGV[2] then
    return 0
end
redis.call('DEL', KEYS[2])
redis.call('INCRBY', KEYS[1], ARGV[1])
return 1
)lua";

constexpr const char* kSoldOutScript = R"lua(
redis.call('SET', KEYS[1], '0')
redis.call('DEL', KEYS[2])
return 1
)lua";

constexpr const char* kReleaseStockScript = R"lua(
if redis.call('EXISTS', KEYS[1]) == 0 then
    return -1
end
return redis.call('INCRBY', KEYS[1], ARGV[1])
)lua";

struct RedisReplyDeleter {
    void operator()(redisReply* reply) const {
        if (reply)
            freeReplyObject(reply);
    }
};

using RedisReplyPtr = std::unique_ptr<redisReply, RedisReplyDeleter>;

struct ThreadConnection {
    redisContext* context = nullptr;
    std::string host;
    int port = 0;
    int timeout_ms = 0;
    std::string reserve_sha;

    ~ThreadConnection() {
        if (context)
            redisFree(context);
    }
};

thread_local ThreadConnection tls_connection;

std::string stock_key(const std::string& ticket_type_id) {
    return "ticket:stock:" + ticket_type_id;
}

std::string idempotency_key(uint64_t user_id,
                            const std::string& request_key) {
    return "ticket:idem:" + std::to_string(user_id) + ":" + request_key;
}

void close_connection() {
    if (tls_connection.context)
        redisFree(tls_connection.context);
    tls_connection.context = nullptr;
    tls_connection.host.clear();
    tls_connection.port = 0;
    tls_connection.timeout_ms = 0;
    tls_connection.reserve_sha.clear();
}

redisContext* connection(const std::string& host,
                         int port,
                         int timeout_ms,
                         std::string& error) {
    if (tls_connection.context &&
        (tls_connection.host != host ||
         tls_connection.port != port ||
         tls_connection.timeout_ms != timeout_ms)) {
        close_connection();
    }

    if (!tls_connection.context) {
        timeval timeout{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        tls_connection.context =
            redisConnectWithTimeout(host.c_str(), port, timeout);
        if (!tls_connection.context ||
            tls_connection.context->err) {
            error = tls_connection.context
                ? tls_connection.context->errstr
                : "redisConnectWithTimeout failed";
            close_connection();
            return nullptr;
        }
        tls_connection.host = host;
        tls_connection.port = port;
        tls_connection.timeout_ms = timeout_ms;
    }

    return tls_connection.context;
}

std::string reply_error(redisReply* reply) {
    if (!reply)
        return "redis command returned null";
    if (reply->type == REDIS_REPLY_ERROR)
        return std::string(reply->str, reply->len);
    return "unexpected redis reply type " +
           std::to_string(static_cast<int>(reply->type));
}

RedisReplyPtr command_argv(redisContext* context,
                           const char** argv,
                           const size_t* argvlen,
                           int argc) {
    return RedisReplyPtr(static_cast<redisReply*>(
        redisCommandArgv(context, argc, argv, argvlen)));
}

bool ensure_reserve_script(redisContext*& context, std::string& error) {
    if (!tls_connection.reserve_sha.empty())
        return true;

    const char* argv[] = {"SCRIPT", "LOAD", kReserveScript};
    const size_t argvlen[] = {6, 4, std::strlen(kReserveScript)};
    RedisReplyPtr reply = command_argv(context, argv, argvlen, 3);
    if (!reply || reply->type != REDIS_REPLY_STRING) {
        error = reply_error(reply.get());
        close_connection();
        context = nullptr;
        return false;
    }

    tls_connection.reserve_sha.assign(reply->str, reply->len);
    return true;
}

std::string value_of(redisReply* reply) {
    if (!reply)
        return {};
    if (reply->type == REDIS_REPLY_STRING)
        return std::string(reply->str, reply->len);
    if (reply->type == REDIS_REPLY_INTEGER)
        return std::to_string(reply->integer);
    return {};
}

}  // namespace

RedisClient::RedisClient(std::string host, int port, int timeout_ms)
    : host_(std::move(host)), port_(port), timeout_ms_(timeout_ms) {}

RedisReserveResult RedisClient::reserve(
    const std::string& ticket_type_id,
    uint64_t user_id,
    const std::string& request_key,
    int quantity,
    const std::string& order_no) {
    RedisReserveResult result;
    std::string error;
    redisContext* context = connection(host_, port_, timeout_ms_, error);
    if (!context) {
        result.error = error;
        return result;
    }

    const std::string stock = stock_key(ticket_type_id);
    const std::string idem = idempotency_key(user_id, request_key);
    const std::string quantity_text = std::to_string(quantity);

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!ensure_reserve_script(context, error)) {
            result.error = error;
            return result;
        }

        const char* argv[] = {
            "EVALSHA",
            tls_connection.reserve_sha.c_str(),
            "2",
            stock.c_str(),
            idem.c_str(),
            quantity_text.c_str(),
            order_no.c_str(),
        };
        const size_t argvlen[] = {
            7,
            tls_connection.reserve_sha.size(),
            1,
            stock.size(),
            idem.size(),
            quantity_text.size(),
            order_no.size(),
        };
        RedisReplyPtr reply = command_argv(context, argv, argvlen, 7);
        if (!reply) {
            result.error = "redis command returned null";
            close_connection();
            return result;
        }

        if (reply->type == REDIS_REPLY_ERROR) {
            std::string message = reply_error(reply.get());
            if (attempt == 0 && message.rfind("NOSCRIPT", 0) == 0) {
                tls_connection.reserve_sha.clear();
                continue;
            }
            result.error = message;
            close_connection();
            return result;
        }

        if (reply->type != REDIS_REPLY_ARRAY || reply->elements < 2 ||
            reply->element[0]->type != REDIS_REPLY_INTEGER) {
            result.error = reply_error(reply.get());
            close_connection();
            return result;
        }

        const long long status = reply->element[0]->integer;
        const std::string value = value_of(reply->element[1]);
        if (status == 1) {
            result.status = RedisReserveStatus::Acquired;
            try {
                result.remaining = std::stoll(value);
            } catch (...) {
                result.error = "redis returned invalid remaining quantity";
                result.status = RedisReserveStatus::Error;
            }
            return result;
        }
        if (status == 0) {
            result.status = RedisReserveStatus::SoldOut;
            try {
                result.remaining = std::stoll(value);
            } catch (...) {
                result.remaining = 0;
            }
            return result;
        }
        if (status == 2) {
            result.status = RedisReserveStatus::Duplicate;
            result.order_no = value;
            return result;
        }
        if (status == -1) {
            result.status = RedisReserveStatus::NotInitialized;
            return result;
        }

        result.error = value.empty() ? "redis reserve failed" : value;
        return result;
    }

    result.error = "redis script load retry failed";
    return result;
}

bool RedisClient::initialize_stock_if_absent(
    const std::string& ticket_type_id,
    long long remaining) {
    std::string error;
    redisContext* context = connection(host_, port_, timeout_ms_, error);
    if (!context)
        return false;

    const std::string key = stock_key(ticket_type_id);
    const std::string value = std::to_string(remaining);
    const char* argv[] = {"SET", key.c_str(), value.c_str(), "NX"};
    const size_t argvlen[] = {3, key.size(), value.size(), 2};
    RedisReplyPtr reply = command_argv(context, argv, argvlen, 4);
    if (!reply ||
        (reply->type != REDIS_REPLY_STATUS &&
         reply->type != REDIS_REPLY_NIL)) {
        close_connection();
        return false;
    }
    return true;
}

bool RedisClient::set_stock(const std::string& ticket_type_id,
                            long long remaining) {
    std::string error;
    redisContext* context = connection(host_, port_, timeout_ms_, error);
    if (!context)
        return false;

    const std::string key = stock_key(ticket_type_id);
    const std::string value = std::to_string(remaining);
    const char* argv[] = {"SET", key.c_str(), value.c_str()};
    const size_t argvlen[] = {3, key.size(), value.size()};
    RedisReplyPtr reply = command_argv(context, argv, argvlen, 3);
    if (!reply || reply->type != REDIS_REPLY_STATUS) {
        close_connection();
        return false;
    }
    return true;
}

bool RedisClient::get_stock(const std::string& ticket_type_id,
                            long long& remaining) {
    std::string error;
    redisContext* context = connection(host_, port_, timeout_ms_, error);
    if (!context)
        return false;

    const std::string key = stock_key(ticket_type_id);
    const char* argv[] = {"GET", key.c_str()};
    const size_t argvlen[] = {3, key.size()};
    RedisReplyPtr reply = command_argv(context, argv, argvlen, 2);
    if (!reply || reply->type != REDIS_REPLY_STRING) {
        if (reply && reply->type == REDIS_REPLY_ERROR)
            close_connection();
        return false;   // 键不存在（NIL）或连接出错
    }
    try {
        remaining = std::stoll(std::string(reply->str, reply->len));
    } catch (...) {
        return false;
    }
    return true;
}

bool RedisClient::release_stock(const std::string& ticket_type_id,
                                int quantity) {
    if (quantity <= 0)
        return false;
    std::string error;
    redisContext* context = connection(host_, port_, timeout_ms_, error);
    if (!context)
        return false;

    const std::string stock = stock_key(ticket_type_id);
    const std::string quantity_text = std::to_string(quantity);
    const char* argv[] = {
        "EVAL",
        kReleaseStockScript,
        "1",
        stock.c_str(),
        quantity_text.c_str(),
    };
    const size_t argvlen[] = {
        4,
        std::strlen(kReleaseStockScript),
        1,
        stock.size(),
        quantity_text.size(),
    };
    RedisReplyPtr reply = command_argv(context, argv, argvlen, 5);
    if (!reply || reply->type != REDIS_REPLY_INTEGER ||
        reply->integer < 0) {
        if (!reply || reply->type == REDIS_REPLY_ERROR)
            close_connection();
        return false;
    }
    return true;
}

bool RedisClient::compensate(const std::string& ticket_type_id,
                             uint64_t user_id,
                             const std::string& request_key,
                             int quantity,
                             const std::string& order_no) {
    std::string error;
    redisContext* context = connection(host_, port_, timeout_ms_, error);
    if (!context)
        return false;

    const std::string stock = stock_key(ticket_type_id);
    const std::string idem = idempotency_key(user_id, request_key);
    const std::string quantity_text = std::to_string(quantity);
    const char* argv[] = {
        "EVAL",
        kCompensateScript,
        "2",
        stock.c_str(),
        idem.c_str(),
        quantity_text.c_str(),
        order_no.c_str(),
    };
    const size_t argvlen[] = {
        4,
        std::strlen(kCompensateScript),
        1,
        stock.size(),
        idem.size(),
        quantity_text.size(),
        order_no.size(),
    };
    RedisReplyPtr reply = command_argv(context, argv, argvlen, 7);
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        close_connection();
        return false;
    }
    return reply->integer > 0;
}

bool RedisClient::mark_sold_out(const std::string& ticket_type_id,
                                uint64_t user_id,
                                const std::string& request_key) {
    std::string error;
    redisContext* context = connection(host_, port_, timeout_ms_, error);
    if (!context)
        return false;

    const std::string stock = stock_key(ticket_type_id);
    const std::string idem = idempotency_key(user_id, request_key);
    const char* argv[] = {
        "EVAL",
        kSoldOutScript,
        "2",
        stock.c_str(),
        idem.c_str(),
    };
    const size_t argvlen[] = {
        4,
        std::strlen(kSoldOutScript),
        1,
        stock.size(),
        idem.size(),
    };
    RedisReplyPtr reply = command_argv(context, argv, argvlen, 5);
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        close_connection();
        return false;
    }
    return reply->integer > 0;
}
