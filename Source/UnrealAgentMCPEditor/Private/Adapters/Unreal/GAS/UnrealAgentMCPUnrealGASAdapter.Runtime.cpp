// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGASAdapter.Runtime.cpp
 * @brief Live Actor 上的 ASC 初始化、属性读写、Effect 应用与状态检查。
 */

#include "Adapters/Unreal/GAS/UnrealAgentMCPUnrealGASAdapter.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "ActiveGameplayEffectHandle.h"
#include "AttributeSet.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/Actor.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool ResolveASC(const TSharedPtr<FJsonObject>& Args, AActor*& OutActor, UAbilitySystemComponent*& OutASC, FString& OutError)
		{
			UWorld* World = nullptr;
			OutActor = FUnrealAgentMCPUnrealGASAdapter::ResolveRuntimeActor(Args, World, OutError);
			if (!OutActor)
				return false;
			OutASC = FUnrealAgentMCPUnrealGASAdapter::FindRuntimeASC(OutActor);
			if (!OutASC)
			{
				OutError = FString::Printf(TEXT("Actor 没有 AbilitySystemComponent：%s"), *OutActor->GetActorLabel());
				return false;
			}
			return true;
		}

		void AddTagsJson(const FGameplayTagContainer& Tags, TArray<TSharedPtr<FJsonValue>>& OutValues)
		{
			for (const FGameplayTag& Tag : Tags)
			{
				OutValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
		}

		TSharedRef<FJsonObject> MakeAttributeJson(UAbilitySystemComponent* ASC, UAttributeSet* Set, const FGameplayAttribute& Attribute)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("attribute"), Attribute.GetName());
			Item->SetStringField(TEXT("attributeSet"), Set ? Set->GetClass()->GetName() : FString());
			Item->SetStringField(TEXT("qualifiedName"), FString::Printf(TEXT("%s.%s"), Set ? *Set->GetClass()->GetName() : TEXT(""), *Attribute.GetName()));
			Item->SetNumberField(TEXT("baseValue"), ASC->GetNumericAttributeBase(Attribute));
			Item->SetNumberField(TEXT("currentValue"), ASC->GetNumericAttribute(Attribute));
			return Item;
		}
	}

	FString FUnrealAgentMCPUnrealGASAdapter::InitASC(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		if (!ResolveASC(Args, Actor, ASC, Error))
			return ErrorJson(Error);

		ASC->Modify();
		ASC->InitAbilityActorInfo(Actor, Actor);
		FString AttributeSetText;
		bool bCreatedSet = false;
		UAttributeSet* AddedSet = nullptr;
		if (Args->TryGetStringField(TEXT("attributeSet"), AttributeSetText) && !AttributeSetText.IsEmpty())
		{
			UClass* AttributeSetClass = ResolveClass(AttributeSetText, UAttributeSet::StaticClass());
			if (!AttributeSetClass)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 AttributeSet 类：%s"), *AttributeSetText));
			}
			for (UAttributeSet* Existing : ASC->GetSpawnedAttributes())
			{
				if (Existing && Existing->GetClass() == AttributeSetClass)
				{
					AddedSet = Existing;
					break;
				}
			}
			if (!AddedSet)
			{
				AddedSet = NewObject<UAttributeSet>(ASC, AttributeSetClass);
				if (!AddedSet)
					return ErrorJson(TEXT("AttributeSet 实例创建失败。"));
				ASC->AddSpawnedAttribute(AddedSet);
				bCreatedSet = true;
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("componentName"), ASC->GetName());
		Result->SetBoolField(TEXT("initialized"), true);
		Result->SetBoolField(TEXT("createdAttributeSet"), bCreatedSet);
		Result->SetStringField(TEXT("attributeSetClass"), AddedSet ? AddedSet->GetClass()->GetPathName() : FString());
		Result->SetNumberField(TEXT("spawnedAttributeSetCount"), ASC->GetSpawnedAttributes().Num());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::SetAttribute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		if (!ResolveASC(Args, Actor, ASC, Error))
			return ErrorJson(Error);
		FString AttributeText;
		if (!Args->TryGetStringField(TEXT("attribute"), AttributeText) || AttributeText.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 attribute。"));
		}
		double Value = 0.0;
		if (!Args->TryGetNumberField(TEXT("value"), Value))
			return ErrorJson(TEXT("缺少必填 value。"));
		FGameplayAttribute Attribute;
		UAttributeSet* Set = nullptr;
		if (!ResolveAttribute(ASC, AttributeText, Attribute, Set, Error))
		{
			return ErrorJson(Error);
		}
		ASC->SetNumericAttributeBase(Attribute, Value);
		TSharedRef<FJsonObject> Result = MakeAttributeJson(ASC, Set, Attribute);
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::GetAttribute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		if (!ResolveASC(Args, Actor, ASC, Error))
			return ErrorJson(Error);
		FString AttributeText;
		Args->TryGetStringField(TEXT("attribute"), AttributeText);
		if (!AttributeText.IsEmpty())
		{
			FGameplayAttribute Attribute;
			UAttributeSet* Set = nullptr;
			if (!ResolveAttribute(ASC, AttributeText, Attribute, Set, Error))
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MakeAttributeJson(ASC, Set, Attribute);
			Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			return SuccessJson(Result);
		}

		TArray<TSharedPtr<FJsonValue>> Attributes;
		for (UAttributeSet* Set : ASC->GetSpawnedAttributes())
		{
			if (!Set)
				continue;
			for (TFieldIterator<FProperty> It(Set->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (!FGameplayAttribute::IsSupportedProperty(*It))
					continue;
				Attributes.Add(MakeShared<FJsonValueObject>(MakeAttributeJson(ASC, Set, FGameplayAttribute(*It))));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetNumberField(TEXT("attributeCount"), Attributes.Num());
		Result->SetArrayField(TEXT("attributes"), Attributes);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::ApplyEffect(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		if (!ResolveASC(Args, Actor, ASC, Error))
			return ErrorJson(Error);
		FString EffectClassText;
		if (!Args->TryGetStringField(TEXT("effectClass"), EffectClassText) || EffectClassText.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 effectClass。"));
		}
		UClass* EffectClass = ResolveClass(EffectClassText, UGameplayEffect::StaticClass());
		if (!EffectClass)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 GameplayEffect 类：%s"), *EffectClassText));
		}
		double Level = 1.0;
		Args->TryGetNumberField(TEXT("level"), Level);
		FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(EffectClass, Level, ASC->MakeEffectContext());
		if (!SpecHandle.IsValid())
			return ErrorJson(TEXT("GameplayEffectSpec 创建失败。"));

		const TSharedPtr<FJsonObject>* SetByCaller = nullptr;
		if (Args->TryGetObjectField(TEXT("setByCaller"), SetByCaller) && SetByCaller && SetByCaller->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*SetByCaller)->Values)
			{
				double Magnitude = 0.0;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetNumber(Magnitude))
				{
					continue;
				}
				const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(*Pair.Key, false);
				if (Tag.IsValid())
					SpecHandle.Data->SetSetByCallerMagnitude(Tag, Magnitude);
				else
					SpecHandle.Data->SetSetByCallerMagnitude(*Pair.Key, Magnitude);
			}
		}
		const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);
		if (!Handle.WasSuccessfullyApplied())
			return ErrorJson(TEXT("GameplayEffect 应用失败。"));

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("effectClass"), EffectClass->GetPathName());
		Result->SetNumberField(TEXT("level"), Level);
		Result->SetStringField(TEXT("activeEffectHandle"), Handle.ToString());
		Result->SetBoolField(TEXT("active"), Handle.IsValid());
		Result->SetBoolField(TEXT("applied"), Handle.WasSuccessfullyApplied());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealGASAdapter::GetASCState(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
		if (!ResolveASC(Args, Actor, ASC, Error))
			return ErrorJson(Error);

		TArray<TSharedPtr<FJsonValue>> Abilities;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("class"), Spec.Ability ? Spec.Ability->GetClass()->GetPathName() : FString());
			Item->SetNumberField(TEXT("level"), Spec.Level);
			Item->SetNumberField(TEXT("inputID"), Spec.InputID);
			Item->SetBoolField(TEXT("active"), Spec.IsActive());
			TArray<TSharedPtr<FJsonValue>> DynamicTags;
			AddTagsJson(Spec.GetDynamicSpecSourceTags(), DynamicTags);
			Item->SetArrayField(TEXT("dynamicTags"), DynamicTags);
			Abilities.Add(MakeShared<FJsonValueObject>(Item));
		}
		TArray<TSharedPtr<FJsonValue>> OwnedTags;
		AddTagsJson(ASC->GetOwnedGameplayTags(), OwnedTags);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("componentName"), ASC->GetName());
		Result->SetBoolField(TEXT("initialized"), ASC->GetOwnerActor() != nullptr && ASC->GetAvatarActor() != nullptr);
		Result->SetNumberField(TEXT("abilityCount"), Abilities.Num());
		Result->SetArrayField(TEXT("abilities"), Abilities);
		Result->SetArrayField(TEXT("ownedTags"), OwnedTags);
		Result->SetNumberField(TEXT("attributeSetCount"), ASC->GetSpawnedAttributes().Num());
		return SuccessJson(Result);
	}
}
