# Unreal Agent 安装供应链

## 固定输入

UnrealAgent 的 UE C++ Editor 插件和 `WorldDataMCPHost.exe` 不依赖 Node.js。Node 只用于用户主动安装的 Codex ACP 适配器。

当前安装基线固定为：

- Node.js `v24.18.0` Windows x64 ZIP；SHA-256 `0ae68406b42d7725661da979b1403ec9926da205c6770827f33aac9d8f26e821`；
- `@agentclientprotocol/codex-acp` `1.11.0`；
- `@openai/codex` `0.153.4`、ACP SDK `1.4.0`；
- npm lockfile v3，位于 `Scripts/CodexACP.package-lock.json`。

Node 压缩包在解压前校验 SHA-256。Codex ACP 安装前校验随包 lockfile 自身的固定 SHA-256，然后执行 `npm ci --omit=dev --ignore-scripts --no-audit --no-fund`。所有下载包还必须通过 lockfile 中的 npm integrity 校验。安装脚本不会动态解析“最新 LTS”，也不会用 semver 范围生成新的依赖树。

## Cursor 边界

若 WSL 中已有 Cursor Agent，UnrealAgent 只检测版本并生成项目本地 launcher。若未安装，自动执行浮动的 `https://cursor.com/install` 已被禁用。管理员必须在仓库外取得、审查并固定安装器，然后同时传入本地路径和 SHA-256：

```powershell
Plugins\UnrealAgent\Scripts\Install-AgentCli.ps1 `
  -Provider Cursor `
  -ProjectSavedDirectory <Project>\Saved `
  -CursorInstallerPath <verified-installer.sh> `
  -CursorInstallerSha256 <64-hex-digest>
```

校验失败、路径缺失或摘要格式非法时安装失败关闭。当前 Cursor 仍运行在 WSL root 用户边界；在建立非 root 账户迁移与配置兼容方案前，不将其描述为最小权限安装。

## 升级流程

1. 在隔离目录更新目标依赖，生成新的 lockfile。
2. 审核一级与传递依赖的版本、许可证、安装脚本和平台二进制。
3. 更新固定版本、Node 官方发布 SHA-256、lockfile 与脚本内 lockfile SHA-256。
4. 运行发布包校验、安装生命周期、Codex ACP `--version` 和 Editor 连接测试。
5. 在隔离宿主中验证新的 Git 修订，按 `Docs/FinalAcceptance.md` 记录已执行与未执行项。

不得只修改 `CodexAcpVersion` 参数而复用旧 lockfile；安装器会拒绝没有随附锁文件的版本。
