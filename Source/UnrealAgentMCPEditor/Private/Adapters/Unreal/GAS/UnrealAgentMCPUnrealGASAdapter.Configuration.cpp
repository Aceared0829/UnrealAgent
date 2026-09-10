// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGASAdapter.Configuration.cpp
 * @brief Actor Blueprint 的 ASC 配置读取与 AttributeSet 默认值绑定。
 */

#include "Adapters/Unreal/GAS/UnrealAgentMCPUnrealGASAdapter.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace UnrealAgentMCP
{
	FString FUnrealAgentMCPUnrealGASAdapter::GetInfo(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, TEXT("blueprintPath"), Error);
		if (!Blueprint)
			return ErrorJson(Error);

		TArray<TSharedPtr<FJsonValue>> Components;
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				UAbilitySystemComponent* Template = Node ? Cast<UAbilitySystemComponent>(Node->ComponentTemplate) : nullptr;
				if (!Template)
					continue;
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("componentName"), Node->GetVariableName().ToString());
				Item->SetStringField(TEXT("componentClass"), Template->GetClass()->GetPathName());
				Item->SetBoolField(TEXT("replicated"), Template->GetIsReplicated());
				TArray<TSharedPtr<FJsonValue>> Defaults;
				for (const FAttributeDefaults& Entry : Template->DefaultStartingData)
				{
					TSharedRef<FJsonObject> DefaultItem = MakeShared<FJsonObject>();
					DefaultItem->SetStringField(TEXT("attributeSetClass"), Entry.Attributes ? Entry.Attributes->GetPathName() : FString());
					DefaultItem->SetStringField(TEXT("initDataTable"), Entry.DefaultStartingTable ? Entry.DefaultStartingTable->GetPathName() : FString());
					Defaults.Add(MakeShared<FJsonValueObject>(DefaultItem));
				}
				Item->SetArrayField(TEXT("defaultStartingData"), Defaults);
				Components.Add(MakeShared<FJsonValueObject>(Item));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : FString());
		Result->SetNumberField(TEXT("ascCount"), Components.Num());
		Result->SetArrayField(TEXT("abilitySystemComponents"), Components);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::SetASCDefaults(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, TEXT("blueprintPath"), Error);
		if (!Blueprint)
			return ErrorJson(Error);
		FString AttributeSetText;
		if (!Args->TryGetStringField(TEXT("attributeSet"), AttributeSetText) || AttributeSetText.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 attributeSet。"));
		}
		UClass* AttributeSetClass = ResolveClass(AttributeSetText, UAttributeSet::StaticClass());
		if (!AttributeSetClass)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 AttributeSet 类：%s"), *AttributeSetText));
		}
		FString ComponentName;
		Args->TryGetStringField(TEXT("componentName"), ComponentName);
		USCS_Node* Node = FindASCNode(Blueprint, ComponentName);
		UAbilitySystemComponent* Template = Node ? Cast<UAbilitySystemComponent>(Node->ComponentTemplate) : nullptr;
		if (!Template)
			return ErrorJson(TEXT("Blueprint 中未找到目标 ASC。"));

		UDataTable* InitTable = nullptr;
		FString TablePath;
		if (Args->TryGetStringField(TEXT("initDataTable"), TablePath) && !TablePath.IsEmpty())
		{
			InitTable = LoadObject<UDataTable>(nullptr, *JsonConversion::NormalizeAssetObjectPath(TablePath));
			if (!InitTable)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 initDataTable：%s"), *TablePath));
			}
		}

		Blueprint->Modify();
		Node->Modify();
		Template->Modify();
		FAttributeDefaults* Existing = Template->DefaultStartingData.FindByPredicate(
			[AttributeSetClass](const FAttributeDefaults& Entry)
			{
				return Entry.Attributes == AttributeSetClass;
			});
		if (!Existing)
		{
			Existing = &Template->DefaultStartingData.AddDefaulted_GetRef();
			Existing->Attributes = AttributeSetClass;
		}
		Existing->DefaultStartingTable = InitTable;
		Template->PostEditChange();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("componentName"), Node->GetVariableName().ToString());
		Result->SetStringField(TEXT("attributeSetClass"), AttributeSetClass->GetPathName());
		Result->SetStringField(TEXT("initDataTable"), InitTable ? InitTable->GetPathName() : FString());
		Result->SetNumberField(TEXT("defaultStartingDataCount"), Template->DefaultStartingData.Num());
		return SuccessJson(Result);
	}
}
