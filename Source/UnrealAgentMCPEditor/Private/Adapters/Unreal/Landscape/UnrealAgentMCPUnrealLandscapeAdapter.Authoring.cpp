// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLandscapeAdapter.Authoring.cpp
 * @brief 地形、图层信息资产和材质的创建与配置实现。
 */

#include "Adapters/Unreal/Landscape/UnrealAgentMCPUnrealLandscapeAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool ShouldStopLandscapeAuthoringTask(FString& OutError)
		{
			const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent();
			if (!Context || !Context->ShouldStop())
			{
				return false;
			}
			OutError = Context->IsDeadlineExceeded() ? TEXT("Landscape task deadline exceeded.") : Context->GetCancellationReason();
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Landscape task cancelled.");
			}
			return true;
		}

		void ReportLandscapeAuthoringTaskProgress(const double Fraction, const TCHAR* Message)
		{
			if (const Execution::FMcpTaskExecutionContext* Context = Execution::FMcpTaskExecutionContext::GetCurrent())
			{
				Context->ReportProgress(Fraction, Message);
			}
		}

		FString NormalizeLandscapePackagePath(FString Path)
		{
			Path.TrimStartAndEndInline();
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (Path.EndsWith(TEXT("/")))
				Path.LeftChopInline(1);
			return Path.IsEmpty() ? TEXT("/Game/Landscape/LayerInfos") : Path;
		}

		bool SaveLayerInfo(ULandscapeLayerInfoObject* LayerInfo, FString& OutError)
		{
			if (!LayerInfo || !LayerInfo->IsAsset())
				return true;
			UPackage* Package = LayerInfo->GetOutermost();
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(Package, LayerInfo, *Filename, SaveArgs))
			{
				OutError = TEXT("Landscape LayerInfo 资产保存失败。");
				return false;
			}
			return true;
		}

		ULandscapeLayerInfoObject* CreateOrLoadLayerInfo(const TSharedPtr<FJsonObject>& Args, FString& OutAssetPath, bool& bOutExisted, FString& OutError)
		{
			FString LayerName;
			if (!Args->TryGetStringField(TEXT("layerName"), LayerName) || LayerName.IsEmpty())
			{
				OutError = TEXT("缺少必填 layerName。");
				return nullptr;
			}
			FString AssetName = TEXT("LI_") + LayerName;
			Args->TryGetStringField(TEXT("name"), AssetName);
			if (!FName::IsValidXName(AssetName, INVALID_OBJECTNAME_CHARACTERS))
			{
				OutError = TEXT("LayerInfo 资产名称包含非法字符。");
				return nullptr;
			}
			FString PackagePath = TEXT("/Game/Landscape/LayerInfos");
			Args->TryGetStringField(TEXT("packagePath"), PackagePath);
			PackagePath = NormalizeLandscapePackagePath(PackagePath);
			if (!PackagePath.StartsWith(TEXT("/Game")))
			{
				OutError = TEXT("packagePath 必须位于 /Game。");
				return nullptr;
			}

			const FString PackageName = PackagePath + TEXT("/") + AssetName;
			OutAssetPath = PackageName + TEXT(".") + AssetName;
			ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *OutAssetPath);
			bOutExisted = LayerInfo != nullptr;
			if (!LayerInfo)
			{
				UPackage* Package = CreatePackage(*PackageName);
				LayerInfo = NewObject<ULandscapeLayerInfoObject>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
				if (!LayerInfo)
				{
					OutError = TEXT("Landscape LayerInfo 创建失败。");
					return nullptr;
				}
				FAssetRegistryModule::AssetCreated(LayerInfo);
			}

			LayerInfo->Modify();
			LayerInfo->SetLayerName(FName(*LayerName), false);
			double Hardness = LayerInfo->GetHardness();
			if (Args->TryGetNumberField(TEXT("hardness"), Hardness))
			{
				LayerInfo->SetHardness(FMath::Clamp(static_cast<float>(Hardness), 0.0f, 1.0f), false, EPropertyChangeType::ValueSet);
			}
			FString PhysMaterialPath;
			if (Args->TryGetStringField(TEXT("physMaterial"), PhysMaterialPath) && !PhysMaterialPath.IsEmpty())
			{
				UPhysicalMaterial* PhysicalMaterial = LoadObject<UPhysicalMaterial>(nullptr, *JsonConversion::NormalizeAssetObjectPath(PhysMaterialPath));
				if (!PhysicalMaterial)
				{
					OutError = FString::Printf(TEXT("未找到 PhysicalMaterial：%s"), *PhysMaterialPath);
					return nullptr;
				}
				LayerInfo->SetPhysicalMaterial(PhysicalMaterial, false);
			}
			LayerInfo->PostEditChange();
			LayerInfo->MarkPackageDirty();
			if (!SaveLayerInfo(LayerInfo, OutError))
				return nullptr;
			return LayerInfo;
		}

		TSharedRef<FJsonObject> MakeLayerInfoResult(ULandscapeLayerInfoObject* LayerInfo, const bool bExisted)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), LayerInfo->GetName());
			Result->SetStringField(TEXT("layerName"), LayerInfo->GetLayerName().ToString());
			Result->SetStringField(TEXT("assetPath"), LayerInfo->GetPathName());
			Result->SetNumberField(TEXT("hardness"), LayerInfo->GetHardness());
			Result->SetStringField(TEXT("physicalMaterialPath"), LayerInfo->GetPhysicalMaterial() ? LayerInfo->GetPhysicalMaterial()->GetPathName() : FString());
			Result->SetBoolField(TEXT("existed"), bExisted);
			Result->SetBoolField(TEXT("saved"), true);
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::CreateLayerInfo(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		FString Error;
		bool bExisted = false;
		ULandscapeLayerInfoObject* LayerInfo = CreateOrLoadLayerInfo(Args, AssetPath, bExisted, Error);
		if (!LayerInfo)
			return ErrorJson(Error);
		return SuccessJson(MakeLayerInfoResult(LayerInfo, bExisted));
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::AddLayerInfo(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = ResolveProxy(Args, &Error);
		if (!Proxy)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);
		FString AssetPath;
		bool bExisted = false;
		ULandscapeLayerInfoObject* LayerInfo = CreateOrLoadLayerInfo(Args, AssetPath, bExisted, Error);
		if (!LayerInfo)
			return ErrorJson(Error);

		const FName LayerName = LayerInfo->GetLayerName();
		Proxy->Modify();
		if (Proxy->HasTargetLayer(LayerName))
		{
			Proxy->UpdateTargetLayer(LayerName, FLandscapeTargetLayerSettings(LayerInfo));
		}
		else
		{
			Proxy->AddTargetLayer(LayerName, FLandscapeTargetLayerSettings(LayerInfo));
		}
		if (ULandscapeInfo* Info = Proxy->GetLandscapeInfo())
			Info->UpdateLayerInfoMap(Proxy, true);
		Proxy->MarkPackageDirty();

		TSharedRef<FJsonObject> Result = MakeLayerInfoResult(LayerInfo, bExisted);
		Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
		Result->SetBoolField(TEXT("attached"), true);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::SetMaterial(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		ALandscapeProxy* Proxy = ResolveProxy(Args, &Error);
		if (!Proxy)
			return ErrorJson(Error.IsEmpty() ? TEXT("当前世界中未找到目标 Landscape。") : Error);
		FString MaterialPath;
		if (!Args->TryGetStringField(TEXT("materialPath"), MaterialPath) || MaterialPath.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 materialPath。"));
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *JsonConversion::NormalizeAssetObjectPath(MaterialPath));
		if (!Material)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 MaterialInterface：%s"), *MaterialPath));
		}
		Proxy->Modify();
		Proxy->LandscapeMaterial = Material;
		Proxy->PostEditChange();
		Proxy->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("landscape"), Proxy->GetActorLabel());
		Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Result->SetNumberField(TEXT("componentCount"), Proxy->LandscapeComponents.Num());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLandscapeAdapter::Create(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
			return ErrorJson(TEXT("当前编辑器世界不可用。"));

		double ComponentCountXNumber = 8.0;
		double ComponentCountYNumber = 8.0;
		double SubsectionSizeNumber = 63.0;
		double NumSubsectionsNumber = 1.0;
		double HeightOffsetNumber = LandscapeDataAccess::MidValue;
		Args->TryGetNumberField(TEXT("componentCountX"), ComponentCountXNumber);
		Args->TryGetNumberField(TEXT("componentCountY"), ComponentCountYNumber);
		Args->TryGetNumberField(TEXT("subsectionSizeQuads"), SubsectionSizeNumber);
		Args->TryGetNumberField(TEXT("numSubsections"), NumSubsectionsNumber);
		Args->TryGetNumberField(TEXT("heightOffset"), HeightOffsetNumber);
		const int32 ComponentCountX = static_cast<int32>(ComponentCountXNumber);
		const int32 ComponentCountY = static_cast<int32>(ComponentCountYNumber);
		const int32 SubsectionSizeQuads = static_cast<int32>(SubsectionSizeNumber);
		const int32 NumSubsections = static_cast<int32>(NumSubsectionsNumber);
		const TSet<int32> ValidSubsectionSizes = { 7, 15, 31, 63, 127, 255 };
		if (ComponentCountX < 1 || ComponentCountY < 1 || !ValidSubsectionSizes.Contains(SubsectionSizeQuads) || (NumSubsections != 1 && NumSubsections != 2))
		{
			return ErrorJson(TEXT("地形尺寸无效：组件数必须大于零，"
								  "subsectionSizeQuads 必须为 "
								  "7/15/31/63/127/255，"
								  "numSubsections 必须为 1 或 2。"));
		}

		const int32 ComponentSizeQuads = SubsectionSizeQuads * NumSubsections;
		const int32 VertexCountX = ComponentCountX * ComponentSizeQuads + 1;
		const int32 VertexCountY = ComponentCountY * ComponentSizeQuads + 1;
		const int64 VertexCount = static_cast<int64>(VertexCountX) * VertexCountY;
		if (VertexCount > 4000000)
		{
			return ErrorJson(TEXT("本次创建超过 400 万顶点安全上限。"));
		}
		constexpr int64 InteractiveVertexLimit = 262144;
		bool bAllowLongFrame = false;
		Args->TryGetBoolField(TEXT("allowLongFrame"), bAllowLongFrame);
		if (VertexCount > InteractiveVertexLimit && !bAllowLongFrame)
		{
			return ErrorJson(FString::Printf(
				TEXT("本次创建包含 %lld 个顶点，超过交互式安全上限 %lld。Landscape::Import 是不可切分的 GameThread 调用；若已接受可能出现长帧，请显式传入 allowLongFrame=true。"),
				VertexCount, InteractiveVertexLimit));
		}
		FString TaskStopError;
		if (ShouldStopLandscapeAuthoringTask(TaskStopError))
		{
			return ErrorJson(TaskStopError);
		}

		FVector Location = FVector::ZeroVector;
		JsonConversion::TryGetVectorField(Args, TEXT("location"), Location);
		FVector Scale(100.0, 100.0, 100.0);
		if (!JsonConversion::TryGetVectorField(Args, TEXT("scale"), Scale))
		{
			double ScaleNumber = 100.0;
			if (Args->TryGetNumberField(TEXT("scale"), ScaleNumber))
				Scale = FVector(ScaleNumber);
		}
		FString Label = TEXT("Landscape_MCP");
		const bool bHasExplicitLabel = Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty();
		bool bReplaceExisting = false;
		Args->TryGetBoolField(TEXT("replaceExisting"), bReplaceExisting);
		if (bReplaceExisting && !bHasExplicitLabel)
		{
			return ErrorJson(TEXT("replaceExisting=true 时必须显式提供非空 label。"));
		}

		TArray<ALandscape*> ExistingLandscapes;
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			ALandscape* Existing = *It;
			if (IsValid(Existing) && Existing->GetActorLabel() == Label)
			{
				ExistingLandscapes.Add(Existing);
			}
		}
		if (ExistingLandscapes.Num() > 1)
		{
			return ErrorJson(FString::Printf(TEXT("存在 %d 个标签为 %s 的主 Landscape，无法安全替换；请先按 landscapeGuid 清理歧义。"), ExistingLandscapes.Num(), *Label));
		}
		ALandscape* ReplacedLandscape = ExistingLandscapes.IsEmpty() ? nullptr : ExistingLandscapes[0];
		if (ReplacedLandscape && !bReplaceExisting)
		{
			return ErrorJson(FString::Printf(TEXT("标签为 %s 的 Landscape 已存在；若要原位重建，请显式传入 replaceExisting=true。"), *Label));
		}
		const FString ReplacedName = ReplacedLandscape ? ReplacedLandscape->GetName() : FString();
		const FString ReplacedGuid = ReplacedLandscape ? ReplacedLandscape->GetLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphensLower) : FString();

		ALandscape* Landscape = World->SpawnActor<ALandscape>(ALandscape::StaticClass(), Location, FRotator::ZeroRotator);
		if (!Landscape)
			return ErrorJson(TEXT("ALandscape 创建失败。"));
		Landscape->SetActorScale3D(Scale);
		Landscape->SetActorLabel(Label);

		const uint16 HeightValue = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(HeightOffsetNumber), 0, LandscapeDataAccess::MaxValue));
		TArray<uint16> Heights;
		Heights.Init(HeightValue, VertexCount);
		if (ShouldStopLandscapeAuthoringTask(TaskStopError))
		{
			World->EditorDestroyActor(Landscape, false);
			return ErrorJson(TaskStopError);
		}
		TMap<FGuid, TArray<uint16>> HeightData;
		HeightData.Add(FGuid(), MoveTemp(Heights));
		TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayers;
		MaterialLayers.Add(FGuid(), {});
		ReportLandscapeAuthoringTaskProgress(0.45, TEXT("Importing landscape"));
		const double ImportStartSeconds = FPlatformTime::Seconds();
		Landscape->Import(FGuid::NewGuid(), 0, 0, VertexCountX - 1, VertexCountY - 1, NumSubsections, SubsectionSizeQuads, HeightData, nullptr, MaterialLayers,
			ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
		const double ImportDurationMs = (FPlatformTime::Seconds() - ImportStartSeconds) * 1000.0;
		ReportLandscapeAuthoringTaskProgress(0.9, TEXT("Finalizing landscape"));
		ULandscapeInfo* Info = Landscape->CreateLandscapeInfo();
		if (!Info)
		{
			World->EditorDestroyActor(Landscape, false);
			return ErrorJson(TEXT("LandscapeInfo 创建失败。"));
		}
		if (Landscape->GetLayersConst().IsEmpty())
			Landscape->CreateDefaultLayer();
		Landscape->RequestLayersInitialization(true, true);
		Landscape->MarkPackageDirty();
		if (ReplacedLandscape && !World->EditorDestroyActor(ReplacedLandscape, true))
		{
			World->EditorDestroyActor(Landscape, false);
			return ErrorJson(FString::Printf(TEXT("新 Landscape 已创建，但旧 Landscape %s 无法删除；本次替换已回滚。"), *ReplacedName));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Landscape->GetName());
		Result->SetStringField(TEXT("label"), Landscape->GetActorLabel());
		Result->SetStringField(TEXT("landscapeGuid"), Landscape->GetLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
		Result->SetObjectField(TEXT("location"), JsonConversion::MakeVectorObject(Location));
		Result->SetObjectField(TEXT("scale"), JsonConversion::MakeVectorObject(Scale));
		Result->SetNumberField(TEXT("componentCountX"), ComponentCountX);
		Result->SetNumberField(TEXT("componentCountY"), ComponentCountY);
		Result->SetNumberField(TEXT("componentCount"), Landscape->LandscapeComponents.Num());
		Result->SetNumberField(TEXT("vertexCountX"), VertexCountX);
		Result->SetNumberField(TEXT("vertexCountY"), VertexCountY);
		Result->SetNumberField(TEXT("rawHeight"), HeightValue);
		Result->SetNumberField(TEXT("importDurationMs"), ImportDurationMs);
		Result->SetBoolField(TEXT("longFrameOptIn"), bAllowLongFrame);
		Result->SetBoolField(TEXT("replacedExisting"), ReplacedLandscape != nullptr);
		Result->SetStringField(TEXT("replacedName"), ReplacedName);
		Result->SetStringField(TEXT("replacedLandscapeGuid"), ReplacedGuid);
		return SuccessJson(Result);
	}
}
