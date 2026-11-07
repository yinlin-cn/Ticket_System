#include "CacheModule.h"

#include <mutex>

CacheModule::CacheModule(std::size_t capacity, std::chrono::milliseconds ttl)
    : capacity_(capacity ? capacity : 1), default_ttl_(ttl) {}

bool CacheModule::expired(const Entry& entry) {
    return std::chrono::steady_clock::now() >= entry.expire_at;
}

bool CacheModule::get(const std::string& key, std::string& out) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) {
        ++stats_.miss;
        return false;
    }
    if (expired(it->second)) {
        lru_.erase(it->second.lru);
        map_.erase(it);
        ++stats_.miss;
        ++stats_.expired;
        return false;
    }

    // 访问情况队列：命中的键挪到队首。
    lru_.splice(lru_.begin(), lru_, it->second.lru);
    out = it->second.value;
    ++stats_.hit;
    return true;
}

void CacheModule::put(const std::string& key, std::string value) {
    put(key, std::move(value), default_ttl_);
}

void CacheModule::put(const std::string& key, std::string value,
                      std::chrono::milliseconds ttl) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto expire_at = std::chrono::steady_clock::now() + ttl;

    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second.value = std::move(value);
        it->second.expire_at = expire_at;
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return;
    }

    evict_if_needed_locked();
    lru_.push_front(key);
    map_.emplace(key, Entry{std::move(value), lru_.begin(), expire_at});
}

void CacheModule::evict_if_needed_locked() {
    while (map_.size() >= capacity_ && !lru_.empty()) {
        const std::string victim = lru_.back();
        lru_.pop_back();
        map_.erase(victim);
        ++stats_.evicted;
    }
}

bool CacheModule::erase(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it == map_.end())
        return false;
    lru_.erase(it->second.lru);
    map_.erase(it);
    return true;
}

void CacheModule::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_.clear();
    lru_.clear();
}

std::size_t CacheModule::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.size();
}

std::size_t CacheModule::capacity() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return capacity_;
}

void CacheModule::set_capacity(std::size_t capacity) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    capacity_ = capacity ? capacity : 1;
    evict_if_needed_locked();
}

CacheModule::Stats CacheModule::stats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return stats_;
}
