# 服务器

服务端使用 C++20 实现，由当前并发框架和票务业务层组成。

## 1. 目录

```text
源代码/
  CMakeLists.txt
  app/          票务业务层
  framework/    服务器并发框架
  db/           MariaDB 建表和演示数据
  scripts/      构建、初始化、启动脚本
```

`app/` 与 `framework/` 保持职责分离：

- `framework/` 负责连接、分帧、Reactor、短任务、数据库协程、发送和通用背压。
- `app/` 负责登录、商品、连接身份、购票、退票、缓存、Redis 和订单批处理。

## 2. 请求入口

框架把完整帧体交给业务入口：

```cpp
EventTask TicketService::handle(int request_id, std::string message);
```

`request_id` 当前不参与票务协议语义。业务根据命令前缀完成参数校验、身份检查、
Redis 准入或框架数据库查询。

## 3. 路由分工

快速短任务路由：

```text
ping|
whoami|
logout|
notice.push|
order.reserve|
refund.submit|
```

框架数据库协程路由：

```text
register|
login|
login.challenge|
login.proof|
catalog.
order.status|
order.my|
```

`order.reserve` 和 `refund.submit` 是短任务的含义是“入口不等待数据库事务”，
不是“订单已经完成”。

## 4. 业务模块

| 模块 | 职责 |
| --- | --- |
| `TicketService` | 命令分发、参数校验、鉴权和业务入口 |
| `ConnectionRoster` | 连接与账号身份映射、异步结果投递 |
| `OrderEngine` | 按票种分队列、批处理、事务、重试 |
| `RedisClient` | 原子准入、幂等键、库存同步和补偿 |
| `CacheModule` | 有容量上限和 TTL 的 LRU 快照 |
| `Password` | PBKDF2、HMAC、随机数和挑战证明 |

## 5. 两条数据库路径

登录、商品和订单查询使用框架数据库协程。购票和退票由 `OrderEngine` 使用独立
数据库连接池和线程池，不占用框架 DB 准入额度。

同一票种内部先处理退款队列，再处理购票队列，并通过 `in_flight` 保证同库存行
不会同时推进多个批次。不同票种可以并行处理。

## 6. 运行

在 WSL 或 Ubuntu 中：

```bash
bash scripts/install_deps.sh
bash scripts/init_db.sh
bash scripts/init_redis.sh
bash scripts/build.sh
bash scripts/run_server.sh
```

服务监听 `0.0.0.0:9101`。数据库、Redis、构建和初始化细节见
[构建运行与验证](../技术文档/构建运行与验证.md)。`install_deps.sh` 只安装
系统依赖，不会创建、删除或重置票务数据。

## 7. 配置

常用环境变量：

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `TICKET_ADMISSION_REDIS` | `1` | 是否启用 Redis 前置准入 |
| `TICKET_TOKEN_FALLBACK` | `1` | 是否允许旧版 token 兜底鉴权 |
| `TICKET_REDIS_HOST` | `127.0.0.1` | Redis 地址 |
| `TICKET_REDIS_PORT` | `6379` | Redis 端口 |
| `TICKET_DB_HOST` | `127.0.0.1` | 业务数据库地址 |
| `TICKET_DB_PORT` | `3306` | 业务数据库端口 |
| `TICKET_DB_USER` | `chat` | 业务数据库用户 |
| `TICKET_DB_PASSWORD` | `chat123` | 业务数据库密码 |
| `TICKET_DB_NAME` | `chat` | 业务数据库名 |
| `TICKET_ORDER_DB_CONNECTIONS` | `8` | 订单模块连接数 |
| `TICKET_ORDER_DB_WORKERS` | `6` | 订单模块 worker 数 |
| `TICKET_ORDER_BATCH_MAX` | `500` | 单批最大项数 |
| `TICKET_ORDER_TICK_MS` | `2` | 调度扫描周期 |

框架 DB 路径当前固定为 `chat/chat123/chat`，20 个连接和 20 个 worker，
配置位于 `app/main.cpp`。

## 8. 详细资料

- [服务端架构](../技术文档/服务端架构.md)
- [通信协议与数据模型](../技术文档/通信协议与数据模型.md)
- [构建运行与验证](../技术文档/构建运行与验证.md)
- [业务层说明](源代码/app/README.md)
