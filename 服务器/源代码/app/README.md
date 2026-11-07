# 服务器业务层

`服务器/源代码/app` 是票务系统的业务实现层。它不改写框架的网络核心，而是通过
`TicketService::handle()` 接入框架短任务，再借助 `ConnectionRoster`、
`OrderEngine`、`CacheModule` 和 `RedisClient` 完成鉴权、库存、下单、退票以及
异步结果投递。

更上层的构建、环境和完整协议说明见 [服务端 README](../../README.md)；本文件重点
说明业务模块的职责、调用链、并发边界和扩展方式。

## 1. 模块划分

| 文件 | 职责 |
| --- | --- |
| `main.cpp` | 创建框架 `Server`，注册快速路由和数据库路由，启动 `TicketService` |
| `TicketService.*` | 业务总入口；解析命令、鉴权、连接身份、挑战登录、商品、订单与退票接口 |
| `ConnectionRoster.*` | 业务身份名册；维护虚拟 FD、账号身份和当前连接的关系 |
| `OrderEngine.*` | 每票种订单队列、退款队列、批处理调度、独立数据库线程池与连接池 |
| `CacheModule.*` | 有容量上限和 TTL 的 LRU 缓存，用于商品、价格和库存快照 |
| `RedisClient.*` | Redis 原子准入、幂等键、库存补偿与库存同步 |
| `Password.*` | PBKDF2、HMAC、随机数以及挑战登录证明校验 |

业务层与框架之间只有两个明确接触点：

1. 框架把完整请求帧交给 `TicketService::handle()`。
2. 业务层通过框架上下文、`ConnectionSession` 和 `ConnectionRoster` 回送消息。

订单落库不使用框架的数据库连接池。`OrderEngine` 自己维护数据库连接和线程，
因此订单处理不会占用框架的 DB 准入额度。

## 2. 总体链路

```text
客户端请求
  -> 框架 Reactor / 解析线程
  -> TicketService::handle()
       |- 登录、商品、订单查询：框架 DB 协程路径
       |- 下单、退票：业务短任务，只做准入和入队
  -> Redis 原子检查库存与幂等键（可选）
  -> OrderEngine 按票种入队
  -> OrderEngine 调度线程切片
  -> 独立数据库线程池 + 独立连接池
  -> MariaDB 事务落库
  -> ConnectionRoster 按账号身份异步投递结果
```

主流程的核心原则是：

- 下单和退票入口必须保持短任务，不在框架业务线程里等待数据库事务。
- MariaDB 是订单、退款和最终库存的事实来源。
- Redis 只承担快速准入、幂等拦截和库存协调，不是最终账本。
- 一个账号同一时刻最多保留一条在线业务连接。

## 3. `TicketService`

`TicketService::handle(int request_id, std::string message)` 接收完整帧体，并按
`|` 拆分命令。`request_id` 当前未参与业务语义。

### 3.1 命令到实现

| 命令 | 实现入口 | 处理路径 |
| --- | --- | --- |
| `ping` | `handle()` | 直接回 `pong|ok` |
| `register` | `register_user()` | 框架 DB |
| `login` | `login()` | 框架 DB，兼容旧客户端 |
| `login.challenge` | `login_challenge()` | 框架 DB + 内存挑战表 |
| `login.proof` | `login_proof()` | 框架 DB + 密码证明校验 |
| `catalog.list` | `catalog_list()` | 缓存优先，缺失时框架 DB 回源 |
| `order.reserve` | `reserve()` | Redis 准入 + `OrderEngine` 入队 |
| `order.status` | `order_status()` | 框架 DB |
| `order.my` | `my_orders()` | 框架 DB |
| `refund.submit` | `refund()` | `OrderEngine` 退款队列 |
| `whoami` | `whoami()` | 连接身份名册 |
| `logout` | `logout()` | 连接身份名册 + token 清理 |
| `notice.push` | `notice_push()` | 按业务身份定向发送 |

### 3.2 鉴权顺序

业务请求首先按当前连接在 `ConnectionRoster` 中查身份。若找不到，在
`TICKET_TOKEN_FALLBACK=1` 时再尝试旧版 token 路径。

生产方向应关闭 token 兼容路径：

```bash
TICKET_TOKEN_FALLBACK=0
```

当前 token 保存在进程内存中，服务重启后失效。连接身份则是主路径，断线后由框架
连接变更通知和周期对账清理。

### 3.3 挑战式登录

1. 客户端发送 `login.challenge|username`。
2. 服务端返回一次性 `nonce` 和账号 `salt`。
3. 客户端计算：

```text
key   = PBKDF2-HMAC-SHA256(password, salt, 120000 iterations, 256 bits)
proof = HMAC-SHA256(key, nonce)
```

4. 客户端以 64 位小写十六进制发送 `login.proof|username|proof`。

`nonce` 有效期为 30 秒，只能使用一次；同一条连接只保留最新挑战。未知账号也会
获得格式正确的随机 salt 并执行等价密码计算，避免明显的账号枚举时序差。

## 4. `ConnectionRoster`

框架中的虚拟 FD 表示“当前连接”，业务身份 ID 表示“账号”。两者不是同一概念。

`ConnectionRoster` 维护：

```text
virtual_fd -> RosterIdentity
identity_id -> virtual_fd
```

关键接口：

```cpp
LoginResult login(virtual_fd, conn, identity_id, user_id, username, existing_fd);
bool resolve(virtual_fd, conn, identity);
bool send_to_identity(identity_id, message);
bool deliver(virtual_fd, conn, identity_id, message);
void apply_changes(changes);
size_t reconcile();
```

投递策略是：

1. 如果提交请求时的连接仍然存活，优先送回原连接。
2. 如果原连接已断开，则按账号身份寻找当前在线连接。
3. 如果账号当前不在线，结果不进入业务连接。

连接是否仍然有效，使用连接对象指针和框架名册共同判断，不能只相信可能被复用的
虚拟 FD。

## 5. 下单调用链

入口：

```text
order.reserve|<token>|<ticket_type_id>|<quantity>|<idempotency_key>
```

约束：

- 必须已登录。
- `quantity` 为 `1..10`。
- `idempotency_key` 非空且不超过 64 字节。

Redis 准入开启时：

1. `TicketService::reserve()` 生成订单号。
2. `RedisClient::reserve()` 通过 Lua 原子检查幂等键与库存。
3. 库存不足、未初始化、Redis 错误或幂等冲突时直接返回最终错误结果。
4. 准入成功时先发送 `reserve|queued|...`，再将订单放入票种队列。
5. `OrderEngine` 从队列切片，使用独立数据库事务写入订单。
6. 成功后发送 `reserve.result|ok|...`。
7. 失败且超过重试上限时发送 `reserve.result|error|...`，并补偿 Redis 预扣。

必须保持“先发 queued、再入队”的顺序。同一条连接的发送队列是 FIFO，这样可以
保证客户端先看到受理回执，再看到异步最终结果。

## 6. 退票调用链

入口：

```text
refund.submit|<token>|<order_no>|<quantity>|<idempotency_key>|<ticket_type_id>
```

退票同样是两阶段接口：

```text
refund|queued|<refund_no>|<order_no>|accepted
refund.result|ok|...       或       refund.result|error|...
```

`OrderEngine` 对同一票种按以下顺序调度：

1. 先处理退款队列。
2. 退款事务完成后释放该票种的购票队列。
3. 再处理购票请求，使恢复后的库存能参与下一批准入预算。

退款事务会校验：

- 订单是否存在。
- 订单是否属于当前用户。
- 请求票种是否与订单票种一致。
- 订单状态是否允许退款。
- 累计退款数量是否超过购买数量。
- 幂等键是否已经使用。

成功后同时更新 `ticket_orders`、`ticket_refunds` 和
`ticket_inventory.remaining_qty`。

## 7. `OrderEngine`

### 7.1 队列结构

每个票种独立维护：

```cpp
struct TypeQueue {
    std::deque<PendingOrder> items;
    std::deque<PendingRefund> refunds;
    bool in_flight;
    int attempts;
    steady_clock::time_point retry_after;
};
```

按票种拆队列的目的：

- 不同票种可以并行落库。
- 同一票种的购票队列由单个 `in_flight` 批次串行推进，避免同票种内部竞争。
- 退票可以在同票种内优先于新购票处理。
- 某一票种失败重试时，不会直接阻塞其他票种。

### 7.2 批处理

调度线程默认每 2ms 扫描一次，每批最多取 500 条。购票批次在一个事务中完成：

```text
UPDATE ticket_inventory
   SET remaining_qty = remaining_qty - batch_units
 WHERE ticket_type_id = ? AND remaining_qty >= batch_units;

INSERT INTO ticket_orders (...) VALUES (...), (...), ...;
```

多值 `INSERT` 减少语句数，条件 `UPDATE` 负责数据库侧最终防超卖。事务提交后
回写缓存库存并投递结果。

### 7.3 额度与准入

开启 Redis 时，批处理预算近似为：

```text
数据库可处理量 = Redis 当前库存 + 已准入但尚未落库的票数
```

因为 Redis 在准入时已经扣减库存，而数据库要在落库时再扣减。`admitted_units_`
记录两者之间的未落库差额。

关闭 Redis 时：

```bash
TICKET_ADMISSION_REDIS=0
```

所有请求直接入业务队列，最终靠数据库条件更新防超卖。该模式省掉 Redis 往返，
但没有前置库存闸门和前置幂等拦截；重复幂等键可能浪费一次库存扣减。需要明确
取舍后再用于高并发环境。

### 7.4 背压现状

当前实现已经具备：

- Redis 库存闸门限制可进入订单队列的有效请求量。
- 每票种 `in_flight` 状态限制同票种同时推进的购票批次。
- `batch_max` 限制一次切片的订单数量。
- 框架本身有网络发送队列、数据库准入和线程池背压。

当前业务订单队列没有单独的硬容量上限或高低水位。若长期关闭 Redis 且入口流量
远高于数据库处理速度，队列和进程内存仍可能持续增长。生产化时应增加队列上限、
水位阈值、暂停接收或快速失败策略。

## 8. `CacheModule`

缓存实现是可设容量的 LRU，并在读取时惰性检查 TTL。

| 键 | 内容 | 默认 TTL |
| --- | --- | --- |
| `catalog:types` | 全部票种 ID 列表 | 30 秒 |
| `catalog:item:<id>` | 活动名、票名、价格 | 30 秒 |
| `catalog:price:<id>` | 单价 | 300 秒 |
| `stock:<id>` | 最近一次落库或退款后的库存 | 300 秒 |

默认容量为 4096 项。缓存命中不会改变数据库事实，也不代表数据库事务已经完成；
它主要用于降低商品浏览和批处理读取价格/库存时的数据库压力。

## 9. 扩展业务模块

新增业务前先判断它属于哪条路径：

### 短查询任务

适合直接使用框架数据库协程：

- 单次、快速 SQL。
- 可以接受框架 DB 准入和线程池限制。
- 不持有连接等待其他外部事件。
- 例如商品查询、订单状态、个人订单。

### 独立长任务

适合放入业务专用线程池：

- 需要批处理、事务重试或多阶段推进。
- 不应长期占用框架业务线程。
- 需要独立数据库连接额度。
- 例如订单落库和退票处理。

接入时遵循以下规则：

1. 不要在框架业务线程中阻塞等待 `OrderEngine`。
2. 异步结果通过 `ConnectionRoster` 按 `identity_id` 投递。
3. 不要在队列项里长期持有裸 socket 或强连接对象。
4. 对需要幂等的写请求，在入口和数据库层都要设计防重。
5. 明确失败、重试、补偿和最终状态，不把“已入队”当成“已成功”。

## 10. 验证入口

在 Ubuntu 或 WSL 中，从 `服务器/源代码` 完成初始化并构建：

```bash
bash scripts/init_db.sh
bash scripts/init_redis.sh
bash scripts/build.sh
bash scripts/run_server.sh
```

随后使用 Java 客户端完成注册、登录、商品查询、购票和退票链路。完整环境、
演示数据重置说明和故障排查见
[构建运行与验证](../../../技术文档/构建运行与验证.md)。

## 11. 当前限制

- 业务协议基于明文 TCP，没有 TLS。
- 会话 token 保存在进程内存，不支持多实例共享。
- 一个账号只允许一条在线连接。
- 订单队列尚无硬容量限制。
- 支付状态模拟为 `PENDING_PAY`，未接入真实支付。
- 关闭 Redis 时，购票幂等只由数据库唯一键兜底，重复请求仍可能消耗一次库存。
- 框架登录、商品查询和订单查询的数据库配置在 `app/main.cpp` 中硬编码，尚未完全
  环境变量化。
