# Unreal Agent 0.3 已知限制与回滚

## 已知边界

- 生产服务端控制当前打开的 Unreal Editor 世界，不适用于已烘焙游戏，也不是远程多用户服务。
- HTTP 仅绑定回环地址，并要求使用生成的令牌。目前不支持远程绑定、TLS 终止代理或共享用户授权模型。
- Registry 最多保留 16 个会话。创建下一个会话时，会按确定性规则淘汰最早的会话。
- 请求体上限为 1 MiB。目录分页默认每页 100 项，最大不能超过 250 项。
- SSE 传递会话和生命周期事件；直接 POST 响应仍是权威结果。当前没有持久化事件重放缓冲区。
- 策略审计与 Task 状态会追加到 owner-only、BLAKE3 哈希链 `Saved/UnrealAgent/Audit/execution-ledger.jsonl`。它是跨重启证据，不会恢复或继续中断任务；当前不自动轮换、压缩或重新灌入 `list_tasks`。
- 取消采用协作式语义。无法观察取消信号的工作可能完成、失败或留下真实副作用；任务证据通过终态、`sideEffectState` 和 `cancellationDeferred` 如实报告结果。
- Editor 模块卸载会停止接收任务、移除阶段式 Ticker、请求取消并等待正在执行的调度退出插件代码。排空超时会触发明确错误而不是静默卸载；排空只保证代码生命周期安全，不承诺撤销已发生的外部副作用。
- Host 对工具 POST 采用 at-most-once 语义。发送后无法确认响应时返回 `request_outcome_unknown`，不会自动重放；成功结果后的 Session 清理失败仅通过 `_meta.worlddataSessionCleanupWarning` 报告。
- 有作用域的 Unreal Transaction 能改善 Editor 撤销体验，但不能保证外部进程、所有文件格式、所有引擎子系统或第三方 Provider 都可通过事务回滚。
- `execute_python_blocking` 在 Editor 进程内运行，无法隔离任意第三方原生代码缺陷。插件会拒绝已确认可导致 UE 5.8 原生崩溃的 `MaterialEditingLibrary.get_material_property_input_node*`，材质查询和连线应使用 `worlddata.material` 结构化 Action。废弃的 `execute_python` 只返回迁移提示，不执行代码。
- Provider API-v1 在 0.3.x 范围内保持稳定。`Tests/Release/public-api-0.3.json` 中标记为 `preview` 的其他导出头文件，不构成扩展兼容性保证。
- 源码发布包不包含 Blueprint、内容资产或生成的连接文件。项目专用内容不属于本插件发布包。
- 内嵌会话的 token 数量目前是确定性字符估算，不是 Provider 返回的精确 usage；Provider 支持 usage 回传后应覆盖估算值。默认容量为 512K，可在 256K–1M 之间按 64K 分档调整，自动压缩阈值为 90%。Provider 或模型自身的实际上下文上限仍是最终硬边界。
- 压缩会重放项目连续性快照、滚动摘要和最近对话，可显著降低长会话丢失架构与当前状态的风险，但不能从工程上绝对保证模型永远不产生遗忘或推理退化。关键决策和验收事实仍应落到仓库文档或可验证状态中。
- 会话 UI 为保持 Slate 响应性，只渲染最近 400 条消息；更早消息仍保存在会话文档并参与摘要，但当前没有“逐页加载更早消息”的交互。
- `capture_scene_png` 已废弃且失败关闭；它不再提供任意 SceneCapture2D 分辨率/相机位姿。需要该能力时必须新增基于异步 GPU readback 的独立 Task Adapter，不能恢复同步 `ExportRenderTarget`。
- Niagara `validate` 是非阻塞请求：`validationPending=true` 表示调用方需要稍后用 `get_info` 或再次 `validate` 查询，不能把首次回执当成编译完成。

## 回滚到 0.2

1. 停止 Unreal Editor 以及所有 `WorldDataMCPHost.exe` 进程。
2. 保留调查所需的证据或审计文件（包括 `execution-ledger.jsonl`），但不要保留或发布生成的令牌。
3. 删除完整的 0.3 插件目录，再安装此前归档的完整 0.2 目录。禁止混用两个版本的二进制文件或源文件。
4. 仅删除 UnrealAgent 的 `Binaries`、`Intermediate` 和生成的 `Saved/UnrealAgent/mcp.json` 连接状态，然后重新构建 Editor Target。
5. 启动 Editor，并使用新生成的连接清单配置客户端。

回滚前如果保留 `conversations.json` 或 `conversation-audit.jsonl` 用于调查，必须维持当前用户专属 ACL；不得恢复包含原始 Token 的旧备份。

回滚后，所有 0.3 会话、游标、请求到任务的绑定关系以及生成的令牌均失效。客户端必须初始化新会话并刷新目录。使用 0.3 预览 API 编译的 Provider，必须替换为兼容 0.2 的构建版本。
