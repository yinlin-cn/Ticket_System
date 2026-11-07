#include "ConnectionRoster.h"

#include <utility>

#include "FrameworkCall.h"
#include "thread_context.h"

using namespace std;

namespace {

// 框架册子在 Server::start() 之后才存在，这里按需取，不缓存指针。
shared_ptr<connect_book> current_book() {
    auto* call = g_framework_call;
    return call ? call->connection_book() : nullptr;
}

bool send_to_fd(uint64_t virtual_fd, const string& message) {
    auto* call = g_framework_call;
    if (!call)
        return false;
    return call->call("send_to_sb", {std::to_string(virtual_fd), message}).success;
}

}  // namespace

bool ConnectionRoster::alive_locked(const Entry& entry) const {
    auto conn = entry.conn.lock();
    if (!conn)
        return false;

    auto book = current_book();
    if (!book)
        return conn->connected.load();

    // 用连接对象本身比对，而不是 find(vfd)：连接号会被 socket 复用，
    // 只看号会把"号还活着但是另一条连接"误判成原连接在线。
    uint64_t vfd = 0;
    int group = -1;
    if (!book->connection_info(conn, vfd, group))
        return false;
    return vfd == entry.virtual_fd;
}

void ConnectionRoster::erase_locked(uint64_t virtual_fd) {
    auto it = by_vfd_.find(virtual_fd);
    if (it == by_vfd_.end())
        return;
    auto rev = by_identity_.find(it->second.identity_id);
    if (rev != by_identity_.end() && rev->second == virtual_fd)
        by_identity_.erase(rev);
    by_vfd_.erase(it);
}

ConnectionRoster::LoginResult ConnectionRoster::login(
    uint64_t virtual_fd, weak_ptr<Internalconnection> conn, uint64_t identity_id,
    uint64_t user_id, const string& username, uint64_t& existing_fd) {
    existing_fd = 0;
    if (virtual_fd == 0 || identity_id == 0)
        return LoginResult::Invalid;

    lock_guard<mutex> lock(mutex_);

    auto self = conn.lock();

    // 同一条连接重复登录同一个账号：幂等接受，不当作冲突。
    // 必须连连接本身一起比对：连接号会被 socket 复用，只比身份会把
    // 新连接错认成旧会话，条目里的连接对象还指着已经死掉的那条。
    auto same = by_vfd_.find(virtual_fd);
    if (same != by_vfd_.end() && same->second.identity_id == identity_id &&
        same->second.conn.lock().get() == self.get()) {
        same->second.username = username;
        return LoginResult::Ok;
    }

    // 账号级一对一：该身份已经有活着的连接，后来者拒绝。
    auto rev = by_identity_.find(identity_id);
    if (rev != by_identity_.end()) {
        auto old = by_vfd_.find(rev->second);
        if (old != by_vfd_.end() && alive_locked(old->second)) {
            existing_fd = rev->second;
            return LoginResult::AlreadyOnline;
        }
        // 陈旧记录（连接已死，但 remove 变更没能送到或被挤掉）：清掉放行。
        if (old != by_vfd_.end())
            by_vfd_.erase(old);
        by_identity_.erase(rev);
    }

    // 同一条连接换账号登录：先摘掉旧身份。
    erase_locked(virtual_fd);

    Entry entry;
    entry.identity_id = identity_id;
    entry.user_id = user_id;
    entry.virtual_fd = virtual_fd;
    entry.username = username;
    entry.conn = std::move(conn);
    by_vfd_[virtual_fd] = std::move(entry);
    by_identity_[identity_id] = virtual_fd;
    return LoginResult::Ok;
}

bool ConnectionRoster::resolve(uint64_t virtual_fd, const Conn& conn,
                               RosterIdentity& identity) {
    if (virtual_fd == 0 || !conn)
        return false;
    lock_guard<mutex> lock(mutex_);
    auto it = by_vfd_.find(virtual_fd);
    if (it == by_vfd_.end())
        return false;
    // 连接号被复用的情况：这条记录已经不属于当前连接。
    if (it->second.conn.lock().get() != conn.get())
        return false;

    identity.identity_id = it->second.identity_id;
    identity.user_id = it->second.user_id;
    identity.virtual_fd = it->second.virtual_fd;
    identity.username = it->second.username;
    return true;
}

bool ConnectionRoster::logout(uint64_t virtual_fd) {
    lock_guard<mutex> lock(mutex_);
    if (by_vfd_.find(virtual_fd) == by_vfd_.end())
        return false;
    erase_locked(virtual_fd);
    return true;
}

bool ConnectionRoster::send_to_identity(uint64_t identity_id,
                                        const string& message) {
    return deliver(0, std::weak_ptr<Internalconnection>(), identity_id, message);
}

bool ConnectionRoster::deliver(uint64_t virtual_fd,
                               const weak_ptr<Internalconnection>& conn,
                               uint64_t identity_id, const string& message) {
    if (identity_id == 0)
        return false;

    // 优先回到发起请求的那条连接。连接号可能被 socket 复用，所以必须拿连接
    // 对象去框架名册里比对，确认这个号现在仍属于同一条连接。
    if (virtual_fd != 0) {
        auto requested = conn.lock();
        if (requested) {
            auto book = current_book();
            uint64_t current_fd = 0;
            int group = -1;
            if (book &&
                book->connection_info(requested, current_fd, group) &&
                current_fd == virtual_fd) {
                if (send_to_fd(virtual_fd, message))
                    return true;
            }
        }
    }

    uint64_t vfd = 0;
    {
        lock_guard<mutex> lock(mutex_);
        auto rev = by_identity_.find(identity_id);
        if (rev == by_identity_.end())
            return false;
        auto it = by_vfd_.find(rev->second);
        if (it == by_vfd_.end()) {
            by_identity_.erase(rev);
            return false;
        }
        if (!alive_locked(it->second)) {
            erase_locked(rev->second);
            return false;
        }
        vfd = it->second.virtual_fd;
    }

    return send_to_fd(vfd, message);
}

void ConnectionRoster::apply_changes(const vector<net_changer>& changes) {
    lock_guard<mutex> lock(mutex_);
    for (const auto& change : changes) {
        if (change.cmd != "remove" && change.cmd != "rebind")
            continue;
        auto it = by_vfd_.find(change.virtual_fd);
        if (it == by_vfd_.end())
            continue;
        // 连接号可能已经被复用给一条新连接：这条变更若对应的连接还活着
        // 就说明它是过期通知，不能照着它删。
        if (alive_locked(it->second))
            continue;
        erase_locked(change.virtual_fd);
    }
}

size_t ConnectionRoster::reconcile() {
    lock_guard<mutex> lock(mutex_);
    size_t removed = 0;
    for (auto it = by_vfd_.begin(); it != by_vfd_.end();) {
        if (alive_locked(it->second)) {
            ++it;
            continue;
        }
        auto rev = by_identity_.find(it->second.identity_id);
        if (rev != by_identity_.end() && rev->second == it->first)
            by_identity_.erase(rev);
        it = by_vfd_.erase(it);
        ++removed;
    }
    return removed;
}

size_t ConnectionRoster::online_count() const {
    lock_guard<mutex> lock(mutex_);
    return by_vfd_.size();
}

vector<RosterIdentity> ConnectionRoster::online_snapshot() const {
    lock_guard<mutex> lock(mutex_);
    vector<RosterIdentity> result;
    result.reserve(by_vfd_.size());
    for (const auto& [fd, entry] : by_vfd_) {
        RosterIdentity item;
        item.identity_id = entry.identity_id;
        item.user_id = entry.user_id;
        item.virtual_fd = entry.virtual_fd;
        item.username = entry.username;
        result.push_back(std::move(item));
    }
    return result;
}
