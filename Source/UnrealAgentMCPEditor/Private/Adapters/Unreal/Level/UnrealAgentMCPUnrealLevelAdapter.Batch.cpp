// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.Batch.cpp
 * @brief Level 网格生成、批量平移与批量静态网格放置实现。
 */

#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"

namespace UnrealAgentMCP
{
	namespace
	{
		UStaticMesh* LoadBatchStaticMesh(const FString& Path)
		{
			return LoadObject<UStaticMesh>(nullptr, *JsonConversion::NormalizeAssetObjectPath(Path));
		}

		int32 ReadClampedCount(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, const int32 Default)
		{
			double Value = Default;
			if (Args.IsValid())
			{
				Args->TryGetNumberField(Field, Value);
			}
			return FMath::Clamp(static_cast<int32>(Value), 1, 100);
		}

		AStaticMeshActor* SpawnBatchStaticMeshActor(UWorld* World, UStaticMesh* Mesh, const FTransform& Transform, const FString& Label)
		{
			if (!World || !Mesh)
			{
				return nullptr;
			}
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform.GetLocation(), Transform.Rotator());
			if (!Actor || !Actor->GetStaticMeshComponent())
			{
				return nullptr;
			}
			Actor->SetMobility(EComponentMobility::Movable);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->SetActorScale3D(Transform.GetScale3D());
			if (!Label.IsEmpty())
			{
				Actor->SetActorLabel(Label);
			}
			Actor->PostEditChange();
			Actor->MarkPackageDirty();
			return Actor;
		}
	}

	class FLevelPlaceActorsBatchStepper final : public Execution::IMcpTaskStepper
	{
	public:
		explicit FLevelPlaceActorsBatchStepper(const TSharedPtr<FJsonObject>& InArguments)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
		{
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (!IsInGameThread())
			{
				return Fail(TEXT("place_actors_batch 必须在 GameThread 上执行。"));
			}
			if (!bInitialized && !Initialize())
			{
				return Fail(Error);
			}
			if (!World.IsValid())
			{
				return Fail(TEXT("place_actors_batch 执行期间编辑器世界已失效。"));
			}

			const double StopTime = FPlatformTime::Seconds() + FMath::Max(FrameBudget.GetTotalSeconds(), 0.00025);
			bool bProcessedOne = false;
			do
			{
				if (Context.ShouldStop())
				{
					return Fail(Context.IsDeadlineExceeded() ? TEXT("Level batch task deadline exceeded.") : Context.GetCancellationReason());
				}
				if (NextIndex >= ActorValues.Num())
				{
					return Succeed();
				}
				ProcessNextActor();
				bProcessedOne = true;
				Context.ReportProgress(static_cast<double>(NextIndex) / FMath::Max(ActorValues.Num(), 1), TEXT("Placing actors"));
			} while (!bProcessedOne || FPlatformTime::Seconds() < StopTime);

			return NextIndex >= ActorValues.Num() ? Succeed() : Execution::FMcpTaskStepResult::Continue();
		}

		virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			(void)Context;
			return Fail(Reason.IsEmpty() ? TEXT("place_actors_batch 已取消。") : MoveTemp(Reason));
		}

	private:
		bool Initialize()
		{
			World = ActorSupport::GetEditorWorld();
			const TArray<TSharedPtr<FJsonValue>>* RequestedActors = nullptr;
			if (!World.IsValid() || !Arguments->TryGetArrayField(TEXT("actors"), RequestedActors) || RequestedActors == nullptr)
			{
				Error = TEXT("编辑器世界不可用，或缺少 actors 数组。");
				return false;
			}
			if (RequestedActors->Num() > 10000)
			{
				Error = TEXT("单次批量放置不能超过 10000 个 Actor。");
				return false;
			}
			ActorValues = *RequestedActors;
			Labels.Reserve(ActorValues.Num());
			bInitialized = true;
			return true;
		}

		void ProcessNextActor()
		{
			const int32 Index = NextIndex++;
			const TSharedPtr<FJsonObject> Row = ActorValues[Index].IsValid() ? ActorValues[Index]->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				++FailedSpawn;
				AddError(Index, FString(), TEXT("invalid_actor_entry"));
				return;
			}

			FString MeshPath;
			Row->TryGetStringField(TEXT("staticMesh"), MeshPath);
			UStaticMesh* Mesh = nullptr;
			if (const TWeakObjectPtr<UStaticMesh>* Cached = MeshCache.Find(MeshPath))
			{
				Mesh = Cached->Get();
			}
			if (!Mesh)
			{
				Mesh = LoadBatchStaticMesh(MeshPath);
				MeshCache.Add(MeshPath, Mesh);
			}
			if (!Mesh)
			{
				++FailedMesh;
				AddError(Index, MeshPath, TEXT("static_mesh_not_found"));
				return;
			}

			FVector Location = FVector::ZeroVector;
			FVector Scale = FVector::OneVector;
			FRotator Rotation = FRotator::ZeroRotator;
			JsonConversion::TryGetVectorField(Row, TEXT("location"), Location);
			JsonConversion::TryGetVectorField(Row, TEXT("scale"), Scale);
			JsonConversion::TryGetRotatorField(Row, TEXT("rotation"), Rotation);
			FString Label;
			Row->TryGetStringField(TEXT("label"), Label);
			AStaticMeshActor* Actor = SpawnBatchStaticMeshActor(World.Get(), Mesh, FTransform(Rotation, Location, Scale), Label);
			if (!Actor)
			{
				++FailedSpawn;
				AddError(Index, MeshPath, TEXT("actor_spawn_failed"));
				return;
			}
			Labels.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
			++Spawned;
		}

		void AddError(const int32 Index, const FString& MeshPath, const TCHAR* Reason)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), Index);
			if (!MeshPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("staticMesh"), MeshPath);
			}
			Entry->SetStringField(TEXT("reason"), Reason);
			Errors.Add(MakeShared<FJsonValueObject>(Entry));
		}

		Execution::FMcpTaskStepResult Succeed()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("requested"), ActorValues.Num());
			Result->SetNumberField(TEXT("spawned"), Spawned);
			Result->SetNumberField(TEXT("failedMesh"), FailedMesh);
			Result->SetNumberField(TEXT("failedSpawn"), FailedSpawn);
			Result->SetArrayField(TEXT("labels"), Labels);
			Result->SetArrayField(TEXT("errors"), Errors);
			Result->SetBoolField(TEXT("resumable"), true);
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

		Execution::FMcpTaskStepResult Fail(FString Reason)
		{
			if (Reason.IsEmpty())
			{
				Reason = TEXT("place_actors_batch 执行失败。");
			}
			Execution::FMcpTaskStepResult Result = Execution::FMcpTaskStepResult::Failed(Reason);
			Result.ValueJson = ErrorJson(Reason);
			return Result;
		}

		TSharedRef<FJsonObject> Arguments;
		TWeakObjectPtr<UWorld> World;
		TArray<TSharedPtr<FJsonValue>> ActorValues;
		TMap<FString, TWeakObjectPtr<UStaticMesh>> MeshCache;
		TArray<TSharedPtr<FJsonValue>> Labels;
		TArray<TSharedPtr<FJsonValue>> Errors;
		FString Error;
		int32 NextIndex = 0;
		int32 Spawned = 0;
		int32 FailedMesh = 0;
		int32 FailedSpawn = 0;
		bool bInitialized = false;
	};

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealLevelAdapter::CreateBatchTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		if (Action == TEXT("place_actors_batch"))
		{
			return MakeShared<FLevelPlaceActorsBatchStepper>(Args);
		}
		return nullptr;
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SpawnGrid(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString MeshPath;
		if (!World || !Args.IsValid() || !Args->TryGetStringField(TEXT("staticMesh"), MeshPath) || MeshPath.IsEmpty())
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 staticMesh。"));
		}
		UStaticMesh* Mesh = LoadBatchStaticMesh(MeshPath);
		if (!Mesh)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 StaticMesh：%s"), *MeshPath));
		}

		FVector Minimum;
		FVector Maximum;
		if (!JsonConversion::TryGetVectorField(Args, TEXT("min"), Minimum) || !JsonConversion::TryGetVectorField(Args, TEXT("max"), Maximum))
		{
			return ErrorJson(TEXT("min 与 max 都是必填向量。"));
		}
		const int32 CountX = ReadClampedCount(Args, TEXT("countX"), 4);
		const int32 CountY = ReadClampedCount(Args, TEXT("countY"), 4);
		const int32 CountZ = ReadClampedCount(Args, TEXT("countZ"), 1);
		const int64 Requested = static_cast<int64>(CountX) * CountY * CountZ;
		if (Requested > 10000)
		{
			return ErrorJson(TEXT("单次网格生成不能超过 10000 个 Actor。"));
		}

		double Jitter = 0.0;
		Args->TryGetNumberField(TEXT("jitter"), Jitter);
		Jitter = FMath::Max(0.0, Jitter);
		FString LabelPrefix = TEXT("Grid");
		Args->TryGetStringField(TEXT("labelPrefix"), LabelPrefix);
		const FVector Step(CountX > 1 ? (Maximum.X - Minimum.X) / (CountX - 1) : 0.0, CountY > 1 ? (Maximum.Y - Minimum.Y) / (CountY - 1) : 0.0,
			CountZ > 1 ? (Maximum.Z - Minimum.Z) / (CountZ - 1) : 0.0);

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SpawnGrid", "MCP 批量生成网格 Actor"));
		FRandomStream Random(0x57444D43);
		int32 Index = 0;
		int32 Failed = 0;
		TArray<TSharedPtr<FJsonValue>> Labels;
		for (int32 Z = 0; Z < CountZ; ++Z)
		{
			for (int32 Y = 0; Y < CountY; ++Y)
			{
				for (int32 X = 0; X < CountX; ++X)
				{
					FVector Location = Minimum + FVector(X * Step.X, Y * Step.Y, Z * Step.Z);
					if (Jitter > 0.0)
					{
						Location += FVector(Random.FRandRange(-Jitter, Jitter), Random.FRandRange(-Jitter, Jitter), Random.FRandRange(-Jitter, Jitter));
					}
					const FString Label = FString::Printf(TEXT("%s_%d"), *LabelPrefix, Index++);
					FTransform Transform;
					Transform.SetLocation(Location);
					AStaticMeshActor* Actor = SpawnBatchStaticMeshActor(World, Mesh, Transform, Label);
					if (!Actor)
					{
						++Failed;
						continue;
					}
					Labels.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("requested"), Requested);
		Result->SetNumberField(TEXT("count"), Labels.Num());
		Result->SetNumberField(TEXT("failedSpawn"), Failed);
		Result->SetArrayField(TEXT("labels"), Labels);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::BatchTranslate(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FVector Offset;
		if (!World || !JsonConversion::TryGetVectorField(Args, TEXT("offset"), Offset))
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 offset 向量。"));
		}

		TSet<AActor*> Targets;
		TArray<FString> MissingLabels;
		TArray<FString> Labels;
		if (Args.IsValid())
		{
			Args->TryGetStringArrayField(TEXT("actorLabels"), Labels);
		}
		for (const FString& Label : Labels)
		{
			if (AActor* Actor = ActorSupport::FindActorByNameOrLabel(World, Label))
			{
				Targets.Add(Actor);
			}
			else
			{
				MissingLabels.Add(Label);
			}
		}

		FString Tag;
		if (Args.IsValid() && Args->TryGetStringField(TEXT("tag"), Tag) && !Tag.IsEmpty())
		{
			const FName TagName(*Tag);
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(TagName))
				{
					Targets.Add(*It);
				}
			}
		}
		if (Targets.IsEmpty())
		{
			return ErrorJson(TEXT("actorLabels 或 tag 没有匹配到任何 Actor。"));
		}

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "BatchTranslate", "MCP 批量平移 Actor"));
		TArray<TSharedPtr<FJsonValue>> UpdatedLabels;
		for (AActor* Actor : Targets)
		{
			Actor->Modify();
			Actor->SetActorLocation(Actor->GetActorLocation() + Offset, false, nullptr, ETeleportType::TeleportPhysics);
			Actor->MarkPackageDirty();
			UpdatedLabels.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
		}

		TArray<TSharedPtr<FJsonValue>> MissingValues;
		for (const FString& Label : MissingLabels)
		{
			MissingValues.Add(MakeShared<FJsonValueString>(Label));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Targets.Num());
		Result->SetObjectField(TEXT("offset"), JsonConversion::MakeVectorObject(Offset));
		Result->SetArrayField(TEXT("labels"), UpdatedLabels);
		Result->SetArrayField(TEXT("missingLabels"), MissingValues);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::PlaceActorsBatch(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		const TArray<TSharedPtr<FJsonValue>>* ActorValues = nullptr;
		if (!World || !Args.IsValid() || !Args->TryGetArrayField(TEXT("actors"), ActorValues) || !ActorValues)
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 actors 数组。"));
		}
		if (ActorValues->Num() > 10000)
		{
			return ErrorJson(TEXT("单次批量放置不能超过 10000 个 Actor。"));
		}

		TMap<FString, UStaticMesh*> MeshCache;
		int32 Spawned = 0;
		int32 FailedMesh = 0;
		int32 FailedSpawn = 0;
		TArray<TSharedPtr<FJsonValue>> Labels;
		TArray<TSharedPtr<FJsonValue>> Errors;

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "PlaceActorsBatch", "MCP 批量放置 Actor"));
		for (int32 Index = 0; Index < ActorValues->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Row = (*ActorValues)[Index].IsValid() ? (*ActorValues)[Index]->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				++FailedSpawn;
				continue;
			}

			FString MeshPath;
			Row->TryGetStringField(TEXT("staticMesh"), MeshPath);
			UStaticMesh* Mesh = nullptr;
			if (UStaticMesh** Cached = MeshCache.Find(MeshPath))
			{
				Mesh = *Cached;
			}
			else
			{
				Mesh = LoadBatchStaticMesh(MeshPath);
				MeshCache.Add(MeshPath, Mesh);
			}
			if (!Mesh)
			{
				++FailedMesh;
				TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
				Error->SetNumberField(TEXT("index"), Index);
				Error->SetStringField(TEXT("staticMesh"), MeshPath);
				Error->SetStringField(TEXT("reason"), TEXT("static_mesh_not_found"));
				Errors.Add(MakeShared<FJsonValueObject>(Error));
				continue;
			}

			FVector Location = FVector::ZeroVector;
			FVector Scale = FVector::OneVector;
			FRotator Rotation = FRotator::ZeroRotator;
			JsonConversion::TryGetVectorField(Row, TEXT("location"), Location);
			JsonConversion::TryGetVectorField(Row, TEXT("scale"), Scale);
			JsonConversion::TryGetRotatorField(Row, TEXT("rotation"), Rotation);
			FString Label;
			Row->TryGetStringField(TEXT("label"), Label);
			const FTransform Transform(Rotation, Location, Scale);
			AStaticMeshActor* Actor = SpawnBatchStaticMeshActor(World, Mesh, Transform, Label);
			if (!Actor)
			{
				++FailedSpawn;
				continue;
			}
			Labels.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
			++Spawned;
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("requested"), ActorValues->Num());
		Result->SetNumberField(TEXT("spawned"), Spawned);
		Result->SetNumberField(TEXT("failedMesh"), FailedMesh);
		Result->SetNumberField(TEXT("failedSpawn"), FailedSpawn);
		Result->SetArrayField(TEXT("labels"), Labels);
		Result->SetArrayField(TEXT("errors"), Errors);
		return SuccessJson(Result);
	}
}
