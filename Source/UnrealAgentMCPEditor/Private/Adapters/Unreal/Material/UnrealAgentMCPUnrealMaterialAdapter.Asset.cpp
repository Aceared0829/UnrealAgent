// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.Asset.cpp
 * @brief 基础材质创建、核心属性、复制、重编译与预览实现。
 */

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.Internal.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Infrastructure/Transactions/UnrealAgentMCPFileMutationCompensation.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	using namespace MaterialInternal;

	namespace
	{
		constexpr int64 MaximumPreviewCompensationBytes = 64LL * 1024LL * 1024LL;

		TSharedRef<FJsonObject> AssetResult(UObject* Asset)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Asset ? Asset->GetPathName() : TEXT(""));
			Result->SetStringField(TEXT("class"), Asset ? Asset->GetClass()->GetPathName() : TEXT(""));
			return Result;
		}

		bool ResolveUsage(const FString& Name, EMaterialUsage& OutUsage)
		{
			FString Key = Name;
			Key.ReplaceInline(TEXT("_"), TEXT(""));
			Key = Key.ToLower();
			const TMap<FString, EMaterialUsage> Values{ { TEXT("skeletalmesh"), MATUSAGE_SkeletalMesh }, { TEXT("particlesprites"), MATUSAGE_ParticleSprites },
				{ TEXT("beamtrail"), MATUSAGE_BeamTrails }, { TEXT("meshemparticles"), MATUSAGE_MeshParticles }, { TEXT("staticlighting"), MATUSAGE_StaticLighting },
				{ TEXT("morphtargets"), MATUSAGE_MorphTargets }, { TEXT("splinemesh"), MATUSAGE_SplineMesh }, { TEXT("instancedstaticmeshes"), MATUSAGE_InstancedStaticMeshes },
				{ TEXT("geometrycollections"), MATUSAGE_GeometryCollections }, { TEXT("cloth"), MATUSAGE_Clothing }, { TEXT("niagarasprites"), MATUSAGE_NiagaraSprites },
				{ TEXT("niagararibbons"), MATUSAGE_NiagaraRibbons }, { TEXT("niagarameshparticles"), MATUSAGE_NiagaraMeshParticles }, { TEXT("nanite"), MATUSAGE_Nanite },
				{ TEXT("heterogeneousvolumes"), MATUSAGE_HeterogeneousVolumes } };
			if (const EMaterialUsage* Usage = Values.Find(Key))
			{
				OutUsage = *Usage;
				return true;
			}
			return false;
		}

		bool ResolveShadingModel(const FString& Name, EMaterialShadingModel& OutValue)
		{
			FString Key = Name;
			Key.ReplaceInline(TEXT("_"), TEXT(""));
			Key = Key.ToLower();
			const TMap<FString, EMaterialShadingModel> Values{ { TEXT("unlit"), MSM_Unlit }, { TEXT("defaultlit"), MSM_DefaultLit }, { TEXT("subsurface"), MSM_Subsurface },
				{ TEXT("preskinned"), MSM_PreintegratedSkin }, { TEXT("preintegratedskin"), MSM_PreintegratedSkin }, { TEXT("clearcoat"), MSM_ClearCoat },
				{ TEXT("subsurfaceprofile"), MSM_SubsurfaceProfile }, { TEXT("twosidedfoliage"), MSM_TwoSidedFoliage }, { TEXT("hair"), MSM_Hair }, { TEXT("cloth"), MSM_Cloth },
				{ TEXT("eye"), MSM_Eye }, { TEXT("singlelayerwater"), MSM_SingleLayerWater }, { TEXT("thintranslucent"), MSM_ThinTranslucent },
				{ TEXT("frommaterialexpression"), MSM_FromMaterialExpression } };
			if (const EMaterialShadingModel* Value = Values.Find(Key))
			{
				OutValue = *Value;
				return true;
			}
			return false;
		}

		bool ResolveBlendMode(const FString& Name, EBlendMode& OutValue)
		{
			FString Key = Name;
			Key.ReplaceInline(TEXT("_"), TEXT(""));
			Key = Key.ToLower();
			const TMap<FString, EBlendMode> Values{ { TEXT("opaque"), BLEND_Opaque }, { TEXT("masked"), BLEND_Masked }, { TEXT("translucent"), BLEND_Translucent },
				{ TEXT("additive"), BLEND_Additive }, { TEXT("modulate"), BLEND_Modulate }, { TEXT("alphacomposite"), BLEND_AlphaComposite },
				{ TEXT("alphaholdout"), BLEND_AlphaHoldout }, { TEXT("translucentcoloredtransmittance"), BLEND_TranslucentColoredTransmittance },
				{ TEXT("translucentgreytransmittance"), BLEND_TranslucentGreyTransmittance } };
			if (const EBlendMode* Value = Values.Find(Key))
			{
				OutValue = *Value;
				return true;
			}
			return false;
		}

		bool ResolveDomain(const FString& Name, EMaterialDomain& OutValue)
		{
			FString Key = Name;
			Key.ReplaceInline(TEXT("_"), TEXT(""));
			Key = Key.ToLower();
			const TMap<FString, EMaterialDomain> Values{ { TEXT("surface"), MD_Surface }, { TEXT("deferreddecal"), MD_DeferredDecal }, { TEXT("lightfunction"), MD_LightFunction },
				{ TEXT("volume"), MD_Volume }, { TEXT("postprocess"), MD_PostProcess }, { TEXT("ui"), MD_UI }, { TEXT("runtimevirtualtexture"), MD_RuntimeVirtualTexture } };
			if (const EMaterialDomain* Value = Values.Find(Key))
			{
				OutValue = *Value;
				return true;
			}
			return false;
		}

		void AddScalarOutput(UMaterial* Material, const float Value, const EMaterialProperty Property, const int32 Y)
		{
			UMaterialExpressionConstant* Expression =
				Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -400, Y));
			if (Expression)
			{
				Expression->R = Value;
				Expression->Desc = TEXT("Unreal Agent 简易材质常量");
				UMaterialEditingLibrary::ConnectMaterialProperty(Expression, TEXT(""), Property);
			}
		}

		bool IsChildOfMaterial(UMaterialInstanceConstant* Instance, UMaterial* Material)
		{
			for (UMaterialInterface* Parent = Instance ? Instance->Parent.Get() : nullptr; Parent;
				Parent = Cast<UMaterialInstance>(Parent) ? Cast<UMaterialInstance>(Parent)->Parent.Get() : nullptr)
			{
				if (Parent == Material)
				{
					return true;
				}
				if (Cast<UMaterial>(Parent))
				{
					return false;
				}
			}
			return false;
		}
	}

	FString FUnrealAgentMCPUnrealMaterialAdapter::ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create") || Action == TEXT("create_simple"))
		{
			FString Name;
			FString Error;
			if (!RequireString(Args, TEXT("name"), Name, Error))
			{
				return ErrorJson(Error);
			}
			FString PackagePath = TEXT("/Game/Materials");
			Args->TryGetStringField(TEXT("packagePath"), PackagePath);
			UMaterial* Material = CreateMaterial(Name, PackagePath, Error);
			if (!Material)
			{
				return ErrorJson(Error);
			}
			if (Action == TEXT("create_simple"))
			{
				FLinearColor BaseColor(0.18f, 0.18f, 0.18f, 1.0f);
				TryReadColor(Args->TryGetField(TEXT("baseColor")), BaseColor);
				UMaterialExpressionConstant3Vector* Color = Cast<UMaterialExpressionConstant3Vector>(
					UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -400, -200));
				if (Color)
				{
					Color->Constant = BaseColor;
					Color->Desc = TEXT("Unreal Agent 简易材质基础色");
					UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT(""), MP_BaseColor);
				}
				double Metallic = 0.0;
				double Specular = 0.5;
				double Roughness = 0.5;
				Args->TryGetNumberField(TEXT("metallic"), Metallic);
				Args->TryGetNumberField(TEXT("specular"), Specular);
				Args->TryGetNumberField(TEXT("roughness"), Roughness);
				AddScalarOutput(Material, static_cast<float>(Metallic), MP_Metallic, -80);
				AddScalarOutput(Material, static_cast<float>(Specular), MP_Specular, 40);
				AddScalarOutput(Material, static_cast<float>(Roughness), MP_Roughness, 160);
				FLinearColor Emissive;
				if (TryReadColor(Args->TryGetField(TEXT("emissive")), Emissive))
				{
					UMaterialExpressionConstant3Vector* EmissiveNode = Cast<UMaterialExpressionConstant3Vector>(
						UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -400, 280));
					if (EmissiveNode)
					{
						EmissiveNode->Constant = Emissive;
						EmissiveNode->Desc = TEXT("Unreal Agent 简易材质自发光");
						UMaterialEditingLibrary::ConnectMaterialProperty(EmissiveNode, TEXT(""), MP_EmissiveColor);
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* Usages = nullptr;
				if (Args->TryGetArrayField(TEXT("usages"), Usages) && Usages)
				{
					for (const TSharedPtr<FJsonValue>& Value : *Usages)
					{
						EMaterialUsage Usage = MATUSAGE_SkeletalMesh;
						if (ResolveUsage(Value->AsString(), Usage))
						{
							UMaterialEditingLibrary::SetBaseMaterialUsage(Material, Usage, true);
						}
					}
				}
				MarkMaterialChanged(Material);
			}
			if (!SaveAsset(Material, Error))
			{
				return ErrorJson(Error);
			}
			return SuccessJson(AssetResult(Material));
		}

		if (Action == TEXT("duplicate"))
		{
			FString SourcePath;
			FString DestinationPath;
			FString Error;
			if (!RequireString(Args, TEXT("sourcePath"), SourcePath, Error) || !RequireString(Args, TEXT("destinationPath"), DestinationPath, Error))
			{
				return ErrorJson(Error);
			}
			UEditorAssetSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
			UObject* Duplicated = Subsystem ? Subsystem->DuplicateAsset(SourcePath, DestinationPath) : nullptr;
			if (!Cast<UMaterialInterface>(Duplicated))
			{
				return ErrorJson(TEXT("材质资产复制失败。"));
			}
			if (!SaveAsset(Duplicated, Error))
			{
				return ErrorJson(Error);
			}
			return SuccessJson(AssetResult(Duplicated));
		}

		FString AssetPath;
		FString Error;
		if (!RequireString(Args, Action == TEXT("recompile") || Action == TEXT("render_preview") ? TEXT("materialPath") : TEXT("assetPath"), AssetPath, Error))
		{
			// 兼容文档中两种等价的材质路径字段。
			if (!Args->TryGetStringField(TEXT("assetPath"), AssetPath) && !Args->TryGetStringField(TEXT("materialPath"), AssetPath))
			{
				return ErrorJson(Error);
			}
		}
		UMaterial* Material = LoadMaterial(AssetPath, Error);
		if (!Material)
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("set_usage"))
		{
			const bool bEnabled = !Args->HasField(TEXT("enabled")) || Args->GetBoolField(TEXT("enabled"));
			TArray<FString> Requested;
			FString Single;
			if (Args->TryGetStringField(TEXT("usage"), Single))
			{
				Requested.Add(Single);
			}
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (Args->TryGetArrayField(TEXT("usages"), Values) && Values)
			{
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					Requested.Add(Value->AsString());
				}
			}
			if (Requested.IsEmpty())
			{
				return ErrorJson(TEXT("缺少 usage 或 usages。"));
			}
			for (const FString& Name : Requested)
			{
				EMaterialUsage Usage = MATUSAGE_SkeletalMesh;
				if (!ResolveUsage(Name, Usage))
				{
					return ErrorJson(FString::Printf(TEXT("不支持的材质用途：%s"), *Name));
				}
				UMaterialEditingLibrary::SetBaseMaterialUsage(Material, Usage, bEnabled);
			}
		}
		else if (Action == TEXT("set_shading_model"))
		{
			FString Name;
			if (!RequireString(Args, TEXT("shadingModel"), Name, Error))
			{
				return ErrorJson(Error);
			}
			EMaterialShadingModel Value = MSM_DefaultLit;
			if (!ResolveShadingModel(Name, Value))
			{
				return ErrorJson(TEXT("不支持的 shadingModel。"));
			}
			Material->SetShadingModel(Value);
		}
		else if (Action == TEXT("set_blend_mode"))
		{
			FString Name;
			if (!RequireString(Args, TEXT("blendMode"), Name, Error))
			{
				return ErrorJson(Error);
			}
			EBlendMode Value = BLEND_Opaque;
			if (!ResolveBlendMode(Name, Value))
			{
				return ErrorJson(TEXT("不支持的 blendMode。"));
			}
			Material->BlendMode = Value;
		}
		else if (Action == TEXT("set_domain"))
		{
			FString Name;
			if (!RequireString(Args, TEXT("materialDomain"), Name, Error))
			{
				return ErrorJson(Error);
			}
			EMaterialDomain Value = MD_Surface;
			if (!ResolveDomain(Name, Value))
			{
				return ErrorJson(TEXT("不支持的 materialDomain。"));
			}
			Material->MaterialDomain = Value;
		}
		else if (Action == TEXT("recompile"))
		{
			const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
			int32 ChildCount = 0;
			bool bChildren = false;
			Args->TryGetBoolField(TEXT("recompileChildren"), bChildren);
			if (bChildren)
			{
				for (TObjectIterator<UMaterialInstanceConstant> It; It; ++It)
				{
					if (!It->HasAnyFlags(RF_ClassDefaultObject) && IsChildOfMaterial(*It, Material))
					{
						It->PostEditChange();
						++ChildCount;
					}
				}
			}
			TArray<TSharedPtr<FJsonValue>> Errors;
			for (const FString& Message : CompileErrors)
			{
				Errors.Add(MakeShared<FJsonValueString>(Message));
			}
			TSharedRef<FJsonObject> Result = AssetResult(Material);
			Result->SetArrayField(TEXT("compilerErrors"), Errors);
			Result->SetNumberField(TEXT("childrenRecompiled"), ChildCount);
			Result->SetBoolField(TEXT("compiled"), CompileErrors.IsEmpty());
			if (!CompileErrors.IsEmpty())
			{
				Result->SetBoolField(TEXT("success"), false);
				Result->SetStringField(TEXT("error"), TEXT("材质编译产生错误。"));
				return JsonObjectToString(Result);
			}
			return SuccessJson(Result);
		}
		else if (Action == TEXT("render_preview"))
		{
			FString OutputPath;
			if (!RequireString(Args, TEXT("outputPath"), OutputPath, Error))
			{
				return ErrorJson(Error);
			}
			int32 Width = 256;
			int32 Height = 256;
			double Number = 0.0;
			if (Args->TryGetNumberField(TEXT("width"), Number))
				Width = FMath::Clamp(static_cast<int32>(Number), 64, 2048);
			if (Args->TryGetNumberField(TEXT("height"), Number))
				Height = FMath::Clamp(static_cast<int32>(Number), 64, 2048);
			if (FPaths::IsRelative(OutputPath))
			{
				OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("MaterialPreviews"), OutputPath);
			}
			OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);
			FPaths::NormalizeFilename(OutputPath);
			FString SavedRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
			FPaths::NormalizeDirectoryName(SavedRoot);
			if (!FPaths::IsUnderDirectory(OutputPath, SavedRoot))
			{
				return ErrorJson(TEXT("预览输出必须位于项目 Saved 目录。"));
			}
			if (!OutputPath.EndsWith(TEXT(".png")))
			{
				OutputPath += TEXT(".png");
			}
			if (!Transactions::RegisterFileWriteCompensation(OutputPath, MaximumPreviewCompensationBytes, Error))
			{
				return ErrorJson(Error);
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
			FObjectThumbnail Thumbnail;
			ThumbnailTools::RenderThumbnail(Material, Width, Height, ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush, nullptr, &Thumbnail);
			if (!Thumbnail.HasValidImageData() || Thumbnail.AccessImageData().Num() < Width * Height * static_cast<int32>(sizeof(FColor)))
			{
				return ErrorJson(TEXT("材质预览渲染未返回有效像素。"));
			}
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(Width * Height);
			FMemory::Memcpy(Pixels.GetData(), Thumbnail.AccessImageData().GetData(), Width * Height * sizeof(FColor));
			TArray<uint8> Png;
			FImageUtils::ThumbnailCompressImageArray(Width, Height, Pixels, Png);
			if (!FFileHelper::SaveArrayToFile(Png, *OutputPath))
			{
				return ErrorJson(TEXT("材质预览 PNG 写入失败。"));
			}
			TSharedRef<FJsonObject> Result = AssetResult(Material);
			Result->SetStringField(TEXT("outputPath"), OutputPath);
			Result->SetNumberField(TEXT("width"), Width);
			Result->SetNumberField(TEXT("height"), Height);
			Result->SetNumberField(TEXT("bytes"), Png.Num());
			return SuccessJson(Result);
		}
		else
		{
			return ErrorJson(FString::Printf(TEXT("未实现的材质资产 action：%s"), *Action));
		}

		MarkMaterialChanged(Material);
		if (!SaveAsset(Material, Error))
		{
			return ErrorJson(Error);
		}
		return SuccessJson(AssetResult(Material));
	}
}
