// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealReflectionAdapter.h
 * @brief 使用 Unreal 反射、资产、标签和 SaveGame API 实现 Reflection 端口。
 */

#include "Application/Ports/UnrealAgentMCPReflectionPort.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealReflectionAdapter final : public IUnrealAgentMCPReflectionPort
	{
	public:
		FString ReflectClass(const TSharedPtr<FJsonObject>& Args) override;
		FString ReflectStruct(const TSharedPtr<FJsonObject>& Args) override;
		FString ReflectEnum(const TSharedPtr<FJsonObject>& Args) override;
		FString ListClasses(const TSharedPtr<FJsonObject>& Args) override;
		FString ListTags(const TSharedPtr<FJsonObject>& Args) override;
		FString CreateTag(const TSharedPtr<FJsonObject>& Args) override;
		FString CreateEnum(const TSharedPtr<FJsonObject>& Args) override;
		FString SetEnumEntries(const TSharedPtr<FJsonObject>& Args) override;
		FString IsClassLoaded(const TSharedPtr<FJsonObject>& Args) override;
		FString IsModuleLoaded(const TSharedPtr<FJsonObject>& Args) override;
		FString ListLoadedModules(const TSharedPtr<FJsonObject>& Args) override;
		FString InspectSaveGame(const TSharedPtr<FJsonObject>& Args) override;
	};
}
