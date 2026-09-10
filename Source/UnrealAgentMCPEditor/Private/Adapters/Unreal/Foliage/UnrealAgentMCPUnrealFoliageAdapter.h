// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealFoliageAdapter.h
 * @brief 通过 UE Foliage API 实现植被类型和实例操作。
 */

#include "Application/Ports/UnrealAgentMCPFoliagePort.h"

class AInstancedFoliageActor;
class UFoliageType;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealFoliageAdapter final : public IUnrealAgentMCPFoliagePort
	{
	public:
		virtual FString ListTypes(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetSettings(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Sample(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateType(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetSettings(const TSharedPtr<FJsonObject>& Args) override;

	private:
		static UFoliageType* ResolveFoliageType(const FString& Name, AInstancedFoliageActor** OutActor = nullptr);
		static TSharedRef<FJsonObject> MakeTypeJson(UFoliageType* Type, int32 InstanceCount);
		static TSharedRef<FJsonObject> MakeSettingsJson(UFoliageType* Type);
	};
}
