// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPAssetService.h
 * @brief 资产 Toolset 的应用服务、动作白名单与 Port 调度。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPAssetPort;

	class FUnrealAgentMCPAssetService
	{
	public:
		explicit FUnrealAgentMCPAssetService(TSharedRef<IUnrealAgentMCPAssetPort> InAssetPort);

		FString Execute(const TSharedPtr<FJsonObject>& Args) const;
		static TArray<FString> GetImplementedActions();

	private:
		TSharedRef<IUnrealAgentMCPAssetPort> AssetPort;
	};
}
