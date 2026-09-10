// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPNetworkingService.cpp
 * @brief Networking 应用服务实现；只依赖 Networking Port 与 JSON 契约。
 */

#include "Application/Domains/Networking/UnrealAgentMCPNetworkingService.h"

#include "Application/Ports/UnrealAgentMCPNetworkingPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPNetworkingService::FUnrealAgentMCPNetworkingService(TSharedRef<IUnrealAgentMCPNetworkingPort> InNetworkingPort) : NetworkingPort(MoveTemp(InNetworkingPort))
	{
	}

	TArray<FString> FUnrealAgentMCPNetworkingService::GetImplementedActions()
	{
		return { TEXT("set_replicates"), TEXT("set_property_replicated"), TEXT("configure_net_frequency"), TEXT("set_dormancy"), TEXT("set_net_load_on_client"),
			TEXT("set_always_relevant"), TEXT("set_only_relevant_to_owner"), TEXT("configure_cull_distance"), TEXT("set_priority"), TEXT("set_replicate_movement"),
			TEXT("get_info") };
	}

	FString FUnrealAgentMCPNetworkingService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
			Args->TryGetStringField(TEXT("action"), Action);
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();

		if (Action == TEXT("set_replicates"))
			return NetworkingPort->SetReplicates(SafeArgs);
		if (Action == TEXT("set_property_replicated"))
			return NetworkingPort->SetPropertyReplicated(SafeArgs);
		if (Action == TEXT("configure_net_frequency"))
			return NetworkingPort->ConfigureNetFrequency(SafeArgs);
		if (Action == TEXT("set_dormancy"))
			return NetworkingPort->SetDormancy(SafeArgs);
		if (Action == TEXT("set_net_load_on_client"))
			return NetworkingPort->SetNetLoadOnClient(SafeArgs);
		if (Action == TEXT("set_always_relevant"))
			return NetworkingPort->SetAlwaysRelevant(SafeArgs);
		if (Action == TEXT("set_only_relevant_to_owner"))
			return NetworkingPort->SetOnlyRelevantToOwner(SafeArgs);
		if (Action == TEXT("configure_cull_distance"))
			return NetworkingPort->ConfigureCullDistance(SafeArgs);
		if (Action == TEXT("set_priority"))
			return NetworkingPort->SetPriority(SafeArgs);
		if (Action == TEXT("set_replicate_movement"))
			return NetworkingPort->SetReplicateMovement(SafeArgs);
		if (Action == TEXT("get_info"))
			return NetworkingPort->GetInfo(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
			Actions.Add(MakeShared<FJsonValueString>(Name));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("networking"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Networking action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
