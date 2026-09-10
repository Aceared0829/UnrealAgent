# Unreal Agent 0.3 Provider SDK 接入指南

Unreal Agent 0.3.0 提供主版本为 `1` 的 Provider API。内部插件与模块标识继续使用 `UnrealAgent`。可选 Editor 模块可通过以下两个稳定头文件批量注册一组完整工具：

- `Source/UnrealAgentMCPCore/Public/Core/Tooling/UnrealAgentMCPToolDescriptor.h`
- `Source/UnrealAgentProviderSDK/Public/Provider/UnrealAgentMCPToolExtensions.h`

Provider 模块只需依赖 `UnrealAgentMCPCore` 与 `UnrealAgentProviderSDK`，不再链接巨型
`UnrealAgentMCPEditor`。旧的 `Application/UnrealAgentMCPToolExtensions.h` 在 0.3.x 保留为
兼容转发头；新代码必须改用 `Provider/...` 路径。

`IMcpToolProvider::GetProviderApiVersion()` 必须返回 `UnrealAgentMCP::McpToolProviderApiVersion`。版本不匹配、Descriptor 无效、名称重复、Handler 缺失或所有权冲突时，整批注册都会失败，且目录代次与哈希保持不变。

## 生命周期

```cpp
#include "Provider/UnrealAgentMCPToolExtensions.h"

void FExampleModule::StartupModule()
{
    Provider = MakeShared<FExampleToolProvider>();
    TArray<FString> Errors;
    if (!UnrealAgentMCP::Extensions::RegisterToolProvider(
            Provider.ToSharedRef(), Errors))
    {
        Provider.Reset();
    }
}

void FExampleModule::ShutdownModule()
{
    UnrealAgentMCP::Extensions::UnregisterToolProvider(TEXT("ExampleProvider"));
    Provider.Reset();
}
```

注册和注销必须在 Game Thread 执行。SDK 会保存已知 Provider：如果领域模块在 Editor
Host 之前加载，注册会先安全排队，Host 绑定后再原子加入目录；Editor Host 热重载后也会
重新绑定已知 Provider。Provider 模块卸载前，必须先停止自身的 Application Service 和
后台工作，再注销 Provider。绑定到旧注册代次的排队调用会返回 `provider_unavailable`，
绝不会跳转到替换后的 Handler。UnrealAgent 主模块退出时会停止接收新任务、移除阶段式
Ticker、请求取消，并等待已经进入 Handler 的调度退出插件代码；Provider 必须让自身任务
在有限时间内响应取消。

共享领域会话必须使用 Owner 感知的操作句柄。启动返回的句柄应至少包含稳定的 OperationId 和 Owner；状态、Tick 与取消只能作用于精确匹配的句柄。Provider 不得“收养”其他模块发起的进程级任务，也不得在模块卸载时无条件取消其他 Owner 的任务。

原采集项目曾通过独立的 WorldDataCaptureMCP 插件调用自己的领域应用门面；该业务插件不包含在本仓库中。扩展者可参考上面的生命周期示例，不应持有 UnrealAgent Registry 的内部实现。

反射工具必须显式声明 `UnrealAgentMCPRisk`、`UnrealAgentMCPTransaction` 和
`UnrealAgentMCPIdempotent`。缺失或非法的安全元数据会让该工具失败关闭，不再按只读、
幂等默认值注册。反射只适合短时 Game Thread 调用；领域任务应通过 typed Application
Service 接入普通 Provider。

## 兼容性策略

机器可读的接口清单位于 `Tests/Release/public-api-0.3.json`。

- `stable` 符号在 0.3.x 版本线内保持源码兼容。移除符号或进行不兼容的签名修改，必须发布新的次版本并提供迁移说明。
- `preview` 符号为插件内部模块边界而导出，虽然会被门禁跟踪，但不构成对外部扩展的兼容承诺。
- Provider API 主版本变化时采用失败关闭策略。Provider 必须重新构建并显式适配；UnrealAgent 不会静默兼容不匹配的 Provider。
- 稳定符号必须先在文档和代码中标记为弃用，至少保留一个次版本，之后才能配合带版本的公共 API 清单更新予以移除。

## Provider 发布检查

Provider 变更必须通过：原子失败场景，注册、列出、查找、调用与注销，同名重载，旧代次拒绝，1,000 次生命周期循环，并发快照读取，扩展禁用和启用时的目录哈希，模块卸载排空与 Owner 隔离，Editor Automation 测试套件，以及真实 HTTP 目录契约测试。
