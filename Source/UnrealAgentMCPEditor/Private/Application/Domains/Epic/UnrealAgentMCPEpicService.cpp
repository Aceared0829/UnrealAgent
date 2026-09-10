// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPEpicService.cpp
 * @brief Epic 应用服务实现，只依赖自有反射注册表端口和 JSON 契约。
 */

#include "Application/Domains/Epic/UnrealAgentMCPEpicService.h"

#include "Application/Ports/UnrealAgentMCPEpicPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPEpicService::FUnrealAgentMCPEpicService(TSharedRef<IUnrealAgentMCPEpicPort> InEpicPort) : EpicPort(MoveTemp(InEpicPort))
	{
	}

	TArray<FString> FUnrealAgentMCPEpicService::GetImplementedActions()
	{
		return { TEXT("status"), TEXT("list_toolsets"), TEXT("describe_toolset"), TEXT("call_tool") };
	}

	FString FUnrealAgentMCPEpicService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		if (Action == TEXT("status"))
		{
			return EpicPort->Status(SafeArgs);
		}
		if (Action == TEXT("list_toolsets"))
		{
			return EpicPort->ListToolsets(SafeArgs);
		}
		if (Action == TEXT("describe_toolset"))
		{
			return EpicPort->DescribeToolset(SafeArgs);
		}
		if (Action == TEXT("call_tool"))
		{
			return EpicPort->CallTool(SafeArgs);
		}

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
		{
			Actions.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("epic"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Epic action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
