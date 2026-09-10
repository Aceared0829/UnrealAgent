#pragma once

/**
 * @file UnrealAgentMCPNetworkingPort.h
 * @brief Networking 应用服务访问 Blueprint 复制配置的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPNetworkingPort
	{
	public:
		virtual ~IUnrealAgentMCPNetworkingPort() = default;

		virtual FString SetReplicates(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetPropertyReplicated(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ConfigureNetFrequency(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetDormancy(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetNetLoadOnClient(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetAlwaysRelevant(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetOnlyRelevantToOwner(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ConfigureCullDistance(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetPriority(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetReplicateMovement(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
