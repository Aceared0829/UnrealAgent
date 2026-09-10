#pragma once

/**
 * @file UnrealAgentMCPFoliagePort.h
 * @brief Foliage 应用服务访问植被类型、实例和资产写入能力的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPFoliagePort
	{
	public:
		virtual ~IUnrealAgentMCPFoliagePort() = default;

		virtual FString ListTypes(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetSettings(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Sample(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateType(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetSettings(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
