// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPLevelSpatialMigrationTests.cpp
 * @brief Level 空间、组件、Spline、实例网格与落地吸附的真实黑盒。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"
#include "Application/Domains/Level/UnrealAgentMCPLevelService.h"
#include "Application/Ports/UnrealAgentMCPLevelPort.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPLevelSpatialMigrationIntegrationTest, "WorldData.UnrealAgent.Level.SpatialActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPLevelSpatialMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!TestNotNull(TEXT("编辑器测试世界可用"), World))
			return false;
		UPackage* WorldPackage = World->GetOutermost();
		const bool bWorldWasDirty = WorldPackage && WorldPackage->IsDirty();
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!TestNotNull(TEXT("引擎 Cube 资产可用"), Cube))
			return false;

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString GroundLabel = TEXT("MCP_LevelGround_") + Suffix;
		const FString ActorLabel = TEXT("MCP_LevelSpatial_") + Suffix;
		const FString TargetLabel = TEXT("MCP_LevelTarget_") + Suffix;
		AStaticMeshActor* Ground = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(0, 0, -50), FVector(20, 20, 1)));
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(500, 0, 500)));
		AStaticMeshActor* Target = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(500, 500, 500)));
		if (!TestNotNull(TEXT("测试地面创建成功"), Ground) || !TestNotNull(TEXT("测试 Actor 创建成功"), Actor) || !TestNotNull(TEXT("朝向目标创建成功"), Target))
		{
			return false;
		}
		Ground->SetActorLabel(GroundLabel);
		Actor->SetActorLabel(ActorLabel);
		Target->SetActorLabel(TargetLabel);
		Ground->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Target->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Ground->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Ground->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
		Ground->GetStaticMeshComponent()->RecreatePhysicsState();
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Target->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		World->UpdateWorldComponents(false, false);
		TArray<TSharedPtr<FJsonValue>> IgnoredSceneActors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* ExistingActor = *It;
			if (!ExistingActor || ExistingActor == Ground)
				continue;
			IgnoredSceneActors.Add(MakeShared<FJsonValueString>(ExistingActor->GetActorLabel()));
		}

		TSharedRef<IUnrealAgentMCPLevelPort> Port = MakeShared<FUnrealAgentMCPUnrealLevelAdapter>();
		FUnrealAgentMCPLevelService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1800)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto ForActor = [&ActorLabel]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("actorLabel"), ActorLabel);
			Args->SetStringField(TEXT("world"), TEXT("editor"));
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("targetActor"), TargetLabel);
			Execute(TEXT("aim_actor_at"), Args);
			TestTrue(TEXT("Actor +X 已朝向目标"), Actor->GetActorForwardVector().Y > 0.99);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Point = MakeShared<FJsonObject>();
			Point->SetNumberField(TEXT("x"), 0);
			Point->SetNumberField(TEXT("y"), 0);
			Point->SetNumberField(TEXT("z"), 100);
			Args->SetObjectField(TEXT("point"), Point);
			Execute(TEXT("nav_project_point"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentClass"), TEXT("SplineComponent"));
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_Spline"));
			Execute(TEXT("add_component"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentClass"), TEXT("HierarchicalInstancedStaticMeshComponent"));
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_HISMC"));
			Execute(TEXT("add_component"), Args);
		}
		USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>();
		UInstancedStaticMeshComponent* ISMC = Actor->FindComponentByClass<UInstancedStaticMeshComponent>();
		TestNotNull(TEXT("真实 SplineComponent 已添加"), Spline);
		TestNotNull(TEXT("真实 HISMC 已添加"), ISMC);

		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_HISMC"));
			Args->SetStringField(TEXT("propertyName"), TEXT("StaticMesh"));
			Args->SetStringField(TEXT("value"), TEXT("/Engine/BasicShapes/Cube.Cube"));
			Execute(TEXT("set_component_property"), Args);
			TestTrue(TEXT("HISMC StaticMesh 已写入"), ISMC && ISMC->GetStaticMesh() == Cube);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_HISMC"));
			Args->SetBoolField(TEXT("includeValues"), true);
			Args->SetArrayField(TEXT("propertyNames"), { MakeShared<FJsonValueString>(TEXT("StaticMesh")) });
			Execute(TEXT("get_component_details"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("componentClass"), TEXT("SplineComponent"));
			const FString Result = Execute(TEXT("get_actors_by_component_class"), Args);
			TestTrue(TEXT("组件类查询包含测试 Actor"), Result.Contains(ActorLabel));
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_Spline"));
			TArray<TSharedPtr<FJsonValue>> Points;
			for (const FVector Location : { FVector(500, 0, 100), FVector(600, 100, 120), FVector(700, 0, 100) })
			{
				TSharedRef<FJsonObject> Point = MakeShared<FJsonObject>();
				Point->SetNumberField(TEXT("x"), Location.X);
				Point->SetNumberField(TEXT("y"), Location.Y);
				Point->SetNumberField(TEXT("z"), Location.Z);
				Points.Add(MakeShared<FJsonValueObject>(Point));
			}
			Args->SetArrayField(TEXT("points"), Points);
			Args->SetBoolField(TEXT("closedLoop"), false);
			Execute(TEXT("set_spline_points"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_Spline"));
			const FString Result = Execute(TEXT("get_spline_info"), Args);
			TestTrue(TEXT("Spline 返回三个真实点"), Result.Contains(TEXT("\"points\":[{")));
			TestEqual(TEXT("Spline 点数量正确"), Spline ? Spline->GetNumberOfSplinePoints() : 0, 3);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("materialPath"), TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			Args->SetNumberField(TEXT("slotIndex"), 0);
			Execute(TEXT("set_actor_material"), Args);
		}
		Execute(TEXT("read_actor_motion"), ForActor());

		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_HISMC"));
			TArray<TSharedPtr<FJsonValue>> Transforms;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				TSharedRef<FJsonObject> Transform = MakeShared<FJsonObject>();
				TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
				Location->SetNumberField(TEXT("x"), 800 + Index * 120);
				Location->SetNumberField(TEXT("y"), 0);
				Location->SetNumberField(TEXT("z"), 50);
				Transform->SetObjectField(TEXT("location"), Location);
				Transforms.Add(MakeShared<FJsonValueObject>(Transform));
			}
			Args->SetArrayField(TEXT("transforms"), Transforms);
			Args->SetBoolField(TEXT("worldSpace"), true);
			Execute(TEXT("add_hismc_instances"), Args);
			TestEqual(TEXT("HISMC 添加两个实例"), ISMC ? ISMC->GetInstanceCount() : 0, 2);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_HISMC"));
			Execute(TEXT("get_instance_transforms"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_HISMC"));
			Args->SetNumberField(TEXT("index"), 0);
			TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
			Location->SetNumberField(TEXT("x"), 900);
			Location->SetNumberField(TEXT("y"), 200);
			Location->SetNumberField(TEXT("z"), 50);
			Args->SetObjectField(TEXT("location"), Location);
			Execute(TEXT("update_instance_transform"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), TEXT("MCP_HISMC"));
			Args->SetNumberField(TEXT("index"), 1);
			Execute(TEXT("remove_instance"), Args);
			TestEqual(TEXT("HISMC 移除后剩一个实例"), ISMC ? ISMC->GetInstanceCount() : 0, 1);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Start = MakeShared<FJsonObject>();
			Start->SetNumberField(TEXT("x"), 0);
			Start->SetNumberField(TEXT("y"), 0);
			Start->SetNumberField(TEXT("z"), 1000);
			TSharedRef<FJsonObject> End = MakeShared<FJsonObject>();
			End->SetNumberField(TEXT("x"), 0);
			End->SetNumberField(TEXT("y"), 0);
			End->SetNumberField(TEXT("z"), -1000);
			Args->SetObjectField(TEXT("start"), Start);
			Args->SetObjectField(TEXT("end"), End);
			Args->SetArrayField(TEXT("ignoreActors"), IgnoredSceneActors);
			const FString Result = Execute(TEXT("line_trace"), Args);
			TestTrue(TEXT("射线命中真实地面"), Result.Contains(GroundLabel));
		}
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetNumberField(TEXT("maxDistance"), 10000);
			Args->SetArrayField(TEXT("ignoreActors"), IgnoredSceneActors);
			const FString SnapResult = Execute(TEXT("snap_actor_to_floor"), Args);
			FVector SnappedOrigin;
			FVector SnappedExtent;
			Actor->GetActorBounds(false, SnappedOrigin, SnappedExtent, true);
			AddInfo(FString::Printf(TEXT("贴地诊断：Result=%s，ActorZ=%.3f，BoundsBottom=%.3f"), *SnapResult, Actor->GetActorLocation().Z, SnappedOrigin.Z - SnappedExtent.Z));
			TestTrue(TEXT("贴地命中指定测试地面"), SnapResult.Contains(GroundLabel));
			TestTrue(TEXT("Actor 底面吸附到地面"), FMath::IsNearlyEqual(SnappedOrigin.Z - SnappedExtent.Z, 0.0, 1.0));
		}
		for (const FString ComponentName : { TEXT("MCP_Spline"), TEXT("MCP_HISMC") })
		{
			TSharedRef<FJsonObject> Args = ForActor();
			Args->SetStringField(TEXT("componentName"), ComponentName);
			Execute(TEXT("remove_component"), Args);
		}

		World->EditorDestroyActor(Target, false);
		World->EditorDestroyActor(Actor, false);
		World->EditorDestroyActor(Ground, false);
		if (WorldPackage)
			WorldPackage->SetDirtyFlag(bWorldWasDirty);
		return true;
	}
}

#endif
