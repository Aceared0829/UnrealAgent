// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPAssetPort.h
 * @brief 资产领域访问 Unreal 编辑器能力时使用的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPAssetPort
	{
	public:
		virtual ~IUnrealAgentMCPAssetPort() = default;

		/**
		 * 执行已经由应用服务核验过的资产动作。
		 *
		 * Port 只表达业务意图，不向应用层暴露 AssetRegistry、
		 * EditorSubsystem 等 Unreal 实现类型。
		 */
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
