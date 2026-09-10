// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGASAdapter.Assets.cpp
 * @brief GAS Blueprint 资产、属性、标签、效果与 Cue 的独立创建实现。
 */

#include "Adapters/Unreal/GAS/UnrealAgentMCPUnrealGASAdapter.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AttributeSet.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "GameplayCueNotify_Actor.h"
#include "GameplayCueNotify_Burst.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NormalizePackagePath(FString Path, const FString& DefaultPath)
		{
			Path.TrimStartAndEndInline();
			if (Path.IsEmpty())
				Path = DefaultPath;
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			if (!Path.StartsWith(TEXT("/Game")))
				Path = TEXT("/Game/") + Path.TrimChar(TEXT('/'));
			while (Path.EndsWith(TEXT("/")))
				Path.LeftChopInline(1);
			return Path;
		}

		UBlueprint* CreateBlueprintAsset(const TSharedPtr<FJsonObject>& Args, UClass* ParentClass, const FString& DefaultPath, FString& OutError)
		{
			FString Name;
			if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
			{
				OutError = TEXT("缺少必填 name。");
				return nullptr;
			}
			FString PackagePath;
			Args->TryGetStringField(TEXT("packagePath"), PackagePath);
			PackagePath = NormalizePackagePath(PackagePath, DefaultPath);
			const FString PackageName = PackagePath + TEXT("/") + Name;
			if (!FPackageName::IsValidLongPackageName(PackageName))
			{
				OutError = FString::Printf(TEXT("无效资产路径：%s"), *PackageName);
				return nullptr;
			}
			const FString ObjectPath = PackageName + TEXT(".") + Name;
			if (FindObject<UBlueprint>(nullptr, *ObjectPath) || FPackageName::DoesPackageExist(PackageName))
			{
				OutError = FString::Printf(TEXT("资产已存在：%s"), *PackageName);
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, *Name, BPTYPE_Normal, UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(), TEXT("UnrealAgent"));
			if (!Blueprint)
			{
				OutError = TEXT("Blueprint 创建失败。");
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			Blueprint->MarkPackageDirty();
			return Blueprint;
		}

		TArray<FString> ReadStringArray(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field)
		{
			TArray<FString> Result;
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Args->TryGetArrayField(Field, Values) || !Values)
				return Result;
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString Text;
				if (Value.IsValid() && Value->TryGetString(Text) && !Text.IsEmpty())
				{
					Result.Add(Text);
				}
			}
			return Result;
		}

		bool MakeTagContainer(const TArray<FString>& Names, FGameplayTagContainer& OutTags, FString& OutError)
		{
			OutTags.Reset();
			for (const FString& Name : Names)
			{
				FGameplayTag Tag;
				if (!FUnrealAgentMCPUnrealGASAdapter::EnsureGameplayTag(Name, Tag, OutError))
				{
					return false;
				}
				OutTags.AddTag(Tag);
			}
			return true;
		}

		bool SetTagContainerProperty(UObject* Object, const FName PropertyName, const FGameplayTagContainer& Value)
		{
			FStructProperty* Property = Object ? FindFProperty<FStructProperty>(Object->GetClass(), PropertyName) : nullptr;
			if (!Property || Property->Struct != FGameplayTagContainer::StaticStruct())
			{
				return false;
			}
			*Property->ContainerPtrToValuePtr<FGameplayTagContainer>(Object) = Value;
			return true;
		}

		FGameplayAttribute FindLoadedAttribute(const FString& AttributeText)
		{
			FString SetName;
			FString PropertyName = AttributeText;
			AttributeText.Split(TEXT("."), &SetName, &PropertyName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (!Class->IsChildOf(UAttributeSet::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
				{
					continue;
				}
				FString CandidateName = Class->GetName();
				CandidateName.RemoveFromStart(TEXT("U"));
				CandidateName.RemoveFromEnd(TEXT("_C"));
				if (!SetName.IsEmpty() && !CandidateName.Equals(SetName, ESearchCase::IgnoreCase))
				{
					continue;
				}
				FProperty* Property = Class->FindPropertyByName(*PropertyName);
				if (Property && FGameplayAttribute::IsSupportedProperty(Property))
				{
					return FGameplayAttribute(Property);
				}
			}
			return FGameplayAttribute();
		}

		EGameplayModOp::Type ParseModifierOperation(FString Value)
		{
			Value.ToLowerInline();
			if (Value == TEXT("multiply") || Value == TEXT("multiplicative"))
			{
				return EGameplayModOp::Multiplicitive;
			}
			if (Value == TEXT("divide") || Value == TEXT("division"))
			{
				return EGameplayModOp::Division;
			}
			if (Value == TEXT("override"))
				return EGameplayModOp::Override;
			if (Value == TEXT("addfinal") || Value == TEXT("add_final"))
			{
				return EGameplayModOp::AddFinal;
			}
			return EGameplayModOp::Additive;
		}
	}

	FString FUnrealAgentMCPUnrealGASAdapter::AddASC(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, TEXT("blueprintPath"), Error);
		if (!Blueprint)
			return ErrorJson(Error);
		if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()) || !Blueprint->SimpleConstructionScript)
		{
			return ErrorJson(TEXT("blueprintPath 必须指向 Actor Blueprint。"));
		}
		FString ComponentName = TEXT("AbilitySystem");
		Args->TryGetStringField(TEXT("componentName"), ComponentName);
		if (USCS_Node* Existing = FindASCNode(Blueprint, ComponentName))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
			Result->SetStringField(TEXT("componentName"), Existing->GetVariableName().ToString());
			Result->SetBoolField(TEXT("existed"), true);
			return SuccessJson(Result);
		}

		Blueprint->Modify();
		USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(UAbilitySystemComponent::StaticClass(), *ComponentName);
		if (!Node)
			return ErrorJson(TEXT("AbilitySystemComponent 节点创建失败。"));
		Blueprint->SimpleConstructionScript->AddNode(Node);
		if (UAbilitySystemComponent* Template = Cast<UAbilitySystemComponent>(Node->ComponentTemplate))
		{
			Template->SetIsReplicated(true);
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("componentName"), Node->GetVariableName().ToString());
		Result->SetStringField(TEXT("componentClass"), UAbilitySystemComponent::StaticClass()->GetPathName());
		Result->SetBoolField(TEXT("existed"), false);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::CreateAttributeSet(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = CreateBlueprintAsset(Args, UAttributeSet::StaticClass(), TEXT("/Game/GAS/AttributeSets"), Error);
		if (!Blueprint)
			return ErrorJson(Error);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass->GetPathName());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::AddAttribute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, TEXT("attributeSetPath"), Error);
		if (!Blueprint)
			return ErrorJson(Error);
		if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UAttributeSet::StaticClass()))
		{
			return ErrorJson(TEXT("attributeSetPath 不是 AttributeSet Blueprint。"));
		}
		FString Name;
		if (!Args->TryGetStringField(TEXT("attributeName"), Name) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 attributeName。"));
		}
		if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->FindPropertyByName(*Name))
		{
			return ErrorJson(FString::Printf(TEXT("属性已存在：%s"), *Name));
		}
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = FGameplayAttributeData::StaticStruct();
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, *Name, PinType))
		{
			return ErrorJson(TEXT("GameplayAttributeData 变量添加失败。"));
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		double DefaultValue = 0.0;
		Args->TryGetNumberField(TEXT("defaultValue"), DefaultValue);
		UAttributeSet* Defaults = Blueprint->GeneratedClass ? Cast<UAttributeSet>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		FStructProperty* Property = Blueprint->GeneratedClass ? FindFProperty<FStructProperty>(Blueprint->GeneratedClass, *Name) : nullptr;
		if (!Defaults || !Property || Property->Struct != FGameplayAttributeData::StaticStruct())
		{
			return ErrorJson(TEXT("新属性编译后未生成 GameplayAttributeData。"));
		}
		Defaults->Modify();
		FGameplayAttributeData* Data = Property->ContainerPtrToValuePtr<FGameplayAttributeData>(Defaults);
		Data->SetBaseValue(DefaultValue);
		Data->SetCurrentValue(DefaultValue);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("attributeSetPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("attributeName"), Name);
		Result->SetNumberField(TEXT("defaultValue"), DefaultValue);
		Result->SetStringField(TEXT("propertyType"), FGameplayAttributeData::StaticStruct()->GetPathName());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::CreateAbility(const TSharedPtr<FJsonObject>& Args)
	{
		FString ParentText;
		Args->TryGetStringField(TEXT("parentClass"), ParentText);
		UClass* Parent = ParentText.IsEmpty() ? UGameplayAbility::StaticClass() : ResolveClass(ParentText, UGameplayAbility::StaticClass());
		if (!Parent)
			return ErrorJson(TEXT("parentClass 不是 GameplayAbility 子类。"));
		FString Error;
		UBlueprint* Blueprint = CreateBlueprintAsset(Args, Parent, TEXT("/Game/GAS/Abilities"), Error);
		if (!Blueprint)
			return ErrorJson(Error);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass->GetPathName());
		Result->SetStringField(TEXT("parentClass"), Parent->GetPathName());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::SetAbilityTags(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, TEXT("abilityPath"), Error);
		UGameplayAbility* Defaults = Blueprint && Blueprint->GeneratedClass ? Cast<UGameplayAbility>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults)
			return ErrorJson(Error.IsEmpty() ? TEXT("abilityPath 不是 GameplayAbility Blueprint。") : Error);

		FGameplayTagContainer AbilityTags;
		FGameplayTagContainer CancelTags;
		FGameplayTagContainer BlockTags;
		FGameplayTagContainer RequiredTags;
		FGameplayTagContainer BlockedTags;
		if (!MakeTagContainer(ReadStringArray(Args, TEXT("ability_tags")), AbilityTags, Error) ||
			!MakeTagContainer(ReadStringArray(Args, TEXT("cancel_abilities_with_tag")), CancelTags, Error) ||
			!MakeTagContainer(ReadStringArray(Args, TEXT("block_abilities_with_tag")), BlockTags, Error) ||
			!MakeTagContainer(ReadStringArray(Args, TEXT("activation_required_tags")), RequiredTags, Error) ||
			!MakeTagContainer(ReadStringArray(Args, TEXT("activation_blocked_tags")), BlockedTags, Error))
		{
			return ErrorJson(Error);
		}

		Blueprint->Modify();
		Defaults->Modify();
		Defaults->EditorGetAssetTags() = AbilityTags;
		const bool bPropertiesWritten = SetTagContainerProperty(Defaults, TEXT("CancelAbilitiesWithTag"), CancelTags) &&
			SetTagContainerProperty(Defaults, TEXT("BlockAbilitiesWithTag"), BlockTags) && SetTagContainerProperty(Defaults, TEXT("ActivationRequiredTags"), RequiredTags) &&
			SetTagContainerProperty(Defaults, TEXT("ActivationBlockedTags"), BlockedTags);
		if (!bPropertiesWritten)
			return ErrorJson(TEXT("GameplayAbility 标签属性写入失败。"));
		Defaults->PostEditChange();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("abilityPath"), Blueprint->GetPathName());
		Result->SetNumberField(TEXT("abilityTagCount"), AbilityTags.Num());
		Result->SetNumberField(TEXT("cancelTagCount"), CancelTags.Num());
		Result->SetNumberField(TEXT("requiredTagCount"), RequiredTags.Num());
		Result->SetNumberField(TEXT("blockedTagCount"), BlockedTags.Num());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::CreateEffect(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = CreateBlueprintAsset(Args, UGameplayEffect::StaticClass(), TEXT("/Game/GAS/Effects"), Error);
		if (!Blueprint)
			return ErrorJson(Error);
		UGameplayEffect* Defaults = Cast<UGameplayEffect>(Blueprint->GeneratedClass->GetDefaultObject());
		FString Policy = TEXT("Instant");
		Args->TryGetStringField(TEXT("durationPolicy"), Policy);
		Policy.ToLowerInline();
		if (Policy == TEXT("infinite"))
			Defaults->DurationPolicy = EGameplayEffectDurationType::Infinite;
		else if (Policy == TEXT("hasduration") || Policy == TEXT("duration") || Policy == TEXT("has_duration"))
		{
			Defaults->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		}
		else
			Defaults->DurationPolicy = EGameplayEffectDurationType::Instant;
		Defaults->PostEditChange();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass->GetPathName());
		Result->SetStringField(TEXT("durationPolicy"), UEnum::GetValueAsString(Defaults->DurationPolicy));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::SetEffectModifier(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, TEXT("effectPath"), Error);
		UGameplayEffect* Defaults = Blueprint && Blueprint->GeneratedClass ? Cast<UGameplayEffect>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults)
			return ErrorJson(Error.IsEmpty() ? TEXT("effectPath 不是 GameplayEffect Blueprint。") : Error);
		FString AttributeText;
		if (!Args->TryGetStringField(TEXT("attribute"), AttributeText) || AttributeText.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 attribute。"));
		}
		const FGameplayAttribute Attribute = FindLoadedAttribute(AttributeText);
		if (!Attribute.IsValid())
		{
			return ErrorJson(FString::Printf(TEXT("未找到 GameplayAttribute：%s"), *AttributeText));
		}
		FString Operation = TEXT("Additive");
		Args->TryGetStringField(TEXT("operation"), Operation);
		double Magnitude = 0.0;
		Args->TryGetNumberField(TEXT("magnitude"), Magnitude);

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = ParseModifierOperation(Operation);
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Magnitude));
		Defaults->Modify();
		Defaults->Modifiers.Add(Modifier);
		Defaults->PostEditChange();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("effectPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("attribute"), Attribute.GetName());
		Result->SetStringField(TEXT("operation"), UEnum::GetValueAsString(Modifier.ModifierOp));
		Result->SetNumberField(TEXT("magnitude"), Magnitude);
		Result->SetNumberField(TEXT("modifierCount"), Defaults->Modifiers.Num());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::CreateCue(const TSharedPtr<FJsonObject>& Args)
	{
		FString CueType = TEXT("burst");
		Args->TryGetStringField(TEXT("cueType"), CueType);
		CueType.ToLowerInline();
		UClass* Parent = CueType == TEXT("actor") ? AGameplayCueNotify_Actor::StaticClass()
			: CueType == TEXT("static")           ? UGameplayCueNotify_Static::StaticClass()
												  : UGameplayCueNotify_Burst::StaticClass();
		FString Error;
		UBlueprint* Blueprint = CreateBlueprintAsset(Args, Parent, TEXT("/Game/GAS/GameplayCues"), Error);
		if (!Blueprint)
			return ErrorJson(Error);
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass->GetPathName());
		Result->SetStringField(TEXT("cueType"), CueType);
		return SuccessJson(Result);
	}
}
