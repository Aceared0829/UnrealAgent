// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPGASService.cpp
 * @brief GAS 应用服务实现，只依赖 GAS Port 与 JSON 契约。
 */

#include "Application/Domains/GAS/UnrealAgentMCPGASService.h"

#include "Application/Ports/UnrealAgentMCPGASPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	FUnrealAgentMCPGASService::FUnrealAgentMCPGASService(TSharedRef<IUnrealAgentMCPGASPort> InGASPort) : GASPort(MoveTemp(InGASPort))
	{
	}

	TArray<FString> FUnrealAgentMCPGASService::GetImplementedActions()
	{
		return { TEXT("add_asc"), TEXT("create_attribute_set"), TEXT("add_attribute"), TEXT("create_ability"), TEXT("set_ability_tags"), TEXT("create_effect"),
			TEXT("set_effect_modifier"), TEXT("create_cue"), TEXT("get_info"), TEXT("set_asc_defaults"), TEXT("apply_effect"), TEXT("set_attribute"), TEXT("get_attribute"),
			TEXT("init_asc"), TEXT("get_asc_state") };
	}

	FString FUnrealAgentMCPGASService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
			Args->TryGetStringField(TEXT("action"), Action);
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();

		if (Action == TEXT("add_asc"))
			return GASPort->AddASC(SafeArgs);
		if (Action == TEXT("create_attribute_set"))
			return GASPort->CreateAttributeSet(SafeArgs);
		if (Action == TEXT("add_attribute"))
			return GASPort->AddAttribute(SafeArgs);
		if (Action == TEXT("create_ability"))
			return GASPort->CreateAbility(SafeArgs);
		if (Action == TEXT("set_ability_tags"))
			return GASPort->SetAbilityTags(SafeArgs);
		if (Action == TEXT("create_effect"))
			return GASPort->CreateEffect(SafeArgs);
		if (Action == TEXT("set_effect_modifier"))
			return GASPort->SetEffectModifier(SafeArgs);
		if (Action == TEXT("create_cue"))
			return GASPort->CreateCue(SafeArgs);
		if (Action == TEXT("get_info"))
			return GASPort->GetInfo(SafeArgs);
		if (Action == TEXT("set_asc_defaults"))
			return GASPort->SetASCDefaults(SafeArgs);
		if (Action == TEXT("apply_effect"))
			return GASPort->ApplyEffect(SafeArgs);
		if (Action == TEXT("set_attribute"))
			return GASPort->SetAttribute(SafeArgs);
		if (Action == TEXT("get_attribute"))
			return GASPort->GetAttribute(SafeArgs);
		if (Action == TEXT("init_asc"))
			return GASPort->InitASC(SafeArgs);
		if (Action == TEXT("get_asc_state"))
			return GASPort->GetASCState(SafeArgs);

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
			Actions.Add(MakeShared<FJsonValueString>(Name));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("gas"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("GAS action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}
}
