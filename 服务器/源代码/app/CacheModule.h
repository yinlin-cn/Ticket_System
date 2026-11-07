#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <shared_mutex>
#include <string>
#include <unordered_map>

// 缓存键约定：商品条目、商品价格、库存额度分别独立成键，
// 订单处理模块与商品浏览模块共用同一套键名。
inline std::string cache_key_catalog_item(const std::string& ticket_type_id) {
    return "catalog:item:" + ticket_type_id;
}
inline std::string cache_key_catalog_price(const std::string& ticket_type_id) {
    return "catalog:price:" + ticket_type_id;
}
inline std::string cache_key_stock(const std::string& ticket_type_id) {
    return "stock:" + ticket_type_id;
}
inline constexpr const char* kCacheKeyCatalogTypes = "catalog:types";

// 缓存模块：商品信息等热点数据的驻留层。
//
// 对应设计图里的缓存模块：
//   - 哈希键值对集合：存商品信息，读共享、写独占；
//   - 访问情况有序队列：队首最新、队尾最旧，容量不足时淘汰队尾；
//   - 缓存最高容纳量 / 当前可用量：size() < capacity() 判断；
//   - 条目过期时间：读时惰性判定，过期即视为未命中。
//
// 读路径要更新访问队列，所以 get() 会短暂独占；写路径独占。
class CacheModule {
public:
    struct Stats {
        uint64_t hit = 0;
        uint64_t miss = 0;
        uint64_t expired = 0;
        uint64_t evicted = 0;
    };

    explicit CacheModule(std::size_t capacity = 4096,
                         std::chrono::milliseconds ttl =
                             std::chrono::milliseconds(30000));

    // 命中返回 true 并回填 value；未命中或已过期返回 false。
    bool get(const std::string& key, std::string& out);

    // 插入或更新（设计图：向缓存内插入或更新商品信息，可能涉及置换）。
    void put(const std::string& key, std::string value);
    void put(const std::string& key, std::string value,
             std::chrono::milliseconds ttl);

    bool erase(const std::string& key);
    void clear();

    std::size_t size() const;
    std::size_t capacity() const;
    void set_capacity(std::size_t capacity);
    Stats stats() const;

private:
    struct Entry {
        std::string value;
        std::list<std::string>::iterator lru;
        std::chrono::steady_clock::time_point expire_at;
    };

    static bool expired(const Entry& entry);
    void evict_if_needed_locked();

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> lru_;   // front = 最近被访问
    std::size_t capacity_;
    std::chrono::milliseconds default_ttl_;
    Stats stats_;
};
