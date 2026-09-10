// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealNetworkingAdapter.h
 * @brief 通过 Blueprint 与 Actor API 实现网络复制配置。
 */

#include "Application/Ports/UnrealAgentMCPNetworkingPort.h"

class AActor;
class UBlueprint;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealNetworkingAdapter final : public IUnrealAgentMCPNetworkingPort
	{
	public:
		virtual FString SetReplicates(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetPropertyReplicated(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ConfigureNetFrequency(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetDormancy(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetNetLoadOnClient(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetAlwaysRelevant(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetOnlyRelevantToOwner(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ConfigureCullDistance(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetPriority(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetReplicateMovement(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) override;

	private:
		static UBlueprint* ResolveBlueprint(const TSharedPtr<FJsonObject>& Args, FString& OutError);
		static AActor* ResolveActorDefaults(UBlueprint* Blueprint, FString& OutError);
		static bool SaveBlueprint(UBlueprint* Blueprint, FString& OutError);
		static FString ApplyActorDefaults(const TSharedPtr<FJsonObject>& Args, TFunctionRef<bool(AActor*, FString&)> Mutator);
		static TSharedRef<FJsonObject> MakeInfoJson(UBlueprint* Blueprint, AActor* Defaults);
	};
}
