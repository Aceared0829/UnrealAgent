// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.Assets.cpp
 * @brief Level 关联的 Nanite、后处理、FBX 导出与骨骼网格预览实现。
 */

#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/Skeleton.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Scene.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Exporters/Exporter.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"

namespace UnrealAgentMCP
{
	namespace
	{
		AActor* FindAssetActor(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			FString Label;
			if (!Args.IsValid() || !Args->TryGetStringField(TEXT("name"), Label) || Label.IsEmpty())
			{
				OutError = TEXT("缺少必填 actorLabel。");
				return nullptr;
			}
			AActor* Actor = ActorSupport::FindActorByNameOrLabel(ActorSupport::GetEditorWorld(), Label);
			if (!Actor)
			{
				OutError = FString::Printf(TEXT("未找到 Actor：%s"), *Label);
			}
			return Actor;
		}

		template <typename TObjectType> TObjectType* LoadLevelAsset(const FString& Path)
		{
			return LoadObject<TObjectType>(nullptr, *JsonConversion::NormalizeAssetObjectPath(Path));
		}

		TSharedRef<FJsonObject> MakeTransformRecord(const FTransform& Transform)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Transform.GetLocation()));
			Result->SetObjectField(TEXT("rotation"), JsonConversion::MakeRotatorObject(Transform.Rotator()));
			Result->SetObjectField(TEXT("scale"), JsonConversion::MakeVectorObject(Transform.GetScale3D()));
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> MakeMaterialPaths(const UMeshComponent* Component)
		{
			TArray<TSharedPtr<FJsonValue>> Materials;
			if (!Component)
			{
				return Materials;
			}
			for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
			{
				UMaterialInterface* Material = Component->GetMaterial(Index);
				Materials.Add(MakeShared<FJsonValueString>(Material ? Material->GetPathName() : FString()));
			}
			return Materials;
		}
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetNaniteSettings(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), AssetPath))
		{
			return ErrorJson(TEXT("缺少 assetPath。"));
		}
		UStaticMesh* Mesh = LoadLevelAsset<UStaticMesh>(AssetPath);
		if (!Mesh)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 StaticMesh：%s"), *AssetPath));
		}
		FMeshNaniteSettings Settings = Mesh->GetNaniteSettings();
		bool bEnabled = true;
		Args->TryGetBoolField(TEXT("enabled"), bEnabled);
		Settings.bEnabled = bEnabled;
		double Precision = 0.0;
		if (Args->TryGetNumberField(TEXT("positionPrecision"), Precision))
		{
			Settings.PositionPrecision = static_cast<int32>(Precision);
		}

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetNaniteSettings", "MCP 修改 Nanite"));
		Mesh->Modify();
		Mesh->SetNaniteSettings(Settings);
		Mesh->Build(true);
		Mesh->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
		Result->SetBoolField(TEXT("naniteEnabled"), Settings.bEnabled);
		Result->SetNumberField(TEXT("positionPrecision"), Settings.PositionPrecision);
		Result->SetNumberField(TEXT("numLODs"), Mesh->GetNumLODs());
		Result->SetBoolField(TEXT("rebuilt"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetNaniteInfo(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("assetPath"), AssetPath))
		{
			return ErrorJson(TEXT("缺少 assetPath。"));
		}
		UStaticMesh* Mesh = LoadLevelAsset<UStaticMesh>(AssetPath);
		if (!Mesh)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 StaticMesh：%s"), *AssetPath));
		}
		const FMeshNaniteSettings& Settings = Mesh->GetNaniteSettings();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Mesh->GetPathName());
		Result->SetBoolField(TEXT("naniteEnabled"), Settings.bEnabled);
		Result->SetNumberField(TEXT("positionPrecision"), Settings.PositionPrecision);
		Result->SetNumberField(TEXT("numLODs"), Mesh->GetNumLODs());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::AddPostProcessBlendable(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		APostProcessVolume* Volume = Cast<APostProcessVolume>(FindAssetActor(Args, Error));
		if (!Volume)
		{
			return ErrorJson(Error.IsEmpty() ? TEXT("目标不是 PostProcessVolume。") : Error);
		}
		FString MaterialPath;
		if (!Args->TryGetStringField(TEXT("materialPath"), MaterialPath))
		{
			return ErrorJson(TEXT("缺少 materialPath。"));
		}
		UMaterialInterface* Material = LoadLevelAsset<UMaterialInterface>(MaterialPath);
		if (!Material)
		{
			return ErrorJson(FString::Printf(TEXT("未找到材质：%s"), *MaterialPath));
		}
		double Weight = 1.0;
		Args->TryGetNumberField(TEXT("weight"), Weight);

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "AddPostProcessBlendable", "MCP 添加后处理混合材质"));
		Volume->Modify();
		bool bUpdated = false;
		for (FWeightedBlendable& Blendable : Volume->Settings.WeightedBlendables.Array)
		{
			if (Blendable.Object == Material)
			{
				Blendable.Weight = Weight;
				bUpdated = true;
				break;
			}
		}
		if (!bUpdated)
		{
			Volume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(Weight, Material));
		}
		Volume->PostEditChange();
		Volume->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Volume->GetActorLabel());
		Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Result->SetNumberField(TEXT("weight"), Weight);
		Result->SetBoolField(TEXT("updatedExisting"), bUpdated);
		Result->SetNumberField(TEXT("blendableCount"), Volume->Settings.WeightedBlendables.Array.Num());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ExportActorFbx(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindAssetActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		FString OutputPath;
		if (!Args->TryGetStringField(TEXT("outputPath"), OutputPath) || !OutputPath.EndsWith(TEXT(".fbx"), ESearchCase::IgnoreCase))
		{
			return ErrorJson(TEXT("outputPath 必须是 .fbx 文件。"));
		}
		OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

		UObject* MeshAsset = nullptr;
		UMeshComponent* MeshComponent = nullptr;
		FString SkeletonPath;
		if (USkeletalMeshComponent* Skeletal = Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			MeshAsset = Skeletal->GetSkeletalMeshAsset();
			MeshComponent = Skeletal;
			if (USkeletalMesh* Mesh = Skeletal->GetSkeletalMeshAsset(); Mesh && Mesh->GetSkeleton())
			{
				SkeletonPath = Mesh->GetSkeleton()->GetPathName();
			}
		}
		else if (UStaticMeshComponent* Static = Actor->FindComponentByClass<UStaticMeshComponent>())
		{
			MeshAsset = Static->GetStaticMesh();
			MeshComponent = Static;
		}
		if (!MeshAsset || !MeshComponent)
		{
			return ErrorJson(TEXT("Actor 没有可导出的骨骼或静态网格。"));
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
		const bool bExported = UExporter::ExportToFile(MeshAsset, nullptr, *OutputPath, false, false, false) != 0;
		if (!bExported)
		{
			return ErrorJson(TEXT("FBX 导出失败；当前构建未找到兼容导出器。"));
		}

		TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
		Metadata->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Metadata->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetPathName());
		Metadata->SetStringField(TEXT("mesh"), MeshAsset->GetPathName());
		Metadata->SetStringField(TEXT("skeleton"), SkeletonPath);
		Metadata->SetObjectField(TEXT("transform"), MakeTransformRecord(Actor->GetActorTransform()));
		Metadata->SetArrayField(TEXT("materials"), MakeMaterialPaths(MeshComponent));
		const FString MetadataPath = FPaths::ChangeExtension(OutputPath, TEXT("json"));
		if (!FFileHelper::SaveStringToFile(JsonObjectToString(Metadata), *MetadataPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return ErrorJson(TEXT("FBX 已导出，但元数据 JSON 写入失败。"));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("fbxPath"), OutputPath);
		Result->SetStringField(TEXT("metadataPath"), MetadataPath);
		Result->SetStringField(TEXT("mesh"), MeshAsset->GetPathName());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SpawnSkeletalMeshActor(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString SkeletalMeshPath;
		if (!World || !Args.IsValid() || !Args->TryGetStringField(TEXT("skeletalMesh"), SkeletalMeshPath))
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 skeletalMesh。"));
		}
		USkeletalMesh* Mesh = LoadLevelAsset<USkeletalMesh>(SkeletalMeshPath);
		if (!Mesh)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 SkeletalMesh：%s"), *SkeletalMeshPath));
		}
		FVector Location = FVector::ZeroVector;
		FVector Scale = FVector::OneVector;
		FRotator Rotation = FRotator::ZeroRotator;
		JsonConversion::TryGetVectorField(Args, TEXT("location"), Location);
		JsonConversion::TryGetVectorField(Args, TEXT("scale"), Scale);
		JsonConversion::TryGetRotatorField(Args, TEXT("rotation"), Rotation);

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SpawnSkeletalMeshActor", "MCP 创建骨骼网格 Actor"));
		ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>(Location, Rotation);
		if (!Actor || !Actor->GetSkeletalMeshComponent())
		{
			return ErrorJson(TEXT("SkeletalMeshActor 创建失败。"));
		}
		USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
		Component->SetSkeletalMeshAsset(Mesh);
		Actor->SetActorScale3D(Scale);
		FString Label;
		if (Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}

		TArray<FString> MaterialPaths;
		if (Args->TryGetStringArrayField(TEXT("materials"), MaterialPaths))
		{
			for (int32 Index = 0; Index < MaterialPaths.Num(); ++Index)
			{
				if (UMaterialInterface* Material = LoadLevelAsset<UMaterialInterface>(MaterialPaths[Index]))
				{
					Component->SetMaterial(Index, Material);
				}
			}
		}

		FString AnimationPath;
		bool bLoop = true;
		Args->TryGetBoolField(TEXT("loop"), bLoop);
		if (Args->TryGetStringField(TEXT("animSequence"), AnimationPath) && !AnimationPath.IsEmpty())
		{
			UAnimSequence* Sequence = LoadLevelAsset<UAnimSequence>(AnimationPath);
			if (!Sequence)
			{
				World->DestroyActor(Actor);
				return ErrorJson(FString::Printf(TEXT("未找到 AnimSequence：%s"), *AnimationPath));
			}
			Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Component->SetAnimation(Sequence);
			Component->PlayAnimation(Sequence, bLoop);
		}
		Actor->PostEditChange();
		Actor->MarkPackageDirty();

		FVector BoundsOrigin;
		FVector BoundsExtent;
		Actor->GetActorBounds(false, BoundsOrigin, BoundsExtent, true);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), ActorSupport::MakeActorObject(Actor));
		Result->SetObjectField(TEXT("boxExtent"), JsonConversion::MakeVectorObject(BoundsExtent));
		Result->SetStringField(TEXT("skeletalMesh"), Mesh->GetPathName());
		Result->SetStringField(TEXT("animSequence"), AnimationPath);
		Result->SetBoolField(TEXT("loop"), bLoop);
		return SuccessJson(Result);
	}
}
