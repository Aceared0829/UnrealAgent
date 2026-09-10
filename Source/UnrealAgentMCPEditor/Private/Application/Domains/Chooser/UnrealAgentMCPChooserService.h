// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPChooserService.h
 * @brief Chooser 分类的应用服务与 action 路由。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPChooserPort;

	class FUnrealAgentMCPChooserService
	{
	public:
		explicit FUnrealAgentMCPChooserService(TSharedRef<IUnrealAgentMCPChooserPort> InChooserPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPChooserPort> ChooserPort;
	};
}
