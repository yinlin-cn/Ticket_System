#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Box.h"
#include "DBResult.h"

struct DBTask {
    uint64_t wait_name;
    std::shared_ptr<Box> box;
    std::string sql;                       // 单语句 SQL 模板，含 ? 占位符
    std::vector<std::string> params;       // 单语句参数，按顺序对应 ?
    std::vector<DBStatement> statements;   // 事务模式下的语句列表
    bool transaction = false;              // true 时在一条连接上执行完整事务
};
