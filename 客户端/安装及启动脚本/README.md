# 客户端安装与启动

## 1. 免 JDK 启动

如果交付包已经包含 `源代码/release/TicketClient`，直接双击：

```text
启动免JDK客户端.bat
```

该版本自带 Java 运行时，目标电脑不需要安装 JDK、JRE，也不需要配置环境变量。
也可以直接打开：

```text
../源代码/release/TicketClient/TicketClient.exe
```

如果该目录不存在，需要在有 JDK 17 或更高版本的构建电脑上执行：

```bat
cd ../源代码
package.bat
```

## 2. 源码启动

环境要求：

- JDK 17 或更高版本，且 `java`、`javac` 已加入 `PATH`。
- Windows 10/11，或带 WSLg/图形桌面的 Linux 环境。
- 能访问服务端 `9101` 端口。

Windows 用户可以直接双击：

```text
启动客户端.bat
```

该入口会调用 `../源代码/run.bat`，每次清理 `out/`、使用 Java 17 目标版本
重新编译，然后启动主类 `ticketslots.TicketClientApp`。编译或启动失败时窗口
会保留错误信息。

也可以在 `cmd.exe` 中手工执行：

```bat
cd ..\源代码
build.bat
run.bat
```

Linux/WSL 图形终端使用：

```bash
cd ../源代码
./run.sh
```
