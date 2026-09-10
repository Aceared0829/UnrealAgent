// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.Spatial.cpp
 * @brief Level 空间、组件、Spline、实例网格和射线操作实现。
 */

#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"

#include "Adapters/Tooling/UnrealAgentMCPTools.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Components/ActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	namespace
	{
		UWorld* ResolveSpatialWorld(const TSharedPtr<FJsonObject>& Args)
		{
			FString Scope = TEXT("editor");
			if (Args.IsValid())
				Args->TryGetStringField(TEXT("world"), Scope);
			Scope.ToLowerInline();
			if (GEngine && Scope != TEXT("editor"))
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World())
					{
						return Context.World();
					}
				}
			}
			return ActorSupport::GetEditorWorld();
		}

		AActor* ResolveSpatialActor(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			FString Name;
			if (!Args.IsValid() || !Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
			{
				OutError = TEXT("缺少必填 actorLabel。");
				return nullptr;
			}
			AActor* Actor = ActorSupport::FindActorByNameOrLabel(ResolveSpatialWorld(Args), Name);
			if (!Actor)
			{
				OutError = FString::Printf(TEXT("未找到 Actor：%s"), *Name);
			}
			return Actor;
		}

		UActorComponent* FindActorComponent(AActor* Actor, const FString& Name, UClass* RequiredBase = UActorComponent::StaticClass())
		{
			if (!Actor)
				return nullptr;
			TInlineComponentArray<UActorComponent*> Components(Actor);
			for (UActorComponent* Component : Components)
			{
				if (!IsValid(Component) || !Component->IsA(RequiredBase))
				{
					continue;
				}
				if (Name.IsEmpty() || Component->GetName().Equals(Name, ESearchCase::IgnoreCase) || Component->GetClass()->GetName().Equals(Name, ESearchCase::IgnoreCase))
				{
					return Component;
				}
			}
			return nullptr;
		}

		UClass* ResolveComponentClass(const FString& ClassText)
		{
			UClass* Result = FindObject<UClass>(nullptr, *ClassText);
			if (!Result)
				Result = LoadObject<UClass>(nullptr, *ClassText);
			if (!Result && !ClassText.Contains(TEXT("/")))
			{
				const FString EnginePath = TEXT("/Script/Engine.") + ClassText;
				Result = FindObject<UClass>(nullptr, *EnginePath);
			}
			if (!Result)
			{
				FString ShortName = ClassText;
				ShortName.RemoveFromStart(TEXT("U"));
				for (TObjectIterator<UClass> It; It; ++It)
				{
					UClass* Candidate = *It;
					FString CandidateName = Candidate->GetName();
					CandidateName.RemoveFromStart(TEXT("U"));
					if (CandidateName.Equals(ShortName, ESearchCase::IgnoreCase))
					{
						Result = Candidate;
						break;
					}
				}
			}
			return Result && Result->IsChildOf(UActorComponent::StaticClass()) && !Result->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated) ? Result : nullptr;
		}

		TSharedRef<FJsonObject> MakeLevelSpatialTransformJson(const FTransform& Transform)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Transform.GetLocation()));
			Result->SetObjectField(TEXT("rotation"), JsonConversion::MakeRotatorObject(Transform.Rotator()));
			Result->SetObjectField(TEXT("scale"), JsonConversion::MakeVectorObject(Transform.GetScale3D()));
			return Result;
		}

		TSharedRef<FJsonObject> MakeComponentJson(UActorComponent* Component, const bool bIncludeValues, const TArray<FString>& PropertyNames)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Component->GetName());
			Item->SetStringField(TEXT("class"), Component->GetClass()->GetPathName());
			Item->SetBoolField(TEXT("registered"), Component->IsRegistered());
			if (USceneComponent* Scene = Cast<USceneComponent>(Component))
			{
				Item->SetStringField(TEXT("attachParent"), Scene->GetAttachParent() ? Scene->GetAttachParent()->GetName() : FString());
				Item->SetObjectField(TEXT("relativeTransform"), MakeLevelSpatialTransformJson(Scene->GetRelativeTransform()));
				Item->SetObjectField(TEXT("worldTransform"), MakeLevelSpatialTransformJson(Scene->GetComponentTransform()));
			}
			if (bIncludeValues)
			{
				TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
				for (TFieldIterator<FProperty> It(Component->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
				{
					FProperty* Property = *It;
					if (!PropertyNames.IsEmpty() && !PropertyNames.Contains(Property->GetName()))
					{
						continue;
					}
					FString Text;
					Property->ExportText_InContainer(0, Text, Component, Component, Component, PPF_None);
					Values->SetStringField(Property->GetName(), Text);
				}
				Item->SetObjectField(TEXT("values"), Values);
			}
			return Item;
		}

		UInstancedStaticMeshComponent* ResolveISMC(AActor* Actor, const TSharedPtr<FJsonObject>& Args)
		{
			FString Name;
			Args->TryGetStringField(TEXT("componentName"), Name);
			return Cast<UInstancedStaticMeshComponent>(FindActorComponent(Actor, Name, UInstancedStaticMeshComponent::StaticClass()));
		}
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::AimActorAt(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		FVector Target;
		if (!JsonConversion::TryGetVectorField(Args, TEXT("targetPoint"), Target))
		{
			FString TargetActorName;
			Args->TryGetStringField(TEXT("targetActor"), TargetActorName);
			AActor* TargetActor = ActorSupport::FindActorByNameOrLabel(ResolveSpatialWorld(Args), TargetActorName);
			if (!TargetActor)
			{
				return ErrorJson(TEXT("必须提供 targetPoint 或有效 targetActor。"));
			}
			Target = TargetActor->GetActorLocation();
		}
		FRotator Rotation = (Target - Actor->GetActorLocation()).Rotation();
		double Roll = 0.0;
		Args->TryGetNumberField(TEXT("roll"), Roll);
		Rotation.Roll = Roll;
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "AimActorAt", "MCP 朝向目标"));
		Actor->Modify();
		Actor->SetActorRotation(Rotation);
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetObjectField(TEXT("targetPoint"), JsonConversion::MakeVectorObject(Target));
		Result->SetObjectField(TEXT("rotation"), JsonConversion::MakeRotatorObject(Rotation));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::NavProjectPoint(const TSharedPtr<FJsonObject>& Args)
	{
		FVector Point;
		if (!JsonConversion::TryGetVectorField(Args, TEXT("point"), Point))
		{
			return ErrorJson(TEXT("缺少必填 point。"));
		}
		FVector Extent(100.0);
		JsonConversion::TryGetVectorField(Args, TEXT("extent"), Extent);
		UWorld* World = ResolveSpatialWorld(Args);
		UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(World);
		FNavLocation Projected;
		const bool bProjected = Nav && Nav->ProjectPointToNavigation(Point, Projected, Extent);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("onNavMesh"), bProjected);
		Result->SetObjectField(TEXT("inputLocation"), JsonConversion::MakeVectorObject(Point));
		Result->SetObjectField(TEXT("projectedLocation"), JsonConversion::MakeVectorObject(bProjected ? Projected.Location : Point));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::AddComponent(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		FString ClassText;
		if (!Args->TryGetStringField(TEXT("componentClass"), ClassText) || ClassText.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 componentClass。"));
		}
		UClass* ComponentClass = ResolveComponentClass(ClassText);
		if (!ComponentClass)
			return ErrorJson(TEXT("未找到有效 Component 类。"));
		FString ComponentName;
		Args->TryGetStringField(TEXT("componentName"), ComponentName);
		if (ComponentName.IsEmpty())
			ComponentName = ComponentClass->GetName();
		if (FindActorComponent(Actor, ComponentName))
		{
			return ErrorJson(FString::Printf(TEXT("Component 名称已存在：%s"), *ComponentName));
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "AddLevelComponent", "MCP 添加实例组件"));
		Actor->Modify();
		UActorComponent* Component = NewObject<UActorComponent>(Actor, ComponentClass, *ComponentName, RF_Transactional);
		if (!Component)
			return ErrorJson(TEXT("Component 创建失败。"));
		if (USceneComponent* Scene = Cast<USceneComponent>(Component))
		{
			if (USceneComponent* Root = Actor->GetRootComponent())
				Scene->SetupAttachment(Root);
			else
				Actor->SetRootComponent(Scene);
		}
		Actor->AddInstanceComponent(Component);
		Component->OnComponentCreated();
		Component->RegisterComponent();
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeComponentJson(Component, false, {});
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::RemoveComponent(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		FString Name;
		if (!Args->TryGetStringField(TEXT("componentName"), Name) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 componentName。"));
		}
		UActorComponent* Component = FindActorComponent(Actor, Name);
		if (!Component)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Result->SetStringField(TEXT("componentName"), Name);
			Result->SetBoolField(TEXT("alreadyDeleted"), true);
			return SuccessJson(Result);
		}
		if (Component == Actor->GetRootComponent())
			return ErrorJson(TEXT("不能移除 Actor RootComponent。"));
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "RemoveLevelComponent", "MCP 移除实例组件"));
		Actor->Modify();
		Component->Modify();
		Actor->RemoveInstanceComponent(Component);
		Component->DestroyComponent();
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("componentName"), Name);
		Result->SetBoolField(TEXT("alreadyDeleted"), false);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetComponentProperty(const TSharedPtr<FJsonObject>& Args)
	{
		TSharedRef<FJsonObject> Normalized = MakeShared<FJsonObject>(*Args);
		FString ComponentName;
		FString PropertyName;
		Args->TryGetStringField(TEXT("componentName"), ComponentName);
		Args->TryGetStringField(TEXT("propertyName"), PropertyName);
		Normalized->SetStringField(TEXT("component"), ComponentName);
		Normalized->SetStringField(TEXT("property"), PropertyName);
		return Tools::SetActorProperty(Normalized);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetComponentDetails(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		FString Name;
		Args->TryGetStringField(TEXT("componentName"), Name);
		bool bIncludeValues = false;
		Args->TryGetBoolField(TEXT("includeValues"), bIncludeValues);
		TArray<FString> PropertyNames;
		Args->TryGetStringArrayField(TEXT("propertyNames"), PropertyNames);
		TArray<TSharedPtr<FJsonValue>> Values;
		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (UActorComponent* Component : Components)
		{
			if (!IsValid(Component) ||
				(!Name.IsEmpty() && !Component->GetName().Equals(Name, ESearchCase::IgnoreCase) && !Component->GetClass()->GetName().Equals(Name, ESearchCase::IgnoreCase)))
			{
				continue;
			}
			Values.Add(MakeShared<FJsonValueObject>(MakeComponentJson(Component, bIncludeValues, PropertyNames)));
		}
		if (!Name.IsEmpty() && Values.IsEmpty())
			return ErrorJson(TEXT("未找到目标 Component。"));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetNumberField(TEXT("componentCount"), Values.Num());
		Result->SetArrayField(TEXT("components"), Values);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetActorsByComponentClass(const TSharedPtr<FJsonObject>& Args)
	{
		FString ClassText;
		if (!Args->TryGetStringField(TEXT("componentClass"), ClassText) || ClassText.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 componentClass。"));
		}
		UClass* ComponentClass = ResolveComponentClass(ClassText);
		TArray<TSharedPtr<FJsonValue>> Actors;
		for (TActorIterator<AActor> It(ResolveSpatialWorld(Args)); It; ++It)
		{
			TArray<TSharedPtr<FJsonValue>> Matched;
			TInlineComponentArray<UActorComponent*> Components(*It);
			for (UActorComponent* Component : Components)
			{
				const bool bMatches = ComponentClass ? Component->IsA(ComponentClass) : Component->GetClass()->GetName().Contains(ClassText, ESearchCase::IgnoreCase);
				if (bMatches)
					Matched.Add(MakeShared<FJsonValueString>(Component->GetName()));
			}
			if (Matched.IsEmpty())
				continue;
			TSharedPtr<FJsonObject> Item = ActorSupport::MakeActorObject(*It);
			Item->SetArrayField(TEXT("matchedComponents"), Matched);
			Actors.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Actors.Num());
		Result->SetArrayField(TEXT("actors"), Actors);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetSplineInfo(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		FString Name;
		Args->TryGetStringField(TEXT("componentName"), Name);
		USplineComponent* Spline = Cast<USplineComponent>(FindActorComponent(Actor, Name, USplineComponent::StaticClass()));
		if (!Spline)
			return ErrorJson(TEXT("Actor 没有目标 SplineComponent。"));
		TArray<TSharedPtr<FJsonValue>> Points;
		for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetNumberField(TEXT("index"), Index);
			Item->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World)));
			Item->SetObjectField(TEXT("tangent"), JsonConversion::MakeVectorObject(Spline->GetTangentAtSplinePoint(Index, ESplineCoordinateSpace::World)));
			Item->SetStringField(TEXT("type"), UEnum::GetValueAsString(Spline->GetSplinePointType(Index)));
			Points.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("componentName"), Spline->GetName());
		Result->SetBoolField(TEXT("closedLoop"), Spline->IsClosedLoop());
		Result->SetNumberField(TEXT("length"), Spline->GetSplineLength());
		Result->SetArrayField(TEXT("points"), Points);
		FVector ProjectPoint;
		if (JsonConversion::TryGetVectorField(Args, TEXT("projectPoint"), ProjectPoint))
		{
			const float Key = Spline->FindInputKeyClosestToWorldLocation(ProjectPoint);
			Result->SetNumberField(TEXT("inputKey"), Key);
			Result->SetObjectField(TEXT("projectedLocation"), JsonConversion::MakeVectorObject(Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World)));
			Result->SetNumberField(TEXT("distanceAlongSpline"), Spline->GetDistanceAlongSplineAtSplineInputKey(Key));
		}
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetSplinePoints(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		FString Name;
		Args->TryGetStringField(TEXT("componentName"), Name);
		USplineComponent* Spline = Cast<USplineComponent>(FindActorComponent(Actor, Name, USplineComponent::StaticClass()));
		if (!Spline)
			return ErrorJson(TEXT("Actor 没有目标 SplineComponent。"));
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!Args->TryGetArrayField(TEXT("points"), Points) || !Points)
		{
			return ErrorJson(TEXT("缺少必填 points。"));
		}
		TArray<FVector> Locations;
		for (const TSharedPtr<FJsonValue>& Value : *Points)
		{
			FVector Location;
			if (!JsonConversion::TryValueToVector(Value, Location))
			{
				return ErrorJson(TEXT("points 包含无效 Vec3。"));
			}
			Locations.Add(Location);
		}
		bool bClosedLoop = false;
		Args->TryGetBoolField(TEXT("closedLoop"), bClosedLoop);
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetSplinePoints", "MCP 设置 Spline 点"));
		Spline->Modify();
		Spline->ClearSplinePoints(false);
		for (const FVector& Location : Locations)
		{
			Spline->AddSplinePoint(Location, ESplineCoordinateSpace::World, false);
		}
		Spline->SetClosedLoop(bClosedLoop, false);
		Spline->UpdateSpline();
		Spline->MarkPackageDirty();
		return GetSplineInfo(Args);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetActorMaterial(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		FString MaterialPath;
		if (!Args->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 materialPath。"));
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *JsonConversion::NormalizeAssetObjectPath(MaterialPath));
		if (!Material)
			return ErrorJson(TEXT("未找到 Material。"));
		double SlotValue = 0.0;
		Args->TryGetNumberField(TEXT("slotIndex"), SlotValue);
		const int32 SlotIndex = FMath::Max(0, int32(SlotValue));
		int32 Updated = 0;
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || SlotIndex >= Mesh->GetNumMaterials())
				continue;
			Mesh->Modify();
			Mesh->SetMaterial(SlotIndex, Material);
			++Updated;
		}
		if (Updated == 0)
			return ErrorJson(TEXT("Actor 没有可写入目标 Slot 的 MeshComponent。"));
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Result->SetNumberField(TEXT("slotIndex"), SlotIndex);
		Result->SetNumberField(TEXT("updatedComponents"), Updated);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ReadActorMotion(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<FString> Names;
		Args->TryGetStringArrayField(TEXT("actorLabels"), Names);
		if (Names.IsEmpty())
		{
			FString Name;
			if (Args->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
			{
				Names.Add(Name);
			}
		}
		if (Names.IsEmpty())
			return ErrorJson(TEXT("缺少 actorLabel 或 actorLabels。"));
		UWorld* World = ResolveSpatialWorld(Args);
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Name : Names)
		{
			AActor* Actor = ActorSupport::FindActorByNameOrLabel(World, Name);
			if (!Actor)
				continue;
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
			Item->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Actor->GetActorLocation()));
			Item->SetObjectField(TEXT("rotation"), JsonConversion::MakeRotatorObject(Actor->GetActorRotation()));
			Item->SetObjectField(TEXT("scale"), JsonConversion::MakeVectorObject(Actor->GetActorScale3D()));
			Item->SetObjectField(TEXT("velocity"), JsonConversion::MakeVectorObject(Actor->GetVelocity()));
			FVector AngularVelocity = FVector::ZeroVector;
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
			{
				AngularVelocity = Primitive->GetPhysicsAngularVelocityInDegrees();
			}
			Item->SetObjectField(TEXT("angularVelocity"), JsonConversion::MakeVectorObject(AngularVelocity));
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPActorMotion), true, Actor);
			const FVector Start = Actor->GetActorLocation();
			const bool bGrounded = World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, 200), ECC_Visibility, Query);
			Item->SetBoolField(TEXT("grounded"), bGrounded);
			Item->SetNumberField(TEXT("distanceToGround"), bGrounded ? Hit.Distance : -1.0);
			Values.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Values.Num());
		Result->SetArrayField(TEXT("actors"), Values);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::AddHISMCInstances(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, Args);
		if (!ISMC)
			return ErrorJson(TEXT("Actor 没有目标 ISMC/HISMC。"));
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Args->TryGetArrayField(TEXT("transforms"), Values) || !Values)
		{
			return ErrorJson(TEXT("缺少必填 transforms。"));
		}
		bool bWorldSpace = true;
		Args->TryGetBoolField(TEXT("worldSpace"), bWorldSpace);
		ISMC->Modify();
		TArray<TSharedPtr<FJsonValue>> Indices;
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid())
			{
				return ErrorJson(TEXT("transforms 包含无效对象。"));
			}
			FVector Location = FVector::ZeroVector;
			FRotator Rotation = FRotator::ZeroRotator;
			FVector Scale = FVector::OneVector;
			JsonConversion::TryGetVectorField(*Object, TEXT("location"), Location);
			JsonConversion::TryGetRotatorField(*Object, TEXT("rotation"), Rotation);
			JsonConversion::TryGetVectorField(*Object, TEXT("scale"), Scale);
			const FTransform Transform(Rotation, Location, Scale);
			const int32 Index = ISMC->AddInstance(Transform, bWorldSpace);
			Indices.Add(MakeShared<FJsonValueNumber>(Index));
		}
		ISMC->MarkRenderStateDirty();
		ISMC->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("componentName"), ISMC->GetName());
		Result->SetNumberField(TEXT("added"), Indices.Num());
		Result->SetNumberField(TEXT("instanceCount"), ISMC->GetInstanceCount());
		Result->SetArrayField(TEXT("indices"), Indices);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetInstanceTransforms(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, Args);
		if (!ISMC)
			return ErrorJson(TEXT("Actor 没有目标 ISMC/HISMC。"));
		bool bWorldSpace = true;
		Args->TryGetBoolField(TEXT("worldSpace"), bWorldSpace);
		TArray<TSharedPtr<FJsonValue>> Instances;
		for (int32 Index = 0; Index < ISMC->GetInstanceCount(); ++Index)
		{
			FTransform Transform;
			if (!ISMC->GetInstanceTransform(Index, Transform, bWorldSpace))
			{
				continue;
			}
			TSharedRef<FJsonObject> Item = MakeLevelSpatialTransformJson(Transform);
			Item->SetNumberField(TEXT("index"), Index);
			Instances.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("componentName"), ISMC->GetName());
		Result->SetArrayField(TEXT("instances"), Instances);
		Result->SetNumberField(TEXT("instanceCount"), Instances.Num());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::UpdateInstanceTransform(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, Args);
		if (!ISMC)
			return ErrorJson(TEXT("Actor 没有目标 ISMC/HISMC。"));
		double IndexValue = -1.0;
		if (!Args->TryGetNumberField(TEXT("index"), IndexValue))
			return ErrorJson(TEXT("缺少必填 index。"));
		const int32 Index = int32(IndexValue);
		bool bWorldSpace = true;
		Args->TryGetBoolField(TEXT("worldSpace"), bWorldSpace);
		FTransform Transform;
		if (!ISMC->GetInstanceTransform(Index, Transform, bWorldSpace))
		{
			return ErrorJson(TEXT("Instance index 越界。"));
		}
		FVector Location = Transform.GetLocation();
		FRotator Rotation = Transform.Rotator();
		FVector Scale = Transform.GetScale3D();
		JsonConversion::TryGetVectorField(Args, TEXT("location"), Location);
		JsonConversion::TryGetRotatorField(Args, TEXT("rotation"), Rotation);
		JsonConversion::TryGetVectorField(Args, TEXT("scale"), Scale);
		Transform = FTransform(Rotation, Location, Scale);
		ISMC->Modify();
		const bool bUpdated = ISMC->UpdateInstanceTransform(Index, Transform, bWorldSpace, true, true);
		if (!bUpdated)
			return ErrorJson(TEXT("Instance transform 更新失败。"));
		TSharedRef<FJsonObject> Result = MakeLevelSpatialTransformJson(Transform);
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetStringField(TEXT("componentName"), ISMC->GetName());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::RemoveInstance(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		UInstancedStaticMeshComponent* ISMC = ResolveISMC(Actor, Args);
		if (!ISMC)
			return ErrorJson(TEXT("Actor 没有目标 ISMC/HISMC。"));
		double IndexValue = -1.0;
		if (!Args->TryGetNumberField(TEXT("index"), IndexValue))
			return ErrorJson(TEXT("缺少必填 index。"));
		const int32 Index = int32(IndexValue);
		if (Index < 0 || Index >= ISMC->GetInstanceCount())
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("index"), Index);
			Result->SetBoolField(TEXT("alreadyDeleted"), true);
			return SuccessJson(Result);
		}
		ISMC->Modify();
		if (!ISMC->RemoveInstance(Index))
			return ErrorJson(TEXT("Instance 移除失败。"));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), Index);
		Result->SetBoolField(TEXT("alreadyDeleted"), false);
		Result->SetNumberField(TEXT("instanceCount"), ISMC->GetInstanceCount());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::LineTrace(const TSharedPtr<FJsonObject>& Args)
	{
		FVector Start;
		if (!JsonConversion::TryGetVectorField(Args, TEXT("start"), Start))
		{
			return ErrorJson(TEXT("缺少必填 start。"));
		}
		FVector End;
		if (!JsonConversion::TryGetVectorField(Args, TEXT("end"), End))
		{
			FVector Direction;
			if (!JsonConversion::TryGetVectorField(Args, TEXT("direction"), Direction))
			{
				return ErrorJson(TEXT("必须提供 end 或 direction。"));
			}
			double Distance = 200000.0;
			Args->TryGetNumberField(TEXT("distance"), Distance);
			End = Start + Direction.GetSafeNormal() * Distance;
		}
		UWorld* World = ResolveSpatialWorld(Args);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPLevelLineTrace), true);
		TArray<FString> IgnoreActors;
		Args->TryGetStringArrayField(TEXT("ignoreActors"), IgnoreActors);
		for (const FString& Name : IgnoreActors)
		{
			if (AActor* Actor = ActorSupport::FindActorByNameOrLabel(World, Name))
			{
				Query.AddIgnoredActor(Actor);
			}
		}
		FHitResult Hit;
		const bool bHit = World && World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("hit"), bHit);
		Result->SetObjectField(TEXT("start"), JsonConversion::MakeVectorObject(Start));
		Result->SetObjectField(TEXT("end"), JsonConversion::MakeVectorObject(End));
		if (bHit)
		{
			Result->SetStringField(TEXT("actorLabel"), Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : FString());
			Result->SetStringField(TEXT("actorClass"), Hit.GetActor() ? Hit.GetActor()->GetClass()->GetPathName() : FString());
			Result->SetStringField(TEXT("componentName"), Hit.GetComponent() ? Hit.GetComponent()->GetName() : FString());
			Result->SetObjectField(TEXT("impactPoint"), JsonConversion::MakeVectorObject(Hit.ImpactPoint));
			Result->SetObjectField(TEXT("normal"), JsonConversion::MakeVectorObject(Hit.ImpactNormal));
			Result->SetNumberField(TEXT("distance"), Hit.Distance);
			Result->SetNumberField(TEXT("faceIndex"), Hit.FaceIndex);
			Result->SetStringField(TEXT("boneName"), Hit.BoneName.ToString());
			Result->SetStringField(TEXT("physicalMaterial"), Hit.PhysMaterial.IsValid() ? Hit.PhysMaterial->GetPathName() : FString());
		}
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SnapActorToFloor(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = ResolveSpatialActor(Args, Error);
		if (!Actor)
			return ErrorJson(Error);
		// HISM、Spline 等实例组件可能延迟更新 Bounds；贴地前必须先同步，
		// 否则只会按 RootMesh 计算高度，移动后子组件会穿入地面。
		Actor->UpdateComponentTransforms();
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (Primitive)
				Primitive->UpdateBounds();
		}
		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(false, Origin, Extent, true);
		double MaxDistance = 100000.0;
		double FloorOffset = 0.0;
		Args->TryGetNumberField(TEXT("maxDistance"), MaxDistance);
		Args->TryGetNumberField(TEXT("floorOffset"), FloorOffset);
		const FVector Start = Origin + FVector(0, 0, Extent.Z + 1.0);
		const FVector End = Origin - FVector(0, 0, Extent.Z + MaxDistance);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPSnapActorToFloor), true, Actor);
		FHitResult Hit;
		UWorld* World = ResolveSpatialWorld(Args);
		TArray<FString> IgnoreActors;
		Args->TryGetStringArrayField(TEXT("ignoreActors"), IgnoreActors);
		for (const FString& Name : IgnoreActors)
		{
			if (AActor* Ignored = ActorSupport::FindActorByNameOrLabel(World, Name))
			{
				Query.AddIgnoredActor(Ignored);
			}
		}
		if (!World || !World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query))
		{
			return ErrorJson(TEXT("Actor 下方未命中可见碰撞表面。"));
		}
		FVector NewLocation = Actor->GetActorLocation();
		NewLocation.Z += Hit.ImpactPoint.Z + FloorOffset - (Origin.Z - Extent.Z);
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SnapActorToFloor", "MCP Actor 吸附地面"));
		Actor->Modify();
		Actor->SetActorLocation(NewLocation, false);
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(NewLocation));
		Result->SetObjectField(TEXT("impactPoint"), JsonConversion::MakeVectorObject(Hit.ImpactPoint));
		Result->SetStringField(TEXT("floorActor"), Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : FString());
		return SuccessJson(Result);
	}
}
