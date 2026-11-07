# 服务端安装与启动

实际脚本位于 `../源代码/scripts/`，因为它们通过相对路径定位源码根目录。

```bash
cd ../源代码
bash scripts/install_deps.sh
bash scripts/init_db.sh
bash scripts/init_redis.sh
bash scripts/build.sh
bash scripts/run_server.sh
```

脚本职责：

| 脚本 | 作用 |
| --- | --- |
| `install_deps.sh` | 在 Ubuntu/Debian/WSL 中安装并检查全部系统依赖 |
| `init_db.sh` | 建表、补充兼容列并写入演示票种 |
| `init_redis.sh` | 按 MariaDB 当前库存初始化 Redis 准入值 |
| `build.sh` | 使用 CMake 构建 Release 版本 |
| `run_server.sh` | 提高文件描述符上限并启动服务 |

`init_db.sh` 和 `init_redis.sh` 会修改演示数据，只应在隔离环境执行。
`install_deps.sh` 可重复执行，不会修改数据库。完整依赖、数据库账号创建和验证
步骤见 [构建运行与验证](../../技术文档/构建运行与验证.md)。
