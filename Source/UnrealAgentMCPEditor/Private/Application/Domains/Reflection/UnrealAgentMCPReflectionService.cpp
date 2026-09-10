// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPReflectionService.cpp
 * @brief Reflection 应用服务实现；只依赖 Reflection Port 和 JSON 契约。
 */

#include "Application/Domains/Reflection/UnrealAgentMCPReflectionService.h"

#include "Application/Ports/UnrealAgentMCPReflectionPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPReflectionService::FUnrealAgentMCPReflectionService(TSharedRef<IUnrealAgentMCPReflectionPort> InReflectionPort) : ReflectionPort(MoveTemp(InReflectionPort))
	{
	}

	TArray<FString> FUnrealAgentMCPReflectionService::GetImplementedActions()
	{
		return { TEXT("reflect_class"), TEXT("reflect_struct"), TEXT("reflect_enum"), TEXT("list_classes"), TEXT("list_tags"), TEXT("create_tag"), TEXT("create_enum"),
			TEXT("set_enum_entries"), TEXT("is_class_loaded"), TEXT("is_module_loaded"), TEXT("list_loaded_modules"), TEXT("inspect_save_game") };
	}

	FString FUnrealAgentMCPReflectionService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
			Args->TryGetStringField(TEXT("action"), Action);
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		if (Action == TEXT("reflect_class"))
			return ReflectionPort->ReflectClass(SafeArgs);
		if (Action == TEXT("reflect_struct"))
			return ReflectionPort->ReflectStruct(SafeArgs);
		if (Action == TEXT("reflect_enum"))
			return ReflectionPort->ReflectEnum(SafeArgs);
		if (Action == TEXT("list_classes"))
			return ReflectionPort->ListClasses(SafeArgs);
		if (Action == TEXT("list_tags"))
			return ReflectionPort->ListTags(SafeArgs);
		if (Action == TEXT("create_tag"))
			return ReflectionPort->CreateTag(SafeArgs);
		if (Action == TEXT("create_enum"))
			return ReflectionPort->CreateEnum(SafeArgs);
		if (Action == TEXT("set_enum_entries"))
			return ReflectionPort->SetEnumEntries(SafeArgs);
		if (Action == TEXT("is_class_loaded"))
			return ReflectionPort->IsClassLoaded(SafeArgs);
		if (Action == TEXT("is_module_loaded"))
			return ReflectionPort->IsModuleLoaded(SafeArgs);
		if (Action == TEXT("list_loaded_modules"))
			return ReflectionPort->ListLoadedModules(SafeArgs);
		if (Action == TEXT("inspect_save_game"))
			return ReflectionPort->InspectSaveGame(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
			Actions.Add(MakeShared<FJsonValueString>(Name));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("reflection"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Reflection action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
