# 客户端

客户端使用 Java 17、Swing 和 JDK 标准库实现，不依赖 Maven 或 Gradle。

## 1. 目录

```text
源代码/
  build.bat
  run.bat
  build.sh
  run.sh
  package.bat
  src/ticketslots/
    tasks/       可插拔业务任务
    ui/          Swing 页面和界面端口
    model/       客户端数据模型
```

## 2. 运行

### 2.1 免 JDK 发布包

如果目标电脑没有安装 JDK 或 JRE，可以在有 JDK 17 或更高版本的构建电脑上执行：

```bat
cd 源代码
package.bat
```

脚本会预编译客户端，并使用 JDK 自带的 `jpackage` 把裁剪后的 Java 运行时打进
Windows 应用目录。生成结果：

```text
源代码/release/TicketClient/TicketClient.exe
源代码/release/TicketClient-Windows-x64.zip
```

目标电脑只需要解压 ZIP 并双击 `TicketClient.exe`，不需要安装 Java，也不需要
配置 `JAVA_HOME` 或 `PATH`。启动器
`安装及启动脚本/启动免JDK客户端.bat` 也可以打开同目录结构中的应用。

发布包包含约几十 MB 的 Java 运行时，这是脱离系统 Java 环境运行的代价。
目标电脑必须保留整个发布目录，不能只拿走 `TicketClient.exe`；`runtime/` 和
`app/` 分别保存 Java 运行时和客户端程序。当前 `package.bat` 生成 Windows
x64 应用，不能直接复制到 Linux、macOS 或 ARM 电脑运行。

### 2.2 源码运行

需要安装 JDK 17 或更高版本，并确保 `java` 和 `javac` 都能从命令行直接执行。
只安装 JRE 时客户端可以运行已编译程序，但无法在当前交付包中完成构建。

在 Windows `cmd.exe` 中：

```bat
cd 源代码
build.bat
run.bat
```

也可以直接双击 `安装及启动脚本/启动客户端.bat`。启动脚本每次都会清理 `out/`
并重新编译，避免旧的 `.class` 文件造成“源码已经修改但界面没有变化”或类加载
失败。编译失败时，窗口会保留并显示 `javac` 的具体错误。

在 Linux、WSLg 或其他带图形桌面的环境中：

```bash
cd 源代码
chmod +x build.sh run.sh
./run.sh
```

手工运行编译结果：

```bat
java -cp out ticketslots.TicketClientApp
```

IDE 中应把 `src/` 标记为源码根目录，选择 JDK 17 或更高版本，并将主类设置为
`ticketslots.TicketClientApp`。不要把 `ui`、`tasks` 等子包单独作为主类启动。

客户端默认连接 `127.0.0.1:9101`。连接页面可以修改服务端地址和端口。

如果启动窗口没有出现，请先在终端执行 `run.bat` 并保留完整报错。常见原因是
JDK 未安装、`JAVA_HOME` 指向旧版本、编译缓存损坏，或在没有图形桌面的 WSL
终端中直接运行 Swing 客户端。

## 3. 页面

```text
连接
登录
商品
购票
订单
退票
日志
```

页面只负责渲染、收集输入和提交事件，不直接访问 Socket，也不直接修改共享业务
状态。

## 4. 线程模型

```text
Swing EDT
  -> FrontendSlot
  -> LongTaskRuntime
  -> ClientTask
  -> ProtocolWorker
  -> NetworkClient writer

NetworkClient reader
  -> AppEvent.WIRE
  -> LongTaskRuntime
  -> ProtocolWorker
  -> ClientTask
  -> AppState
  -> ComponentWorker
  -> Swing EDT
```

| 组件 | 职责 |
| --- | --- |
| `LongTaskRuntime` | 单线程处理客户端业务事件 |
| `NetworkClient` reader | 阻塞读取长度帧 |
| `NetworkClient` writer | 阻塞写帧并在空闲时发送心跳 |
| `ProtocolWorker` | 请求登记、命令匹配、后续响应和超时 |
| `ComponentWorker` | 将状态更新切回 Swing EDT |

客户端队列边界：

| 队列 | 容量 | 满时行为 |
| --- | --- | --- |
| 前端事件队列 | 2048 | 丢弃新事件并记录日志 |
| 出站网络队列 | 1024 | `send()` 返回失败 |
| 协议待处理请求 | 动态 | 60 秒超时 |

## 5. 两阶段业务

购票和退票不能收到第一帧就结束：

```text
order.reserve -> reserve -> reserve.result
refund.submit -> refund  -> refund.result
```

`queued` 是受理状态，最终结果必须继续等待 `.result`。早失败也可能直接返回
`error`，因此任务需要同时处理两种路径。

## 6. 扩展方式

新增业务时：

1. 实现 `ui.ScreenView`。
2. 在 `MainFrame` 注册页面。
3. 在 `NavigationService` 注册页面标识。
4. 在 `AppState` 增加状态和快照字段。
5. 新建 `ClientTask` 或在已有任务中增加事件分支。
6. 在 `TicketClientApp` 注册任务。

详细线程、状态和导航设计见 [客户端架构](../技术文档/客户端架构.md)。
