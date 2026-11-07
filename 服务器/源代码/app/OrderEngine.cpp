#include "OrderEngine.h"

#include <mysql.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {

using Clock = std::chrono::steady_clock;

std::string quoted(MYSQL* conn, const std::string& value) {
    std::string buffer(value.size() * 2 + 1, '\0');
    const unsigned long written = mysql_real_escape_string(
        conn, buffer.data(), value.data(),
        static_cast<unsigned long>(value.size()));
    buffer.resize(written);
    return "'" + buffer + "'";
}

bool run_sql(MYSQL* conn, const std::string& sql, std::string& err) {
    if (mysql_real_query(conn, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
        err = mysql_error(conn);
        return false;
    }
    return true;
}

// 取单列单行的标量值；无行时 out 清空并返回 true。
bool run_scalar(MYSQL* conn, const std::string& sql, std::string& out,
                std::string& err) {
    out.clear();
    if (!run_sql(conn, sql, err))
        return false;
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        err = mysql_error(conn);
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row && row[0]) {
        unsigned long* lengths = mysql_fetch_lengths(res);
        out.assign(row[0], lengths ? lengths[0] : std::strlen(row[0]));
    }
    mysql_free_result(res);
    return true;
}

bool run_row(MYSQL* conn, const std::string& sql, std::vector<std::string>& out,
             std::string& err) {
    out.clear();
    if (!run_sql(conn, sql, err))
        return false;
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        err = mysql_error(conn);
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        unsigned int columns = mysql_num_fields(res);
        unsigned long* lengths = mysql_fetch_lengths(res);
        out.reserve(columns);
        for (unsigned int i = 0; i < columns; ++i) {
            out.emplace_back(row[i] ? row[i] : "",
                             row[i] && lengths ? lengths[i] : 0);
        }
    }
    mysql_free_result(res);
    return true;
}

bool parse_ll(const std::string& text, long long& value) {
    if (text.empty())
        return false;
    try {
        std::size_t used = 0;
        const long long parsed = std::stoll(text, &used);
        if (used != text.size())
            return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

OrderEngine::OrderEngine(Options options, RedisClient* redis, CacheModule* cache)
    : options_(std::move(options)), redis_(redis), cache_(cache) {
    if (options_.tick_ms < 1)
        options_.tick_ms = 1;
    if (options_.batch_max == 0)
        options_.batch_max = 1;
    if (options_.db_workers < 1)
        options_.db_workers = 1;

    // 内部专有连接池：订单落库这条路的所有连接都从这里借，和框架的 DB_pool
    // 完全无关，互不占额度。
    conn_pool_ = std::make_unique<connect_pool>(
        options_.db_connections, options_.host, options_.user,
        options_.password, options_.database, options_.port);
}

OrderEngine::~OrderEngine() {
    stop_.store(true);
    wake_cv_.notify_all();
    job_cv_.notify_all();
    if (scheduler_thread_.joinable())
        scheduler_thread_.join();
    for (auto& worker : workers_)
        if (worker.joinable())
            worker.join();
    if (conn_pool_)
        conn_pool_->shutdown();
}

void OrderEngine::set_deliver(Deliver deliver) {
    deliver_ = std::move(deliver);
}

void OrderEngine::set_refund_deliver(DeliverRefund deliver) {
    refund_deliver_ = std::move(deliver);
}

void OrderEngine::start() {
    if (started_)
        return;
    started_ = true;

    if (conn_pool_ && !conn_pool_->healthy()) {
        std::cerr << "[order-engine] warning: no database connection available, "
                     "orders will be rejected until the pool recovers"
                  << std::endl;
    }

    workers_.reserve(static_cast<std::size_t>(options_.db_workers));
    for (int i = 0; i < options_.db_workers; ++i)
        workers_.emplace_back([this] { worker_loop(); });
    scheduler_thread_ = std::thread([this] { scheduler_loop(); });
}

void OrderEngine::enqueue(PendingOrder order) {
    const long long units = order.quantity;
    const uint64_t type = order.ticket_type_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_[order.ticket_type_id].items.push_back(std::move(order));
        admitted_units_[type] += units;
    }
    accepted_.fetch_add(1);
    wake_cv_.notify_one();
}

void OrderEngine::enqueue_refund(PendingRefund refund) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_[refund.ticket_type_id].refunds.push_back(std::move(refund));
    }
    accepted_refunds_.fetch_add(1);
    wake_cv_.notify_one();
}

void OrderEngine::request_reseed() {
    reseed_requested_.store(true);
    wake_cv_.notify_one();
}

OrderEngine::Stats OrderEngine::stats() const {
    Stats snapshot;
    snapshot.accepted = accepted_.load();
    snapshot.batches = batches_.load();
    snapshot.committed_orders = committed_orders_.load();
    snapshot.failed_orders = failed_orders_.load();
    snapshot.rejected_orders = rejected_orders_.load();
    snapshot.delivered = delivered_.load();
    snapshot.retries = retries_.load();
    snapshot.sold_out = sold_out_.load();
    snapshot.accepted_refunds = accepted_refunds_.load();
    snapshot.committed_refunds = committed_refunds_.load();
    snapshot.failed_refunds = failed_refunds_.load();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [type, queue] : queues_) {
            snapshot.queue_depth += queue.items.size();
            snapshot.refund_queue_depth += queue.refunds.size();
        }
    }
    return snapshot;
}

void OrderEngine::scheduler_loop() {
    const auto tick = std::chrono::milliseconds(options_.tick_ms);
    while (!stop_.load()) {
        if (reseed_requested_.exchange(false))
            seed_redis_stock();

        // 阶段一：拿着锁只做"挑候选"，不做任何 IO。退款队列先于购票队列。
        std::vector<uint64_t> candidates;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto now = Clock::now();
            for (auto it = queues_.begin(); it != queues_.end();) {
                TypeQueue& queue = it->second;
                if (!queue.in_flight && queue.items.empty() &&
                    queue.refunds.empty() && queue.attempts == 0)
                    it = queues_.erase(it);
                else
                    ++it;
            }
            for (const auto& [type, queue] : queues_) {
                if (queue.in_flight ||
                    (queue.items.empty() && queue.refunds.empty()))
                    continue;
                if (now < queue.retry_after)
                    continue;
                candidates.push_back(type);
            }
        }

        std::vector<RefundJob> ready_refunds;
        std::vector<BatchJob> ready;
        for (uint64_t type : candidates) {
            if (stop_.load())
                break;

            // 同票种退款优先。退款 worker 完成后下一轮才会放行购票，
            // 因此 Redis 额度已经恢复，购票不会提前看到售罄。
            RefundJob refund_job;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = queues_.find(type);
                if (it == queues_.end() || it->second.in_flight)
                    continue;
                TypeQueue& queue = it->second;
                if (!queue.refunds.empty()) {
                    const std::size_t take =
                        std::min(queue.refunds.size(), options_.batch_max);
                    refund_job.ticket_type_id = type;
                    refund_job.refunds.reserve(take);
                    for (std::size_t i = 0; i < take; ++i) {
                        refund_job.refunds.push_back(
                            std::move(queue.refunds.front()));
                        queue.refunds.pop_front();
                    }
                    queue.in_flight = true;
                }
            }
            if (!refund_job.refunds.empty()) {
                ready_refunds.push_back(std::move(refund_job));
                continue;
            }

            // 购票才需要读额度；退款不占库存额度。
            long long quota = 0;
            if (!quota_of(type, quota))
                continue;   // 额度暂时读不到：下一轮再试

            BatchJob job;
            job.ticket_type_id = type;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = queues_.find(type);
                if (it == queues_.end() || it->second.in_flight ||
                    it->second.items.empty() ||
                    !it->second.refunds.empty())
                    continue;
                TypeQueue& queue = it->second;

                if (quota <= 0) {
                    // 额度没了：这商品后面排队的订单不可能再成交，整队回失败。
                    job.orders.reserve(queue.items.size());
                    for (auto& item : queue.items)
                        job.orders.push_back(std::move(item));
                    queue.items.clear();
                    queue.in_flight = true;
                    queue.attempts = 0;
                } else {
                    const std::size_t budget = static_cast<std::size_t>(
                        std::min<long long>(quota,
                                            static_cast<long long>(options_.batch_max)));
                    const std::size_t take =
                        std::min(budget, queue.items.size());
                    job.orders.reserve(take);
                    for (std::size_t i = 0; i < take; ++i) {
                        job.orders.push_back(std::move(queue.items.front()));
                        queue.items.pop_front();
                    }
                    job.queue_prefix = take;
                    queue.in_flight = true;
                }
            }
            ready.push_back(std::move(job));
        }

        for (auto& job : ready_refunds) {
            WorkJob work;
            work.kind = WorkJob::Kind::Refund;
            work.refund = std::move(job);
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                jobs_.push_back(std::move(work));
            }
            job_cv_.notify_one();
        }

        for (auto& job : ready) {
            batches_.fetch_add(1);
            if (job.queue_prefix == 0) {
                run_batch(job);   // 余量不足的整队回失败，就地处理
                continue;
            }
            WorkJob work;
            work.kind = WorkJob::Kind::Reserve;
            work.reserve = std::move(job);
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                jobs_.push_back(std::move(work));
            }
            job_cv_.notify_one();
        }

        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_cv_.wait_for(lock, tick);
    }
}

void OrderEngine::worker_loop() {
    while (true) {
        WorkJob job;
        {
            std::unique_lock<std::mutex> lock(job_mutex_);
            job_cv_.wait(lock, [this] { return stop_.load() || !jobs_.empty(); });
            if (stop_.load() && jobs_.empty())
                return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        if (job.kind == WorkJob::Kind::Refund)
            run_refund_batch(job.refund);
        else
            run_batch(job.reserve);
    }
}

void OrderEngine::run_refund_batch(const RefundJob& job) {
    std::vector<PendingRefund> retries;

    for (auto refund : job.refunds) {
        RefundExecution result = process_refund(refund);
        if (result.outcome == RefundOutcome::Committed) {
            committed_refunds_.fetch_add(1);
            if (redis_)
                redis_->release_stock(
                    std::to_string(refund.ticket_type_id), refund.quantity);
            if (cache_) {
                cache_->put(
                    cache_key_stock(std::to_string(refund.ticket_type_id)),
                    std::to_string(result.remaining_after),
                    std::chrono::milliseconds(300000));
            }
            deliver_refund(refund, true, "", result.status,
                           result.remaining_after);
            continue;
        }

        if (result.outcome == RefundOutcome::Retry) {
            ++refund.attempts;
            if (refund.attempts < options_.max_retry) {
                retries.push_back(std::move(refund));
                continue;
            }
            result.outcome = RefundOutcome::BusinessFailure;
            result.code = "SERVER_BUSY";
            result.detail = result.detail.empty()
                                ? "refund processing unavailable"
                                : result.detail;
        }

        failed_refunds_.fetch_add(1);
        deliver_refund(refund, false, result.code, result.detail, 0);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = queues_.find(job.ticket_type_id);
        if (it != queues_.end()) {
            TypeQueue& queue = it->second;
            for (auto rit = retries.rbegin(); rit != retries.rend(); ++rit)
                queue.refunds.push_front(std::move(*rit));
            queue.in_flight = false;
            if (!retries.empty()) {
                queue.retry_after =
                    Clock::now() + std::chrono::milliseconds(20);
            }
        }
    }
    if (!retries.empty())
        retries_.fetch_add(1);
    wake_cv_.notify_one();
}

OrderEngine::RefundExecution OrderEngine::process_refund(
    const PendingRefund& refund) {
    RefundExecution result;
    if (!conn_pool_ || !conn_pool_->healthy()) {
        result.detail = "database pool unavailable";
        return result;
    }

    DBHandle conn = conn_pool_->get();
    if (!conn) {
        result.detail = "database pool stopped";
        return result;
    }

    std::string err;
    bool failed = false;
    if (!run_sql(conn, "START TRANSACTION", err))
        failed = true;

    if (!failed) {
        const std::string existing_sql =
            "SELECT refund_no FROM ticket_refunds WHERE user_id=" +
            std::to_string(refund.user_id) + " AND request_id=" +
            quoted(conn, refund.idempotency_key) + " LIMIT 1 FOR UPDATE";
        std::string existing;
        if (!run_scalar(conn, existing_sql, existing, err)) {
            failed = true;
        } else if (!existing.empty()) {
            mysql_rollback(conn);
            conn_pool_->release(conn);
            result.outcome = RefundOutcome::BusinessFailure;
            result.code = "DUPLICATE";
            result.detail = existing;
            return result;
        }
    }

    std::vector<std::string> order;
    if (!failed) {
        const std::string select_order =
            "SELECT ticket_type_id,user_id,quantity,status,refunded_qty "
            "FROM ticket_orders WHERE order_no=" +
            quoted(conn, refund.order_no) + " LIMIT 1 FOR UPDATE";
        if (!run_row(conn, select_order, order, err)) {
            failed = true;
        } else if (order.size() < 5) {
            mysql_rollback(conn);
            conn_pool_->release(conn);
            result.outcome = RefundOutcome::BusinessFailure;
            result.code = "NOT_FOUND";
            result.detail = "order not found";
            return result;
        }
    }

    long long db_user_id = 0;
    long long db_type = 0;
    long long purchased = 0;
    long long refunded = 0;
    if (!failed &&
        (!parse_ll(order[0], db_type) || !parse_ll(order[1], db_user_id) ||
         !parse_ll(order[2], purchased) || !parse_ll(order[4], refunded))) {
        err = "order record is invalid";
        failed = true;
    }

    if (!failed && db_user_id != static_cast<long long>(refund.user_id)) {
        mysql_rollback(conn);
        conn_pool_->release(conn);
        result.outcome = RefundOutcome::BusinessFailure;
        result.code = "NOT_OWNER";
        result.detail = "order does not belong to this account";
        return result;
    }
    if (!failed && db_type != static_cast<long long>(refund.ticket_type_id)) {
        mysql_rollback(conn);
        conn_pool_->release(conn);
        result.outcome = RefundOutcome::BusinessFailure;
        result.code = "TICKET_TYPE_MISMATCH";
        result.detail = "ticket type does not match order";
        return result;
    }

    const std::string& status = order[3];
    if (!failed && status != "PENDING_PAY" && status != "PAID" &&
        status != "PARTIALLY_REFUNDED") {
        mysql_rollback(conn);
        conn_pool_->release(conn);
        result.outcome = RefundOutcome::BusinessFailure;
        result.code = "STATUS_INVALID";
        result.detail = "order status does not allow refund";
        return result;
    }
    if (!failed && (refund.quantity <= 0 ||
                    refunded + refund.quantity > purchased)) {
        mysql_rollback(conn);
        conn_pool_->release(conn);
        result.outcome = RefundOutcome::BusinessFailure;
        result.code = "EXCEED_REFUNDABLE";
        result.detail = "requested quantity exceeds refundable quantity";
        return result;
    }

    if (!failed) {
        const std::string insert_refund =
            "INSERT INTO ticket_refunds("
            "refund_no,original_order_no,user_id,ticket_type_id,quantity,"
            "request_id,status) VALUES(" +
            quoted(conn, refund.refund_no) + "," +
            quoted(conn, refund.order_no) + "," +
            std::to_string(refund.user_id) + "," +
            std::to_string(refund.ticket_type_id) + "," +
            std::to_string(refund.quantity) + "," +
            quoted(conn, refund.idempotency_key) + ",'COMMITTED')";
        if (!run_sql(conn, insert_refund, err)) {
            if (mysql_errno(conn) == 1062) {
                mysql_rollback(conn);
                conn_pool_->release(conn);
                result.outcome = RefundOutcome::BusinessFailure;
                result.code = "DUPLICATE";
                result.detail = "refund request already processed";
                return result;
            }
            failed = true;
        }
    }

    if (!failed) {
        const std::string update_order =
            "UPDATE ticket_orders SET status=IF(refunded_qty+" +
            std::to_string(refund.quantity) +
            ">=quantity,'REFUNDED','PARTIALLY_REFUNDED'),"
            "refunded_qty=refunded_qty+" +
            std::to_string(refund.quantity) + " WHERE order_no=" +
            quoted(conn, refund.order_no) +
            " AND user_id=" + std::to_string(refund.user_id) +
            " AND refunded_qty+" + std::to_string(refund.quantity) +
            "<=quantity";
        if (!run_sql(conn, update_order, err)) {
            failed = true;
        } else if (mysql_affected_rows(conn) != 1) {
            err = "refund order update lost a race";
            failed = true;
        }
    }

    if (!failed) {
        const std::string update_stock =
            "UPDATE ticket_inventory SET remaining_qty=remaining_qty+" +
            std::to_string(refund.quantity) +
            " WHERE ticket_type_id=" +
            std::to_string(refund.ticket_type_id) +
            " AND remaining_qty+" + std::to_string(refund.quantity) +
            "<=total_qty";
        if (!run_sql(conn, update_stock, err)) {
            failed = true;
        } else if (mysql_affected_rows(conn) != 1) {
            err = "refund inventory update lost a race";
            failed = true;
        }
    }

    std::string remaining_text;
    if (!failed) {
        const std::string select_stock =
            "SELECT remaining_qty FROM ticket_inventory WHERE ticket_type_id=" +
            std::to_string(refund.ticket_type_id);
        if (!run_scalar(conn, select_stock, remaining_text, err) ||
            !parse_ll(remaining_text, result.remaining_after)) {
            if (err.empty())
                err = "remaining quantity unreadable";
            failed = true;
        }
    }

    if (!failed && mysql_commit(conn) != 0) {
        err = mysql_error(conn);
        failed = true;
    }

    if (failed) {
        mysql_rollback(conn);
        conn_pool_->release(conn);
        result.outcome = RefundOutcome::Retry;
        result.detail = err.empty() ? "refund transaction failed" : err;
        return result;
    }

    conn_pool_->release(conn);
    result.outcome = RefundOutcome::Committed;
    result.status = refunded + refund.quantity >= purchased
                        ? "REFUNDED"
                        : "PARTIALLY_REFUNDED";
    return result;
}

void OrderEngine::run_batch(const BatchJob& job) {
    if (job.orders.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        mark_queue_free_locked(job.ticket_type_id, true);
        return;
    }

    // 余量不足的整队回失败：不用碰数据库。
    if (job.queue_prefix == 0) {
        finish_sold_out(job, 0);
        return;
    }

    if (!conn_pool_ || !conn_pool_->healthy()) {
        finish_retry(job, "database pool unavailable");
        return;
    }

    long long price_cents = 0;
    if (!price_of(job.ticket_type_id, price_cents) || price_cents < 0) {
        // 价格读不到属于业务数据问题：本批回失败，不再无限重试。
        finish_failure(job, "BAD_PRICE", "ticket price unavailable");
        return;
    }

    long long units = 0;
    for (const auto& order : job.orders)
        units += order.quantity;

    DBHandle conn = conn_pool_->get();
    if (!conn) {
        finish_retry(job, "database pool stopped");
        return;
    }

    const std::string type_text = std::to_string(job.ticket_type_id);
    std::string err;
    bool failed = false;
    long long remaining_after = 0;

    if (!run_sql(conn, "START TRANSACTION", err)) {
        failed = true;
    }

    if (!failed) {
        // 兜底防超卖：只有库里确实还有 enough 余量才扣。
        const std::string update_sql =
            "UPDATE ticket_inventory SET remaining_qty = remaining_qty - " +
            std::to_string(units) + " WHERE ticket_type_id = " + type_text +
            " AND remaining_qty >= " + std::to_string(units);
        if (!run_sql(conn, update_sql, err)) {
            failed = true;
        } else if (mysql_affected_rows(conn) == 0) {
            mysql_rollback(conn);
            long long db_remaining = 0;
            if (!query_quota(job.ticket_type_id, db_remaining))
                db_remaining = 0;
            conn_pool_->release(conn);
            finish_sold_out(job, db_remaining);
            return;
        }
    }

    if (!failed) {
        // 一批订单合成一条多值 INSERT，这才是"批处理写库"的收益点。
        std::ostringstream insert;
        insert << "INSERT INTO ticket_orders(order_no, user_id, ticket_type_id, "
                  "quantity, amount_cents, status, idempotency_key) VALUES ";
        for (std::size_t i = 0; i < job.orders.size(); ++i) {
            const PendingOrder& order = job.orders[i];
            if (i)
                insert << ',';
            insert << '(' << quoted(conn, order.order_no) << ','
                   << order.user_id << ',' << order.ticket_type_id << ','
                   << order.quantity << ','
                   << (price_cents * order.quantity) << ",'PENDING_PAY',"
                   << quoted(conn, order.idempotency_key) << ')';
        }
        // 极端情况下（Redis 丢 key 但库里已有落库记录）重复订单退化成幂等空操作，
        // 不会把整批拖垮。
        insert << " ON DUPLICATE KEY UPDATE status = status";
        if (!run_sql(conn, insert.str(), err))
            failed = true;
    }

    if (!failed) {
        std::string remaining_text;
        const std::string select_sql =
            "SELECT remaining_qty FROM ticket_inventory WHERE ticket_type_id = " +
            type_text;
        if (!run_scalar(conn, select_sql, remaining_text, err) ||
            !parse_ll(remaining_text, remaining_after)) {
            if (err.empty())
                err = "remaining quantity unreadable";
            failed = true;
        }
    }

    if (!failed) {
        if (mysql_commit(conn) != 0) {
            err = mysql_error(conn);
            failed = true;
        }
    }

    if (failed) {
        mysql_rollback(conn);
        conn_pool_->release(conn);
        finish_retry(job, err.empty() ? "transaction failed" : err);
        return;
    }

    conn_pool_->release(conn);
    finish_success(job, remaining_after, price_cents);
}

void OrderEngine::finish_success(const BatchJob& job, long long remaining_after,
                                 long long price_cents) {
    long long units = 0;
    for (const auto& order : job.orders)
        units += order.quantity;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 这批订单在挑选阶段就已经从队列挪进了 BatchJob，这里只需要解锁队列。
        // 注意不能在此时按 queue_prefix 再弹一次：队列里此刻排的是这批之后
        // 新到的订单，再弹会把它们误删。
        mark_queue_free_locked(job.ticket_type_id, true);
        subtract_admitted_locked(job.ticket_type_id, units);
    }

    // 缓存里的额度是"库里真实剩余量"，供下一批批处理读取。
    // Redis 那边不用动：预扣已经在 reserve 时做过，把它当成准入侧额度即可。
    if (cache_) {
        cache_->put(cache_key_stock(std::to_string(job.ticket_type_id)),
                    std::to_string(remaining_after),
                    std::chrono::milliseconds(300000));
    }

    committed_orders_.fetch_add(job.orders.size());
    for (const auto& order : job.orders)
        deliver_one(order, true, "", "", price_cents * order.quantity);
}

void OrderEngine::finish_retry(const BatchJob& job, const std::string& reason) {
    int attempts = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = queues_.find(job.ticket_type_id);
        if (it != queues_.end()) {
            TypeQueue& queue = it->second;
            // 订单放回队首，顺序保持：下一轮还会优先处理它们。
            for (auto rit = job.orders.rbegin(); rit != job.orders.rend(); ++rit)
                queue.items.push_front(*rit);
            queue.in_flight = false;
            queue.attempts += 1;
            attempts = queue.attempts;
            const int backoff_ms = std::min(500, 20 * attempts);
            queue.retry_after = Clock::now() + std::chrono::milliseconds(backoff_ms);
        }
    }
    retries_.fetch_add(1);

    if (attempts >= options_.max_retry) {
        // 重试到上限仍然失败：不能再无限重试，本批按业务失败回给客户端，
        // 并把队列解除阻塞，避免整条商品队列被一批坏单卡死。
        std::cerr << "[order-engine] batch giving up after " << attempts
                  << " retries: " << reason << std::endl;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = queues_.find(job.ticket_type_id);
            if (it != queues_.end()) {
                it->second.attempts = 0;
                it->second.retry_after = Clock::now();
            }
        }
        finish_failure(job, "SERVER_BUSY", "order processing unavailable");
    }
}

void OrderEngine::finish_failure(const BatchJob& job, const std::string& code,
                                 const std::string& reason) {
    long long units = 0;
    for (const auto& order : job.orders)
        units += order.quantity;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mark_queue_free_locked(job.ticket_type_id, true);
        subtract_admitted_locked(job.ticket_type_id, units);
    }
    failed_orders_.fetch_add(job.orders.size());
    for (const auto& order : job.orders)
        reject_one(order, code, reason);
}

void OrderEngine::finish_sold_out(const BatchJob& job, long long db_remaining) {
    std::vector<PendingOrder> rejected = job.orders;
    long long units = 0;
    for (const auto& order : job.orders)
        units += order.quantity;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = queues_.find(job.ticket_type_id);
        if (it != queues_.end()) {
            TypeQueue& queue = it->second;
            for (auto& item : queue.items) {
                units += item.quantity;
                rejected.push_back(std::move(item));
            }
            queue.items.clear();
            queue.in_flight = false;
            queue.attempts = 0;
            queue.retry_after = Clock::now();
        } else {
            mark_queue_free_locked(job.ticket_type_id, true);
        }
        subtract_admitted_locked(job.ticket_type_id, units);
    }

    sold_out_.fetch_add(rejected.size());
    rejected_orders_.fetch_add(rejected.size());

    // 这些订单没写进库，把 Redis 预扣的额度还回去，并把准入侧额度对齐到
    // 数据库真值：这一步之后 pending 归零，Redis = 库里剩余，两边重新一致。
    for (const auto& order : rejected) {
        if (redis_)
            redis_->compensate(std::to_string(order.ticket_type_id),
                               order.user_id, order.idempotency_key,
                               order.quantity, order.order_no);
    }
    if (redis_)
        redis_->set_stock(std::to_string(job.ticket_type_id), db_remaining);
    if (cache_)
        cache_->put(cache_key_stock(std::to_string(job.ticket_type_id)),
                    std::to_string(db_remaining),
                    std::chrono::milliseconds(300000));

    for (const auto& order : rejected)
        reject_one(order, "SOLD_OUT", "ticket sold out");
}

void OrderEngine::mark_queue_free_locked(uint64_t ticket_type_id,
                                         bool reset_attempts) {
    auto it = queues_.find(ticket_type_id);
    if (it == queues_.end())
        return;
    it->second.in_flight = false;
    if (reset_attempts)
        it->second.attempts = 0;
}

void OrderEngine::subtract_admitted_locked(uint64_t ticket_type_id,
                                           long long units) {
    auto it = admitted_units_.find(ticket_type_id);
    if (it == admitted_units_.end())
        return;
    it->second -= units;
    if (it->second <= 0)
        admitted_units_.erase(it);
}

bool OrderEngine::quota_of(uint64_t ticket_type_id, long long& quota) {
    // 批处理预算 = Redis 里剩下的准入额度 + 已准入但还没落库的票数。
    //   库里剩余 = Redis 剩余 + 已准入未落库   （准入时 Redis 已扣、落库时再扣库）
    // 这样即使运维重置了库存（重跑 init_redis.sh / reset_demo.sql），下一次
    // 就能从 Redis 直接读到新值，不用等进程内缓存过期。
    long long admitted = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = admitted_units_.find(ticket_type_id);
        if (it != admitted_units_.end())
            admitted = it->second;
    }

    long long redis_stock = 0;
    if (redis_ && redis_->get_stock(std::to_string(ticket_type_id), redis_stock)) {
        quota = redis_stock + admitted;
        return true;
    }
    // Redis 读不到：退回数据库真值（可能偏大，写库时的条件 UPDATE 会兜底防超卖）。
    return query_quota(ticket_type_id, quota);
}

bool OrderEngine::price_of(uint64_t ticket_type_id, long long& price_cents) {
    if (cache_) {
        std::string text;
        if (cache_->get(cache_key_catalog_price(std::to_string(ticket_type_id)),
                        text) &&
            parse_ll(text, price_cents))
            return true;
    }
    return query_price(ticket_type_id, price_cents);
}

bool OrderEngine::query_quota(uint64_t ticket_type_id, long long& quota) {
    if (!conn_pool_ || !conn_pool_->healthy())
        return false;
    DBHandle conn = conn_pool_->get();
    if (!conn)
        return false;
    std::string text;
    std::string err;
    const std::string sql =
        "SELECT remaining_qty FROM ticket_inventory WHERE ticket_type_id = " +
        std::to_string(ticket_type_id);
    const bool ok = run_scalar(conn, sql, text, err) && parse_ll(text, quota);
    conn_pool_->release(conn);
    if (ok && cache_) {
        cache_->put(cache_key_stock(std::to_string(ticket_type_id)), text,
                    std::chrono::milliseconds(300000));
    }
    return ok;
}

bool OrderEngine::query_price(uint64_t ticket_type_id, long long& price_cents) {
    if (!conn_pool_ || !conn_pool_->healthy())
        return false;
    DBHandle conn = conn_pool_->get();
    if (!conn)
        return false;
    std::string text;
    std::string err;
    const std::string sql =
        "SELECT price_cents FROM ticket_inventory WHERE ticket_type_id = " +
        std::to_string(ticket_type_id);
    const bool ok = run_scalar(conn, sql, text, err) && parse_ll(text, price_cents);
    conn_pool_->release(conn);
    if (ok && cache_) {
        cache_->put(cache_key_catalog_price(std::to_string(ticket_type_id)), text,
                    std::chrono::milliseconds(300000));
    }
    return ok;
}

void OrderEngine::deliver_one(const PendingOrder& order, bool ok,
                              const std::string& code,
                              const std::string& detail,
                              long long amount_cents) {
    if (!deliver_)
        return;
    std::string message;
    if (ok) {
        message = "reserve.result|ok|" + order.order_no + "|" +
                  std::to_string(amount_cents) + "|PENDING_PAY|created";
    } else {
        message = "reserve.result|error|" + order.order_no + "|" + code + "|" +
                  detail;
    }
    deliver_(order, message);
    delivered_.fetch_add(1);
}

void OrderEngine::reject_one(const PendingOrder& order, const std::string& code,
                             const std::string& reason) {
    if (redis_) {
        redis_->compensate(std::to_string(order.ticket_type_id), order.user_id,
                           order.idempotency_key, order.quantity, order.order_no);
    }
    deliver_one(order, false, code, reason, 0);
}

void OrderEngine::deliver_refund(const PendingRefund& refund, bool ok,
                                 const std::string& code,
                                 const std::string& detail,
                                 long long remaining_after) {
    if (!refund_deliver_)
        return;
    std::string message;
    if (ok) {
        message = "refund.result|ok|" + refund.refund_no + "|" +
                  refund.order_no + "|" + std::to_string(refund.quantity) +
                  "|" + detail + "|" + std::to_string(remaining_after);
    } else {
        message = "refund.result|error|" + refund.refund_no + "|" +
                  refund.order_no + "|" + code + "|" + detail;
    }
    refund_deliver_(refund, message);
    delivered_.fetch_add(1);
}

void OrderEngine::refresh_quota_cache(uint64_t ticket_type_id,
                                      long long remaining) {
    if (!cache_)
        return;
    cache_->put(cache_key_stock(std::to_string(ticket_type_id)),
                std::to_string(remaining),
                std::chrono::milliseconds(300000));
}

bool OrderEngine::seed_redis_stock() {
    if (!conn_pool_ || !conn_pool_->healthy()) {
        std::cerr << "[order-engine] seed skipped: no database connection"
                  << std::endl;
        return false;
    }
    DBHandle conn = conn_pool_->get();
    if (!conn)
        return false;

    std::string err;
    bool ok = run_sql(conn,
                      "SELECT ticket_type_id, price_cents, remaining_qty "
                      "FROM ticket_inventory",
                      err);
    std::vector<std::vector<std::string>> rows;
    if (ok) {
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) {
            err = mysql_error(conn);
            ok = false;
        } else {
            MYSQL_ROW row = nullptr;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                unsigned long* lengths = mysql_fetch_lengths(res);
                std::vector<std::string> line;
                for (unsigned int c = 0; c < 3; ++c)
                    line.emplace_back(row[c] ? row[c] : "",
                                      row[c] && lengths ? lengths[c] : 0);
                rows.push_back(std::move(line));
            }
            mysql_free_result(res);
        }
    }
    conn_pool_->release(conn);

    if (!ok) {
        std::cerr << "[order-engine] seed failed: " << err << std::endl;
        return false;
    }

    for (const auto& row : rows) {
        if (row.size() < 3)
            continue;
        const std::string& type = row[0];
        if (redis_)
            redis_->set_stock(type, std::stoll(row[2]));
        if (cache_) {
            cache_->put(cache_key_stock(type), row[2],
                        std::chrono::milliseconds(300000));
            cache_->put(cache_key_catalog_price(type), row[1],
                        std::chrono::milliseconds(300000));
        }
    }
    std::cout << "[order-engine] seeded " << rows.size()
              << " ticket types from database" << std::endl;
    return true;
}
