#include "DB_pool.h"
#include "Metrics.h"

#include <cstring>
#include <utility>
#include <vector>

namespace {

bool read_all_rows(MYSQL_STMT* stmt, MYSQL_RES* res,
                   std::vector<std::vector<std::string>>& rows,
                   std::string& err) {
    const unsigned int cols = mysql_num_fields(res);
    if (cols == 0)
        return true;

    if (mysql_stmt_store_result(stmt) != 0) {
        err = mysql_stmt_error(stmt);
        return false;
    }

    constexpr size_t init_column_bytes = 256;
    std::vector<MYSQL_BIND> rb(cols);
    std::vector<std::vector<char>> bufs(cols);
    std::vector<unsigned long> lengths(cols, 0);
    std::vector<my_bool> is_null(cols, 0);

    for (unsigned int c = 0; c < cols; c++) {
        bufs[c].assign(init_column_bytes, '\0');
        rb[c].buffer_type = MYSQL_TYPE_STRING;
        rb[c].buffer = bufs[c].data();
        rb[c].buffer_length = bufs[c].size();
        rb[c].length = &lengths[c];
        rb[c].is_null = &is_null[c];
    }
    if (mysql_stmt_bind_result(stmt, rb.data()) != 0) {
        err = mysql_stmt_error(stmt);
        return false;
    }

    while (true) {
        int rc = mysql_stmt_fetch(stmt);
        if (rc == MYSQL_NO_DATA)
            break;
        if (rc != 0 && rc != MYSQL_DATA_TRUNCATED) {
            err = mysql_stmt_error(stmt);
            if (err.empty()) err = "mysql_stmt_fetch failed";
            return false;
        }

        if (rc == MYSQL_DATA_TRUNCATED) {
            std::vector<unsigned char> refetch(cols, 0);
            for (unsigned int c = 0; c < cols; c++) {
                if (is_null[c]) continue;
                unsigned long need = lengths[c];
                if (need >= bufs[c].size()) {
                    bufs[c].resize(need + 1);
                    rb[c].buffer = bufs[c].data();
                    rb[c].buffer_length = static_cast<unsigned long>(bufs[c].size());
                    refetch[c] = 1;
                }
            }
            for (unsigned int c = 0; c < cols; c++) {
                if (!refetch[c]) continue;
                if (mysql_stmt_fetch_column(stmt, &rb[c], c, 0) != 0) {
                    err = mysql_stmt_error(stmt);
                    return false;
                }
            }
        }

        std::vector<std::string> line;
        line.reserve(cols);
        for (unsigned int c = 0; c < cols; c++)
            line.emplace_back(bufs[c].data(), lengths[c]);
        rows.push_back(std::move(line));
    }
    return true;
}

bool execute_statement(MYSQL* conn, const DBStatement& request,
                       DBStatementResult& result) {
    result = {};
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        result.err = "stmt init failed";
        return false;
    }

    if (mysql_stmt_prepare(stmt, request.sql.c_str(), request.sql.size()) != 0) {
        result.err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    std::vector<MYSQL_BIND> binds(request.params.size());
    std::vector<std::string> data = request.params;
    std::vector<unsigned long> lengths(request.params.size());
    std::vector<my_bool> is_null(request.params.size(), 0);
    for (size_t i = 0; i < request.params.size(); i++) {
        lengths[i] = static_cast<unsigned long>(data[i].size());
        binds[i].buffer_type = MYSQL_TYPE_STRING;
        binds[i].buffer = const_cast<char*>(data[i].data());
        binds[i].buffer_length = lengths[i];
        binds[i].length = &lengths[i];
        binds[i].is_null = &is_null[i];
    }

    if (!binds.empty() && mysql_stmt_bind_param(stmt, binds.data()) != 0) {
        result.err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        result.err = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_RES* res = mysql_stmt_result_metadata(stmt);
    if (res) {
        bool read_ok = read_all_rows(stmt, res, result.rows, result.err);
        mysql_free_result(res);
        if (!read_ok) {
            mysql_stmt_close(stmt);
            return false;
        }
    } else {
        result.affected_rows = static_cast<uint64_t>(mysql_stmt_affected_rows(stmt));
        result.insert_id = static_cast<uint64_t>(mysql_stmt_insert_id(stmt));
    }

    result.ok = true;
    mysql_stmt_close(stmt);
    return true;
}

void fill_box_from_statement(Box& box, const DBStatementResult& result) {
    box.rows = result.rows;
    box.affected_rows = result.affected_rows;
    box.insert_id = result.insert_id;
    if (!result.rows.empty() && !result.rows[0].empty())
        box.result = result.rows[0][0];
    else if (result.ok)
        box.result = "OK";
    if (!result.ok)
        box.err = result.err;
}

bool execute_single(DBHandle conn, const DBTask& job) {
    DBStatement request{job.sql, job.params};
    DBStatementResult result;
    bool ok = execute_statement(conn, request, result);
    fill_box_from_statement(*job.box, result);
    return ok;
}

bool execute_transaction(DBHandle conn, const DBTask& job) {
    if (mysql_query(conn, "START TRANSACTION") != 0) {
        job.box->err = mysql_error(conn);
        return false;
    }

    bool success = true;
    for (const auto& request : job.statements) {
        DBStatementResult result;
        bool statement_ok = execute_statement(conn, request, result);
        job.box->statements.push_back(result);
        if (!statement_ok) {
            success = false;
            job.box->err = result.err;
            break;
        }
        if (request.require_affected && result.affected_rows == 0) {
            success = false;
            job.box->err = "no rows affected";
            break;
        }
    }

    if (success) {
        if (mysql_commit(conn) != 0) {
            job.box->err = mysql_error(conn);
            success = false;
        } else {
            job.box->committed = true;
            job.box->result = "OK";
        }
    }
    if (!success) {
        mysql_rollback(conn);
        if (job.box->err.empty())
            job.box->err = "transaction rolled back";
    }
    return success;
}

}  // namespace

DB_pool::DB_pool(int conns, int workers, work_pool* business_pool,
                 const std::string& host, const std::string& user,
                 const std::string& password, const std::string& database,
                 unsigned int port)
    : conn_pool_(conns, host, user, password, database, port),
      business_pool_(business_pool), worker_count_(workers),
      tasks_(static_cast<size_t>(workers * 32)) {
    for (int i = 0; i < worker_count_; i++)
        workers_.emplace_back(&DB_pool::worker_loop, this);
}

DB_pool::~DB_pool() {
    shutdown();
}

push_result DB_pool::submit(DBTask task) {
    record_task_enqueued();
    push_result result = tasks_.push(std::move(task));
    if (result != push_result::Ok) {
        finish_task();
        return result;
    }
    if (metrics_) metrics_->on_task_enqueued(PoolId::DB);
    return push_result::Ok;
}

void DB_pool::record_task_enqueued() {
    std::lock_guard<std::mutex> lock(idle_mutex_);
    ++unfinished_task_count_;
}

void DB_pool::finish_task() {
    {
        std::lock_guard<std::mutex> lock(idle_mutex_);
        if (unfinished_task_count_ > 0)
            --unfinished_task_count_;
    }
    idle_cv_.notify_all();
}

void DB_pool::worker_loop() {
    while (true) {
        DBTask job;
        if (!tasks_.pop(job))
            return;
        if (metrics_) metrics_->on_task_dequeued(PoolId::DB);
        active_query_count_++;

        DBHandle conn = conn_pool_.get();
        if (!conn) {
            if (job.box) {
                job.box->err = "database pool stopped";
                job.box->ready = true;
            }
            if (business_pool_ && job.box) {
                auto* pool = business_pool_;
                push_result result = pool->add_task(
                    [pool, key = job.wait_name] { pool->on_event(key); });
                if (result == push_result::Closed && log_)
                    log_->error("db resume drop: work pool closed");
            }
            active_query_count_--;
            finish_task();
            continue;
        }

        uint64_t db_start_us = Metrics::now_us();
        try {
            bool ok = job.transaction ? execute_transaction(conn, job)
                                      : execute_single(conn, job);
            if (!ok && log_ && job.box && !job.box->err.empty() &&
                job.box->err != "no rows affected")
                log_->error("db execute failed: " + job.box->err);
        } catch (const std::exception& e) {
            job.box->err = e.what();
            if (metrics_) metrics_->on_error(ErrorStage::DB);
        } catch (...) {
            job.box->err = "unknown";
            if (metrics_) metrics_->on_error(ErrorStage::DB);
        }
        if (metrics_) metrics_->on_module_task_done(PoolId::DB, Metrics::now_us() - db_start_us);
        job.box->ready = true;
        conn_pool_.release(conn);
        active_query_count_--;

        if (job.box && job.box->suspend_guard)
            job.box->suspend_guard->wait_finished();

        uint64_t key = job.wait_name;
        auto* pool = business_pool_;
        push_result result = pool->add_task([pool, key]{ pool->on_event(key); });
        if (result == push_result::Closed && log_)
            log_->error("db resume drop: work pool closed");
        finish_task();
    }
}

void DB_pool::shutdown() {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true))
        return;
    tasks_.close();
    conn_pool_.shutdown();
    for (auto& t : workers_)
        if (t.joinable()) t.join();
}

bool DB_pool::wait_idle(const std::chrono::milliseconds& timeout) {
    std::unique_lock<std::mutex> lock(idle_mutex_);
    return idle_cv_.wait_for(lock, timeout, [this] {
        return unfinished_task_count_ == 0;
    });
}
