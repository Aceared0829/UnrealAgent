// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPFabService.cpp
 * @brief Fab 应用服务实现，只依赖 Fab Port 和 JSON 契约。
 */

#include "Application/Domains/Fab/UnrealAgentMCPFabService.h"

#include "Application/Ports/UnrealAgentMCPFabPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPFabService::FUnrealAgentMCPFabService(TSharedRef<IUnrealAgentMCPFabPort> InFabPort) : FabPort(MoveTemp(InFabPort))
	{
	}

	TArray<FString> FUnrealAgentMCPFabService::GetImplementedActions()
	{
		return { TEXT("status"), TEXT("login"), TEXT("logout"), TEXT("sync_library"), TEXT("list_cached"), TEXT("cache_info"), TEXT("clear_cache"), TEXT("import_file") };
	}

	FString FUnrealAgentMCPFabService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		if (Action == TEXT("status"))
			return FabPort->Status(SafeArgs);
		if (Action == TEXT("login"))
			return FabPort->Login(SafeArgs);
		if (Action == TEXT("logout"))
			return FabPort->Logout(SafeArgs);
		if (Action == TEXT("sync_library"))
			return FabPort->SyncLibrary(SafeArgs);
		if (Action == TEXT("list_cached"))
			return FabPort->ListCached(SafeArgs);
		if (Action == TEXT("cache_info"))
			return FabPort->CacheInfo(SafeArgs);
		if (Action == TEXT("clear_cache"))
			return FabPort->ClearCache(SafeArgs);
		if (Action == TEXT("import_file"))
			return FabPort->ImportFile(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
		{
			Actions.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("fab"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Fab action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
