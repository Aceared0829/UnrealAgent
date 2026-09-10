// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.h
 * @brief 基于 Unreal 公共 Kismet、SCS 与反射 API 的 Blueprint 适配器。
 */

#include "Application/Ports/UnrealAgentMCPBlueprintPort.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealBlueprintAdapter final : public IUnrealAgentMCPBlueprintPort
	{
	public:
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) override;

	private:
		FString ExecuteCore(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteGraph(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteComponent(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteAdvanced(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
