# Unreal Agent 0.3 持久执行账本

UnrealAgent 在 Editor 侧把每次工具策略决策与 Task 状态转换追加到：

```text
<Project>/Saved/UnrealAgent/Audit/execution-ledger.jsonl
```

该文件是操作证据，不是模型记忆，也不是 Task 恢复数据库。MCP `tools/call` 的
`result._meta` 返回服务器生成的 `traceId` 与 `requestId`；同一次调用的脱敏审计和全部
Task 状态使用同一 `traceId`。异步 Task 回执、`get_task`、`list_tasks` 与
`list_tool_audit` 也会返回这些关联字段。

## 完整性与访问控制

- 一行一个紧凑 JSON 事件，`schemaVersion` 当前为 `1`；
- `sequence` 单调递增，`previousHash` 指向上一事件；
- `eventHash` 是删除自身字段后的规范化 JSON 的 BLAKE3；对象键排序，数组保持顺序；
- 文件创建时应用并验证当前用户专属 ACL，后续只做追加写；
- 启动时逐行校验 JSON、序号、前向链接与事件哈希；任一异常使账本进入不健康状态；
- 账本不可写、不健康或 ACL 不合规时，统一执行服务返回
  `audit_persistence_unavailable`，不会进入领域 Handler。

账本不记录原始参数、访问 Token、Python 正文、完整工具结果或原始错误正文。Task 事件
只保留身份摘要、Tool/Owner、状态、取消/副作用标记、时间，以及 Worker Prepare、
GameThread Apply p95/p99、超预算、取消检查点和补偿计数/耗时；工具结果仍由当前进程内
Task Store 与领域产物负责。字段与门禁见 `GameThreadExecution.md`。

## 事件类型

- `audit`：Schema/Policy 决策、风险、调用来源、是否接受；
- `task_state`：Received、Validating、Authorizing、Queued、Running、Waiting 与终态。

`list_tool_audit` 读取当前进程的有界内存投影，同时返回 `durable`、`ledgerHealthy`、
`ledgerLocation` 和可选 `ledgerError`。JSONL 是跨重启的持久证据；当前版本不会把历史
事件重新灌回 `list_tasks` 或自动恢复中断任务。

## 运维与残余边界

- 回滚或重装前保留调查所需账本；不要把它提交或打入源码发布包；
- 当前 0.3 不自动轮换/压缩 JSONL，也不提供 SSE 事件重放；运维应监控文件大小；
- 不要手工删除或修改损坏账本来“恢复服务”。先备份证据，再由授权运维归档旧文件，
  重新启动生成新的 genesis 链；
- 多机、集中签名、外部时间戳与可配置保留期属于后续版本。

自动化 `WorldData.UnrealAgent.Infrastructure.ExecutionLedger` 验证 owner-only ACL、重启
复验、结果/错误脱敏和篡改检测。
