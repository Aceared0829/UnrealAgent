// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealEpicAdapter.h
 * @brief 将 Epic 元工具契约适配到 Unreal Agent 自有反射注册表。
 */

#include "Application/Ports/UnrealAgentMCPEpicPort.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealEpicAdapter final : public IUnrealAgentMCPEpicPort
	{
	public:
		virtual FString Status(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListToolsets(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString DescribeToolset(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CallTool(const TSharedPtr<FJsonObject>& Args) override;
	};
}
