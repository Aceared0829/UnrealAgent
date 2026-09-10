// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPEpicPort.h
 * @brief Epic 元工具访问 Unreal Agent 自有反射注册表的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPEpicPort
	{
	public:
		virtual ~IUnrealAgentMCPEpicPort() = default;

		virtual FString Status(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListToolsets(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString DescribeToolset(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CallTool(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
