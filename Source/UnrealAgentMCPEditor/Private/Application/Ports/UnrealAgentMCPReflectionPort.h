#pragma once

/**
 * @file UnrealAgentMCPReflectionPort.h
 * @brief Reflection 应用服务访问 Unreal 类型系统、标签和存档的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPReflectionPort
	{
	public:
		virtual ~IUnrealAgentMCPReflectionPort() = default;

		virtual FString ReflectClass(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReflectStruct(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReflectEnum(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListClasses(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListTags(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateTag(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateEnum(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetEnumEntries(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString IsClassLoaded(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString IsModuleLoaded(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListLoadedModules(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString InspectSaveGame(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
