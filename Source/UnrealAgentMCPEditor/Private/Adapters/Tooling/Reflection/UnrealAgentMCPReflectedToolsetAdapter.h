#pragma once

/**
 * @file UnrealAgentMCPReflectedToolsetAdapter.h
 * @brief 仅由 Unreal Agent 自有反射基础设施支撑的兼容网关。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP::Toolsets
{
	FString GetStatus(const TSharedPtr<FJsonObject>& Arguments);
	FString ListToolsets(const TSharedPtr<FJsonObject>& Arguments);
	FString DescribeToolset(const TSharedPtr<FJsonObject>& Arguments);
	FString CallTool(const TSharedPtr<FJsonObject>& Arguments);
	FString GetCallResult(const TSharedPtr<FJsonObject>& Arguments);
}
