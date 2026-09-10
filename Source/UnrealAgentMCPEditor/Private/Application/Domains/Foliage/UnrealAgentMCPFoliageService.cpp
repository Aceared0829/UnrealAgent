// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPFoliageService.cpp
 * @brief Foliage 应用服务实现；只依赖 Foliage Port 与 JSON 契约。
 */

#include "Application/Domains/Foliage/UnrealAgentMCPFoliageService.h"

#include "Application/Ports/UnrealAgentMCPFoliagePort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPFoliageService::FUnrealAgentMCPFoliageService(TSharedRef<IUnrealAgentMCPFoliagePort> InFoliagePort) : FoliagePort(MoveTemp(InFoliagePort))
	{
	}

	TArray<FString> FUnrealAgentMCPFoliageService::GetImplementedActions()
	{
		return { TEXT("list_types"), TEXT("get_settings"), TEXT("sample"), TEXT("create_type"), TEXT("set_settings") };
	}

	FString FUnrealAgentMCPFoliageService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
			Args->TryGetStringField(TEXT("action"), Action);
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();

		if (Action == TEXT("list_types"))
			return FoliagePort->ListTypes(SafeArgs);
		if (Action == TEXT("get_settings"))
			return FoliagePort->GetSettings(SafeArgs);
		if (Action == TEXT("sample"))
			return FoliagePort->Sample(SafeArgs);
		if (Action == TEXT("create_type"))
			return FoliagePort->CreateType(SafeArgs);
		if (Action == TEXT("set_settings"))
			return FoliagePort->SetSettings(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
			Actions.Add(MakeShared<FJsonValueString>(Name));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("foliage"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Foliage action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
