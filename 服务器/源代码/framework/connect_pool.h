#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

#include <mysql.h>   // 真实版需要 mysql client 库

using DBHandle = MYSQL*;

class connect_pool {
    std::queue<DBHandle> conns_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;

    std::string host_, user_, password_, database_;
    unsigned int port_;
    bool healthy_ = false;   // 构造函数里是否至少成功建过一条连接

    DBHandle create_connection();
    void close_connection(DBHandle conn);

public:
    connect_pool(int size,
                 const std::string& host,
                 const std::string& user,
                 const std::string& password,
                 const std::string& database,
                 unsigned int port = 3306);
    ~connect_pool();
    void shutdown(); 
    DBHandle get();          // 池空时阻塞等待
    void release(DBHandle conn);
    // 当前空闲连接数（诊断用；0 表示建连失败或全部借出）
    std::size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return conns_.size();
    }
    // 池是否可用：构造函数里至少成功建过一条连接。
    // 用它区分"暂时全借出"和"建连全失败（get 会永久阻塞）"。
    bool healthy() const { return healthy_; }
};
