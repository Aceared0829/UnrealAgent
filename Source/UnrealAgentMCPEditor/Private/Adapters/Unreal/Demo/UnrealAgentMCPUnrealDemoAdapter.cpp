// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealDemoAdapter.cpp
 * @brief Neon Shrine 十九步场景构建、回家与隔离清理实现。
 */

#include "Adapters/Unreal/Demo/UnrealAgentMCPUnrealDemoAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "LevelEditorSubsystem.h"
#include "LevelSequence.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "PCGGraph.h"
#include "PCGVolume.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP
{
	namespace
	{
		struct FDemoStep
		{
			int32 Index;
			const TCHAR* Id;
			const TCHAR* Description;
		};

		const TArray<FDemoStep>& Steps()
		{
			static const TArray<FDemoStep> Values = { { 1, TEXT("create_level"), TEXT("创建并加载 DemoLevel") }, { 2, TEXT("materials"), TEXT("创建地面、发光与立柱材质") },
				{ 3, TEXT("floor"), TEXT("创建六十米暗色地面") }, { 4, TEXT("pedestal"), TEXT("创建中央基座") }, { 5, TEXT("hero_sphere"), TEXT("创建中央发光球") },
				{ 6, TEXT("pillars"), TEXT("创建四根角柱") }, { 7, TEXT("orbs"), TEXT("创建四个柱脚光球") }, { 8, TEXT("neon_lights"), TEXT("创建四盏彩色点光") },
				{ 9, TEXT("hero_light"), TEXT("创建主角暖色点光") }, { 10, TEXT("moonlight"), TEXT("创建月光方向光") }, { 11, TEXT("sky_light"), TEXT("创建环境 SkyLight") },
				{ 12, TEXT("fog"), TEXT("创建指数高度雾") }, { 13, TEXT("post_process"), TEXT("创建后处理体积") }, { 14, TEXT("niagara_vfx"), TEXT("创建 Niagara 效果锚点") },
				{ 15, TEXT("pcg_scatter"), TEXT("创建 PCG 散布体积与图") }, { 16, TEXT("orbit_rings"), TEXT("创建八个环绕球与旋转组件") },
				{ 17, TEXT("level_sequence"), TEXT("创建展示 LevelSequence") }, { 18, TEXT("tuning_panel"), TEXT("创建调参面板锚点") },
				{ 19, TEXT("save"), TEXT("保存当前关卡") } };
			return Values;
		}

		FString StringArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const FString& Default)
		{
			FString Value;
			return Args && Args->TryGetStringField(Name, Value) && !Value.IsEmpty() ? Value : Default;
		}

		bool BoolArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const bool Default)
		{
			bool Value = Default;
			return Args && Args->TryGetBoolField(Name, Value) ? Value : Default;
		}

		FString Success(const TSharedRef<FJsonObject>& Result)
		{
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("domain"), TEXT("demo"));
			return JsonObjectToString(Result);
		}

		FString Error(const FString& Message)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("demo"));
			Result->SetStringField(TEXT("error"), Message);
			return JsonObjectToString(Result);
		}

		FString GetSteps()
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FDemoStep& Step : Steps())
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("index"), Step.Index);
				Entry->SetStringField(TEXT("id"), Step.Id);
				Entry->SetStringField(TEXT("description"), Step.Description);
				Values.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("steps"), Values);
			Result->SetNumberField(TEXT("count"), Values.Num());
			return Success(Result);
		}

		UWorld* EditorWorld()
		{
			return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		}

		UObject* CreateAsset(UClass* Class, const FString& Root, const FString& Name)
		{
			if (!Class)
				return nullptr;
			const FString ObjectPath = Root / Name + TEXT(".") + Name;
			if (UObject* Existing = StaticLoadObject(Class, nullptr, *ObjectPath))
				return Existing;
			UPackage* Package = CreatePackage(*FPackageName::ObjectPathToPackageName(ObjectPath));
			UObject* Asset = NewObject<UObject>(Package, Class, *Name, RF_Public | RF_Standalone | RF_Transactional);
			if (Asset)
			{
				FAssetRegistryModule::AssetCreated(Asset);
				Asset->MarkPackageDirty();
				UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
			}
			return Asset;
		}

		AStaticMeshActor* SpawnMesh(const FString& Label, const FString& MeshPath, const FVector& Location, const FVector& Scale)
		{
			UWorld* World = EditorWorld();
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!World || !Mesh)
				return nullptr;
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
			if (!Actor)
				return nullptr;
			Actor->SetActorLabel(Label);
			Actor->SetActorScale3D(Scale);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			return Actor;
		}

		bool EnsureLevel(const FString& Path, FString& OutError)
		{
			ULevelEditorSubsystem* Levels = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
			if (!Levels)
			{
				OutError = TEXT("LevelEditorSubsystem 不可用。");
				return false;
			}
			if (UEditorAssetLibrary::DoesAssetExist(Path))
				return Levels->LoadLevel(Path);
			if (!Levels->NewLevel(Path))
			{
				OutError = FString::Printf(TEXT("创建关卡失败：%s"), *Path);
				return false;
			}
			return Levels->SaveCurrentLevel();
		}

		FString ExecuteStep(const int32 Index, const TSharedPtr<FJsonObject>& Args)
		{
			if (Index < 1 || Index > Steps().Num())
				return Error(TEXT("stepIndex 必须位于 1 到 19。"));
			const FString Root = StringArg(Args, TEXT("rootPath"), TEXT("/Game/Demo"));
			const FString Prefix = StringArg(Args, TEXT("actorPrefix"), TEXT("Demo_"));
			if (BoolArg(Args, TEXT("dryRun"), false))
			{
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("step"), Index);
				Result->SetStringField(TEXT("stepId"), Steps()[Index - 1].Id);
				Result->SetBoolField(TEXT("dryRun"), true);
				return Success(Result);
			}

			bool bCompleted = false;
			FString Detail;
			if (Index == 1)
			{
				bCompleted = EnsureLevel(Root / TEXT("DemoLevel"), Detail);
			}
			else if (Index == 2)
			{
				bCompleted = CreateAsset(UMaterial::StaticClass(), Root, TEXT("M_Demo_Floor")) && CreateAsset(UMaterial::StaticClass(), Root, TEXT("M_Demo_Glow")) &&
					CreateAsset(UMaterial::StaticClass(), Root, TEXT("M_Demo_Pillar"));
			}
			else if (Index >= 3 && Index <= 7)
			{
				if (Index == 3)
					bCompleted = SpawnMesh(Prefix + TEXT("Floor"), TEXT("/Engine/BasicShapes/Cube.Cube"), FVector::ZeroVector, FVector(60, 60, 0.1)) != nullptr;
				if (Index == 4)
					bCompleted = SpawnMesh(Prefix + TEXT("Pedestal"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0, 0, 100), FVector(3, 3, 2)) != nullptr;
				if (Index == 5)
					bCompleted = SpawnMesh(Prefix + TEXT("HeroSphere"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), FVector(0, 0, 350), FVector(2)) != nullptr;
				if (Index == 6 || Index == 7)
				{
					bCompleted = true;
					for (int32 Corner = 0; Corner < 4; ++Corner)
					{
						const float X = Corner < 2 ? -1200.0f : 1200.0f;
						const float Y = Corner % 2 == 0 ? -1200.0f : 1200.0f;
						const bool bPillar = Index == 6;
						bCompleted &= SpawnMesh(Prefix + (bPillar ? TEXT("Pillar_") : TEXT("Orb_")) + FString::FromInt(Corner + 1),
										  bPillar ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Engine/BasicShapes/Sphere.Sphere"),
										  FVector(X, Y, bPillar ? 300.0f : 100.0f), bPillar ? FVector(1.5f, 1.5f, 6.0f) : FVector(0.8f)) != nullptr;
					}
				}
			}
			else if (Index == 8 || Index == 9)
			{
				UWorld* World = EditorWorld();
				const int32 Count = Index == 8 ? 4 : 1;
				bCompleted = World != nullptr;
				for (int32 LightIndex = 0; World && LightIndex < Count; ++LightIndex)
				{
					APointLight* Light = World->SpawnActor<APointLight>(FVector((LightIndex - 1.5f) * 600.0f, 0, Index == 9 ? 600.0f : 250.0f), FRotator::ZeroRotator);
					if (!Light)
					{
						bCompleted = false;
						continue;
					}
					Light->SetActorLabel(Prefix + (Index == 8 ? TEXT("NeonLight_") : TEXT("HeroLight")) + FString::FromInt(LightIndex + 1));
					Light->PointLightComponent->SetIntensity(Index == 9 ? 8000.0f : 4000.0f);
					Light->PointLightComponent->SetLightColor(FLinearColor::MakeFromHSV8(LightIndex * 64, 220, 255));
				}
			}
			else if (Index == 10)
			{
				if (UWorld* World = EditorWorld())
				{
					ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>();
					bCompleted = Light != nullptr;
					if (Light)
					{
						Light->SetActorLabel(Prefix + TEXT("Moonlight"));
						Light->SetActorRotation(FRotator(-45, -30, 0));
					}
				}
			}
			else if (Index == 11)
			{
				if (UWorld* World = EditorWorld())
				{
					ASkyLight* Light = World->SpawnActor<ASkyLight>();
					bCompleted = Light != nullptr;
					if (Light)
					{
						Light->SetActorLabel(Prefix + TEXT("SkyLight"));
						Light->GetLightComponent()->SetIntensity(0.6f);
					}
				}
			}
			else if (Index == 12)
			{
				if (UWorld* World = EditorWorld())
				{
					AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>();
					bCompleted = Fog != nullptr;
					if (Fog)
					{
						Fog->SetActorLabel(Prefix + TEXT("Fog"));
						Fog->GetComponent()->SetFogDensity(0.02f);
					}
				}
			}
			else if (Index == 13)
			{
				if (UWorld* World = EditorWorld())
				{
					APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>();
					bCompleted = Volume != nullptr;
					if (Volume)
					{
						Volume->SetActorLabel(Prefix + TEXT("PostProcess"));
						Volume->bUnbound = true;
						Volume->Settings.bOverride_BloomIntensity = true;
						Volume->Settings.BloomIntensity = 2.0f;
					}
				}
			}
			else if (Index == 14 || Index == 18)
			{
				if (UWorld* World = EditorWorld())
				{
					AActor* Marker = World->SpawnActor<AActor>();
					bCompleted = Marker != nullptr;
					if (Marker)
						Marker->SetActorLabel(Prefix + (Index == 14 ? TEXT("NiagaraVFX") : TEXT("TuningPanel")));
				}
			}
			else if (Index == 15)
			{
				UPCGGraph* Graph = Cast<UPCGGraph>(CreateAsset(UPCGGraph::StaticClass(), Root, TEXT("PCG_Demo_Scatter")));
				if (UWorld* World = EditorWorld())
				{
					APCGVolume* Volume = World->SpawnActor<APCGVolume>();
					bCompleted = Graph && Volume;
					if (Volume)
						Volume->SetActorLabel(Prefix + TEXT("PCGScatter"));
				}
			}
			else if (Index == 16)
			{
				bCompleted = true;
				for (int32 Orb = 0; Orb < 8; ++Orb)
				{
					const float Angle = 2.0f * PI * Orb / 8.0f;
					AStaticMeshActor* Actor = SpawnMesh(Prefix + TEXT("Orbit_") + FString::FromInt(Orb + 1), TEXT("/Engine/BasicShapes/Sphere.Sphere"),
						FVector(FMath::Cos(Angle) * 600, FMath::Sin(Angle) * 600, 350), FVector(0.3f));
					bCompleted &= Actor != nullptr;
					if (Actor)
					{
						URotatingMovementComponent* Movement = NewObject<URotatingMovementComponent>(Actor);
						Movement->RotationRate = FRotator(0, 20, 0);
						Movement->RegisterComponent();
					}
				}
			}
			else if (Index == 17)
			{
				bCompleted = CreateAsset(ULevelSequence::StaticClass(), Root, TEXT("SEQ_Demo_Showcase")) != nullptr;
			}
			else if (Index == 19)
			{
				ULevelEditorSubsystem* Levels = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
				bCompleted = Levels && Levels->SaveCurrentLevel();
			}

			if (!bCompleted)
				return Error(Detail.IsEmpty() ? FString::Printf(TEXT("Demo 第 %d 步执行失败。"), Index) : Detail);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("step"), Index);
			Result->SetStringField(TEXT("stepId"), Steps()[Index - 1].Id);
			Result->SetStringField(TEXT("rootPath"), Root);
			return Success(Result);
		}
	}

	TArray<FString> FUnrealAgentMCPUnrealDemoAdapter::GetImplementedActions()
	{
		return { TEXT("step"), TEXT("get_steps"), TEXT("cleanup"), TEXT("go_home") };
	}

	FString FUnrealAgentMCPUnrealDemoAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		if (!GetImplementedActions().Contains(Action))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("demo"));
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Demo action '%s' 尚未迁移。"), *Action));
			return JsonObjectToString(Result);
		}
		return ExecuteAction(Action, Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>());
	}

	FString FUnrealAgentMCPUnrealDemoAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("get_steps"))
			return GetSteps();
		if (Action == TEXT("step"))
		{
			double Step = 0.0;
			if (!Args || (!Args->TryGetNumberField(TEXT("stepIndex"), Step) && !Args->TryGetNumberField(TEXT("step"), Step)))
				return GetSteps();
			return ExecuteStep(static_cast<int32>(Step), Args);
		}
		if (Action == TEXT("go_home"))
		{
			const FString Home = StringArg(Args, TEXT("homeLevelPath"), TEXT("/Game/MCP_Home"));
			if (BoolArg(Args, TEXT("dryRun"), false))
			{
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("levelPath"), Home);
				Result->SetBoolField(TEXT("dryRun"), true);
				return Success(Result);
			}
			FString ErrorText;
			if (!EnsureLevel(Home, ErrorText))
				return Error(ErrorText);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("levelPath"), Home);
			return Success(Result);
		}
		if (Action == TEXT("cleanup"))
		{
			const FString Root = StringArg(Args, TEXT("rootPath"), TEXT("/Game/Demo"));
			const FString Prefix = StringArg(Args, TEXT("actorPrefix"), TEXT("Demo_"));
			if (BoolArg(Args, TEXT("dryRun"), false))
			{
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("rootPath"), Root);
				Result->SetStringField(TEXT("actorPrefix"), Prefix);
				Result->SetBoolField(TEXT("dryRun"), true);
				return Success(Result);
			}
			int32 ActorsDeleted = 0;
			if (UWorld* World = EditorWorld())
			{
				TArray<AActor*> Actors;
				for (TActorIterator<AActor> It(World); It; ++It)
					if (It->GetActorLabel().StartsWith(Prefix))
						Actors.Add(*It);
				for (AActor* Actor : Actors)
				{
					World->DestroyActor(Actor);
					++ActorsDeleted;
				}
			}
			int32 AssetsDeleted = 0;
			if (UEditorAssetLibrary::DoesDirectoryExist(Root))
			{
				AssetsDeleted = UEditorAssetLibrary::ListAssets(Root, true, false).Num();
				UEditorAssetLibrary::DeleteDirectory(Root);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("actorsDeleted"), ActorsDeleted);
			Result->SetNumberField(TEXT("assetsDeleted"), AssetsDeleted);
			return Success(Result);
		}
		return Error(FString::Printf(TEXT("未识别的 Demo 操作：%s"), *Action));
	}
}
