// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPGASMigrationTests.cpp
 * @brief GAS 十五项迁移能力的真实资产、ASC、属性聚合与 Effect 黑盒。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/GAS/UnrealAgentMCPUnrealGASAdapter.h"
#include "Application/Domains/GAS/UnrealAgentMCPGASService.h"
#include "Application/Ports/UnrealAgentMCPGASPort.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AttributeSet.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPGASMigrationIntegrationTest, "WorldData.UnrealAgent.GAS.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPGASMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
			return false;
		UPackage* WorldPackage = World->GetOutermost();
		const bool bWorldWasDirty = WorldPackage && WorldPackage->IsDirty();

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackagePath = TEXT("/Game/UnrealAgentAutomation");
		const FString ActorName = TEXT("BP_GASActor_") + Suffix;
		const FString SetName = TEXT("AS_MCP_") + Suffix;
		const FString AbilityName = TEXT("GA_MCP_") + Suffix;
		const FString EffectName = TEXT("GE_MCP_") + Suffix;
		const FString CueName = TEXT("GC_MCP_") + Suffix;
		const FString ActorPath = PackagePath + TEXT("/") + ActorName + TEXT(".") + ActorName;
		const FString SetPath = PackagePath + TEXT("/") + SetName + TEXT(".") + SetName;
		const FString AbilityPath = PackagePath + TEXT("/") + AbilityName + TEXT(".") + AbilityName;
		const FString EffectPath = PackagePath + TEXT("/") + EffectName + TEXT(".") + EffectName;
		const FString CuePath = PackagePath + TEXT("/") + CueName + TEXT(".") + CueName;

		UPackage* ActorPackage = CreatePackage(*(PackagePath + TEXT("/") + ActorName));
		UBlueprint* ActorBlueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), ActorPackage, *ActorName, BPTYPE_Normal, UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(), TEXT("UnrealAgentMCPAutomation"));
		if (!TestNotNull(TEXT("测试 Actor Blueprint 创建成功"), ActorBlueprint))
		{
			return false;
		}
		FAssetRegistryModule::AssetCreated(ActorBlueprint);
		FKismetEditorUtilities::CompileBlueprint(ActorBlueprint);
		FString SaveError;
		TestTrue(TEXT("测试 Actor Blueprint 保存成功"), FUnrealAgentMCPUnrealGASAdapter::SaveBlueprint(ActorBlueprint, SaveError));

		TSharedRef<FUnrealAgentMCPUnrealGASAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealGASAdapter>();
		TSharedRef<IUnrealAgentMCPGASPort> Port = Adapter;
		FUnrealAgentMCPGASService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1800)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), ActorPath);
			Args->SetStringField(TEXT("componentName"), TEXT("AbilitySystem"));
			Execute(TEXT("add_asc"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), SetName);
			Args->SetStringField(TEXT("packagePath"), PackagePath);
			Execute(TEXT("create_attribute_set"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("attributeSetPath"), SetPath);
			Args->SetStringField(TEXT("attributeName"), TEXT("Health"));
			Args->SetNumberField(TEXT("defaultValue"), 100.0);
			Execute(TEXT("add_attribute"), Args);
		}

		UBlueprint* SetBlueprint = LoadObject<UBlueprint>(nullptr, *SetPath);
		FStructProperty* HealthProperty = SetBlueprint && SetBlueprint->GeneratedClass ? FindFProperty<FStructProperty>(SetBlueprint->GeneratedClass, TEXT("Health")) : nullptr;
		TestNotNull(TEXT("AttributeSet 生成真实 Health 属性"), HealthProperty);
		if (HealthProperty)
		{
			TestTrue(TEXT("Health 使用 FGameplayAttributeData"), HealthProperty->Struct == FGameplayAttributeData::StaticStruct());
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), AbilityName);
			Args->SetStringField(TEXT("packagePath"), PackagePath);
			Execute(TEXT("create_ability"), Args);
		}
		const FString AbilityTagName = TEXT("UnrealAgentMCP.Test.Ability.") + Suffix;
		const FString RequiredTagName = TEXT("UnrealAgentMCP.Test.Required.") + Suffix;
		UGameplayTagsManager::Get().AddNativeGameplayTag(*AbilityTagName, TEXT("Unreal Agent GAS 黑盒"));
		UGameplayTagsManager::Get().AddNativeGameplayTag(*RequiredTagName, TEXT("Unreal Agent GAS 黑盒"));
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("abilityPath"), AbilityPath);
			Args->SetArrayField(TEXT("ability_tags"), { MakeShared<FJsonValueString>(AbilityTagName) });
			Args->SetArrayField(TEXT("activation_required_tags"), { MakeShared<FJsonValueString>(RequiredTagName) });
			Execute(TEXT("set_ability_tags"), Args);
		}
		UBlueprint* AbilityBlueprint = LoadObject<UBlueprint>(nullptr, *AbilityPath);
		UGameplayAbility* AbilityDefaults =
			AbilityBlueprint && AbilityBlueprint->GeneratedClass ? Cast<UGameplayAbility>(AbilityBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		TestTrue(TEXT("Ability CDO 保存真实 AssetTag"), AbilityDefaults && AbilityDefaults->GetAssetTags().HasTagExact(FGameplayTag::RequestGameplayTag(*AbilityTagName, false)));

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), EffectName);
			Args->SetStringField(TEXT("packagePath"), PackagePath);
			Args->SetStringField(TEXT("durationPolicy"), TEXT("Instant"));
			Execute(TEXT("create_effect"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("effectPath"), EffectPath);
			Args->SetStringField(TEXT("attribute"), SetName + TEXT(".Health"));
			Args->SetStringField(TEXT("operation"), TEXT("Additive"));
			Args->SetNumberField(TEXT("magnitude"), -25.0);
			Execute(TEXT("set_effect_modifier"), Args);
		}
		UBlueprint* EffectBlueprint = LoadObject<UBlueprint>(nullptr, *EffectPath);
		UGameplayEffect* EffectDefaults = EffectBlueprint && EffectBlueprint->GeneratedClass ? Cast<UGameplayEffect>(EffectBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		TestTrue(TEXT("GameplayEffect CDO 保存真实 Modifier"), EffectDefaults && EffectDefaults->Modifiers.Num() == 1 && EffectDefaults->Modifiers[0].Attribute.IsValid());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), CueName);
			Args->SetStringField(TEXT("packagePath"), PackagePath);
			Args->SetStringField(TEXT("cueType"), TEXT("burst"));
			Execute(TEXT("create_cue"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), ActorPath);
			Args->SetStringField(TEXT("attributeSet"), SetPath);
			Args->SetStringField(TEXT("componentName"), TEXT("AbilitySystem"));
			Execute(TEXT("set_asc_defaults"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), ActorPath);
			const FString Result = Execute(TEXT("get_info"), Args);
			TestTrue(TEXT("ASC 信息包含 AttributeSet"), Result.Contains(SetName));
		}

		ActorBlueprint = LoadObject<UBlueprint>(nullptr, *ActorPath);
		AActor* Actor = ActorBlueprint && ActorBlueprint->GeneratedClass ? World->SpawnActor<AActor>(ActorBlueprint->GeneratedClass, FTransform::Identity) : nullptr;
		TestNotNull(TEXT("GAS Actor 实例创建成功"), Actor);
		const FString ActorLabel = TEXT("MCP_GAS_") + Suffix;
		if (Actor)
			Actor->SetActorLabel(ActorLabel);
		UAbilitySystemComponent* RuntimeASC = Actor ? Actor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
		TestNotNull(TEXT("实例包含真实 AbilitySystemComponent"), RuntimeASC);

		if (Actor && RuntimeASC)
		{
			auto ForActor = [&ActorLabel]()
			{
				TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("actorLabel"), ActorLabel);
				Args->SetStringField(TEXT("world"), TEXT("editor"));
				return Args;
			};
			{
				TSharedRef<FJsonObject> Args = ForActor();
				Args->SetStringField(TEXT("attributeSet"), SetPath);
				Execute(TEXT("init_asc"), Args);
			}
			{
				TSharedRef<FJsonObject> Args = ForActor();
				Args->SetStringField(TEXT("attribute"), TEXT("Health"));
				Args->SetNumberField(TEXT("value"), 100.0);
				const FString Result = Execute(TEXT("set_attribute"), Args);
				TestTrue(TEXT("Health BaseValue 写入 100"), Result.Contains(TEXT("\"baseValue\":100")));
			}
			{
				TSharedRef<FJsonObject> Args = ForActor();
				Args->SetStringField(TEXT("attribute"), TEXT("Health"));
				Execute(TEXT("get_attribute"), Args);
			}
			{
				TSharedRef<FJsonObject> Args = ForActor();
				Args->SetStringField(TEXT("effectClass"), EffectPath);
				Args->SetNumberField(TEXT("level"), 1.0);
				Execute(TEXT("apply_effect"), Args);
			}
			{
				TSharedRef<FJsonObject> Args = ForActor();
				Args->SetStringField(TEXT("attribute"), TEXT("Health"));
				const FString Result = Execute(TEXT("get_attribute"), Args);
				TestTrue(TEXT("Instant GameplayEffect 将 Health 从 100 改为 75"), Result.Contains(TEXT("\"currentValue\":75")));
			}
			Execute(TEXT("get_asc_state"), ForActor());
		}

		if (Actor)
			World->EditorDestroyActor(Actor, false);
		TArray<UObject*> Assets;
		for (const FString& Path : { ActorPath, SetPath, AbilityPath, EffectPath, CuePath })
		{
			if (UObject* Asset = LoadObject<UObject>(nullptr, *Path))
				Assets.Add(Asset);
		}
		if (!Assets.IsEmpty())
			ObjectTools::DeleteObjectsUnchecked(Assets);
		if (WorldPackage)
			WorldPackage->SetDirtyFlag(bWorldWasDirty);
		return true;
	}
}

#endif
