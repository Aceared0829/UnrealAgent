// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPBuiltInToolsets.h
 * @brief Unreal Agent 自有领域 Toolset 的统一组合入口。
 */

#include "CoreMinimal.h"

namespace UnrealAgentMCP
{
	class FMcpToolRuntimeRegistry;
}

namespace UnrealAgentMCP::BuiltInToolsets
{
	/**
	 * 创建各领域 Service、Port 与 Unreal Adapter，并将完整 Toolset 注册到
	 * 自有运行时注册表。此入口不引用官方 ToolsetRegistry 或第三方 Bridge。
	 */
	void Register(FMcpToolRuntimeRegistry& Registry, TArray<FString>& OutErrors);
}
