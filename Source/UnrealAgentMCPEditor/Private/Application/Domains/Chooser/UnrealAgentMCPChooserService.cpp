// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPChooserService.cpp
 * @brief Chooser 应用服务实现，只依赖 Chooser Port 和 JSON 契约。
 */

#include "Application/Domains/Chooser/UnrealAgentMCPChooserService.h"

#include "Application/Ports/UnrealAgentMCPChooserPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPChooserService::FUnrealAgentMCPChooserService(TSharedRef<IUnrealAgentMCPChooserPort> InChooserPort) : ChooserPort(MoveTemp(InChooserPort))
	{
	}

	TArray<FString> FUnrealAgentMCPChooserService::GetImplementedActions()
	{
		return { TEXT("create"), TEXT("describe"), TEXT("add_column"), TEXT("list_rows"), TEXT("add_row"), TEXT("set_row"), TEXT("delete_row"), TEXT("list_object_references"),
			TEXT("remap_object_references") };
	}

	FString FUnrealAgentMCPChooserService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		if (Action == TEXT("create"))
			return ChooserPort->Create(SafeArgs);
		if (Action == TEXT("describe"))
			return ChooserPort->Describe(SafeArgs);
		if (Action == TEXT("add_column"))
			return ChooserPort->AddColumn(SafeArgs);
		if (Action == TEXT("list_rows"))
			return ChooserPort->ListRows(SafeArgs);
		if (Action == TEXT("add_row"))
			return ChooserPort->AddRow(SafeArgs);
		if (Action == TEXT("set_row"))
			return ChooserPort->SetRow(SafeArgs);
		if (Action == TEXT("delete_row"))
			return ChooserPort->DeleteRow(SafeArgs);
		if (Action == TEXT("list_object_references"))
			return ChooserPort->ListObjectReferences(SafeArgs);
		if (Action == TEXT("remap_object_references"))
			return ChooserPort->RemapObjectReferences(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
		{
			Actions.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("chooser"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Chooser action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
