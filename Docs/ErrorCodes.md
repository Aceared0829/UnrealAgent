# Unreal Agent 0.3 错误码

`Tests/Release/error-codes-0.3.json` 是带版本的机器可读错误码目录。客户端必须根据错误码字段进行分支处理，不能依赖可能被本地化的消息文本。

## 错误层级

- HTTP 状态码用于报告传输层与会话信封错误。`401` 表示访问令牌缺失或无效；`404` 表示会话未知或已过期；`409` 表示仍需发送 `notifications/initialized`；`413` 表示请求超过 1 MiB 限制；`503` 表示 Editor 或网关暂时不可用。
- JSON-RPC 的 `error.code` 使用 `-32700`（解析失败）、`-32600`（无效请求）、`-32601`（未知方法）和 `-32602`（参数或游标无效）。每个错误同时返回 `error.data.errorCode`、`retryable` 与 `recovery`；客户端按稳定机器码分支，并把恢复提示展示给用户。
- JSON-RPC 信封本身成功时，工具结果仍可能失败。策略失败使用 `policyCode`；Schema 与 Provider 失败使用 `code`，并可能包含结构化的 `schemaErrors`。

## 稳定命名错误码

| 错误码 | 含义 | 重试规则 |
| --- | --- | --- |
| `authentication_required` | 需要已认证的策略上下文。 | 重新认证；不得自动重放请求。 |
| `local_connection_required` | 调用方不是本机回环连接。 | 将调用方迁移到本机。 |
| `tool_denied` | 工具被策略明确拒绝。 | 仅在人工调整服务端策略后重试。 |
| `tool_not_allowed` | 工具不在非空允许列表中。 | 仅在人工调整服务端策略后重试。 |
| `provider_not_allowed` | 所属 Provider 不在非空允许列表中。 | 仅在人工调整服务端策略后重试。 |
| `risk_exceeds_policy` | 风险级别超过配置上限。 | 调整策略；显式确认不能覆盖此限制。 |
| `file_path_outside_sandbox` | 文件路径逃逸了项目沙箱。 | 修正路径。 |
| `asset_path_outside_sandbox` | 包路径逃逸了允许的内容根目录。 | 使用允许的 `/Game` 路径。 |
| `confirmation_required` | 缺少完全匹配的显式确认。 | 仅携带所需确认后重试。 |
| `input_schema_invalid` | 审计记录表明参数被拒绝。 | 依据 `schemaErrors` 修正字段或类型。 |
| `invalid_arguments` | 结构化 Schema 校验失败。 | 依据 `schemaErrors` 修正字段或类型。 |
| `provider_unavailable` | Provider 已卸载或注册代次已经变化。 | 刷新目录，再判断重试是否安全。 |
| `audit_persistence_unavailable` | owner-only 持久执行账本不可写、ACL 不合规或哈希链损坏；领域调用未执行。 | 修复账本权限/完整性并重启 Editor；确认调用未进入领域层后才可重试。 |
| `response_budget_exceeded` | 工具已完成，但超过 512 KiB 的结果被省略。 | 不得自动重放变更调用；先查询 Task、产物或领域状态，再使用更窄的只读请求。 |
| `allowed` | 表示调用获准的审计决策，并非错误。 | 无。 |

只有在能够确定请求未成功时才允许自动重试。如果可变更状态的工具已经返回成功，而后续会话 `DELETE` 失败，客户端必须保留成功结果，不得重放该工具调用。
