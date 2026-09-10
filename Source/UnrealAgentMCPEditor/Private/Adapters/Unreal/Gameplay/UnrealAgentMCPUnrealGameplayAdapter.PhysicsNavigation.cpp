// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.PhysicsNavigation.cpp
 * @brief 场景物理、碰撞、导航查询、导航体积与 Recast 信息实现。
 */

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.h"

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.Internal.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "NavModifierVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavigationInvokerComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace UnrealAgentMCP
{
	using namespace GameplayPrivate;

	namespace
	{
		UPrimitiveComponent* FindPrimitive(AActor* Actor)
		{
			if (!Actor)
				return nullptr;
			if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
				return Root;
			return Actor->FindComponentByClass<UPrimitiveComponent>();
		}

		ECollisionEnabled::Type CollisionMode(const FString& Text)
		{
			if (Text.Equals(TEXT("NoCollision"), ESearchCase::IgnoreCase))
				return ECollisionEnabled::NoCollision;
			if (Text.Equals(TEXT("QueryOnly"), ESearchCase::IgnoreCase))
				return ECollisionEnabled::QueryOnly;
			if (Text.Equals(TEXT("PhysicsOnly"), ESearchCase::IgnoreCase))
				return ECollisionEnabled::PhysicsOnly;
			return ECollisionEnabled::QueryAndPhysics;
		}
	}

	FString FUnrealAgentMCPUnrealGameplayAdapter::PhysicsAndNavigation(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action.StartsWith(TEXT("set_collision")) || Action == TEXT("set_simulate_physics") || Action == TEXT("set_physics_properties") || Action == TEXT("add_impulse"))
		{
			AActor* Actor = FindActor(StringArg(Args, { TEXT("actorLabel"), TEXT("actorName") }));
			UPrimitiveComponent* Primitive = FindPrimitive(Actor);
			if (!Primitive)
				return Error(TEXT("找不到 Actor 的 PrimitiveComponent。"));
			Primitive->Modify();
			if (Action == TEXT("set_collision_profile"))
			{
				Primitive->SetCollisionProfileName(*StringArg(Args, { TEXT("profileName") }, TEXT("BlockAll")));
			}
			else if (Action == TEXT("set_collision_enabled"))
			{
				Primitive->SetCollisionEnabled(CollisionMode(StringArg(Args, { TEXT("collisionEnabled") }, TEXT("QueryAndPhysics"))));
			}
			else if (Action == TEXT("set_collision"))
			{
				Primitive->SetCollisionProfileName(*StringArg(Args, { TEXT("profileName") }, Primitive->GetCollisionProfileName().ToString()));
				Primitive->SetCollisionEnabled(CollisionMode(StringArg(Args, { TEXT("collisionEnabled") }, TEXT("QueryAndPhysics"))));
			}
			else if (Action == TEXT("set_simulate_physics"))
				Primitive->SetSimulatePhysics(BoolArg(Args, TEXT("simulate"), true));
			else if (Action == TEXT("add_impulse"))
				Primitive->AddImpulse(VectorArg(Args, TEXT("impulse"), FVector(0, 0, 1000)), NAME_None, BoolArg(Args, TEXT("velocityChange"), false));
			else
			{
				Primitive->SetMassOverrideInKg(NAME_None, static_cast<float>(NumberArg(Args, TEXT("mass"), Primitive->GetMass())), true);
				Primitive->SetLinearDamping(static_cast<float>(NumberArg(Args, TEXT("linearDamping"), Primitive->GetLinearDamping())));
				Primitive->SetAngularDamping(static_cast<float>(NumberArg(Args, TEXT("angularDamping"), Primitive->GetAngularDamping())));
				Primitive->SetEnableGravity(BoolArg(Args, TEXT("enableGravity"), Primitive->IsGravityEnabled()));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
			Result->SetStringField(TEXT("component"), Primitive->GetName());
			return Success(Result);
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		UNavigationSystemV1* Navigation = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!World || !Navigation)
			return Error(TEXT("当前编辑器世界没有 NavigationSystemV1。"));
		if (Action == TEXT("rebuild_navigation"))
		{
			Navigation->Build();
			return Success(MakeShared<FJsonObject>());
		}
		if (Action == TEXT("find_nav_path"))
		{
			UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, VectorArg(Args, TEXT("start")), VectorArg(Args, TEXT("end")),
				FindActor(StringArg(Args, { TEXT("pathfindingContext") })));
			if (!Path)
				return Error(TEXT("导航路径查询失败。"));
			TArray<TSharedPtr<FJsonValue>> Points;
			for (const FVector& Point : Path->PathPoints)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetNumberField(TEXT("x"), Point.X);
				Item->SetNumberField(TEXT("y"), Point.Y);
				Item->SetNumberField(TEXT("z"), Point.Z);
				Points.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("valid"), Path->IsValid());
			Result->SetBoolField(TEXT("partial"), Path->IsPartial());
			Result->SetArrayField(TEXT("points"), Points);
			return Success(Result);
		}
		if (Action == TEXT("project_to_nav"))
		{
			FNavLocation Projected;
			const bool bProjected = Navigation->ProjectPointToNavigation(VectorArg(Args, TEXT("location")), Projected, VectorArg(Args, TEXT("extent"), FVector(100)));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("projected"), bProjected);
			Result->SetNumberField(TEXT("x"), Projected.Location.X);
			Result->SetNumberField(TEXT("y"), Projected.Location.Y);
			Result->SetNumberField(TEXT("z"), Projected.Location.Z);
			return Success(Result);
		}
		if (Action == TEXT("list_nav_invokers"))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (UNavigationInvokerComponent* Component = It->FindComponentByClass<UNavigationInvokerComponent>())
				{
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("actor"), It->GetActorLabel());
					Item->SetNumberField(TEXT("generationRadius"), Component->GetGenerationRadius());
					Item->SetNumberField(TEXT("removalRadius"), Component->GetRemovalRadius());
					Values.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("invokers"), Values);
			return Success(Result);
		}
		if (Action == TEXT("spawn_nav_modifier"))
		{
			ANavModifierVolume* Volume = World->SpawnActor<ANavModifierVolume>(VectorArg(Args, TEXT("location")), FRotator::ZeroRotator);
			if (!Volume)
				return Error(TEXT("创建 NavModifierVolume 失败。"));
			Volume->SetActorLabel(StringArg(Args, { TEXT("label") }, TEXT("MCP_NavModifier")));
			Volume->SetActorScale3D(VectorArg(Args, TEXT("extent"), FVector(100)) / 100.0);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), Volume->GetActorLabel());
			return Success(Result);
		}

		const ANavigationData* Data = Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("navigationData"), Data ? Data->GetPathName() : TEXT("None"));
		if (Recast)
		{
			Result->SetNumberField(TEXT("cellSize"), Recast->GetCellSize(ENavigationDataResolution::Default));
			Result->SetNumberField(TEXT("cellHeight"), Recast->GetCellHeight(ENavigationDataResolution::Default));
			Result->SetNumberField(TEXT("agentRadius"), Recast->GetConfig().AgentRadius);
			Result->SetNumberField(TEXT("agentHeight"), Recast->GetConfig().AgentHeight);
		}
		return Success(Result);
	}
}
