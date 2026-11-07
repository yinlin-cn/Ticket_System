# 设计草稿

本目录用于保存系统演进过程，不作为最终代码或当前协议契约。

建议归档内容：

- 服务器并发框架早期结构图。
- Reactor、连接视图、分帧和发送队列设计。
- 框架调用层、连接名册和背压设计。
- 短任务与长事务分离方案。
- Redis 准入、订单队列和数据库批处理设计。
- 客户端任务插槽、页面导航和线程模型设计。

建议命名方式：

```text
01-framework-overview.drawio
02-framework-connection-flow.drawio
03-ticket-short-task-boundary.drawio
04-order-engine-queues.drawio
05-client-task-slots.drawio
```

同一设计的不同阶段可以使用日期区分：

```text
2026-10-01-order-engine-v1.drawio
2026-10-15-order-engine-batch-final.drawio
```

目录说明：

| 目录 | 内容 |
| --- | --- |
| `框架演进/` | 网络框架、调用层、连接、背压和发送机制 |
| `业务设计/` | 鉴权、缓存、Redis、订单队列、退票和结果投递 |
| `客户端设计/` | 页面、任务插槽、网络线程、状态和导航 |

本目录当前不包含伪造的历史材料，由项目作者放入真实设计图、截图和阶段文档。
