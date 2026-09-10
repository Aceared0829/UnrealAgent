// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEpicAdapter.cpp
 * @brief Epic 元工具适配实现，不读取引擎 ToolsetRegistry 文件。
 */

#include "Adapters/Unreal/Epic/UnrealAgentMCPUnrealEpicAdapter.h"

#include "Adapters/Tooling/Reflection/UnrealAgentMCPReflectedToolsetAdapter.h"

namespace UnrealAgentMCP
{
	FString FUnrealAgentMCPUnrealEpicAdapter::Status(const TSharedPtr<FJsonObject>& Args)
	{
		return Toolsets::GetStatus(Args);
	}

	FString FUnrealAgentMCPUnrealEpicAdapter::ListToolsets(const TSharedPtr<FJsonObject>& Args)
	{
		return Toolsets::ListToolsets(Args);
	}

	FString FUnrealAgentMCPUnrealEpicAdapter::DescribeToolset(const TSharedPtr<FJsonObject>& Args)
	{
		return Toolsets::DescribeToolset(Args);
	}

	FString FUnrealAgentMCPUnrealEpicAdapter::CallTool(const TSharedPtr<FJsonObject>& Args)
	{
		return Toolsets::CallTool(Args);
	}
}
