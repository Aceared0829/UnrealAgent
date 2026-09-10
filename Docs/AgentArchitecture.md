# UnrealAgent 架构

UnrealAgent 是 Editor MCP 控制面加内嵌 Codex / Cursor ACP。Agent 的规划与推理由外部 Provider 执行；本插件没有自建 Native Brain。

## 所有权与调用方向

```text
Slate Presentation → Application → Composition
                                      ├─ ACP / HTTP / Saved 基础设施
                                      └─ 工具目录 → 注册表 → 校验与执行 → UE 领域适配器

外部插件 → UnrealAgentProviderSDK + UnrealAgentMCPCore
```

- MCPCore 不依赖 Slate 或具体 UE 编辑器工具。
- ProviderSDK 负责工具注册和 Host 绑定生命周期。
- MCPEditor 在 PostEngineInit 中组合服务；其 HTTP 和 ACP 控制台只存在于 Editor。
- 工具执行必须经过参数、策略与审计边界；需要时使用事务与分帧执行。某些不可取消的底层调用仍可能产生副作用。
- 外部 Provider 不应链接 MCPEditor 私有实现；见 [Provider SDK](ProviderSDK.md)。
- 运行配置、端口、令牌和会话归属于宿主项目 Saved，不属于源码包。

ACP 协议版本由 initialize 协商，当前客户端请求版本 1。Codex ACP npm 包版本与协议版本不是同一概念；MCP 的日期版本也与二者独立。模型及能力以 Agent 返回的运行时元数据为准。

## 定位入口

- 模块启动：`Source/UnrealAgentMCPEditor/Private/Composition/UnrealAgentMCPEditorModule.cpp`
- ACP：`Source/UnrealAgentMCPEditor/Private/Infrastructure/ACP/`
- UI：`Source/UnrealAgentMCPEditor/Private/Presentation/Panel/`
- Action 契约：`Config/ActionContracts.json`
- 执行与任务：`Source/UnrealAgentMCPCore/`

旧 Native / 自定义模型配置只为历史迁移读取，不构成受支持的产品入口。
