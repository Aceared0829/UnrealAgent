// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealGASAdapter.h
 * @brief 通过 GameplayAbilities 公共 API 实现 GAS 资产与运行时操作。
 */

#include "Application/Ports/UnrealAgentMCPGASPort.h"

class AActor;
class UAbilitySystemComponent;
class UAttributeSet;
class UBlueprint;
class UClass;
class UDataTable;
class UGameplayEffect;
class USCS_Node;
class UWorld;
struct FGameplayAttribute;
struct FGameplayTag;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealGASAdapter final : public IUnrealAgentMCPGASPort
	{
	public:
		virtual FString AddASC(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateAttributeSet(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString AddAttribute(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateAbility(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetAbilityTags(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateEffect(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetEffectModifier(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateCue(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetASCDefaults(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ApplyEffect(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetAttribute(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetAttribute(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString InitASC(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetASCState(const TSharedPtr<FJsonObject>& Args) override;

		/** 以下辅助入口供同一 Adapter 的分拆实现文件复用。 */
		static UBlueprint* ResolveBlueprint(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutError);
		static UClass* ResolveClass(const FString& ClassText, UClass* RequiredBase);
		static USCS_Node* FindASCNode(UBlueprint* Blueprint, const FString& ComponentName);
		static UAbilitySystemComponent* FindRuntimeASC(AActor* Actor);
		static AActor* ResolveRuntimeActor(const TSharedPtr<FJsonObject>& Args, UWorld*& OutWorld, FString& OutError);
		static bool ResolveAttribute(UAbilitySystemComponent* ASC, const FString& AttributeText, FGameplayAttribute& OutAttribute, UAttributeSet*& OutSet, FString& OutError);
		static bool SaveBlueprint(UBlueprint* Blueprint, FString& OutError);
		static bool EnsureGameplayTag(const FString& TagText, FGameplayTag& OutTag, FString& OutError);
	};
}
