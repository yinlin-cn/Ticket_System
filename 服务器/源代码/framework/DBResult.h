#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct DBStatement {
    std::string sql;
    std::vector<std::string> params;
    bool require_affected = false;   // 事务中若影响行数为 0，则中止并回滚
};

struct DBStatementResult {
    bool ok = false;
    std::string err;
    uint64_t affected_rows = 0;
    uint64_t insert_id = 0;
    std::vector<std::vector<std::string>> rows;
};

// 数据库查询结果：业务层必须显式判断，避免把取消/错误当正常结果。
struct DBResult {
    bool ok = false;          // 是否成功拿到结果
    bool cancelled = false;   // 是否被取消（退出/超时）
    std::string err;          // 错误描述（!ok 且未取消时有效）
    std::string data;         // 正常结果（ok 时有效）
    std::vector<std::vector<std::string>> rows;   // 完整多行多列结果

    // 写入语句的返回值，以及事务批处理结果。
    uint64_t affected_rows = 0;
    uint64_t insert_id = 0;
    bool committed = false;
    std::vector<DBStatementResult> statements;
};
