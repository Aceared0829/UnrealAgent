// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWhiteboxAdapter.cpp
 * @brief 可恢复的 HISM 白膜搭建与 Landscape 对齐实现。
 */

#include "Adapters/Unreal/Whitebox/UnrealAgentMCPUnrealWhiteboxAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Application/Domains/Whitebox/UnrealAgentMCPWhiteboxService.h"
#include "Application/Ports/UnrealAgentMCPLandscapePort.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP
{
	namespace
	{
		const FName WhiteboxOwnerTag(TEXT("UnrealAgentMCP.Whitebox"));
		const FName CityWallTag(TEXT("UnrealAgentMCP.Whitebox.CityWall"));
		constexpr int32 ActorsPerDiscoveryStep = 128;
		constexpr int32 ActorsPerDeleteStep = 4;
		constexpr int32 InstancesPerStep = 64;

		enum class EWhiteboxTaskPhase : uint8
		{
			Resolve,
			Discover,
			SampleTerrain,
			FlattenTerrain,
			SpawnActor,
			AddInstances,
			ReplacePrevious,
			AlignActors,
			Complete
		};

		struct FCityWallSpec
		{
			FVector Center = FVector::ZeroVector;
			double HalfExtentCm = 18000.0;
			double BaseThicknessCm = 2400.0;
			double TopThicknessCm = 1400.0;
			double WallHeightCm = 1600.0;
			double GateOpeningWidthCm = 1400.0;
			double MerlonSpacingCm = 600.0;
			double BaseClearanceCm = 0.0;
			double TerrainSampleSpacingCm = 1000.0;
			int32 MamianPerSide = 4;
			int32 MaxInstances = 20000;
			bool bSnapBaseToLandscape = true;
			bool bFlattenFootprint = false;
			bool bInstanceMerlons = true;
			bool bReplaceExisting = true;
			FString Folder = TEXT("CityWall_Whitebox");
			FString ActorLabel = TEXT("CityWall_Whitebox_Root");
			FString MeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
			FString MaterialPath;
		};

		struct FWhiteboxInstanceBatch
		{
			FName ComponentName;
			TArray<FTransform> Transforms;
			TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Component;
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

		bool IsInFolder(const AActor& Actor, const FString& Folder, const bool bRecursive)
		{
			const FString ActorFolder = NormalizeFolderPath(Actor.GetFolderPath().ToString());
			return ActorFolder == Folder || (bRecursive && ActorFolder.StartsWith(Folder + TEXT("/")));
		}

		bool IsOwnedWhiteboxActor(const AActor& Actor)
		{
			return Actor.Tags.Contains(WhiteboxOwnerTag);
		}

		bool TryReadJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		void CopyLandscapeSelector(const TSharedPtr<FJsonObject>& Source, const TSharedRef<FJsonObject>& Target)
		{
			FString Value;
			if (Source->TryGetStringField(TEXT("landscapeActorLabel"), Value) && !Value.IsEmpty())
			{
				Target->SetStringField(TEXT("actorLabel"), Value);
			}
			if (Source->TryGetStringField(TEXT("landscapeName"), Value) && !Value.IsEmpty())
			{
				Target->SetStringField(TEXT("name"), Value);
			}
			if (Source->TryGetStringField(TEXT("landscapeGuid"), Value) && !Value.IsEmpty())
			{
				Target->SetStringField(TEXT("landscapeGuid"), Value);
			}
			if (Source->TryGetStringField(TEXT("editLayer"), Value) && !Value.IsEmpty())
			{
				Target->SetStringField(TEXT("editLayer"), Value);
			}
			double EditLayerIndex = 0.0;
			if (Source->TryGetNumberField(TEXT("editLayerIndex"), EditLayerIndex))
			{
				Target->SetNumberField(TEXT("editLayerIndex"), EditLayerIndex);
			}
		}

		TSharedRef<FJsonObject> MakePoint(const double X, const double Y)
		{
			TSharedRef<FJsonObject> Point = MakeShared<FJsonObject>();
			Point->SetNumberField(TEXT("x"), X);
			Point->SetNumberField(TEXT("y"), Y);
			return Point;
		}

		void AddPointValue(TArray<TSharedPtr<FJsonValue>>& Points, const double X, const double Y)
		{
			Points.Add(MakeShared<FJsonValueObject>(MakePoint(X, Y)));
		}

		void AddBox(TArray<FTransform>& Transforms, const FVector& Center, const FVector& Dimensions, const double YawDegrees = 0.0)
		{
			Transforms.Emplace(FRotator(0.0, YawDegrees, 0.0), Center, Dimensions / 100.0);
		}
	}

	class FUnrealAgentMCPWhiteboxTaskStepper final : public Execution::IMcpTaskStepper
	{
	public:
		FUnrealAgentMCPWhiteboxTaskStepper(const TSharedPtr<FJsonObject>& InArguments, TSharedRef<IUnrealAgentMCPLandscapePort> InLandscapePort)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>()), LandscapePort(MoveTemp(InLandscapePort))
		{
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (!IsInGameThread())
			{
				return Fail(TEXT("Whitebox tasks must run on the GameThread."));
			}
			if (Phase != EWhiteboxTaskPhase::Resolve && !World.IsValid())
			{
				return Fail(TEXT("The editor world became invalid during the Whitebox task."));
			}

			const double StopTime = FPlatformTime::Seconds() + FMath::Max(FrameBudget.GetTotalSeconds(), 0.00025);
			constexpr int32 MaximumAdvancesPerStep = 8;
			int32 AdvanceCount = 0;
			do
			{
				if (Context.ShouldStop())
				{
					const FString Reason = Context.IsDeadlineExceeded() ? TEXT("Whitebox task deadline exceeded.") : Context.GetCancellationReason();
					return Fail(Reason);
				}

				switch (Phase)
				{
				case EWhiteboxTaskPhase::Resolve:
					if (!ResolveRequest(Context))
					{
						return Fail(Error);
					}
					break;
				case EWhiteboxTaskPhase::Discover:
					DiscoverNextChunk(Context);
					break;
				case EWhiteboxTaskPhase::SampleTerrain:
					if (const TOptional<Execution::FMcpTaskStepResult> Result = StepTerrainSample(Context, FrameBudget))
					{
						return Result.GetValue();
					}
					break;
				case EWhiteboxTaskPhase::FlattenTerrain:
					if (const TOptional<Execution::FMcpTaskStepResult> Result = StepTerrainFlatten(Context, FrameBudget))
					{
						return Result.GetValue();
					}
					break;
				case EWhiteboxTaskPhase::SpawnActor:
					if (!SpawnCityWallActor(Context))
					{
						return Fail(Error);
					}
					break;
				case EWhiteboxTaskPhase::AddInstances:
					if (!AddNextInstanceChunk(Context))
					{
						return Fail(Error);
					}
					break;
				case EWhiteboxTaskPhase::ReplacePrevious:
					DeleteNextChunk(Context);
					break;
				case EWhiteboxTaskPhase::AlignActors:
					if (!AlignNextActor(Context, FrameBudget))
					{
						return Fail(Error);
					}
					break;
				case EWhiteboxTaskPhase::Complete:
					return Succeed();
				default:
					return Fail(TEXT("Whitebox task entered an unknown phase."));
				}
				++AdvanceCount;
			} while (AdvanceCount < MaximumAdvancesPerStep && FPlatformTime::Seconds() < StopTime);

			return Phase == EWhiteboxTaskPhase::Complete ? Succeed() : Execution::FMcpTaskStepResult::Continue();
		}

		virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			if (TerrainStepper.IsValid())
			{
				TerrainStepper->Abort(Context, Reason);
				TerrainStepper.Reset();
			}
			if (NewActor.IsValid() && World.IsValid())
			{
				World->DestroyActor(NewActor.Get());
				NewActor.Reset();
			}
			return Fail(Reason.IsEmpty() ? TEXT("Whitebox task was canceled.") : MoveTemp(Reason));
		}

	private:
		bool ResolveRequest(Execution::FMcpTaskExecutionContext& Context)
		{
			World = ActorSupport::GetEditorWorld();
			if (!World.IsValid())
			{
				Error = TEXT("The editor world is unavailable.");
				return false;
			}
			Arguments->TryGetStringField(TEXT("action"), Action);
			Arguments->TryGetStringField(TEXT("folder"), Folder);
			if (Folder.IsEmpty())
			{
				Folder = TEXT("CityWall_Whitebox");
			}
			Folder = NormalizeFolderPath(MoveTemp(Folder));
			if (Folder.IsEmpty())
			{
				Error = TEXT("folder cannot be empty or the root folder.");
				return false;
			}
			Arguments->TryGetBoolField(TEXT("recursive"), bRecursive);

			if (Action == TEXT("build_city_wall"))
			{
				if (!ResolveCityWallSpec())
				{
					return false;
				}
			}
			else if (Action == TEXT("clear_folder"))
			{
				FString Confirmation;
				Arguments->TryGetStringField(TEXT("confirmation"), Confirmation);
				if (Confirmation != TEXT("clear_whitebox_folder"))
				{
					Error = TEXT("confirmation must equal clear_whitebox_folder.");
					return false;
				}
				Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);
			}
			else if (Action == TEXT("align_to_landscape"))
			{
				Arguments->TryGetStringField(TEXT("actorLabel"), ActorLabelFilter);
				Arguments->TryGetNumberField(TEXT("clearanceCm"), AlignClearanceCm);
				if (!FMath::IsFinite(AlignClearanceCm))
				{
					Error = TEXT("clearanceCm must be finite.");
					return false;
				}
			}
			else
			{
				Error = FString::Printf(TEXT("Unsupported Whitebox action '%s'."), *Action);
				return false;
			}

			DiscoveryIterator = MakeUnique<TActorIterator<AActor>>(World.Get());
			Phase = EWhiteboxTaskPhase::Discover;
			Context.ReportProgress(0.01, TEXT("Discovering managed Whitebox actors"));
			return true;
		}

		bool ResolveCityWallSpec()
		{
			Spec.Folder = Folder;
			Arguments->TryGetStringField(TEXT("actorLabel"), Spec.ActorLabel);
			Arguments->TryGetStringField(TEXT("meshPath"), Spec.MeshPath);
			Arguments->TryGetStringField(TEXT("materialPath"), Spec.MaterialPath);
			JsonConversion::TryGetVectorField(Arguments, TEXT("center"), Spec.Center);
			Arguments->TryGetNumberField(TEXT("halfExtentCm"), Spec.HalfExtentCm);
			if (!ReadMetres(TEXT("baseThicknessM"), Spec.BaseThicknessCm) || !ReadMetres(TEXT("topThicknessM"), Spec.TopThicknessCm) ||
				!ReadMetres(TEXT("wallHeightM"), Spec.WallHeightCm) || !ReadMetres(TEXT("gateOpeningWidthM"), Spec.GateOpeningWidthCm) ||
				!ReadMetres(TEXT("merlonSpacingM"), Spec.MerlonSpacingCm))
			{
				return false;
			}
			Arguments->TryGetNumberField(TEXT("baseClearanceCm"), Spec.BaseClearanceCm);
			Arguments->TryGetNumberField(TEXT("terrainSampleSpacingCm"), Spec.TerrainSampleSpacingCm);
			double MamianPerSide = Spec.MamianPerSide;
			double MaxInstances = Spec.MaxInstances;
			Arguments->TryGetNumberField(TEXT("mamianPerSide"), MamianPerSide);
			Arguments->TryGetNumberField(TEXT("maxInstances"), MaxInstances);
			Spec.MamianPerSide = FMath::RoundToInt(MamianPerSide);
			Spec.MaxInstances = FMath::RoundToInt(MaxInstances);
			Arguments->TryGetBoolField(TEXT("snapBaseToLandscape"), Spec.bSnapBaseToLandscape);
			Arguments->TryGetBoolField(TEXT("flattenFootprint"), Spec.bFlattenFootprint);
			Arguments->TryGetBoolField(TEXT("instanceMerlons"), Spec.bInstanceMerlons);
			Arguments->TryGetBoolField(TEXT("replaceExisting"), Spec.bReplaceExisting);

			const bool bFinite = FMath::IsFinite(Spec.Center.X) && FMath::IsFinite(Spec.Center.Y) && FMath::IsFinite(Spec.Center.Z) && FMath::IsFinite(Spec.HalfExtentCm) &&
				FMath::IsFinite(Spec.BaseThicknessCm) && FMath::IsFinite(Spec.TopThicknessCm) && FMath::IsFinite(Spec.WallHeightCm) && FMath::IsFinite(Spec.GateOpeningWidthCm) &&
				FMath::IsFinite(Spec.MerlonSpacingCm) && FMath::IsFinite(Spec.BaseClearanceCm) && FMath::IsFinite(Spec.TerrainSampleSpacingCm);
			if (!bFinite)
			{
				Error = TEXT("City wall numeric parameters must be finite.");
				return false;
			}
			if (Spec.ActorLabel.IsEmpty() || Spec.MeshPath.IsEmpty())
			{
				Error = TEXT("actorLabel and meshPath cannot be empty.");
				return false;
			}
			if (Spec.HalfExtentCm <= 0.0 || Spec.BaseThicknessCm <= 0.0 || Spec.TopThicknessCm <= 0.0 || Spec.WallHeightCm <= 0.0 || Spec.MerlonSpacingCm < 10.0 ||
				Spec.TerrainSampleSpacingCm < 10.0)
			{
				Error = TEXT("Wall extents, thicknesses, and height must be positive; spacing must be at least 10 cm.");
				return false;
			}
			if (Spec.TopThicknessCm > Spec.BaseThicknessCm)
			{
				Error = TEXT("topThicknessM cannot exceed baseThicknessM.");
				return false;
			}
			if (Spec.GateOpeningWidthCm < 0.0 || Spec.GateOpeningWidthCm >= 2.0 * Spec.HalfExtentCm - 2.0 * Spec.BaseThicknessCm)
			{
				Error = TEXT("gateOpeningWidthM leaves no valid wall segment.");
				return false;
			}
			if (Spec.MamianPerSide < 0 || Spec.MamianPerSide > 64)
			{
				Error = TEXT("mamianPerSide must be between 0 and 64.");
				return false;
			}
			if (Spec.MaxInstances < 1 || Spec.MaxInstances > 20000)
			{
				Error = TEXT("maxInstances must be between 1 and 20000.");
				return false;
			}
			const double EstimatedMerlonCount = Spec.bInstanceMerlons ? 4.0 * FMath::CeilToDouble(2.0 * Spec.HalfExtentCm / Spec.MerlonSpacingCm) : 0.0;
			const double EstimatedInstanceCount = 16.0 + 4.0 * Spec.MamianPerSide + 8.0 + EstimatedMerlonCount;
			if (!FMath::IsFinite(EstimatedInstanceCount) || EstimatedInstanceCount > Spec.MaxInstances)
			{
				Error = FString::Printf(TEXT("The city wall is estimated to require %.0f instances, exceeding maxInstances=%d."), EstimatedInstanceCount, Spec.MaxInstances);
				return false;
			}
			return true;
		}

		bool ReadMetres(const TCHAR* Field, double& InOutCentimetres)
		{
			double Metres = InOutCentimetres / 100.0;
			if (Arguments->TryGetNumberField(Field, Metres))
			{
				if (!FMath::IsFinite(Metres))
				{
					Error = FString::Printf(TEXT("%s must be finite."), Field);
					return false;
				}
				InOutCentimetres = Metres * 100.0;
			}
			return true;
		}

		void DiscoverNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			int32 Processed = 0;
			while (DiscoveryIterator.IsValid() && static_cast<bool>(*DiscoveryIterator) && Processed < ActorsPerDiscoveryStep)
			{
				AActor* Actor = **DiscoveryIterator;
				++(*DiscoveryIterator);
				++Processed;
				++ScannedActorCount;
				if (!IsValid(Actor) || Actor->IsTemplate() || !IsOwnedWhiteboxActor(*Actor) || !IsInFolder(*Actor, Folder, bRecursive))
				{
					continue;
				}
				if (Action == TEXT("build_city_wall") && !Actor->Tags.Contains(CityWallTag))
				{
					continue;
				}
				if (!ActorLabelFilter.IsEmpty() && Actor->GetActorLabel() != ActorLabelFilter && Actor->GetName() != ActorLabelFilter)
				{
					continue;
				}
				ManagedActors.Add(Actor);
			}

			if (!DiscoveryIterator.IsValid() || !static_cast<bool>(*DiscoveryIterator))
			{
				DiscoveryIterator.Reset();
				if (Action == TEXT("build_city_wall"))
				{
					if (!Spec.bReplaceExisting && !ManagedActors.IsEmpty())
					{
						Error = TEXT("Managed actors already exist in the folder and replaceExisting is false.");
						Phase = EWhiteboxTaskPhase::Complete;
						bCompletedWithError = true;
						return;
					}
					Phase = Spec.bSnapBaseToLandscape ? EWhiteboxTaskPhase::SampleTerrain : EWhiteboxTaskPhase::SpawnActor;
					BaseWorldZ = Spec.Center.Z;
					TerrainWorldZ = Spec.Center.Z - Spec.BaseClearanceCm;
				}
				else if (Action == TEXT("align_to_landscape"))
				{
					Phase = EWhiteboxTaskPhase::AlignActors;
				}
				else
				{
					Phase = EWhiteboxTaskPhase::ReplacePrevious;
				}
				Context.ReportProgress(0.12, TEXT("Managed Whitebox actor discovery complete"));
			}
		}

		TOptional<Execution::FMcpTaskStepResult> StepTerrainSample(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget)
		{
			if (!TerrainStepper.IsValid())
			{
				TSharedRef<FJsonObject> SampleArgs = MakeShared<FJsonObject>();
				SampleArgs->SetStringField(TEXT("action"), TEXT("sample_polyline"));
				CopyLandscapeSelector(Arguments, SampleArgs);
				SampleArgs->SetNumberField(TEXT("spacing"), Spec.TerrainSampleSpacingCm);
				SampleArgs->SetNumberField(TEXT("maxPoints"), 4096);
				SampleArgs->SetNumberField(TEXT("maxReadVertices"), 1);
				TArray<TSharedPtr<FJsonValue>> Points;
				AddPointValue(Points, Spec.Center.X - Spec.HalfExtentCm, Spec.Center.Y - Spec.HalfExtentCm);
				AddPointValue(Points, Spec.Center.X + Spec.HalfExtentCm, Spec.Center.Y - Spec.HalfExtentCm);
				AddPointValue(Points, Spec.Center.X + Spec.HalfExtentCm, Spec.Center.Y + Spec.HalfExtentCm);
				AddPointValue(Points, Spec.Center.X - Spec.HalfExtentCm, Spec.Center.Y + Spec.HalfExtentCm);
				AddPointValue(Points, Spec.Center.X - Spec.HalfExtentCm, Spec.Center.Y - Spec.HalfExtentCm);
				SampleArgs->SetArrayField(TEXT("points"), MoveTemp(Points));
				TerrainStepper = LandscapePort->CreateTaskStepper(SampleArgs);
				if (!TerrainStepper.IsValid())
				{
					return Fail(TEXT("Landscape did not create a sample_polyline stepper."));
				}
			}

			const Execution::FMcpTaskStepResult StepResult = TerrainStepper->Step(Context, FrameBudget);
			if (StepResult.State == Execution::EMcpTaskStepState::Continue)
			{
				return StepResult;
			}
			TerrainStepper.Reset();
			if (StepResult.State == Execution::EMcpTaskStepState::Failed)
			{
				return Fail(FString::Printf(TEXT("Landscape perimeter sampling failed: %s"), *StepResult.Error));
			}

			TSharedPtr<FJsonObject> SampleResult;
			const FString& SampleJson = StepResult.ValueJson;
			bool bSuccess = false;
			if (!TryReadJsonObject(SampleJson, SampleResult) || !SampleResult->TryGetBoolField(TEXT("success"), bSuccess) || !bSuccess)
			{
				Error = FString::Printf(TEXT("Landscape perimeter sampling failed: %s"), *SampleJson.Left(1200));
				return Fail(Error);
			}
			double ValidSampleCount = 0.0;
			if (!SampleResult->TryGetNumberField(TEXT("validSampleCount"), ValidSampleCount) || ValidSampleCount < 1.0 ||
				!SampleResult->TryGetNumberField(TEXT("heightMax"), TerrainWorldZ))
			{
				Error = TEXT("No valid Landscape samples were found under the city wall perimeter.");
				return Fail(Error);
			}
			BaseWorldZ = TerrainWorldZ + Spec.BaseClearanceCm;
			SampleCount = FMath::RoundToInt(ValidSampleCount);
			Context.ReportProgress(0.2, TEXT("City wall base height resolved from Landscape"));

			if (Spec.bFlattenFootprint)
			{
				TSharedRef<FJsonObject> SculptArgs = MakeShared<FJsonObject>();
				SculptArgs->SetStringField(TEXT("action"), TEXT("sculpt_batch"));
				CopyLandscapeSelector(Arguments, SculptArgs);
				SculptArgs->SetStringField(TEXT("mode"), TEXT("flatten"));
				SculptArgs->SetNumberField(TEXT("targetWorldZ"), TerrainWorldZ);
				SculptArgs->SetNumberField(TEXT("falloff"), 0.2);
				SculptArgs->SetNumberField(TEXT("maxVertices"), 4000000);
				SculptArgs->SetArrayField(TEXT("strokes"), MakeFootprintStrokes());
				TerrainStepper = LandscapePort->CreateTaskStepper(SculptArgs);
				if (!TerrainStepper.IsValid())
				{
					Error = TEXT("Landscape did not create a sculpt_batch stepper for footprint flattening.");
					return Fail(Error);
				}
				Phase = EWhiteboxTaskPhase::FlattenTerrain;
			}
			else
			{
				Phase = EWhiteboxTaskPhase::SpawnActor;
			}
			return {};
		}

		TArray<TSharedPtr<FJsonValue>> MakeFootprintStrokes() const
		{
			TArray<TSharedPtr<FJsonValue>> Strokes;
			const double Radius = FMath::Max(Spec.BaseThicknessCm * 0.75, 100.0);
			const double Spacing = FMath::Max(Radius, 100.0);
			auto AddStroke = [&Strokes, Radius](const double X, const double Y)
			{
				TSharedRef<FJsonObject> Stroke = MakeShared<FJsonObject>();
				Stroke->SetObjectField(TEXT("center"), MakePoint(X, Y));
				Stroke->SetNumberField(TEXT("radius"), Radius);
				Strokes.Add(MakeShared<FJsonValueObject>(Stroke));
			};
			const int32 StepCount = FMath::Clamp(FMath::CeilToInt((2.0 * Spec.HalfExtentCm) / Spacing), 1, 127);
			for (int32 Index = 0; Index <= StepCount; ++Index)
			{
				const double Alpha = static_cast<double>(Index) / StepCount;
				const double Axis = FMath::Lerp(-Spec.HalfExtentCm, Spec.HalfExtentCm, Alpha);
				AddStroke(Spec.Center.X + Axis, Spec.Center.Y - Spec.HalfExtentCm);
				AddStroke(Spec.Center.X + Axis, Spec.Center.Y + Spec.HalfExtentCm);
				AddStroke(Spec.Center.X - Spec.HalfExtentCm, Spec.Center.Y + Axis);
				AddStroke(Spec.Center.X + Spec.HalfExtentCm, Spec.Center.Y + Axis);
			}
			return Strokes;
		}

		TOptional<Execution::FMcpTaskStepResult> StepTerrainFlatten(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget)
		{
			const Execution::FMcpTaskStepResult Result = TerrainStepper->Step(Context, FrameBudget);
			if (Result.State == Execution::EMcpTaskStepState::Continue)
			{
				return Result;
			}
			TerrainStepper.Reset();
			if (Result.State == Execution::EMcpTaskStepState::Failed)
			{
				return Fail(FString::Printf(TEXT("City wall footprint flattening failed: %s"), *Result.Error));
			}
			bFlattenedFootprint = true;
			Phase = EWhiteboxTaskPhase::SpawnActor;
			Context.ReportProgress(0.55, TEXT("City wall footprint flattened"));
			return {};
		}

		bool SpawnCityWallActor(Execution::FMcpTaskExecutionContext& Context)
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Spec.MeshPath);
			if (!Mesh)
			{
				Error = FString::Printf(TEXT("Static Mesh was not found: %s"), *Spec.MeshPath);
				return false;
			}
			UMaterialInterface* Material = nullptr;
			if (!Spec.MaterialPath.IsEmpty())
			{
				Material = LoadObject<UMaterialInterface>(nullptr, *Spec.MaterialPath);
				if (!Material)
				{
					Error = FString::Printf(TEXT("Material was not found: %s"), *Spec.MaterialPath);
					return false;
				}
			}

			BuildCityWallTransforms();
			if (TotalInstanceCount > Spec.MaxInstances)
			{
				Error = FString::Printf(TEXT("City wall requires %d instances, exceeding maxInstances=%d."), TotalInstanceCount, Spec.MaxInstances);
				return false;
			}

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.ObjectFlags |= RF_Transactional;
			AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(Spec.Center.X, Spec.Center.Y, BaseWorldZ)), SpawnParameters);
			if (!Actor)
			{
				Error = TEXT("Failed to spawn the city wall Whitebox actor.");
				return false;
			}
			NewActor = Actor;
			Actor->SetActorLabel(Spec.ActorLabel + TEXT("_Pending"));
			Actor->SetFolderPath(FName(*Spec.Folder));
			Actor->Tags.AddUnique(WhiteboxOwnerTag);
			Actor->Tags.AddUnique(CityWallTag);

			USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("WhiteboxRoot"), RF_Transactional);
			if (!Root)
			{
				Error = TEXT("Failed to create the city wall root component.");
				return false;
			}
			Actor->SetRootComponent(Root);
			Root->SetMobility(EComponentMobility::Static);
			Actor->AddInstanceComponent(Root);
			Root->OnComponentCreated();
			Root->RegisterComponent();

			for (FWhiteboxInstanceBatch& Batch : InstanceBatches)
			{
				UHierarchicalInstancedStaticMeshComponent* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, Batch.ComponentName, RF_Transactional);
				if (!Component)
				{
					Error = FString::Printf(TEXT("Failed to create component %s."), *Batch.ComponentName.ToString());
					return false;
				}
				Component->SetupAttachment(Root);
				Component->SetStaticMesh(Mesh);
				Component->SetMobility(EComponentMobility::Static);
				if (Material)
				{
					Component->SetMaterial(0, Material);
				}
				Actor->AddInstanceComponent(Component);
				Component->OnComponentCreated();
				Component->RegisterComponent();
				Batch.Component = Component;
			}

			Actor->MarkPackageDirty();
			Phase = EWhiteboxTaskPhase::AddInstances;
			Context.ReportProgress(0.62, TEXT("City wall HISM container created"));
			return true;
		}

		void BuildCityWallTransforms()
		{
			InstanceBatches.Reset();
			InstanceBatches.SetNum(4);
			InstanceBatches[0].ComponentName = TEXT("WallHISM");
			InstanceBatches[1].ComponentName = TEXT("MamianHISM");
			InstanceBatches[2].ComponentName = TEXT("GateTowerHISM");
			InstanceBatches[3].ComponentName = TEXT("MerlonHISM");
			TArray<FTransform>& Walls = InstanceBatches[0].Transforms;
			TArray<FTransform>& Mamian = InstanceBatches[1].Transforms;
			TArray<FTransform>& GateTowers = InstanceBatches[2].Transforms;
			TArray<FTransform>& Merlons = InstanceBatches[3].Transforms;

			const double GateHalf = Spec.GateOpeningWidthCm * 0.5;
			const double SegmentLength = Spec.HalfExtentCm - GateHalf;
			const double SegmentOffset = (Spec.HalfExtentCm + GateHalf) * 0.5;
			const double LowerHeight = Spec.WallHeightCm * 0.65;
			const double UpperHeight = Spec.WallHeightCm - LowerHeight;
			for (const double Sign : { -1.0, 1.0 })
			{
				for (const double Side : { -1.0, 1.0 })
				{
					AddBox(Walls, FVector(Sign * SegmentOffset, Side * Spec.HalfExtentCm, LowerHeight * 0.5), FVector(SegmentLength, Spec.BaseThicknessCm, LowerHeight));
					AddBox(Walls, FVector(Sign * SegmentOffset, Side * Spec.HalfExtentCm, LowerHeight + UpperHeight * 0.5),
						FVector(SegmentLength, Spec.TopThicknessCm, UpperHeight));
					AddBox(Walls, FVector(Side * Spec.HalfExtentCm, Sign * SegmentOffset, LowerHeight * 0.5), FVector(SegmentLength, Spec.BaseThicknessCm, LowerHeight), 90.0);
					AddBox(Walls, FVector(Side * Spec.HalfExtentCm, Sign * SegmentOffset, LowerHeight + UpperHeight * 0.5),
						FVector(SegmentLength, Spec.TopThicknessCm, UpperHeight), 90.0);
				}
			}

			const double TowerWidth = Spec.BaseThicknessCm * 1.5;
			const double TowerHeight = Spec.WallHeightCm * 1.2;
			const int32 NegativeMamianCount = (Spec.MamianPerSide + 1) / 2;
			const int32 PositiveMamianCount = Spec.MamianPerSide / 2;
			TArray<double> MamianAxes;
			for (int32 Index = 1; Index <= NegativeMamianCount; ++Index)
			{
				MamianAxes.Add(FMath::Lerp(-Spec.HalfExtentCm, -GateHalf, static_cast<double>(Index) / (NegativeMamianCount + 1)));
			}
			for (int32 Index = 1; Index <= PositiveMamianCount; ++Index)
			{
				MamianAxes.Add(FMath::Lerp(GateHalf, Spec.HalfExtentCm, static_cast<double>(Index) / (PositiveMamianCount + 1)));
			}
			for (const double Axis : MamianAxes)
			{
				for (const double Side : { -1.0, 1.0 })
				{
					AddBox(Mamian, FVector(Axis, Side * (Spec.HalfExtentCm + TowerWidth * 0.25), TowerHeight * 0.5), FVector(TowerWidth, TowerWidth, TowerHeight));
					AddBox(Mamian, FVector(Side * (Spec.HalfExtentCm + TowerWidth * 0.25), Axis, TowerHeight * 0.5), FVector(TowerWidth, TowerWidth, TowerHeight));
				}
			}

			const double GateTowerHeight = Spec.WallHeightCm * 1.5;
			for (const double GateSide : { -1.0, 1.0 })
			{
				const double GateAxis = GateSide * (GateHalf + Spec.BaseThicknessCm * 0.5);
				for (const double WallSide : { -1.0, 1.0 })
				{
					AddBox(GateTowers, FVector(GateAxis, WallSide * Spec.HalfExtentCm, GateTowerHeight * 0.5),
						FVector(Spec.BaseThicknessCm, Spec.BaseThicknessCm, GateTowerHeight));
					AddBox(GateTowers, FVector(WallSide * Spec.HalfExtentCm, GateAxis, GateTowerHeight * 0.5),
						FVector(Spec.BaseThicknessCm, Spec.BaseThicknessCm, GateTowerHeight));
				}
			}

			if (Spec.bInstanceMerlons)
			{
				const double MerlonWidth = FMath::Min(Spec.MerlonSpacingCm * 0.45, Spec.TopThicknessCm);
				const double MerlonDepth = Spec.TopThicknessCm * 0.65;
				const double MerlonHeight = FMath::Max(Spec.WallHeightCm * 0.18, 60.0);
				for (double Axis = -Spec.HalfExtentCm + Spec.MerlonSpacingCm * 0.5; Axis < Spec.HalfExtentCm; Axis += Spec.MerlonSpacingCm)
				{
					if (FMath::Abs(Axis) <= GateHalf)
					{
						continue;
					}
					for (const double Side : { -1.0, 1.0 })
					{
						AddBox(Merlons, FVector(Axis, Side * Spec.HalfExtentCm, Spec.WallHeightCm + MerlonHeight * 0.5), FVector(MerlonWidth, MerlonDepth, MerlonHeight));
						AddBox(Merlons, FVector(Side * Spec.HalfExtentCm, Axis, Spec.WallHeightCm + MerlonHeight * 0.5), FVector(MerlonWidth, MerlonDepth, MerlonHeight));
					}
				}
			}

			TotalInstanceCount = 0;
			for (const FWhiteboxInstanceBatch& Batch : InstanceBatches)
			{
				TotalInstanceCount += Batch.Transforms.Num();
			}
		}

		bool AddNextInstanceChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			while (CurrentBatchIndex < InstanceBatches.Num() && CurrentInstanceIndex >= InstanceBatches[CurrentBatchIndex].Transforms.Num())
			{
				if (UHierarchicalInstancedStaticMeshComponent* Component = InstanceBatches[CurrentBatchIndex].Component.Get())
				{
					Component->BuildTreeIfOutdated(true, true);
					Component->MarkRenderStateDirty();
					Component->MarkPackageDirty();
				}
				++CurrentBatchIndex;
				CurrentInstanceIndex = 0;
			}
			if (CurrentBatchIndex >= InstanceBatches.Num())
			{
				Phase = EWhiteboxTaskPhase::ReplacePrevious;
				return true;
			}

			FWhiteboxInstanceBatch& Batch = InstanceBatches[CurrentBatchIndex];
			UHierarchicalInstancedStaticMeshComponent* Component = Batch.Component.Get();
			if (!Component)
			{
				Error = FString::Printf(TEXT("Component %s became invalid."), *Batch.ComponentName.ToString());
				return false;
			}
			const int32 EndIndex = FMath::Min(CurrentInstanceIndex + InstancesPerStep, Batch.Transforms.Num());
			TArray<FTransform> Chunk;
			Chunk.Append(&Batch.Transforms[CurrentInstanceIndex], EndIndex - CurrentInstanceIndex);
			Component->AddInstances(Chunk, false, false, false);
			AddedInstanceCount += Chunk.Num();
			CurrentInstanceIndex = EndIndex;
			Context.ReportProgress(0.62 + 0.25 * static_cast<double>(AddedInstanceCount) / FMath::Max(TotalInstanceCount, 1), TEXT("Adding city wall HISM instances"));
			return true;
		}

		void DeleteNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			if (Action == TEXT("clear_folder") && bDryRun)
			{
				Phase = EWhiteboxTaskPhase::Complete;
				return;
			}
			int32 Processed = 0;
			while (NextManagedActorIndex < ManagedActors.Num() && Processed < ActorsPerDeleteStep)
			{
				AActor* Actor = ManagedActors[NextManagedActorIndex++].Get();
				++Processed;
				if (IsValid(Actor) && Actor != NewActor.Get())
				{
					Actor->Modify();
					if (World->DestroyActor(Actor))
					{
						++DeletedActorCount;
					}
					else
					{
						++FailedDeleteCount;
					}
				}
			}
			if (NextManagedActorIndex >= ManagedActors.Num())
			{
				if (NewActor.IsValid())
				{
					NewActor->SetActorLabel(Spec.ActorLabel);
					NewActor->MarkPackageDirty();
				}
				Phase = EWhiteboxTaskPhase::Complete;
			}
			Context.ReportProgress(0.88 + 0.1 * static_cast<double>(NextManagedActorIndex) / FMath::Max(ManagedActors.Num(), 1),
				TEXT("Replacing previous managed Whitebox actors"));
		}

		bool AlignNextActor(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget)
		{
			if (!AlignActor.IsValid() && NextManagedActorIndex >= ManagedActors.Num())
			{
				Phase = EWhiteboxTaskPhase::Complete;
				return true;
			}
			if (!AlignActor.IsValid())
			{
				AActor* Actor = ManagedActors[NextManagedActorIndex++].Get();
				if (!IsValid(Actor))
				{
					return true;
				}
				AlignActor = Actor;

				Actor->GetActorBounds(false, AlignBoundsOrigin, AlignBoundsExtent, true);
				TSharedRef<FJsonObject> SampleArgs = MakeShared<FJsonObject>();
				SampleArgs->SetStringField(TEXT("action"), TEXT("sample_batch"));
				CopyLandscapeSelector(Arguments, SampleArgs);
				SampleArgs->SetNumberField(TEXT("maxReadVertices"), 1);
				SampleArgs->SetNumberField(TEXT("maxPoints"), 5);
				TArray<TSharedPtr<FJsonValue>> Points;
				AddPointValue(Points, AlignBoundsOrigin.X, AlignBoundsOrigin.Y);
				AddPointValue(Points, AlignBoundsOrigin.X - AlignBoundsExtent.X, AlignBoundsOrigin.Y - AlignBoundsExtent.Y);
				AddPointValue(Points, AlignBoundsOrigin.X + AlignBoundsExtent.X, AlignBoundsOrigin.Y - AlignBoundsExtent.Y);
				AddPointValue(Points, AlignBoundsOrigin.X + AlignBoundsExtent.X, AlignBoundsOrigin.Y + AlignBoundsExtent.Y);
				AddPointValue(Points, AlignBoundsOrigin.X - AlignBoundsExtent.X, AlignBoundsOrigin.Y + AlignBoundsExtent.Y);
				SampleArgs->SetArrayField(TEXT("points"), MoveTemp(Points));
				TerrainStepper = LandscapePort->CreateTaskStepper(SampleArgs);
				if (!TerrainStepper.IsValid())
				{
					Error = TEXT("Landscape did not create a sample_batch stepper for Whitebox alignment.");
					return false;
				}
			}

			const Execution::FMcpTaskStepResult StepResult = TerrainStepper->Step(Context, FrameBudget);
			if (StepResult.State == Execution::EMcpTaskStepState::Continue)
			{
				return true;
			}
			TerrainStepper.Reset();
			AActor* Actor = AlignActor.Get();
			if (!IsValid(Actor))
			{
				AlignActor.Reset();
				return true;
			}
			if (StepResult.State == Execution::EMcpTaskStepState::Failed)
			{
				Error = FString::Printf(TEXT("Landscape alignment failed for %s: %s"), Actor ? *Actor->GetActorLabel() : TEXT("invalid actor"), *StepResult.Error);
				return false;
			}

			TSharedPtr<FJsonObject> Result;
			const FString& ResultJson = StepResult.ValueJson;
			bool bSuccess = false;
			double HeightMax = 0.0;
			if (!TryReadJsonObject(ResultJson, Result) || !Result->TryGetBoolField(TEXT("success"), bSuccess) || !bSuccess ||
				!Result->TryGetNumberField(TEXT("heightMax"), HeightMax))
			{
				Error = FString::Printf(TEXT("Landscape alignment failed for %s: %s"), *Actor->GetActorLabel(), *ResultJson.Left(1000));
				return false;
			}
			const double CurrentBottom = AlignBoundsOrigin.Z - AlignBoundsExtent.Z;
			Actor->Modify();
			Actor->AddActorWorldOffset(FVector(0.0, 0.0, HeightMax + AlignClearanceCm - CurrentBottom));
			Actor->MarkPackageDirty();
			++AlignedActorCount;
			AlignActor.Reset();
			Context.ReportProgress(0.15 + 0.8 * static_cast<double>(NextManagedActorIndex) / FMath::Max(ManagedActors.Num(), 1),
				TEXT("Aligning managed Whitebox actors to Landscape"));
			return true;
		}

		Execution::FMcpTaskStepResult Succeed()
		{
			if (bCompletedWithError)
			{
				return Fail(Error);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("folder"), Folder);
			Result->SetNumberField(TEXT("scannedActorCount"), ScannedActorCount);
			Result->SetNumberField(TEXT("matchedActorCount"), ManagedActors.Num());
			if (Action == TEXT("build_city_wall"))
			{
				Result->SetStringField(TEXT("actorLabel"), Spec.ActorLabel);
				Result->SetNumberField(TEXT("actorCount"), NewActor.IsValid() ? 1 : 0);
				Result->SetNumberField(TEXT("componentCount"), InstanceBatches.Num() + 1);
				Result->SetNumberField(TEXT("instanceCount"), AddedInstanceCount);
				Result->SetNumberField(TEXT("replacedActorCount"), DeletedActorCount);
				Result->SetNumberField(TEXT("baseWorldZ"), BaseWorldZ);
				Result->SetNumberField(TEXT("terrainWorldZ"), TerrainWorldZ);
				Result->SetNumberField(TEXT("terrainSampleCount"), SampleCount);
				Result->SetBoolField(TEXT("flattenedFootprint"), bFlattenedFootprint);
			}
			else if (Action == TEXT("clear_folder"))
			{
				Result->SetBoolField(TEXT("dryRun"), bDryRun);
				Result->SetNumberField(TEXT("deletedCount"), DeletedActorCount);
				Result->SetNumberField(TEXT("failedCount"), FailedDeleteCount);
				Result->SetNumberField(TEXT("remainingCount"), bDryRun ? ManagedActors.Num() : FailedDeleteCount);
			}
			else
			{
				Result->SetNumberField(TEXT("alignedCount"), AlignedActorCount);
			}
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

		Execution::FMcpTaskStepResult Fail(FString Message)
		{
			if (NewActor.IsValid() && World.IsValid())
			{
				World->DestroyActor(NewActor.Get());
				NewActor.Reset();
			}
			return Execution::FMcpTaskStepResult::Failed(MoveTemp(Message));
		}

		TSharedRef<FJsonObject> Arguments;
		TSharedRef<IUnrealAgentMCPLandscapePort> LandscapePort;
		TWeakObjectPtr<UWorld> World;
		TUniquePtr<TActorIterator<AActor>> DiscoveryIterator;
		TArray<TWeakObjectPtr<AActor>> ManagedActors;
		TWeakObjectPtr<AActor> NewActor;
		TWeakObjectPtr<AActor> AlignActor;
		TSharedPtr<Execution::IMcpTaskStepper> TerrainStepper;
		TArray<FWhiteboxInstanceBatch> InstanceBatches;
		FCityWallSpec Spec;
		EWhiteboxTaskPhase Phase = EWhiteboxTaskPhase::Resolve;
		FString Action;
		FString Folder;
		FString ActorLabelFilter;
		FString Error;
		double BaseWorldZ = 0.0;
		double TerrainWorldZ = 0.0;
		double AlignClearanceCm = 0.0;
		FVector AlignBoundsOrigin = FVector::ZeroVector;
		FVector AlignBoundsExtent = FVector::ZeroVector;
		int32 CurrentBatchIndex = 0;
		int32 CurrentInstanceIndex = 0;
		int32 NextManagedActorIndex = 0;
		int32 TotalInstanceCount = 0;
		int32 AddedInstanceCount = 0;
		int32 ScannedActorCount = 0;
		int32 DeletedActorCount = 0;
		int32 FailedDeleteCount = 0;
		int32 AlignedActorCount = 0;
		int32 SampleCount = 0;
		bool bRecursive = true;
		bool bDryRun = false;
		bool bFlattenedFootprint = false;
		bool bCompletedWithError = false;
	};

	FUnrealAgentMCPUnrealWhiteboxAdapter::FUnrealAgentMCPUnrealWhiteboxAdapter(TSharedRef<IUnrealAgentMCPLandscapePort> InLandscapePort) : LandscapePort(MoveTemp(InLandscapePort))
	{
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealWhiteboxAdapter::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("action"), Action) || !FUnrealAgentMCPWhiteboxService::GetImplementedActions().Contains(Action))
		{
			return nullptr;
		}
		if (Action == TEXT("patch_city_wall"))
		{
			return CreatePatchTaskStepper(Args);
		}
		return MakeShared<FUnrealAgentMCPWhiteboxTaskStepper>(Args, LandscapePort);
	}
}
