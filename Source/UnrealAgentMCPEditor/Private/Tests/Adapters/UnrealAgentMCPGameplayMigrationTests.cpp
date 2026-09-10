// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPGameplayMigrationTests.cpp
 * @brief Gameplay 五十七项物理、导航、输入、AI 与框架能力黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"
#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.h"
#include "Application/Domains/Gameplay/UnrealAgentMCPGameplayService.h"
#include "Application/Ports/UnrealAgentMCPGameplayPort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "ObjectTools.h"
#include "NavModifierVolume.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPGameplayMigrationIntegrationTest, "WorldData.UnrealAgent.Gameplay.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPGameplayMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPGameplayService::GetImplementedActions();
		TestEqual(TEXT("Gameplay action 数量"), Actions.Num(), 57);
		TSharedRef<IUnrealAgentMCPGameplayPort> Port = MakeShared<FUnrealAgentMCPUnrealGameplayAdapter>();
		FUnrealAgentMCPGameplayService Service(Port);
		TSet<FString> Invoked;
		auto Execute = [&Service, &Invoked, this](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Invoked.Add(Action);
			Args->SetStringField(TEXT("action"), Action);
			const FString Json = Service.Execute(Args);
			const bool bContract = Json.Contains(TEXT("\"success\":true")) || (Json.Contains(TEXT("\"success\":false")) && Json.Contains(TEXT("\"error\"")));
			TestTrue(*FString::Printf(TEXT("%s 返回标准结果：%s"), *Action, *Json.Left(600)), bContract);
			if (bExpectSuccess)
				TestTrue(*FString::Printf(TEXT("%s 执行成功"), *Action), Json.Contains(TEXT("\"success\":true")));
			return Json;
		};
		auto Empty = []()
		{
			return MakeShared<FJsonObject>();
		};
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		auto ObjectPath = [&Directory](const FString& Name)
		{
			return Directory + TEXT("/") + Name + TEXT(".") + Name;
		};
		const FString IAName = TEXT("IA_GP_") + Suffix, IMCName = TEXT("IMC_GP_") + Suffix;
		const FString BBName = TEXT("BB_GP_") + Suffix, BTName = TEXT("BT_GP_") + Suffix;
		const FString EQSName = TEXT("EQS_GP_") + Suffix, STName = TEXT("ST_GP_") + Suffix;
		const FString SOName = TEXT("SO_GP_") + Suffix, ActorBPName = TEXT("BP_GP_") + Suffix;
		const FString IAPath = ObjectPath(IAName), IMCPath = ObjectPath(IMCName), BBPath = ObjectPath(BBName);
		const FString BTPath = ObjectPath(BTName), SOPath = ObjectPath(SOName), ActorBPPath = ObjectPath(ActorBPName);
		TArray<FString> CreatedPaths{ IAPath, IMCPath, BBPath, BTPath, ObjectPath(EQSName), ObjectPath(STName), SOPath, ActorBPPath };

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		AStaticMeshActor* PhysicsActor = World ? World->SpawnActor<AStaticMeshActor>() : nullptr;
		const FString ActorLabel = TEXT("MCP_Gameplay_") + Suffix;
		if (PhysicsActor)
			PhysicsActor->SetActorLabel(ActorLabel);
		auto ForActor = [&ActorLabel]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), ActorLabel);
			return Args;
		};
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("profileName"), TEXT("BlockAll"));
			Execute(TEXT("set_collision_profile"), Args);
			Args = ForActor();
			Args->SetBoolField(TEXT("simulate"), false);
			Execute(TEXT("set_simulate_physics"), Args);
			Args = ForActor();
			TSharedRef<FJsonObject> Impulse = MakeShared<FJsonObject>();
			Impulse->SetNumberField(TEXT("z"), 10);
			Args->SetObjectField(TEXT("impulse"), Impulse);
			Execute(TEXT("add_impulse"), Args);
			Args = ForActor();
			Args->SetStringField(TEXT("collisionEnabled"), TEXT("QueryOnly"));
			Execute(TEXT("set_collision_enabled"), Args);
			Args->SetStringField(TEXT("profileName"), TEXT("BlockAll"));
			Execute(TEXT("set_collision"), Args);
			Args = ForActor();
			Args->SetNumberField(TEXT("mass"), 10);
			Args->SetBoolField(TEXT("enableGravity"), false);
			Execute(TEXT("set_physics_properties"), Args);
		}
		Execute(TEXT("rebuild_navigation"), Empty());
		{
			TSharedRef<FJsonObject> Args = Empty();
			TSharedRef<FJsonObject> Start = MakeShared<FJsonObject>();
			Start->SetNumberField(TEXT("z"), 100);
			TSharedRef<FJsonObject> End = MakeShared<FJsonObject>();
			End->SetNumberField(TEXT("x"), 100);
			End->SetNumberField(TEXT("z"), 100);
			Args->SetObjectField(TEXT("start"), Start);
			Args->SetObjectField(TEXT("end"), End);
			Execute(TEXT("find_nav_path"), Args);
			Args = Empty();
			Args->SetObjectField(TEXT("location"), Start);
			Execute(TEXT("project_to_nav"), Args);
			Args->SetStringField(TEXT("label"), TEXT("MCP_NavModifier_") + Suffix);
			Execute(TEXT("spawn_nav_modifier"), Args);
		}
		Execute(TEXT("list_nav_invokers"), Empty());
		Execute(TEXT("get_navmesh_info"), Empty());
		Execute(TEXT("get_navmesh_details"), Empty());

		auto Named = [&Directory](const FString& Name)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), Name);
			Args->SetStringField(TEXT("packagePath"), Directory);
			return Args;
		};
		{
			TSharedRef<FJsonObject> Args = Named(IAName);
			Args->SetStringField(TEXT("valueType"), TEXT("Axis2D"));
			Execute(TEXT("create_input_action"), Args);
		}
		Execute(TEXT("create_input_mapping"), Named(IMCName));
		Execute(TEXT("list_input_assets"), Empty());
		Execute(TEXT("get_applied_imcs"), Empty());
		auto ForIMC = [&IMCPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("imcPath"), IMCPath);
			return Args;
		};
		{
			TSharedRef<FJsonObject> Args = ForIMC();
			Args->SetStringField(TEXT("inputActionPath"), IAPath);
			Args->SetStringField(TEXT("key"), TEXT("SpaceBar"));
			Execute(TEXT("add_imc_mapping"), Args);
		}
		Execute(TEXT("read_imc"), ForIMC());
		Execute(TEXT("list_input_mappings"), ForIMC());
		{
			TSharedRef<FJsonObject> Args = ForIMC();
			Args->SetArrayField(TEXT("modifiers"), {});
			Args->SetArrayField(TEXT("triggers"), {});
			Execute(TEXT("set_mapping_modifiers"), Args);
			Args->SetStringField(TEXT("newKey"), TEXT("Enter"));
			Execute(TEXT("set_imc_mapping_key"), Args);
			Args->SetStringField(TEXT("newInputActionPath"), IAPath);
			Execute(TEXT("set_imc_mapping_action"), Args);
			Execute(TEXT("remove_imc_mapping"), Args);
		}

		Execute(TEXT("create_blackboard"), Named(BBName));
		auto ForBB = [&BBPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blackboardPath"), BBPath);
			return Args;
		};
		{
			TSharedRef<FJsonObject> Args = ForBB();
			Args->SetStringField(TEXT("keyName"), TEXT("Target"));
			Args->SetStringField(TEXT("keyType"), TEXT("Object"));
			Execute(TEXT("add_blackboard_key"), Args);
			Execute(TEXT("read_blackboard"), Args);
			Args->SetStringField(TEXT("parentPath"), TEXT("None"));
			Execute(TEXT("set_blackboard_parent"), Args);
			Execute(TEXT("remove_blackboard_key"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = Named(BTName);
			Args->SetStringField(TEXT("blackboardPath"), BBPath);
			Execute(TEXT("create_behavior_tree"), Args);
		}
		auto ForBT = [&BTPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("behaviorTreePath"), BTPath);
			Args->SetStringField(TEXT("assetPath"), BTPath);
			return Args;
		};
		{
			TSharedRef<FJsonObject> Args = ForBT();
			Args->SetStringField(TEXT("blackboardPath"), BBPath);
			Execute(TEXT("set_behavior_tree_blackboard"), Args);
		}
		Execute(TEXT("get_behavior_tree_info"), ForBT());
		Execute(TEXT("read_behavior_tree_graph"), ForBT());
		Execute(TEXT("list_behavior_trees"), Empty());
		Execute(TEXT("list_bt_node_classes"), Empty());
		Execute(TEXT("create_eqs_query"), Named(EQSName));
		Execute(TEXT("list_eqs_queries"), Empty());
		Execute(TEXT("create_state_tree"), Named(STName));
		Execute(TEXT("list_state_trees"), Empty());
		Execute(TEXT("get_state_tree_runtime"), Empty());
		Execute(TEXT("create_smart_object_def"), Named(SOName));
		auto ForSO = [&SOPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), SOPath);
			return Args;
		};
		Execute(TEXT("add_smart_object_slot"), ForSO());
		Execute(TEXT("list_smart_object_slots"), ForSO());
		Execute(TEXT("set_smart_object_slot"), ForSO());
		Execute(TEXT("add_smart_object_slot_behavior"), ForSO(), false);
		Execute(TEXT("remove_smart_object_slot"), ForSO());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), ActorBPPath);
			Args->SetStringField(TEXT("parentClass"), TEXT("Actor"));
			FUnrealAgentMCPUnrealBlueprintAdapter Setup;
			TestTrue(TEXT("创建 Gameplay 测试 Blueprint"), Setup.ExecuteAction(TEXT("create"), Args).Contains(TEXT("\"success\":true")));
		}
		auto ForBP = [&ActorBPPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), ActorBPPath);
			return Args;
		};
		Execute(TEXT("add_perception"), ForBP());
		Execute(TEXT("configure_sense"), ForBP(), false);
		Execute(TEXT("add_state_tree_component"), ForBP());
		Execute(TEXT("add_smart_object_component"), ForBP());

		TArray<FString> FrameworkNames;
		for (const TPair<FString, FString>& Pair : TArray<TPair<FString, FString>>{ { TEXT("create_game_mode"), TEXT("GM_GP_") + Suffix },
				 { TEXT("create_game_state"), TEXT("GS_GP_") + Suffix }, { TEXT("create_player_controller"), TEXT("PC_GP_") + Suffix },
				 { TEXT("create_player_state"), TEXT("PS_GP_") + Suffix }, { TEXT("create_hud"), TEXT("HUD_GP_") + Suffix } })
		{
			FrameworkNames.Add(Pair.Value);
			CreatedPaths.Add(ObjectPath(Pair.Value));
			Execute(Pair.Key, Named(Pair.Value));
		}
		UClass* PreviousGameMode = World && World->GetWorldSettings() ? World->GetWorldSettings()->DefaultGameMode : nullptr;
		{
			TSharedRef<FJsonObject> Args = Empty();
			Args->SetStringField(TEXT("gameModePath"), ObjectPath(FrameworkNames[0]));
			Execute(TEXT("set_world_game_mode"), Args);
		}
		Execute(TEXT("get_framework_info"), Empty());
		if (World && World->GetWorldSettings())
			World->GetWorldSettings()->DefaultGameMode = PreviousGameMode;

		TestEqual(TEXT("所有 Gameplay action 均已执行"), Invoked.Num(), Actions.Num());
		for (const FString& Action : Actions)
			TestTrue(*FString::Printf(TEXT("已覆盖 action：%s"), *Action), Invoked.Contains(Action));
		if (PhysicsActor)
			PhysicsActor->Destroy();
		if (World)
			for (TActorIterator<ANavModifierVolume> It(World); It; ++It)
				if (It->GetActorLabel() == TEXT("MCP_NavModifier_") + Suffix)
					It->Destroy();
		if (GEditor)
		{
			UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			TArray<UObject*> Created;
			for (const FString& Path : CreatedPaths)
				if (UObject* Asset = Assets ? Assets->LoadAsset(Path) : nullptr)
					Created.Add(Asset);
			if (!Created.IsEmpty())
				ObjectTools::DeleteObjectsUnchecked(Created);
		}
		return true;
	}
}

#endif
