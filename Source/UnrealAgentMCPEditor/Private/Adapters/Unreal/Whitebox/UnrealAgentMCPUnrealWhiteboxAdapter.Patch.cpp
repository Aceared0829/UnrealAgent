// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWhiteboxAdapter.Patch.cpp
 * @brief 城墙白膜稳定构件的可恢复增量修改实现。
 */

#include "Adapters/Unreal/Whitebox/UnrealAgentMCPUnrealWhiteboxAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"

namespace UnrealAgentMCP
{
	namespace
	{
		const FName WhiteboxOwnerTag(TEXT("UnrealAgentMCP.Whitebox"));
		const FName CityWallTag(TEXT("UnrealAgentMCP.Whitebox.CityWall"));
		constexpr int32 InstancesPerPatchStep = 64;

		struct FCityWallElementPatch
		{
			FString ElementId;
			FName ComponentName;
			FString Operation;
			FString MeshPath;
			FString MaterialPath;
			TArray<FTransform> Transforms;
			TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Component;
			int32 PreviousInstanceCount = 0;
			int32 RemovedInstanceCount = 0;
			int32 AddedInstanceCount = 0;
			int32 NextTransformIndex = 0;
			bool bPrepared = false;
			bool bRemovalComplete = false;
		};

		FString NormalizeFolderPath(FString Path)
		{
			Path.TrimStartAndEndInline();
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (Path.StartsWith(TEXT("/")))
			{
				Path.RightChopInline(1);
			}
			while (Path.EndsWith(TEXT("/")))
			{
				Path.LeftChopInline(1);
			}
			while (Path.ReplaceInline(TEXT("//"), TEXT("/")) > 0)
			{
			}
			return Path;
		}

		FName ResolveComponentName(const FString& ElementId)
		{
			if (ElementId == TEXT("wall"))
			{
				return TEXT("WallHISM");
			}
			if (ElementId == TEXT("mamian"))
			{
				return TEXT("MamianHISM");
			}
			if (ElementId == TEXT("gate_tower"))
			{
				return TEXT("GateTowerHISM");
			}
			if (ElementId == TEXT("merlon"))
			{
				return TEXT("MerlonHISM");
			}
			if (ElementId == TEXT("gate_roof"))
			{
				return TEXT("GateRoofHISM");
			}
			if (ElementId == TEXT("watchtower"))
			{
				return TEXT("WatchtowerHISM");
			}
			return NAME_None;
		}

		UHierarchicalInstancedStaticMeshComponent* FindElementComponent(AActor* Actor, const FName ComponentName)
		{
			if (!Actor)
			{
				return nullptr;
			}
			TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Components(Actor);
			for (UHierarchicalInstancedStaticMeshComponent* Component : Components)
			{
				if (IsValid(Component) && Component->GetFName() == ComponentName)
				{
					return Component;
				}
			}
			return nullptr;
		}
	}

	class FCityWallPatchTaskStepper final : public Execution::IMcpTaskStepper
	{
	public:
		explicit FCityWallPatchTaskStepper(const TSharedPtr<FJsonObject>& InArguments)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
		{
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (!IsInGameThread())
			{
				return Fail(TEXT("patch_city_wall must run on the GameThread."));
			}
			if (!bResolved && !ResolveRequest())
			{
				return Fail(Error);
			}
			if (!World.IsValid() || !TargetActor.IsValid())
			{
				return Fail(TEXT("The managed city-wall actor became invalid during patching."));
			}

			const double StopTime = FPlatformTime::Seconds() + FMath::Max(FrameBudget.GetTotalSeconds(), 0.00025);
			constexpr int32 MaximumAdvancesPerStep = 8;
			int32 AdvanceCount = 0;
			do
			{
				if (Context.ShouldStop())
				{
					return Fail(Context.IsDeadlineExceeded() ? TEXT("Whitebox patch task deadline exceeded.") : Context.GetCancellationReason());
				}
				if (CurrentPatchIndex >= Patches.Num())
				{
					return Succeed();
				}
				if (!ProcessCurrentPatch())
				{
					return Fail(Error);
				}
				++AdvanceCount;
				Context.ReportProgress(CalculateProgress(), TEXT("Patching city-wall elements"));
			} while (AdvanceCount < MaximumAdvancesPerStep && FPlatformTime::Seconds() < StopTime);

			return CurrentPatchIndex >= Patches.Num() ? Succeed() : Execution::FMcpTaskStepResult::Continue();
		}

		virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			(void)Context;
			return Fail(Reason.IsEmpty() ? TEXT("patch_city_wall was canceled.") : MoveTemp(Reason));
		}

	private:
		bool ResolveRequest()
		{
			World = ActorSupport::GetEditorWorld();
			if (!World.IsValid())
			{
				Error = TEXT("The editor world is unavailable.");
				return false;
			}

			FString Folder = TEXT("CityWall_Whitebox");
			FString ActorLabel = TEXT("CityWall_Whitebox_Root");
			Arguments->TryGetStringField(TEXT("folder"), Folder);
			Arguments->TryGetStringField(TEXT("actor_label"), ActorLabel);
			Folder = NormalizeFolderPath(MoveTemp(Folder));
			if (Folder.IsEmpty() || ActorLabel.IsEmpty())
			{
				Error = TEXT("folder and actor_label cannot be empty.");
				return false;
			}

			TArray<AActor*> Matches;
			for (TActorIterator<AActor> It(World.Get()); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor) || !Actor->Tags.Contains(WhiteboxOwnerTag) || !Actor->Tags.Contains(CityWallTag) ||
					NormalizeFolderPath(Actor->GetFolderPath().ToString()) != Folder || (Actor->GetActorLabel() != ActorLabel && Actor->GetName() != ActorLabel))
				{
					continue;
				}
				Matches.Add(Actor);
			}
			if (Matches.Num() != 1)
			{
				Error = FString::Printf(TEXT("Expected one managed city-wall actor named %s in folder %s, found %d."), *ActorLabel, *Folder, Matches.Num());
				return false;
			}
			TargetActor = Matches[0];

			const TArray<TSharedPtr<FJsonValue>>* ElementValues = nullptr;
			if (!Arguments->TryGetArrayField(TEXT("elements"), ElementValues) || ElementValues == nullptr || ElementValues->IsEmpty())
			{
				Error = TEXT("elements must contain at least one patch.");
				return false;
			}
			if (ElementValues->Num() > 16)
			{
				Error = TEXT("patch_city_wall accepts at most 16 element patches per call.");
				return false;
			}

			int32 TotalTransforms = 0;
			for (int32 Index = 0; Index < ElementValues->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject> Element = (*ElementValues)[Index].IsValid() ? (*ElementValues)[Index]->AsObject() : nullptr;
				if (!Element.IsValid() || !ParsePatch(Index, Element, TotalTransforms))
				{
					return false;
				}
			}
			if (TotalTransforms > 20000)
			{
				Error = TEXT("patch_city_wall accepts at most 20000 transforms per call.");
				return false;
			}

			TargetActor->Modify();
			bResolved = true;
			return true;
		}

		bool ParsePatch(const int32 PatchIndex, const TSharedPtr<FJsonObject>& Element, int32& InOutTotalTransforms)
		{
			FCityWallElementPatch& Patch = Patches.AddDefaulted_GetRef();
			Element->TryGetStringField(TEXT("element_id"), Patch.ElementId);
			Patch.ComponentName = ResolveComponentName(Patch.ElementId);
			if (Patch.ComponentName.IsNone())
			{
				Error = FString::Printf(TEXT("elements[%d].element_id is unsupported."), PatchIndex);
				return false;
			}
			Element->TryGetStringField(TEXT("operation"), Patch.Operation);
			if (Patch.Operation != TEXT("replace") && Patch.Operation != TEXT("append") && Patch.Operation != TEXT("clear"))
			{
				Error = FString::Printf(TEXT("elements[%d].operation must be replace, append, or clear."), PatchIndex);
				return false;
			}
			Element->TryGetStringField(TEXT("mesh_path"), Patch.MeshPath);
			Element->TryGetStringField(TEXT("material_path"), Patch.MaterialPath);

			const TArray<TSharedPtr<FJsonValue>>* TransformValues = nullptr;
			if (Element->TryGetArrayField(TEXT("transforms"), TransformValues) && TransformValues != nullptr)
			{
				Patch.Transforms.Reserve(TransformValues->Num());
				for (int32 TransformIndex = 0; TransformIndex < TransformValues->Num(); ++TransformIndex)
				{
					const TSharedPtr<FJsonObject> Value = (*TransformValues)[TransformIndex].IsValid() ? (*TransformValues)[TransformIndex]->AsObject() : nullptr;
					if (!Value.IsValid())
					{
						Error = FString::Printf(TEXT("elements[%d].transforms[%d] must be an object."), PatchIndex, TransformIndex);
						return false;
					}
					FVector Location = FVector::ZeroVector;
					FVector Scale = FVector::OneVector;
					FRotator Rotation = FRotator::ZeroRotator;
					JsonConversion::TryGetVectorField(Value, TEXT("location"), Location);
					JsonConversion::TryGetVectorField(Value, TEXT("scale"), Scale);
					JsonConversion::TryGetRotatorField(Value, TEXT("rotation"), Rotation);
					Patch.Transforms.Emplace(Rotation, Location, Scale);
				}
			}
			if (Patch.Operation == TEXT("append") && Patch.Transforms.IsEmpty())
			{
				Error = FString::Printf(TEXT("elements[%d].transforms cannot be empty for append."), PatchIndex);
				return false;
			}
			InOutTotalTransforms += Patch.Transforms.Num();
			return true;
		}

		bool ProcessCurrentPatch()
		{
			const int32 PatchIndex = CurrentPatchIndex;
			FCityWallElementPatch& Patch = Patches[CurrentPatchIndex];
			if (!Patch.bPrepared && !PreparePatch(Patch))
			{
				return false;
			}
			if (CurrentPatchIndex != PatchIndex)
			{
				return true;
			}

			UHierarchicalInstancedStaticMeshComponent* Component = Patch.Component.Get();
			if (!IsValid(Component))
			{
				Error = FString::Printf(TEXT("Element component %s became invalid."), *Patch.ComponentName.ToString());
				return false;
			}
			if (Patch.Operation != TEXT("append") && !Patch.bRemovalComplete)
			{
				if (Component->GetInstanceCount() == 0)
				{
					Patch.bRemovalComplete = true;
					return true;
				}
				const int32 RemoveCount = FMath::Min(InstancesPerPatchStep, Component->GetInstanceCount());
				TArray<int32> Indices;
				Indices.Reserve(RemoveCount);
				for (int32 Index = Component->GetInstanceCount() - 1; Index >= Component->GetInstanceCount() - RemoveCount; --Index)
				{
					Indices.Add(Index);
				}
				if (!Component->RemoveInstances(Indices, true))
				{
					Error = FString::Printf(TEXT("Failed to remove instances from %s."), *Patch.ComponentName.ToString());
					return false;
				}
				Patch.RemovedInstanceCount += RemoveCount;
				Patch.bRemovalComplete = Component->GetInstanceCount() == 0;
				return true;
			}

			if (Patch.NextTransformIndex < Patch.Transforms.Num())
			{
				const int32 EndIndex = FMath::Min(Patch.NextTransformIndex + InstancesPerPatchStep, Patch.Transforms.Num());
				TArray<FTransform> Chunk;
				Chunk.Append(Patch.Transforms.GetData() + Patch.NextTransformIndex, EndIndex - Patch.NextTransformIndex);
				Component->AddInstances(Chunk, false, false, false);
				Patch.AddedInstanceCount += Chunk.Num();
				Patch.NextTransformIndex = EndIndex;
				return true;
			}

			Component->BuildTreeIfOutdated(true, true);
			Component->MarkRenderStateDirty();
			Component->MarkPackageDirty();
			++CurrentPatchIndex;
			return true;
		}

		bool PreparePatch(FCityWallElementPatch& Patch)
		{
			AActor* Actor = TargetActor.Get();
			UHierarchicalInstancedStaticMeshComponent* Component = FindElementComponent(Actor, Patch.ComponentName);
			if (!Component && Patch.Operation == TEXT("clear"))
			{
				Patch.bPrepared = true;
				++CurrentPatchIndex;
				return true;
			}
			if (!Component)
			{
				Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, Patch.ComponentName, RF_Transactional);
				if (!Component)
				{
					Error = FString::Printf(TEXT("Failed to create component %s."), *Patch.ComponentName.ToString());
					return false;
				}
				Component->SetupAttachment(Actor->GetRootComponent());
				Component->SetMobility(EComponentMobility::Static);
				Actor->AddInstanceComponent(Component);
				Component->OnComponentCreated();
				Component->RegisterComponent();
			}

			Component->Modify();
			Patch.PreviousInstanceCount = Component->GetInstanceCount();
			if (!Patch.MeshPath.IsEmpty() || !Component->GetStaticMesh())
			{
				const FString MeshPath = Patch.MeshPath.IsEmpty() ? TEXT("/Engine/BasicShapes/Cube.Cube") : Patch.MeshPath;
				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
				if (!Mesh)
				{
					Error = FString::Printf(TEXT("Static Mesh was not found: %s"), *MeshPath);
					return false;
				}
				Component->SetStaticMesh(Mesh);
			}
			if (!Patch.MaterialPath.IsEmpty())
			{
				UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Patch.MaterialPath);
				if (!Material)
				{
					Error = FString::Printf(TEXT("Material was not found: %s"), *Patch.MaterialPath);
					return false;
				}
				Component->SetMaterial(0, Material);
			}
			Patch.Component = Component;
			Patch.bPrepared = true;
			return true;
		}

		double CalculateProgress() const
		{
			if (Patches.IsEmpty())
			{
				return 1.0;
			}
			double Completed = CurrentPatchIndex;
			if (CurrentPatchIndex < Patches.Num())
			{
				const FCityWallElementPatch& Patch = Patches[CurrentPatchIndex];
				const int32 WorkUnits = FMath::Max(Patch.PreviousInstanceCount + Patch.Transforms.Num(), 1);
				Completed += static_cast<double>(Patch.RemovedInstanceCount + Patch.AddedInstanceCount) / WorkUnits;
			}
			return FMath::Clamp(Completed / Patches.Num(), 0.0, 1.0);
		}

		Execution::FMcpTaskStepResult Succeed()
		{
			TArray<TSharedPtr<FJsonValue>> Results;
			Results.Reserve(Patches.Num());
			for (const FCityWallElementPatch& Patch : Patches)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("element_id"), Patch.ElementId);
				Entry->SetStringField(TEXT("component_name"), Patch.ComponentName.ToString());
				Entry->SetStringField(TEXT("operation"), Patch.Operation);
				Entry->SetNumberField(TEXT("previous_instance_count"), Patch.PreviousInstanceCount);
				Entry->SetNumberField(TEXT("removed_instance_count"), Patch.RemovedInstanceCount);
				Entry->SetNumberField(TEXT("added_instance_count"), Patch.AddedInstanceCount);
				if (UHierarchicalInstancedStaticMeshComponent* Component = Patch.Component.Get())
				{
					Entry->SetNumberField(TEXT("instance_count"), Component->GetInstanceCount());
				}
				else
				{
					Entry->SetNumberField(TEXT("instance_count"), 0);
				}
				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TargetActor->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("action"), TEXT("patch_city_wall"));
			Result->SetStringField(TEXT("actor_label"), TargetActor->GetActorLabel());
			Result->SetNumberField(TEXT("patched_element_count"), Patches.Num());
			Result->SetArrayField(TEXT("elements"), MoveTemp(Results));
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

		Execution::FMcpTaskStepResult Fail(FString Message)
		{
			if (Message.IsEmpty())
			{
				Message = TEXT("patch_city_wall failed.");
			}
			Execution::FMcpTaskStepResult Result = Execution::FMcpTaskStepResult::Failed(Message);
			Result.ValueJson = ErrorJson(Message);
			return Result;
		}

		TSharedRef<FJsonObject> Arguments;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AActor> TargetActor;
		TArray<FCityWallElementPatch> Patches;
		FString Error;
		int32 CurrentPatchIndex = 0;
		bool bResolved = false;
	};

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealWhiteboxAdapter::CreatePatchTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		return MakeShared<FCityWallPatchTaskStepper>(Args);
	}
}
