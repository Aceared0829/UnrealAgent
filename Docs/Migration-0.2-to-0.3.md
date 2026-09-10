# Unreal Agent 0.2 升级至 0.3 迁移指南

Unreal Agent 0.3.0 继续使用内部插件标识 `UnrealAgent`、Provider API 主版本 `1` 和现有访问令牌配置模型，无需迁移项目内容。升级时必须整体替换插件目录，不能把 0.3 源码树覆盖到 0.2 二进制文件之上。

## 客户端必须调整的行为

1. 将 `Mcp-Session-Id` 和协商得到的 `MCP-Protocol-Version` 视为每个会话独立的值。发送普通请求前，必须先发送 `notifications/initialized`。
2. 使用已认证的 `DELETE /mcp` 完成会话生命周期。会话被删除或因容量淘汰后，必须重新初始化。
3. 对 `tools/list` 和 `resources/list` 按照 `nextCursor` 继续翻页。游标是不透明、URL 安全且绑定目录修订版本的值；目录变化后旧游标立即失效。默认分页大小为 100，最大为 250。
4. 使用已认证的 `GET /mcp` 建立 SSE。相同会话重新连接时会关闭此前的事件流。
5. 发送 `notifications/cancelled` 时携带原始 `requestId`；仍可使用扩展字段 `taskId`。取消是协作式的，因此必须检查任务终态与 `cancellationDeferred` 证据。
6. Editor 网关发生暂时性故障时，丢弃旧会话并初始化新会话。工具已经成功返回后，不能仅因清理失败就重放调用。

支持的协议版本为 `2025-06-18`、`2025-03-26`，并为 `2024-11-05` 提供兼容模式。完整功能矩阵位于 `Tests/Protocol/protocol-matrix.json`。

## 升级步骤

1. 停止 Unreal Editor 和所有独立运行的 `WorldDataMCPHost.exe` 进程。
2. 备份 0.2 插件目录及有意设置的策略覆盖项。不得将生成的访问令牌保存在版本控制中。
3. 使用完整的 0.3.0 发布包替换 `<Project>/Plugins/UnrealAgent`。
4. 如果引擎没有自动重建，只删除旧的 UnrealAgent 构建产物，即 `Binaries` 和 `Intermediate`。
5. 构建 Editor Target、启动 Editor，并允许系统重新生成 `Saved/UnrealAgent/mcp.json`。
6. 仅通过明确的 UI 操作重新配置外部客户端，然后运行 Release 门禁。

现有 API-v1 Provider 需要针对 0.3 头文件重新编译；除非使用了 `preview` 符号，否则无需修改源码。此类符号的使用情况应与 `Tests/Release/public-api-0.3.json` 对照检查。
