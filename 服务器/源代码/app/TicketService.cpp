#include "TicketService.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#include "Password.h"
#include "FrameworkCall.h"
#include "context.h"
#include "thread_context.h"

namespace {

std::vector<std::string> split(const std::string& value, char sep) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t pos = value.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(value.substr(start));
            return parts;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + 1;
    }
}

bool valid_username(const std::string& value) {
    if (value.size() < 3 || value.size() > 32)
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}

std::string error(const std::string& command, const std::string& code,
                  const std::string& message) {
    return command + "|error|" + code + "|" + message;
}

std::string env_string(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

int env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value)
        return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

// 业务层身份 ID：账号级、稳定，和框架的虚拟连接 FD 是两回事。
// 目前直接取数据库用户主键；业务若另有口径，只改这一个函数。
uint64_t business_identity(uint64_t user_id) {
    return user_id;
}

}  // namespace

TicketService::TicketService()
    : token_fallback_(env_int("TICKET_TOKEN_FALLBACK", 1) != 0),
      admission_redis_(env_int("TICKET_ADMISSION_REDIS", 1) != 0),
      redis_(
          env_string("TICKET_REDIS_HOST", "127.0.0.1"),
          env_int("TICKET_REDIS_PORT", 6379),
          env_int("TICKET_REDIS_TIMEOUT_MS", 1000)) {
    roster_thread_ = std::thread([this] { roster_loop(); });

    // 订单处理模块：独立长线程 + 独立连接池，和框架的 DB 路径并行。
    OrderEngine::Options options;
    options.host = env_string("TICKET_DB_HOST", "127.0.0.1");
    options.user = env_string("TICKET_DB_USER", "chat");
    options.password = env_string("TICKET_DB_PASSWORD", "chat123");
    options.database = env_string("TICKET_DB_NAME", "chat");
    options.port = static_cast<unsigned int>(env_int("TICKET_DB_PORT", 3306));
    options.db_connections = env_int("TICKET_ORDER_DB_CONNECTIONS", 8);
    options.db_workers = env_int("TICKET_ORDER_DB_WORKERS", 6);
    options.batch_max = static_cast<std::size_t>(
        env_int("TICKET_ORDER_BATCH_MAX", 500));
    options.tick_ms = env_int("TICKET_ORDER_TICK_MS", 2);

    // 无 Redis 模式把 redis 传成 nullptr：订单引擎内部所有 Redis 调用本来就用
    // if (redis_) 保护，额度读取会自动退化到数据库真值。
    order_engine_ =
        std::make_unique<OrderEngine>(options,
                                      admission_redis_ ? &redis_ : nullptr,
                                      &cache_);
    // 结果投递走连接身份名册：订单在队列里可能停留，期间客户端可能重连，
    // 所以按账号级身份投递，不绑定提交时的连接对象。
    order_engine_->set_deliver(
        [this](const PendingOrder& order, const std::string& message) {
            // 结果优先回到提交订单的那条连接；连接已断开才按身份找回当前连接。
            roster_.deliver(order.virtual_fd, order.conn, order.identity_id,
                            message);
        });
    order_engine_->set_refund_deliver(
        [this](const PendingRefund& refund, const std::string& message) {
            roster_.deliver(refund.virtual_fd, refund.conn,
                            refund.identity_id, message);
        });
    order_engine_->start();
    // 进程重启后 Redis 里可能残留上一轮的库存，用库里真值覆盖一次。
    // 无 Redis 模式下这一步只灌本地缓存（价格与额度），供批处理读取。
    order_engine_->seed_redis_stock();
}

TicketService::~TicketService() {
    // 先停订单模块：投递回调会用到名册，必须在名册销毁前 join 掉工作线程。
    order_engine_.reset();
    roster_stop_.store(true);
    if (roster_thread_.joinable())
        roster_thread_.join();
}

void TicketService::roster_loop() {
    using namespace std::chrono;
    int ticks = 0;
    while (!roster_stop_.load()) {
        auto* call = g_framework_call;
        if (!call) {
            std::this_thread::sleep_for(milliseconds(50));
            continue;
        }
        auto book = call->connection_book();
        if (!book) {
            std::this_thread::sleep_for(milliseconds(50));
            continue;
        }

        // 等变化而不是轮询：有断连/重绑就被叫醒，没变化最多挂 500ms。
        uint64_t version = book->version();
        book->wait_version_change(version, milliseconds(500));
        roster_.apply_changes(book->take_changes());

        if (++ticks >= 20) {
            ticks = 0;
            roster_.reconcile();   // 约 10 秒一次全量对账兜底
        }
    }
}

EventTask TicketService::register_user(std::string username,
                                       std::string password) {
    if (!valid_username(username)) {
        send(error("register", "BAD_USERNAME", "username: 3-32 chars"));
        co_return;
    }
    if (password.size() < 6 || password.size() > 64) {
        send(error("register", "BAD_PASSWORD", "password: 6-64 chars"));
        co_return;
    }

    DBResult exists = co_await query_db(
        "SELECT id FROM ticket_users WHERE username = ? LIMIT 1", {username});
    if (exists.cancelled || !exists.ok) {
        send(error("register", "DB_ERROR", "database unavailable"));
        co_return;
    }
    if (!exists.rows.empty()) {
        send(error("register", "USER_EXISTS", "username already exists"));
        co_return;
    }

    std::string salt = random_hex(16);
    std::string hash = hash_password(password, salt);
    if (salt.empty() || hash.empty()) {
        send(error("register", "CRYPTO_ERROR", "password hashing failed"));
        co_return;
    }

    DBResult inserted = co_await query_db(
        "INSERT INTO ticket_users(username, password_hash, password_salt) VALUES(?,?,?)",
        {username, hash, salt});
    if (inserted.cancelled || !inserted.ok) {
        send(error("register", "USER_EXISTS", "username already exists"));
        co_return;
    }
    send("register|ok|" + username);
}

EventTask TicketService::login(std::string username,
                               std::string password) {
    DBResult result = co_await query_db(
        "SELECT id, password_hash, password_salt, "
        "(lock_until IS NOT NULL AND lock_until > NOW()) AS locked "
        "FROM ticket_users WHERE username = ? LIMIT 1",
        {username});
    if (result.cancelled || !result.ok) {
        send(error("login", "DB_ERROR", "database unavailable"));
        co_return;
    }
    if (result.rows.empty() || result.rows[0].size() < 4) {
        send(error("login", "BAD_CREDENTIALS", "username or password is invalid"));
        co_return;
    }

    const auto& row = result.rows[0];
    const bool locked = row[3] == "1";
    if (locked) {
        send(error("login", "ACCOUNT_LOCKED",
                   "too many failed attempts, try again later"));
        co_return;
    }
    if (!verify_password(password, row[2], row[1])) {
        // 记录登录错误次数：连续 5 次锁定 60 秒。失败写账不影响鉴权结论。
        co_await query_db(
            "UPDATE ticket_users SET login_fail_count = login_fail_count + 1, "
            "lock_until = CASE WHEN login_fail_count + 1 >= 5 "
            "THEN DATE_ADD(NOW(), INTERVAL 60 SECOND) ELSE lock_until END "
            "WHERE id = ?",
            {row[0]});
        send(error("login", "BAD_CREDENTIALS", "username or password is invalid"));
        co_return;
    }

    uint64_t user_id = 0;
    try {
        user_id = std::stoull(row[0]);
    } catch (...) {
        send(error("login", "BAD_USER", "user record is invalid"));
        co_return;
    }
    establish_session(username, user_id, "login");
}

EventTask TicketService::login_challenge(std::string username) {
    if (!valid_username(username)) {
        send(error("login.challenge", "BAD_USERNAME",
                   "username: 3-32 chars"));
        co_return;
    }

    virtual_conn_info conn = get_current_virtual_conn();
    if (!conn.valid || !tls_current_conn) {
        send(error("login.challenge", "NO_CONNECTION",
                   "connection is not registered"));
        co_return;
    }

    DBResult result = co_await query_db(
        "SELECT password_salt FROM ticket_users WHERE username = ? LIMIT 1",
        {username});
    if (result.cancelled || !result.ok) {
        send(error("login.challenge", "DB_ERROR", "database unavailable"));
        co_return;
    }

    // 不暴露账号是否存在。未知账号也得到格式正确的随机 salt，只有后续
    // proof 校验会统一失败。
    std::string salt =
        result.rows.empty() || result.rows[0].empty()
            ? random_hex(16)
            : result.rows[0][0];
    const std::string nonce = random_hex(32);
    if (salt.empty() || nonce.empty()) {
        send(error("login.challenge", "CRYPTO_ERROR",
                   "challenge generation failed"));
        co_return;
    }

    {
        std::lock_guard<std::mutex> lock(challenge_mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto it = challenges_.begin(); it != challenges_.end();) {
            if (it->second.expires_at <= now)
                it = challenges_.erase(it);
            else
                ++it;
        }

        LoginChallenge challenge;
        challenge.virtual_fd = conn.virtual_fd;
        challenge.conn = tls_current_conn;
        challenge.username = username;
        challenge.nonce = nonce;
        challenge.salt = salt;
        challenge.expires_at = now + std::chrono::seconds(30);
        // 每条连接只保留最新挑战，防止重复请求无界占用内存。
        challenges_[conn.virtual_fd] = std::move(challenge);
    }

    send("login.challenge|ok|" + nonce + "|" + salt);
}

EventTask TicketService::login_proof(std::string username,
                                     std::string proof) {
    virtual_conn_info conn = get_current_virtual_conn();
    if (!conn.valid || !tls_current_conn) {
        send(error("login.proof", "NO_CONNECTION",
                   "connection is not registered"));
        co_return;
    }

    LoginChallenge challenge;
    bool consumed = false;
    {
        std::lock_guard<std::mutex> lock(challenge_mutex_);
        auto it = challenges_.find(conn.virtual_fd);
        if (it != challenges_.end()) {
            challenge = it->second;
            challenges_.erase(it);
            consumed = true;
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if (!consumed || challenge.expires_at <= now ||
        challenge.conn.lock().get() != tls_current_conn.get() ||
        challenge.username != username) {
        send(error("login.proof", "CHALLENGE_EXPIRED",
                   "challenge is missing, expired, or already used"));
        co_return;
    }

    if (proof.size() != 64 ||
        !std::all_of(proof.begin(), proof.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        })) {
        send(error("login.proof", "BAD_PROOF", "proof is invalid"));
        co_return;
    }

    DBResult result = co_await query_db(
        "SELECT id, password_hash, "
        "(lock_until IS NOT NULL AND lock_until > NOW()) AS locked "
        "FROM ticket_users WHERE username = ? LIMIT 1",
        {username});
    if (result.cancelled || !result.ok) {
        send(error("login.proof", "DB_ERROR", "database unavailable"));
        co_return;
    }

    if (result.rows.empty() || result.rows[0].size() < 3) {
        // 仍执行一次等价 PBKDF2/HMAC，避免未知账号因提前返回形成明显时序差。
        const std::string fake_hash =
            hash_password(random_hex(16), challenge.salt);
        (void)verify_login_proof(proof, fake_hash, challenge.nonce);
        send(error("login.proof", "BAD_CREDENTIALS",
                   "username or proof is invalid"));
        co_return;
    }

    const auto& row = result.rows[0];
    if (row[2] == "1") {
        send(error("login.proof", "ACCOUNT_LOCKED",
                   "too many failed attempts, try again later"));
        co_return;
    }
    if (!verify_login_proof(proof, row[1], challenge.nonce)) {
        co_await query_db(
            "UPDATE ticket_users SET login_fail_count = login_fail_count + 1, "
            "lock_until = CASE WHEN login_fail_count + 1 >= 5 "
            "THEN DATE_ADD(NOW(), INTERVAL 60 SECOND) ELSE lock_until END "
            "WHERE id = ?",
            {row[0]});
        send(error("login.proof", "BAD_CREDENTIALS",
                   "username or proof is invalid"));
        co_return;
    }

    uint64_t user_id = 0;
    try {
        user_id = std::stoull(row[0]);
    } catch (...) {
        send(error("login.proof", "BAD_USER", "user record is invalid"));
        co_return;
    }
    establish_session(username, user_id, "login.proof");
}

EventTask TicketService::establish_session(
    std::string username, uint64_t user_id,
    std::string response_command) {
    // 账号级一对一：身份登记在当前连接上，同一账号只允许一条在线连接。
    virtual_conn_info conn = get_current_virtual_conn();
    if (!conn.valid) {
        send(error(response_command, "NO_CONNECTION",
                   "connection is not registered"));
        co_return;
    }
    const uint64_t identity_id = business_identity(user_id);
    uint64_t existing_fd = 0;
    ConnectionRoster::LoginResult reg =
        roster_.login(conn.virtual_fd, tls_current_conn, identity_id, user_id,
                      username, existing_fd);
    if (reg == ConnectionRoster::LoginResult::AlreadyOnline) {
        send(error(response_command, "ALREADY_ONLINE",
                   "this account is already online"));
        co_return;
    }
    if (reg != ConnectionRoster::LoginResult::Ok) {
        send(error(response_command, "REGISTER_FAILED",
                   "connection identity registration failed"));
        co_return;
    }

    std::string token = random_hex(32);
    if (token.empty()) {
        roster_.logout(conn.virtual_fd);
        send(error(response_command, "TOKEN_ERROR",
                   "session generation failed"));
        co_return;
    }
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        sessions_[token] = Session{user_id, identity_id, username};
    }
    // 登录成功清零错误计数与锁定时间。
    co_await query_db(
        "UPDATE ticket_users SET login_fail_count = 0, lock_until = NULL "
        "WHERE id = ?",
        {std::to_string(user_id)});
    send(response_command + "|ok|" + token + "|" +
         std::to_string(user_id));
}

EventTask TicketService::catalog_list() {
    // 商品浏览：先走缓存，缓存不全再回源数据库。
    // 剩余量单独存在 stock:<type> 里，由订单处理模块落库后刷新，所以缓存命中也
    // 能拿到接近实时的余量。任何一条缺失就回源，保证一次回源后缓存完整。
    std::string types_text;
    if (cache_.get(kCacheKeyCatalogTypes, types_text)) {
        std::ostringstream cached;
        cached << "catalog|ok";
        bool complete = true;
        for (const auto& type : split(types_text, ',')) {
            if (type.empty())
                continue;
            std::string item;
            std::string stock;
            if (!cache_.get(cache_key_catalog_item(type), item) ||
                !cache_.get(cache_key_stock(type), stock)) {
                complete = false;
                break;
            }
            cached << "\n" << type << "|" << item << "|" << stock;
        }
        if (complete) {
            send(cached.str());
            co_return;
        }
    }

    DBResult result = co_await query_db(
        "SELECT ticket_type_id, event_name, ticket_name, price_cents, remaining_qty "
        "FROM ticket_inventory ORDER BY ticket_type_id");
    if (result.cancelled || !result.ok) {
        send(error("catalog", "DB_ERROR", "database unavailable"));
        co_return;
    }

    std::ostringstream out;
    out << "catalog|ok";
    std::string types;
    for (const auto& row : result.rows) {
        if (row.size() < 5)
            continue;
        out << "\n" << row[0] << "|" << row[1] << "|" << row[2] << "|"
            << row[3] << "|" << row[4];
        if (!types.empty())
            types += ",";
        types += row[0];
        cache_.put(cache_key_catalog_item(row[0]),
                   row[1] + "|" + row[2] + "|" + row[3],
                   std::chrono::milliseconds(30000));
        cache_.put(cache_key_catalog_price(row[0]), row[3],
                   std::chrono::milliseconds(300000));
        cache_.put(cache_key_stock(row[0]), row[4],
                   std::chrono::milliseconds(300000));
    }
    cache_.put(kCacheKeyCatalogTypes, types, std::chrono::milliseconds(30000));
    send(out.str());
}

EventTask TicketService::reserve(std::string token,
                                 std::string ticket_type_id,
                                 std::string quantity_text,
                                 std::string idempotency_key) {
    Session session;
    if (!authenticate(token, session)) {
        send(error("reserve", "UNAUTHORIZED", "login required"));
        co_return;
    }
    if (idempotency_key.empty() || idempotency_key.size() > 64) {
        send(error("reserve", "BAD_IDEMPOTENCY_KEY", "idempotency_key is invalid"));
        co_return;
    }

    uint64_t type_id = 0;
    try {
        type_id = std::stoull(ticket_type_id);
    } catch (...) {
        send(error("reserve", "BAD_TICKET_TYPE", "ticket type is invalid"));
        co_return;
    }

    int quantity = 0;
    try {
        quantity = std::stoi(quantity_text);
    } catch (...) {
        send(error("reserve", "BAD_QUANTITY", "quantity is invalid"));
        co_return;
    }
    if (quantity < 1 || quantity > 10) {
        send(error("reserve", "BAD_QUANTITY", "quantity must be 1-10"));
        co_return;
    }

    // 全程不碰数据库：鉴权 + 准入 + 入队，立刻回 ack。
    // 真正写库和结果回送由订单处理模块的独立长线程完成，之后再按身份投递
    // reserve.result|...，所以框架业务线程不会被数据库请求拖住。
    const std::string order_no = new_order_no();

    if (admission_redis_) {
        // 主路径：Redis Lua 原子检查幂等键并预扣库存，把绝大多数无效请求挡在
        // 队列之外。
        RedisReserveResult admission = redis_.reserve(
            ticket_type_id, session.user_id, idempotency_key, quantity,
            order_no);

        if (admission.status == RedisReserveStatus::NotInitialized) {
            order_engine_->request_reseed();
            send("reserve.result|error|" + order_no +
                 "|NOT_READY|ticket stock is not ready");
            co_return;
        }
        if (admission.status == RedisReserveStatus::Error) {
            send("reserve.result|error|" + order_no + "|REDIS_ERROR|" +
                 (admission.error.empty() ? "redis unavailable"
                                          : admission.error));
            co_return;
        }
        if (admission.status == RedisReserveStatus::SoldOut) {
            send("reserve.result|error|" + order_no +
                 "|SOLD_OUT|ticket sold out");
            co_return;
        }
        if (admission.status == RedisReserveStatus::Duplicate) {
            // 同一用户 + 同一幂等键重复提交：本次不入队，直接告知已有订单号。
            const std::string existing =
                admission.order_no.empty() ? order_no : admission.order_no;
            send("reserve.result|error|" + existing +
                 "|DUPLICATE|idempotency key already used");
            co_return;
        }
    }
    // 无 Redis 模式：不做前置准入，所有请求直接入队，由订单引擎按数据库真值
    // 成批取单，落库时用 "remaining_qty >= n" 的条件更新兜底防超卖。

    PendingOrder pending;
    pending.order_no = order_no;
    pending.user_id = session.user_id;
    pending.identity_id = session.identity_id;
    pending.ticket_type_id = type_id;
    pending.quantity = quantity;
    pending.idempotency_key = idempotency_key;
    virtual_conn_info origin = get_current_virtual_conn();
    if (origin.valid) {
        pending.virtual_fd = origin.virtual_fd;
        pending.conn = tls_current_conn;
    }

    // 先把 accepted 回执发出去再入队：同一连接的发送队列是 FIFO，先入队的帧先出网，
    // 客户端因此一定能先看到 reserve|queued，再看到异步的 reserve.result。
    // （反过来的话，工作线程可能抢在回执之前把结果推出去，帧序就反了。）
    send("reserve|queued|" + order_no + "|accepted");
    order_engine_->enqueue(std::move(pending));
}

EventTask TicketService::refund(std::string token, std::string order_no,
                                std::string quantity_text,
                                std::string idempotency_key,
                                std::string ticket_type_id) {
    Session session;
    if (!authenticate(token, session)) {
        send(error("refund.submit", "UNAUTHORIZED", "login required"));
        co_return;
    }
    if (order_no.empty() || order_no.size() > 64) {
        send(error("refund.submit", "BAD_ORDER", "order_no is invalid"));
        co_return;
    }
    if (idempotency_key.empty() || idempotency_key.size() > 64) {
        send(error("refund.submit", "BAD_IDEMPOTENCY_KEY",
                   "idempotency_key is invalid"));
        co_return;
    }

    uint64_t type_id = 0;
    try {
        type_id = std::stoull(ticket_type_id);
    } catch (...) {
        send(error("refund.submit", "BAD_TICKET_TYPE",
                   "ticket_type_id is required"));
        co_return;
    }

    int quantity = 0;
    try {
        quantity = std::stoi(quantity_text);
    } catch (...) {
        send(error("refund.submit", "BAD_QUANTITY", "quantity is invalid"));
        co_return;
    }
    if (quantity < 1 || quantity > 10) {
        send(error("refund.submit", "BAD_QUANTITY",
                   "quantity must be 1-10"));
        co_return;
    }

    PendingRefund pending;
    pending.refund_no = new_refund_no();
    pending.order_no = std::move(order_no);
    pending.user_id = session.user_id;
    pending.identity_id = session.identity_id;
    pending.ticket_type_id = type_id;
    pending.quantity = quantity;
    pending.idempotency_key = std::move(idempotency_key);
    virtual_conn_info origin = get_current_virtual_conn();
    if (origin.valid) {
        pending.virtual_fd = origin.virtual_fd;
        pending.conn = tls_current_conn;
    }

    send("refund|queued|" + pending.refund_no + "|" +
         pending.order_no + "|accepted");
    order_engine_->enqueue_refund(std::move(pending));
}

EventTask TicketService::order_status(std::string token,
                                      std::string order_no) {
    Session session;
    if (!authenticate(token, session)) {
        send(error("order.status", "UNAUTHORIZED", "login required"));
        co_return;
    }

    DBResult result = co_await query_db(
        "SELECT order_no, ticket_type_id, quantity, amount_cents, status, "
        "refunded_qty "
        "FROM ticket_orders WHERE order_no = ? AND user_id = ? LIMIT 1",
        {order_no, std::to_string(session.user_id)});
    if (result.cancelled || !result.ok) {
        send(error("order.status", "DB_ERROR", "database unavailable"));
        co_return;
    }
    if (result.rows.empty() || result.rows[0].size() < 6) {
        send(error("order.status", "NOT_FOUND", "order not found"));
        co_return;
    }
    const auto& row = result.rows[0];
    send("order.status|ok|" + row[0] + "|" + row[1] + "|" + row[2] + "|" +
         row[3] + "|" + row[4] + "|" + row[5]);
}

EventTask TicketService::my_orders(std::string token) {
    Session session;
    if (!authenticate(token, session)) {
        send(error("order.my", "UNAUTHORIZED", "login required"));
        co_return;
    }

    DBResult result = co_await query_db(
        "SELECT order_no,ticket_type_id,quantity,amount_cents,status,"
        "refunded_qty,created_at FROM ticket_orders WHERE user_id=? "
        "ORDER BY created_at DESC LIMIT 100",
        {std::to_string(session.user_id)});
    if (result.cancelled || !result.ok) {
        send(error("order.my", "DB_ERROR", "database unavailable"));
        co_return;
    }

    std::ostringstream out;
    out << "order.my|ok|" << result.rows.size();
    for (const auto& row : result.rows) {
        if (row.size() < 7)
            continue;
        out << "\n" << row[0] << "|" << row[1] << "|" << row[2] << "|"
            << row[3] << "|" << row[4] << "|" << row[5] << "|" << row[6];
    }
    send(out.str());
}

EventTask TicketService::whoami() {
    Session session;
    if (!authenticate("", session)) {
        send(error("whoami", "UNAUTHORIZED", "login required"));
        co_return;
    }
    virtual_conn_info conn = get_current_virtual_conn();
    send("whoami|ok|" + std::to_string(session.identity_id) + "|" +
         session.username + "|" + std::to_string(conn.virtual_fd));
}

EventTask TicketService::logout() {
    virtual_conn_info conn = get_current_virtual_conn();
    if (!conn.valid) {
        send(error("logout", "NO_CONNECTION", "connection is not registered"));
        co_return;
    }

    RosterIdentity identity;
    const bool logged_in = roster_.resolve(conn.virtual_fd, tls_current_conn, identity);
    roster_.logout(conn.virtual_fd);
    if (!logged_in) {
        send(error("logout", "NOT_LOGGED_IN", "no active session on this connection"));
        co_return;
    }

    // 一并作废该身份签发过的 token，免得旧客户端继续用 token 走兼容路径。
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->second.identity_id == identity.identity_id)
                it = sessions_.erase(it);
            else
                ++it;
        }
    }
    send("logout|ok");
}

EventTask TicketService::notice_push(std::string target_identity,
                                     std::string message) {
    Session session;
    if (!authenticate("", session)) {
        send(error("notice.push", "UNAUTHORIZED", "login required"));
        co_return;
    }

    uint64_t target = 0;
    try {
        target = std::stoull(target_identity);
    } catch (...) {
        send(error("notice.push", "BAD_TARGET", "target identity is invalid"));
        co_return;
    }

    // 只拿业务身份，不碰连接号：由名册自己查出该投到哪条连接。
    const bool delivered = roster_.send_to_identity(
        target, "notice|" + session.username + "|" + message);
    send("notice.push|ok|" + std::to_string(target) + "|" +
         (delivered ? "1" : "0"));
}

EventTask TicketService::handle(int request_id, std::string message) {
    (void)request_id;
    std::vector<std::string> parts = split(message, '|');
    if (parts.empty()) {
        send(error("request", "BAD_REQUEST", "empty command"));
        co_return;
    }
    const std::string& command = parts[0];

    if (command == "ping") {
        send("pong|ok");
        co_return;
    }
    if (command == "register") {
        if (parts.size() < 3) {
            send(error("register", "BAD_REQUEST", "usage: register|user|password"));
            co_return;
        }
        register_user(parts[1], parts[2]);
        co_return;
    }
    if (command == "login") {
        if (parts.size() < 3) {
            send(error("login", "BAD_REQUEST", "usage: login|user|password"));
            co_return;
        }
        login(parts[1], parts[2]);
        co_return;
    }
    if (command == "login.challenge") {
        if (parts.size() < 2) {
            send(error("login.challenge", "BAD_REQUEST",
                       "usage: login.challenge|username"));
            co_return;
        }
        login_challenge(parts[1]);
        co_return;
    }
    if (command == "login.proof") {
        if (parts.size() < 3) {
            send(error("login.proof", "BAD_REQUEST",
                       "usage: login.proof|username|proof"));
            co_return;
        }
        login_proof(parts[1], parts[2]);
        co_return;
    }
    if (command == "catalog.list") {
        catalog_list();
        co_return;
    }
    if (command == "order.reserve") {
        if (parts.size() < 5) {
            send(error("reserve", "BAD_REQUEST",
                       "usage: order.reserve|token|ticket_type_id|quantity|idempotency_key"));
            co_return;
        }
        reserve(parts[1], parts[2], parts[3], parts[4]);
        co_return;
    }
    if (command == "order.status") {
        if (parts.size() < 3) {
            send(error("order.status", "BAD_REQUEST",
                       "usage: order.status|token|order_no"));
            co_return;
        }
        order_status(parts[1], parts[2]);
        co_return;
    }
    if (command == "refund.submit") {
        if (parts.size() < 6) {
            send(error("refund.submit", "BAD_REQUEST",
                       "usage: refund.submit|token|order_no|quantity|"
                       "idempotency_key|ticket_type_id"));
            co_return;
        }
        refund(parts[1], parts[2], parts[3], parts[4], parts[5]);
        co_return;
    }
    if (command == "order.my") {
        my_orders(parts.size() > 1 ? parts[1] : "");
        co_return;
    }
    if (command == "whoami") {
        whoami();
        co_return;
    }
    if (command == "logout") {
        logout();
        co_return;
    }
    if (command == "notice.push") {
        if (parts.size() < 3) {
            send(error("notice.push", "BAD_REQUEST",
                       "usage: notice.push|identity|message"));
            co_return;
        }
        std::string body = parts[2];
        for (std::size_t i = 3; i < parts.size(); ++i)
            body += "|" + parts[i];
        notice_push(parts[1], body);
        co_return;
    }

    send(error("request", "UNKNOWN_COMMAND", "unknown command"));
}

bool TicketService::authenticate(const std::string& token, Session& session) {
    // 主路径：按当前连接查业务身份。
    virtual_conn_info conn = get_current_virtual_conn();
    if (conn.valid) {
        RosterIdentity identity;
        if (roster_.resolve(conn.virtual_fd, tls_current_conn, identity)) {
            session.user_id = identity.user_id;
            session.identity_id = identity.identity_id;
            session.username = identity.username;
            return true;
        }
    }

    // 兼容旧客户端：仍然认 token，方便先前的脚本和 Java 客户端继续跑。
    if (!token_fallback_ || token.empty())
        return false;
    std::lock_guard<std::mutex> lock(session_mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end())
        return false;
    session = it->second;
    return true;
}

std::string TicketService::new_order_no() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const uint64_t seq = order_sequence_.fetch_add(1);
    return "T" + std::to_string(micros) + "-" + std::to_string(seq);
}

std::string TicketService::new_refund_no() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const uint64_t seq = refund_sequence_.fetch_add(1);
    return "R" + std::to_string(micros) + "-" + std::to_string(seq);
}
