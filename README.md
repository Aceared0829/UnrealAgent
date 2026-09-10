# UnrealAgent

**把 Codex / Cursor 的智能体工作流接入 Unreal Editor。**

UnrealAgent 是源码形式发布的 UE 编辑器插件：内嵌 Slate 会话面板，通过 ACP 连接外部 Agent，再通过带鉴权的本机 MCP 工具操作编辑器。它不是模型、不是自建 Agent 推理内核，也不是游戏运行时插件。

[MIT 许可证](LICENSE.md) · [架构](Docs/AgentArchitecture.md) · [安装供应链](Docs/SupplyChain.md) · [验证与限制](Docs/FinalAcceptance.md) · [第三方来源](THIRD_PARTY_NOTICES.md)

> **Beta / 0.3.0。** 当前开发与验证环境为 Windows x64、UE 5.8 系列。其他 UE 版本、Linux/macOS Editor 尚未验证。源码中的工具目录不等于每个 Action 都经过完整场景验收。

## 能做什么

- **编辑器内对话**：Codex / Cursor ACP 会话，流式 Markdown、工具执行反馈、权限确认、会话保存与上下文摘要。
- **结构化编辑器工具**：Actor、资产、Blueprint、材质、动画、Niagara、关卡、PCG 等领域；用 `search_tools` 渐进查询工具，避免一次注入全部 Schema。
- **执行边界**：本机回环 MCP、随机令牌、参数校验、风险审批、协作式取消、事务与执行审计。它们不是任意代码的安全沙箱。
- **Provider SDK**：其他插件可注册自有 MCP 工具，无需链接编辑器面板实现。
- **可选 stdio Host**：供只能使用 stdio 的客户端桥接 Editor；普通内嵌 ACP 使用方式不必构建 Host。

```text
Unreal Editor / Slate 面板
         │ ACP
         ▼
Codex ACP / Cursor Agent
         │ 已鉴权的本机 MCP
         ▼
UnrealAgent 工具注册与执行 → Unreal Editor API
```

## 快速安装

### 1. 准备环境

- Windows x64、Unreal Engine 5.8 系列，以及与引擎匹配的 Visual Studio C++ 工具链和 Windows SDK。
- 推荐从一个 C++ UE 项目开始，并先备份项目或提交版本控制。
- 使用 Codex 时需要自己的可用账户；插件不附带账户、额度或凭据。
- 使用现有 Node.js/npm 可以避免便携 Node 下载。固定依赖见 [供应链说明](Docs/SupplyChain.md)。

### 2. 放入项目

在自己的 UE 项目根目录执行：

```powershell
git clone https://github.com/Aceared0829/UnrealAgent.git Plugins/UnrealAgent
```

在插件管理中启用 **Unreal Agent**，生成项目文件并构建项目的 **Editor / Development / Win64** 目标。插件描述符会声明所需引擎插件；不要复制本机 Binaries 或 Intermediate 到其他引擎版本。

没有安装 Unreal Engine 时，本仓库不能单独启动成一个应用。它不需要原 WorldDataEngine 采集项目、采集场景或其业务插件。

### 3. 连接 Codex

打开 **窗口 → Unreal Agent**，选择 Codex。已有适配器可以重新检测；缺失时使用面板的一键安装功能。

也可以在 UE 项目根目录使用现有 npm 安装固定版本：

```powershell
& .\Plugins\UnrealAgent\Scripts\Install-CodexACP.ps1 -ProjectRoot (Get-Location).Path
```

当前锁定 **Codex ACP 1.11.0 / Codex 0.153.4 / ACP SDK 1.4.0**。手动入口和面板使用同一份完整 lockfile、摘要校验与 `npm ci --ignore-scripts`，不在安装时选择浮动最新版。

账户登录由外部 Codex 完成。升级已有安装需要重新运行安装器，并重启适配器连接；仅拉取源码不会自动替换 Saved 中已安装的版本。

### 4. 可选 Cursor

Cursor 使用现有 WSL Agent 或受校验的离线安装器。不会自动执行未固定版本的远程安装脚本。当前 WSL 安装路径仍涉及 root 用户边界，使用前阅读 [供应链说明](Docs/SupplyChain.md)，不要将其当作最小权限部署。

## 配置与安全

运行状态仅位于宿主项目的 `Saved/UnrealAgent/`：配置、随机令牌、连接清单、会话与审计。仓库只提交 `Config/UnrealAgent.example.json` 等模板。

- 不上传 Saved、登录文件、令牌、会话、运行日志或完整项目素材。
- MCP 仅服务本机回环，不要直接映射到公网。
- 文件变更、删除、代码执行及外部进程具有真实副作用；只对可信项目和请求授予权限。
- ACP 本身也可能有终端/文件能力；插件 MCP 的审批不能替代外部 Agent 的权限策略。
- 取消和 Editor Undo 不保证回滚外部进程或任意文件修改。
- 插件没有自动恢复中断任务的保证，结果不确定时应先检查项目状态。

更多边界见 [已知限制](Docs/LimitationsAndRollback.md)。

## 源码导航

| 路径 | 职责 |
| --- | --- |
| `Source/UnrealAgentMCPCore` | 协议、工具注册、Schema、策略和执行任务 |
| `Source/UnrealAgentProviderSDK` | 外部工具 Provider 注册边界 |
| `Source/UnrealAgentMCPEditor` | Editor 适配、ACP、HTTP、Slate 与组合装配 |
| `Programs/WorldDataMCPHost` | 可选 Windows stdio Host 源码 |
| `Config/ActionContracts.json` | 领域 Action 契约 |
| `Scripts` | 固定依赖安装与独立验证 |
| `Tests` | 契约基线与测试元数据；UE 自动化源码位于各模块内 |

[Provider SDK](Docs/ProviderSDK.md) · [执行账本](Docs/ExecutionLedger.md) · [Game Thread 执行](Docs/GameThreadExecution.md) · [错误码](Docs/ErrorCodes.md)

## 开发与验证

可选 stdio Host 可在 Visual Studio Developer PowerShell 中构建（默认 v145 工具集；按已安装环境覆盖 PlatformToolset）：

```powershell
msbuild Programs/WorldDataMCPHost/WorldDataMCPHost.vcxproj /m /p:Configuration=Development /p:Platform=x64
```

输出位于 `Binaries/Win64/WorldDataMCPHost.exe`，不会提交到源码仓库。

```powershell
python Scripts/check_source_release.py
python Scripts/check_acp_handshake.py --entry <installed-codex-acp>/dist/index.js
python Scripts/check_host_smoke.py --host Binaries/Win64/WorldDataMCPHost.exe
```

第二条只验证真实 ACP initialize 握手，不登录账户、不提交模型 prompt。UE 编译、自动化和真实登录会话是不同验证层级，详见 [验证说明](Docs/FinalAcceptance.md)。

## 来源与授权

自有实现采用 MIT。Action 契约中保留了 `db-lyon/ue-mcp` 的 **24 个领域 / 758 个 Action** 兼容基线，加上原项目扩展构成当前 **25 / 770** 目录；这不是 770 项全部原创或全部已实测的声明。

外部 ACP 适配器、Codex、Cursor、UE 和运行时依赖分别受其自身许可约束。本仓库不分发引擎源码、模型权重、账户或商业场景资产，也不代表 OpenAI、Epic 或 Cursor 官方产品。

问题复现请附 UE 版本、操作步骤和**脱敏**日志；不要在 Issue 中发送令牌、登录文件或私有项目内容。
