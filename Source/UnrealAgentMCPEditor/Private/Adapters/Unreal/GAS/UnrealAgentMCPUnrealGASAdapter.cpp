// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGASAdapter.cpp
 * @brief GAS Adapter 的资产、世界、ASC 与 Attribute 公共解析设施。
 */

#include "Adapters/Unreal/GAS/UnrealAgentMCPUnrealGASAdapter.h"

#include "AbilitySystemComponent.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "AttributeSet.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	UBlueprint* FUnrealAgentMCPUnrealGASAdapter::ResolveBlueprint(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutError)
	{
		FString Path;
		if (!Args.IsValid() || !Args->TryGetStringField(Field, Path) || Path.IsEmpty())
		{
			OutError = FString::Printf(TEXT("缺少必填 %s。"), Field);
			return nullptr;
		}
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *JsonConversion::NormalizeAssetObjectPath(Path));
		if (!Blueprint)
		{
			OutError = FString::Printf(TEXT("未找到 Blueprint：%s"), *Path);
		}
		return Blueprint;
	}

	UClass* FUnrealAgentMCPUnrealGASAdapter::ResolveClass(const FString& ClassText, UClass* RequiredBase)
	{
		if (ClassText.IsEmpty() || !RequiredBase)
			return nullptr;

		UClass* Result = FindObject<UClass>(nullptr, *ClassText);
		if (!Result)
			Result = LoadObject<UClass>(nullptr, *ClassText);
		if (!Result)
		{
			if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *JsonConversion::NormalizeAssetObjectPath(ClassText)))
			{
				Result = Blueprint->GeneratedClass;
			}
		}
		if (!Result && !ClassText.Contains(TEXT("/")))
		{
			FString ShortName = ClassText;
			ShortName.RemoveFromStart(TEXT("U"));
			ShortName.RemoveFromEnd(TEXT("_C"));
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Candidate = *It;
				FString CandidateName = Candidate->GetName();
				CandidateName.RemoveFromStart(TEXT("U"));
				CandidateName.RemoveFromEnd(TEXT("_C"));
				if (CandidateName.Equals(ShortName, ESearchCase::IgnoreCase))
				{
					Result = Candidate;
					break;
				}
			}
		}
		return Result && Result->IsChildOf(RequiredBase) ? Result : nullptr;
	}

	USCS_Node* FUnrealAgentMCPUnrealGASAdapter::FindASCNode(UBlueprint* Blueprint, const FString& ComponentName)
	{
		if (!Blueprint || !Blueprint->SimpleConstructionScript)
			return nullptr;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentClass || !Node->ComponentClass->IsChildOf(UAbilitySystemComponent::StaticClass()))
			{
				continue;
			}
			if (ComponentName.IsEmpty() || Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase) ||
				GetNameSafe(Node->ComponentTemplate).Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}
		return nullptr;
	}

	UAbilitySystemComponent* FUnrealAgentMCPUnrealGASAdapter::FindRuntimeASC(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
	}

	AActor* FUnrealAgentMCPUnrealGASAdapter::ResolveRuntimeActor(const TSharedPtr<FJsonObject>& Args, UWorld*& OutWorld, FString& OutError)
	{
		FString ActorLabel;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("actorLabel"), ActorLabel) || ActorLabel.IsEmpty())
		{
			OutError = TEXT("缺少必填 actorLabel。");
			return nullptr;
		}

		FString Scope = TEXT("auto");
		Args->TryGetStringField(TEXT("world"), Scope);
		Scope.ToLowerInline();
		OutWorld = nullptr;
		if (GEngine && Scope != TEXT("editor"))
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
				{
					OutWorld = Context.World();
					break;
				}
			}
		}
		if (!OutWorld && Scope != TEXT("pie"))
			OutWorld = ActorSupport::GetEditorWorld();
		if (!OutWorld)
		{
			OutError = FString::Printf(TEXT("未找到 world=%s 对应的世界。"), *Scope);
			return nullptr;
		}
		AActor* Actor = ActorSupport::FindActorByNameOrLabel(OutWorld, ActorLabel);
		if (!Actor)
		{
			OutError = FString::Printf(TEXT("未找到 Actor：%s"), *ActorLabel);
		}
		return Actor;
	}

	bool FUnrealAgentMCPUnrealGASAdapter::ResolveAttribute(UAbilitySystemComponent* ASC, const FString& AttributeText, FGameplayAttribute& OutAttribute, UAttributeSet*& OutSet,
		FString& OutError)
	{
		if (!ASC || AttributeText.IsEmpty())
		{
			OutError = TEXT("ASC 或 attribute 无效。");
			return false;
		}
		FString SetName;
		FString PropertyName = AttributeText;
		AttributeText.Split(TEXT("."), &SetName, &PropertyName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		for (UAttributeSet* Set : ASC->GetSpawnedAttributes())
		{
			if (!Set)
				continue;
			FString CandidateSetName = Set->GetClass()->GetName();
			CandidateSetName.RemoveFromStart(TEXT("U"));
			CandidateSetName.RemoveFromEnd(TEXT("_C"));
			if (!SetName.IsEmpty() && !CandidateSetName.Equals(SetName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			FProperty* Property = Set->GetClass()->FindPropertyByName(*PropertyName);
			if (Property && FGameplayAttribute::IsSupportedProperty(Property))
			{
				OutAttribute = FGameplayAttribute(Property);
				OutSet = Set;
				return true;
			}
		}
		OutError = FString::Printf(TEXT("ASC 中未找到 GameplayAttribute：%s"), *AttributeText);
		return false;
	}

	bool FUnrealAgentMCPUnrealGASAdapter::SaveBlueprint(UBlueprint* Blueprint, FString& OutError)
	{
		UPackage* Package = Blueprint ? Blueprint->GetOutermost() : nullptr;
		if (!Package)
		{
			OutError = TEXT("Blueprint Package 无效。");
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
		{
			OutError = TEXT("Blueprint 资产保存失败。");
			return false;
		}
		return true;
	}

	bool FUnrealAgentMCPUnrealGASAdapter::EnsureGameplayTag(const FString& TagText, FGameplayTag& OutTag, FString& OutError)
	{
		if (TagText.IsEmpty())
		{
			OutError = TEXT("GameplayTag 名称不能为空。");
			return false;
		}
		OutTag = FGameplayTag::RequestGameplayTag(*TagText, false);
		if (!OutTag.IsValid())
		{
			IGameplayTagsEditorModule::Get().AddNewGameplayTagToINI(TagText, TEXT("由 Unreal Agent GAS 工具创建"));
			OutTag = FGameplayTag::RequestGameplayTag(*TagText, false);
		}
		if (!OutTag.IsValid())
		{
			OutError = FString::Printf(TEXT("GameplayTag 创建失败：%s"), *TagText);
			return false;
		}
		return true;
	}
}
