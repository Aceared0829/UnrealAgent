// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPBlueprintService.h
 * @brief Blueprint 工具域的应用服务与能力白名单。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPBlueprintPort;

	class FUnrealAgentMCPBlueprintService final
	{
	public:
		explicit FUnrealAgentMCPBlueprintService(TSharedRef<IUnrealAgentMCPBlueprintPort> InBlueprintPort);
		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPBlueprintPort> BlueprintPort;
	};
}
