# 票务服务系统

本交付包包含票务系统的最终 C++ 服务端、最终 Java Swing 客户端，以及一套以
架构和调用链为中心的技术文档。历史设计过程和设计图不混入源码目录，统一放入
`设计草稿/`。

## 1. 系统定位

系统用于验证以下完整链路：

```text
Java Swing 客户端
  -> 长度帧协议
  -> C++ 并发框架
  -> 票务业务短任务
  -> Redis 前置准入
  -> 按票种分片的订单队列
  -> 独立数据库线程池与连接池
  -> MariaDB 事务
  -> 按账号身份异步回送结果
```

重点不是完整商业票务运营能力，而是边界清晰的并发系统设计：

- 网络处理与业务事务分离。
- 短查询与长事务使用两条数据库路径。
- 购票和退票采用“受理回执 + 异步最终结果”两阶段语义。
- 同一票种串行推进批次，不同票种可以并行。
- MariaDB 是订单、退款和最终库存的事实来源。
- Redis 是前置库存闸门和幂等加速层，不是最终账本。

## 2. 包内结构

```text
票务服务系统/
├─ README.md
├─ 服务器/
│  ├─ README.md
│  ├─ 源代码/
│  │  ├─ CMakeLists.txt
│  │  ├─ app/
│  │  ├─ framework/
│  │  ├─ db/
│  │  └─ scripts/
│  └─ 安装及启动脚本/
├─ 客户端/
│  ├─ README.md
│  ├─ 源代码/
│  │  ├─ build.bat
│  │  ├─ run.bat
│  │  ├─ build.sh
│  │  ├─ run.sh
│  │  ├─ package.bat
│  │  └─ src/
│  └─ 安装及启动脚本/
├─ 技术文档/
│  ├─ 总体架构.md
│  ├─ 服务端架构.md
│  ├─ 客户端架构.md
│  ├─ 通信协议与数据模型.md
│  └─ 构建运行与验证.md
└─ 设计草稿/
   ├─ 框架演进/
   ├─ 业务设计/
   └─ 客户端设计/
```

包内不包含构建产物、日志、旧版客户端、实验代码和压测脚本。

## 3. 快速启动

推荐在 Ubuntu 或 WSL 中运行服务端，在 Windows 中运行 Java 客户端。

### 3.1 准备 MariaDB

默认连接参数：

```text
host     127.0.0.1
port     3306
database chat
user     chat
password chat123
```

`服务器/源代码/scripts/init_db.sh` 会自动创建数据库，并把 `chat` 账号的认证
方式修正为密码认证。手工排查时可执行以下等价 SQL：

```sql
CREATE DATABASE IF NOT EXISTS chat
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'chat'@'localhost' IDENTIFIED BY 'chat123';
CREATE USER IF NOT EXISTS 'chat'@'127.0.0.1' IDENTIFIED BY 'chat123';
ALTER USER 'chat'@'localhost' IDENTIFIED BY 'chat123';
ALTER USER 'chat'@'127.0.0.1' IDENTIFIED BY 'chat123';

GRANT ALL PRIVILEGES ON chat.* TO 'chat'@'localhost';
GRANT ALL PRIVILEGES ON chat.* TO 'chat'@'127.0.0.1';
FLUSH PRIVILEGES;
```

### 3.2 初始化并启动服务端

在 WSL 中进入 `服务器/源代码`：

```bash
bash scripts/install_deps.sh
sudo service mariadb start
sudo service redis-server start
bash scripts/init_db.sh
bash scripts/init_redis.sh
bash scripts/build.sh
bash scripts/run_server.sh
```

服务默认监听 `0.0.0.0:9101`。`init_db.sh` 会创建数据库和登录账号，并把
演示库存恢复到初始值，随后用业务账号通过 TCP 验证登录；
`init_redis.sh` 会重写 Redis 库存并删除演示幂等键，只应在演示库执行。
`install_deps.sh` 适用于 Ubuntu、Debian 和 WSL，可重复执行，只安装并检查
编译器、CMake、OpenSSL、MariaDB Connector/C、hiredis、MariaDB 和 Redis。

### 3.3 启动客户端

如果目标电脑没有安装 JDK，在构建电脑上进入 `客户端/源代码` 执行：

```bat
package.bat
```

脚本会生成自带 Java 运行时的 Windows 应用：

```text
客户端/源代码/release/TicketClient/TicketClient.exe
客户端/源代码/release/TicketClient-Windows-x64.zip
```

把 ZIP 解压到目标电脑后直接双击 `TicketClient.exe` 即可，不需要安装 Java，
也不需要配置 `JAVA_HOME` 或 `PATH`。

只有需要从源码编译运行时，才进入 `客户端/源代码` 执行：

```bat
build.bat
run.bat
```

也可以双击 `客户端/安装及启动脚本/启动免JDK客户端.bat` 打开已经生成的免
JDK 版本，或双击 `启动客户端.bat` 从源码编译运行。源码运行需要 JDK 17 或
更高版本，并确保 `java` 和 `javac` 均已加入 `PATH`。

客户端默认连接 `127.0.0.1:9101`，也可以在连接页面修改地址和端口。

完整环境和故障排查见 [构建运行与验证](技术文档/构建运行与验证.md)。

## 4. 架构原则

### 4.1 框架只承载网络和短任务

框架负责连接管理、长度帧解析、Reactor、业务短任务、数据库协程和发送队列。
订单事务不在框架业务线程中等待，而是投递到票务业务自己的订单引擎。

### 4.2 两条数据库路径

| 路径 | 适用场景 | 归属 |
| --- | --- | --- |
| 框架数据库协程 | 注册、登录、商品、订单查询 | 框架连接池与线程池 |
| 业务独立数据库路径 | 购票落库、退票、批处理、事务重试 | `OrderEngine` 自建连接池与线程池 |

这样登录或商品查询拥塞时，不会直接占满订单写库资源；订单批处理也不会长期占用
框架业务线程。

### 4.3 异步结果按账号身份投递

框架虚拟 FD 只表示当前连接，断线重连后可能变化。业务层使用账号身份 ID 维护
映射，异步订单结果优先投回原连接，原连接失效时再寻找同一账号的当前在线连接。

### 4.4 两阶段业务语义

```text
order.reserve -> reserve|queued|... -> reserve.result|ok|... 或 |error|...
refund.submit -> refund|queued|...  -> refund.result|ok|... 或 |error|...
```

`queued` 只表示已受理，不表示订单已经落库。客户端必须继续等待最终结果。

## 5. 文档导航

| 文档 | 内容 |
| --- | --- |
| [总体架构](技术文档/总体架构.md) | 系统边界、分层、请求链路和关键决策 |
| [服务端架构](技术文档/服务端架构.md) | 框架层、业务层、订单队列、缓存、Redis 和背压 |
| [客户端架构](技术文档/客户端架构.md) | Swing、任务插槽、网络线程、状态和导航 |
| [通信协议与数据模型](技术文档/通信协议与数据模型.md) | 帧格式、接口、响应字段、错误码和表结构 |
| [构建运行与验证](技术文档/构建运行与验证.md) | 环境、初始化、编译、启动和验证步骤 |
| [服务端说明](服务器/README.md) | 服务端目录、配置和运行入口 |
| [客户端说明](客户端/README.md) | 客户端目录、构建、页面和任务说明 |

## 6. 设计草稿

`设计草稿/` 用于保存架构演进材料，例如：

- 早期服务器并发框架图。
- 框架调用层、连接名册和背压设计。
- 订单队列、Redis 准入和数据库路径设计。
- 客户端任务插槽、页面导航和线程模型设计。

草稿目录只提供归档位置，不伪造不存在的历史文件。建议在文件名中保留阶段和
日期，例如 `01-framework-reactor.drawio`、`02-order-engine-queues.drawio`。

## 7. 当前边界

- 协议使用明文 TCP，没有 TLS。
- 登录挑战、会话 token 和在线名册保存在单进程内存。
- 同一账号同一时刻只允许一条在线业务连接。
- 支付状态是模拟的 `PENDING_PAY`。
- 业务订单队列没有独立硬容量或高低水位。
- 框架数据库连接参数当前在 `app/main.cpp` 中配置。
- 客户端使用阻塞 Socket，不是 NIO。

这些边界不妨碍课程演示和架构验证，但生产部署前需要针对网络、安全、可靠异步和
多实例一致性继续演进。
