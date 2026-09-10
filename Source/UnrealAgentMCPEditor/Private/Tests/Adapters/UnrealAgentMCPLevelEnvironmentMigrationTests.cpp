// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPLevelEnvironmentMigrationTests.cpp
 * @brief Level 环境、资产、分区和批量操作的真实编辑器黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"
#include "Application/Domains/Level/UnrealAgentMCPLevelService.h"
#include "Application/Ports/UnrealAgentMCPLevelPort.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		TSharedRef<FJsonObject> MakeVector(const double X, const double Y, const double Z)
		{
			TSharedRef<FJsonObject> Vector = MakeShared<FJsonObject>();
			Vector->SetNumberField(TEXT("x"), X);
			Vector->SetNumberField(TEXT("y"), Y);
			Vector->SetNumberField(TEXT("z"), Z);
			return Vector;
		}

		void DestroyActorsWithPrefix(UWorld* World, const FString& Prefix)
		{
			if (!World)
			{
				return;
			}
			TArray<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->GetActorLabel().StartsWith(Prefix))
				{
					Actors.Add(*It);
				}
			}
			for (AActor* Actor : Actors)
			{
				World->EditorDestroyActor(Actor, false);
			}
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLevelEnvironmentMigrationIntegrationTest, "WorldData.UnrealAgent.Level.EnvironmentActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLevelEnvironmentMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
		{
			return false;
		}
		UPackage* WorldPackage = World->GetOutermost();
		const bool bWorldWasDirty = WorldPackage && WorldPackage->IsDirty();
		const FString Prefix = TEXT("MCP_LevelEnvironment_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);

		TSharedRef<IUnrealAgentMCPLevelPort> Port = MakeShared<FUnrealAgentMCPUnrealLevelAdapter>();
		FUnrealAgentMCPLevelService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1600)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		const FString LightLabel = Prefix + TEXT("_Light");
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("lightType"), TEXT("point"));
			Args->SetStringField(TEXT("label"), LightLabel);
			Args->SetObjectField(TEXT("location"), MakeVector(100, 200, 500));
			Args->SetNumberField(TEXT("intensity"), 1800);
			Execute(TEXT("spawn_light"), Args);
		}
		AActor* Light = ActorSupport::FindActorByNameOrLabel(World, LightLabel);
		TestNotNull(TEXT("点光源已真实创建"), Light);
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), LightLabel);
			Args->SetNumberField(TEXT("intensity"), 2400);
			Args->SetNumberField(TEXT("attenuationRadius"), 1600);
			Execute(TEXT("set_light_properties"), Args);
			ULightComponent* Component = Light ? Light->FindComponentByClass<ULightComponent>() : nullptr;
			TestTrue(TEXT("点光源强度已更新"), Component && FMath::IsNearlyEqual(Component->Intensity, 2400.0f));
		}

		const FString FogLabel = Prefix + TEXT("_Fog");
		AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>();
		if (Fog)
		{
			Fog->SetActorLabel(FogLabel);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), FogLabel);
			Args->SetNumberField(TEXT("fogDensity"), 0.025);
			Args->SetNumberField(TEXT("startDistance"), 125);
			Execute(TEXT("set_fog_properties"), Args);
			TestTrue(TEXT("高度雾密度已更新"), Fog && Fog->GetComponent() && FMath::IsNearlyEqual(Fog->GetComponent()->FogDensity, 0.025f));
		}

		const FString VolumeLabel = Prefix + TEXT("_PostProcess");
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("volumeType"), TEXT("PostProcessVolume"));
			Args->SetStringField(TEXT("label"), VolumeLabel);
			Args->SetObjectField(TEXT("location"), MakeVector(500, 500, 300));
			Args->SetObjectField(TEXT("extent"), MakeVector(200, 220, 240));
			Execute(TEXT("spawn_volume"), Args);
		}
		APostProcessVolume* Volume = Cast<APostProcessVolume>(ActorSupport::FindActorByNameOrLabel(World, VolumeLabel));
		TestNotNull(TEXT("后处理体积已真实创建"), Volume);
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			Properties->SetBoolField(TEXT("bUnbound"), true);
			Args->SetObjectField(TEXT("properties"), Properties);
			Execute(TEXT("set_volume_properties"), Args);
			TestTrue(TEXT("后处理体积已设为无限范围"), Volume && Volume->bUnbound);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("volumeType"), TEXT("PostProcess"));
			const FString Result = Execute(TEXT("list_volumes"), Args);
			TestTrue(TEXT("体积列表包含测试体积"), Result.Contains(VolumeLabel));
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), VolumeLabel);
			Args->SetStringField(TEXT("materialPath"), TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			Args->SetNumberField(TEXT("weight"), 0.5);
			Execute(TEXT("add_post_process_blendable"), Args);
			TestTrue(TEXT("后处理混合对象已加入"), Volume && !Volume->Settings.WeightedBlendables.Array.IsEmpty());
		}

		Execute(TEXT("get_runtime_virtual_texture_summary"), MakeShared<FJsonObject>());
		Execute(TEXT("get_world_settings"), MakeShared<FJsonObject>());
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetNumberField(TEXT("killZ"), Settings->KillZ);
			Args->SetNumberField(TEXT("globalGravityZ"), Settings->GlobalGravityZ);
			Args->SetBoolField(TEXT("enableWorldBoundsChecks"), Settings->bEnableWorldBoundsChecks);
			Execute(TEXT("set_world_settings"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), TEXT("/Engine/BasicShapes/Cube.Cube"));
			Execute(TEXT("get_nanite_info"), Args);
		}

		const FString SkeletalLabel = Prefix + TEXT("_Skeletal");
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("skeletalMesh"), TEXT("/Engine/EditorMeshes/SkeletalMesh/DefaultSkeletalMesh.DefaultSkeletalMesh"));
			Args->SetStringField(TEXT("label"), SkeletalLabel);
			Args->SetObjectField(TEXT("location"), MakeVector(800, 0, 100));
			Execute(TEXT("spawn_skeletal_mesh_actor"), Args);
			TestNotNull(TEXT("骨骼网格 Actor 已真实创建"), ActorSupport::FindActorByNameOrLabel(World, SkeletalLabel));
		}

		DestroyActorsWithPrefix(World, Prefix);
		if (WorldPackage)
		{
			WorldPackage->SetDirtyFlag(bWorldWasDirty);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLevelPartitionBatchMigrationIntegrationTest, "WorldData.UnrealAgent.Level.PartitionBatchActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLevelPartitionBatchMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
		{
			return false;
		}
		UPackage* WorldPackage = World->GetOutermost();
		const bool bWorldWasDirty = WorldPackage && WorldPackage->IsDirty();
		const FString Prefix = TEXT("MCP_LevelBatch_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);

		TSharedRef<IUnrealAgentMCPLevelPort> Port = MakeShared<FUnrealAgentMCPUnrealLevelAdapter>();
		FUnrealAgentMCPLevelService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1800)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		const FString CurrentLevelResult = Execute(TEXT("get_current_edit_level"), MakeShared<FJsonObject>());
		const FString CurrentPackage = World->GetCurrentLevel()->GetOutermost()->GetName();
		TestTrue(TEXT("当前编辑关卡路径正确"), CurrentLevelResult.Contains(CurrentPackage));
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("levelPath"), CurrentPackage);
			Execute(TEXT("set_current_edit_level"), Args);
		}
		Execute(TEXT("list_streaming_sublevels"), MakeShared<FJsonObject>());

		if (World->GetWorldPartition())
		{
			TSharedRef<FJsonObject> ListArgs = MakeShared<FJsonObject>();
			ListArgs->SetNumberField(TEXT("limit"), 3);
			Execute(TEXT("list_actor_descs"), ListArgs);

			TSharedRef<FJsonObject> LoadArgs = MakeShared<FJsonObject>();
			LoadArgs->SetStringField(TEXT("mode"), TEXT("pin"));
			LoadArgs->SetNumberField(TEXT("maxActors"), 2);
			LoadArgs->SetBoolField(TEXT("dryRun"), true);
			Execute(TEXT("load_actor_descs"), LoadArgs);
		}

		TArray<FString> GridLabels;
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("staticMesh"), TEXT("/Engine/BasicShapes/Cube.Cube"));
			Args->SetObjectField(TEXT("min"), MakeVector(1000, 1000, 100));
			Args->SetObjectField(TEXT("max"), MakeVector(1300, 1300, 100));
			Args->SetNumberField(TEXT("countX"), 2);
			Args->SetNumberField(TEXT("countY"), 2);
			Args->SetNumberField(TEXT("countZ"), 1);
			Args->SetStringField(TEXT("labelPrefix"), Prefix);
			const FString Result = Execute(TEXT("spawn_grid"), Args);
			TestTrue(TEXT("网格生成四个 Actor"), Result.Contains(TEXT("\"count\":4")));
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().StartsWith(Prefix))
			{
				GridLabels.Add(It->GetActorLabel());
			}
		}
		TestEqual(TEXT("真实网格 Actor 数量正确"), GridLabels.Num(), 4);

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetObjectField(TEXT("offset"), MakeVector(10, 20, 30));
			TArray<TSharedPtr<FJsonValue>> Labels;
			for (const FString& Label : GridLabels)
			{
				Labels.Add(MakeShared<FJsonValueString>(Label));
			}
			Args->SetArrayField(TEXT("actorLabels"), Labels);
			const FString Result = Execute(TEXT("batch_translate"), Args);
			TestTrue(TEXT("批量平移数量正确"), Result.Contains(TEXT("\"count\":4")));
		}

		{
			TArray<TSharedPtr<FJsonValue>> Actors;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("staticMesh"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
				Row->SetStringField(TEXT("label"), FString::Printf(TEXT("%s_Batch_%d"), *Prefix, Index));
				Row->SetObjectField(TEXT("location"), MakeVector(1600 + Index * 150, 1000, 100));
				Actors.Add(MakeShared<FJsonValueObject>(Row));
			}
			TSharedRef<FJsonObject> InvalidRow = MakeShared<FJsonObject>();
			InvalidRow->SetStringField(TEXT("staticMesh"), TEXT("/Game/Missing/MCP_MissingMesh.MCP_MissingMesh"));
			Actors.Add(MakeShared<FJsonValueObject>(InvalidRow));

			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetArrayField(TEXT("actors"), Actors);
			const FString Result = Execute(TEXT("place_actors_batch"), Args);
			TestTrue(TEXT("批量放置成功数正确"), Result.Contains(TEXT("\"spawned\":2")));
			TestTrue(TEXT("丢失网格计数正确"), Result.Contains(TEXT("\"failedMesh\":1")));
		}

		DestroyActorsWithPrefix(World, Prefix);
		if (WorldPackage)
		{
			WorldPackage->SetDirtyFlag(bWorldWasDirty);
		}
		return true;
	}
}

#endif
