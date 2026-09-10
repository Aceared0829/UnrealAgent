# 独立源码仓库验证

本仓库由采集项目中的 UnrealAgent 插件拆出。旧项目中的历史测试数字不自动构成当前 Git 修订的验收结论，也不随源码包上传本机报告。

## 可重复检查

1. 在仓库根目录执行 `python Scripts/check_source_release.py`，检查候选文件、配置 JSON、锁文件与安装器摘要一致性。正则敏感信息检查不是完整安全审计。
2. 在宿主项目中构建 Editor / Development / Win64 目标；确认无需原采集项目的任何业务模块。
3. 或使用引擎的 `RunUAT.bat BuildPlugin -Plugin=<absolute-plugin-path> -Package=<separate-output-directory> -TargetPlatforms=Win64`。不要把生成包当作 Git 源码目录上传。
4. 在临时 Saved 路径运行 `Scripts/Install-AgentCli.ps1 -Provider Codex -ProjectSavedDirectory <temporary-saved-path> -NpmPath <npm.cmd>`，确认安装器完成版本检查。
5. 运行 `python Scripts/check_acp_handshake.py --entry <installed-package>/dist/index.js`。此检查使用隔离 Codex 配置，无账号登录和模型请求。
6. 在启用插件的 Editor 中运行 `WorldData.UnrealAgent` 自动化测试，并记录报告。涉及资产与副作用的测试只应在一次性测试项目运行。
7. 登录后的真实 prompt、MCP 工具调用、取消、账户状态与重连还需单独验收。

## 本次发布检查边界

2026-09-10：从筛选后的独立源码目录通过 BuildPlugin 的 Windows Editor Development 编译（271 个构建动作），不依赖原采集项目。该检查编译了插件及测试代码，但没有运行完整 UE 自动化测试，也不证明每个工具的场景行为。StructUtils 弃用警告仍存在。

Codex ACP 从 1.1.9 更新到 1.11.0，锁定 Codex 0.153.4 与 ACP SDK 1.4.0。已执行真实 npm 安装、版本检查及 ACP v1 initialize；它不等同于登录后的 Editor 端到端对话验证。

可选 Windows stdio Host 已通过独立 MSBuild 重建及 9 条真实 stdio 响应检查，涵盖读取项目与拒绝越界路径；启动、关闭 Editor 和构建项目的副作用未在该冒烟测试中执行。

源码发布保留测试代码、契约和基线，不上传旧机器报告、编译产物、node_modules 或生成连接配置。本次不承诺 770 个 Action 已在全场景中验证，不承诺其他 UE 版本或操作系统兼容。

平台与集成风险见 [已知限制](LimitationsAndRollback.md)，依赖锁定见 [供应链](SupplyChain.md)。
