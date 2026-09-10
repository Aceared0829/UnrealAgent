// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPWhiteboxService.cpp
 * @brief 通过可恢复 Unreal 适配端口路由白膜 action。
 */

#include "Application/Domains/Whitebox/UnrealAgentMCPWhiteboxService.h"

#include "Application/Ports/UnrealAgentMCPWhiteboxPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPWhiteboxService::FUnrealAgentMCPWhiteboxService(TSharedRef<IUnrealAgentMCPWhiteboxPort> InWhiteboxPort) : WhiteboxPort(MoveTemp(InWhiteboxPort))
	{
	}

	TArray<FString> FUnrealAgentMCPWhiteboxService::GetImplementedActions()
	{
		return { TEXT("build_city_wall"), TEXT("patch_city_wall"), TEXT("clear_folder"), TEXT("align_to_landscape") };
	}

	FString FUnrealAgentMCPWhiteboxService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("whitebox"));
		Result->SetStringField(TEXT("action"), Action);
		if (GetImplementedActions().Contains(Action))
		{
			Result->SetStringField(TEXT("error"), TEXT("Whitebox mutations must run through the resumable task executor."));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Actions;
			for (const FString& Name : GetImplementedActions())
			{
				Actions.Add(MakeShared<FJsonValueString>(Name));
			}
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("Missing required action.") : FString::Printf(TEXT("Unknown Whitebox action '%s'."), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Actions);
		}
		return JsonObjectToString(Result);
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPWhiteboxService::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) const
	{
		return WhiteboxPort->CreateTaskStepper(Args);
	}
}
