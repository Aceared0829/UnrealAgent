// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.Advanced.cpp
 * @brief 资产导入导出、全文索引、迁移、差异、布料与 Pivot 高级操作。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "Infrastructure/Transactions/UnrealAgentMCPAssetImportCompensation.h"

#include "Adapters/Tooling/UnrealAgentMCPTools.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "ClothingAssetBase.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorReimportHandler.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Exporters/Exporter.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FCriticalSection AssetSearchIndexMutex;
		TArray<FAssetData> AssetSearchIndex;
		FDateTime AssetSearchIndexBuiltAt;

		TSharedRef<FJsonObject> CopyAdvancedArguments(const TSharedPtr<FJsonObject>& Args)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			if (Args.IsValid())
			{
				Result->Values = Args->Values;
			}
			return Result;
		}

		FString ResolveProjectFilePath(const FString& Input, const bool bRequireExisting, FString& OutError)
		{
			if (Input.IsEmpty())
			{
				OutError = TEXT("文件路径不能为空。");
				return FString();
			}
			const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			FString Path = FPaths::IsRelative(Input) ? FPaths::Combine(Root, Input) : Input;
			Path = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Path);
			FString Relative = Path;
			if (!FPaths::MakePathRelativeTo(Relative, *(Root + TEXT("/"))) || Relative.StartsWith(TEXT("..")))
			{
				OutError = TEXT("文件路径超出项目目录。");
				return FString();
			}
			if (bRequireExisting && !IFileManager::Get().FileExists(*Path))
			{
				OutError = FString::Printf(TEXT("源文件不存在：%s"), *Path);
				return FString();
			}
			return Path;
		}

		TArray<FString> ReadImportFiles(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			TArray<FString> Inputs = FUnrealAgentMCPUnrealAssetAdapter::GetStringArrayArgument(Args, { TEXT("files"), TEXT("filenames") });
			if (Inputs.IsEmpty())
			{
				const FString Single = FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(Args, { TEXT("sourcePath"), TEXT("filePath"), TEXT("filename") });
				if (!Single.IsEmpty())
				{
					Inputs.Add(Single);
				}
			}
			TArray<FString> Files;
			for (const FString& Input : Inputs)
			{
				FString Error;
				const FString Path = ResolveProjectFilePath(Input, true, Error);
				if (Path.IsEmpty())
				{
					OutError = Error;
					return {};
				}
				Files.Add(Path);
			}
			if (Files.IsEmpty())
			{
				OutError = TEXT("缺少待导入文件。");
			}
			return Files;
		}

		void RebuildAssetSearchIndex()
		{
			FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FAssetData> Assets;
			Module.Get().GetAllAssets(Assets, true);
			TSet<FString> IndexedObjects;
			for (const FAssetData& Asset : Assets)
			{
				IndexedObjects.Add(Asset.GetSoftObjectPath().ToString());
			}
			for (TObjectIterator<UObject> It; It; ++It)
			{
				UObject* Object = *It;
				if (!IsValid(Object) || !Object->IsAsset() || Object->HasAnyFlags(RF_ClassDefaultObject | RF_Transient))
				{
					continue;
				}
				FAssetData LiveAsset(Object);
				const FString ObjectPath = LiveAsset.GetSoftObjectPath().ToString();
				if (LiveAsset.IsValid() && !IndexedObjects.Contains(ObjectPath))
				{
					IndexedObjects.Add(ObjectPath);
					Assets.Add(MoveTemp(LiveAsset));
				}
			}
			Assets.Sort(
				[](const FAssetData& Left, const FAssetData& Right)
				{
					return Left.PackageName.LexicalLess(Right.PackageName);
				});
			FScopeLock Guard(&AssetSearchIndexMutex);
			AssetSearchIndex = MoveTemp(Assets);
			AssetSearchIndexBuiltAt = FDateTime::UtcNow();
		}

		bool AssetMatchesSearch(const FAssetData& Asset, const FString& Query)
		{
			return Query.IsEmpty() || Asset.AssetName.ToString().Contains(Query, ESearchCase::IgnoreCase) ||
				Asset.PackageName.ToString().Contains(Query, ESearchCase::IgnoreCase) || Asset.AssetClassPath.ToString().Contains(Query, ESearchCase::IgnoreCase);
		}

		TSharedRef<FJsonObject> SerializeObjectForDiff(UObject* Object)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			if (Object)
			{
				FJsonObjectConverter::UStructToJsonObject(Object->GetClass(), Object, Result);
			}
			return Result;
		}

		TSharedRef<FJsonObject> ClothAssetToJson(const UClothingAssetBase* Cloth)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Cloth->GetName());
			Result->SetStringField(TEXT("class"), Cloth->GetClass()->GetPathName());
			Result->SetStringField(TEXT("guid"), Cloth->GetAssetGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
			Result->SetBoolField(TEXT("valid"), Cloth->IsValid());
			TArray<TSharedPtr<FJsonValue>> Configs;
			TArray<UObject*> InnerObjects;
			GetObjectsWithOuter(Cloth, InnerObjects, EGetObjectsFlags::IncludeNestedObjects, RF_NoFlags);
			for (UObject* Inner : InnerObjects)
			{
				if (!Inner || !Inner->GetClass()->GetName().Contains(TEXT("Config"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				TSharedRef<FJsonObject> Config = SerializeObjectForDiff(Inner);
				Config->SetStringField(TEXT("_name"), Inner->GetName());
				Config->SetStringField(TEXT("_class"), Inner->GetClass()->GetPathName());
				Configs.Add(MakeShared<FJsonValueObject>(Config));
			}
			Result->SetNumberField(TEXT("configCount"), Configs.Num());
			Result->SetArrayField(TEXT("configs"), Configs);
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Advanced(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		UEditorAssetSubsystem* Subsystem = GetAssetSubsystem();
		if (!Subsystem)
		{
			return ErrorJson(TEXT("EditorAssetSubsystem 当前不可用。"));
		}

		if (Action == TEXT("import_static_mesh") || Action == TEXT("import_skeletal_mesh") || Action == TEXT("import_animation") || Action == TEXT("import_texture") ||
			Action == TEXT("import_texture_batch"))
		{
			FString Error;
			const TArray<FString> Files = ReadImportFiles(Args, Error);
			if (Files.IsEmpty())
			{
				return ErrorJson(Error);
			}
			FString DestinationPath = GetStringArgument(Args, { TEXT("destinationPath"), TEXT("packagePath"), TEXT("directory") });
			if (DestinationPath.IsEmpty())
			{
				DestinationPath = TEXT("/Game");
			}
			FUnrealAgentMCPAssetImportCompensation Compensation(DestinationPath);
			bool bReplace = false;
			Args->TryGetBoolField(TEXT("replaceExisting"), bReplace);
			if (!Compensation.ValidateReplacePolicy(bReplace, Error))
			{
				return ErrorJson(Error);
			}
			UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
			ImportData->GroupName = TEXT("UnrealAgent");
			ImportData->Filenames = Files;
			ImportData->DestinationPath = DestinationPath;
			ImportData->bReplaceExisting = bReplace;
			TArray<UObject*> Imported = FAssetToolsModule::GetModule().Get().ImportAssetsAutomated(ImportData);
			if (!Imported.IsEmpty() && !Compensation.RegisterCreatedAssets(Imported, Error))
			{
				return ErrorJson(Error);
			}
			TArray<TSharedPtr<FJsonValue>> Assets;
			for (UObject* Object : Imported)
			{
				if (Object)
				{
					Assets.Add(MakeShared<FJsonValueString>(Object->GetPathName()));
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("fileCount"), Files.Num());
			Result->SetNumberField(TEXT("importedCount"), Assets.Num());
			Result->SetArrayField(TEXT("assets"), Assets);
			return Imported.IsEmpty() ? ErrorJson(TEXT("Unreal 导入器没有生成资产。")) : SuccessJson(Result);
		}

		const FString AssetPath = GetStringArgument(Args, { TEXT("assetPath"), TEXT("path") });

		if (Action == TEXT("reimport"))
		{
			UObject* Asset = Subsystem->LoadAsset(AssetPath);
			if (!Asset)
			{
				return ErrorJson(TEXT("未找到待重导入资产。"));
			}
			FString PreferredFile = GetStringArgument(Args, { TEXT("sourcePath"), TEXT("filePath") });
			if (!PreferredFile.IsEmpty())
			{
				FString Error;
				PreferredFile = ResolveProjectFilePath(PreferredFile, true, Error);
				if (PreferredFile.IsEmpty())
				{
					return ErrorJson(Error);
				}
			}
			const bool bReimported = FReimportManager::Instance()->Reimport(Asset, false, false, PreferredFile, nullptr, INDEX_NONE, false, true, false);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), AssetPath);
			Result->SetBoolField(TEXT("reimported"), bReimported);
			return bReimported ? SuccessJson(Result) : ErrorJson(TEXT("资产重导入失败。"));
		}

		if (Action == TEXT("export") || Action == TEXT("export_texture"))
		{
			UObject* Asset = Subsystem->LoadAsset(AssetPath);
			if (!Asset)
			{
				return ErrorJson(TEXT("未找到待导出资产。"));
			}
			if (Action == TEXT("export_texture") && !Asset->IsA<UTexture>())
			{
				return ErrorJson(TEXT("export_texture 需要纹理资产。"));
			}
			const FString OutputInput = GetStringArgument(Args, { TEXT("outputPath"), TEXT("destinationPath") });
			FString Error;
			const FString OutputPath = ResolveProjectFilePath(OutputInput, false, Error);
			if (OutputPath.IsEmpty())
			{
				return ErrorJson(Error);
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
			const FString Extension = FPaths::GetExtension(OutputPath);
			UExporter* Exporter = UExporter::FindExporter(Asset, *Extension);
			const bool bExported = UExporter::ExportToFile(Asset, Exporter, *OutputPath, false, false, false) != 0;
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), AssetPath);
			Result->SetStringField(TEXT("outputPath"), OutputPath);
			Result->SetBoolField(TEXT("exported"), bExported);
			return bExported ? SuccessJson(Result) : ErrorJson(TEXT("没有可用导出器或导出失败。"));
		}

		if (Action == TEXT("compare_textures"))
		{
			const FString OtherPath = GetStringArgument(Args, { TEXT("otherAssetPath"), TEXT("rightAssetPath"), TEXT("targetPath") });
			UTexture2D* Left = Cast<UTexture2D>(Subsystem->LoadAsset(AssetPath));
			UTexture2D* Right = Cast<UTexture2D>(Subsystem->LoadAsset(OtherPath));
			if (!Left || !Right)
			{
				return ErrorJson(TEXT("两侧都必须是 Texture2D。"));
			}
			const bool bSameDimensions = Left->GetSizeX() == Right->GetSizeX() && Left->GetSizeY() == Right->GetSizeY();
			const bool bSameSource = Left->Source.GetId() == Right->Source.GetId();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("left"), AssetPath);
			Result->SetStringField(TEXT("right"), OtherPath);
			Result->SetBoolField(TEXT("sameDimensions"), bSameDimensions);
			Result->SetBoolField(TEXT("sameSource"), bSameSource);
			Result->SetBoolField(TEXT("identical"), bSameDimensions && bSameSource && Left->SRGB == Right->SRGB && Left->CompressionSettings == Right->CompressionSettings);
			return SuccessJson(Result);
		}

		if (Action == TEXT("recenter_pivot"))
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Subsystem->LoadAsset(AssetPath));
			if (!Mesh)
			{
				return ErrorJson(TEXT("recenter_pivot 需要 StaticMesh。"));
			}
			FBox3f Bounds(ForceInit);
			int32 VertexCount = 0;
			for (int32 LodIndex = 0; LodIndex < Mesh->GetNumSourceModels(); ++LodIndex)
			{
				FMeshDescription* Description = Mesh->GetMeshDescription(LodIndex);
				if (!Description)
				{
					continue;
				}
				FStaticMeshAttributes Attributes(*Description);
				TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
				for (const FVertexID VertexId : Description->Vertices().GetElementIDs())
				{
					Bounds += Positions[VertexId];
					++VertexCount;
				}
			}
			if (VertexCount == 0)
			{
				return ErrorJson(TEXT("StaticMesh 没有可编辑顶点。"));
			}
			const FVector3f Center = Bounds.GetCenter();
			Mesh->Modify();
			for (int32 LodIndex = 0; LodIndex < Mesh->GetNumSourceModels(); ++LodIndex)
			{
				FMeshDescription* Description = Mesh->GetMeshDescription(LodIndex);
				if (!Description)
				{
					continue;
				}
				FStaticMeshAttributes Attributes(*Description);
				TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
				for (const FVertexID VertexId : Description->Vertices().GetElementIDs())
				{
					Positions[VertexId] -= Center;
				}
				Mesh->CommitMeshDescription(LodIndex);
			}
			Mesh->Build(false);
			Mesh->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("vertexCount"), VertexCount);
			Result->SetNumberField(TEXT("offsetX"), Center.X);
			Result->SetNumberField(TEXT("offsetY"), Center.Y);
			Result->SetNumberField(TEXT("offsetZ"), Center.Z);
			return SuccessJson(Result);
		}

		if (Action == TEXT("read_cloth_data") || Action == TEXT("set_cloth_config"))
		{
			USkeletalMesh* Mesh = Cast<USkeletalMesh>(Subsystem->LoadAsset(AssetPath));
			if (!Mesh)
			{
				return ErrorJson(TEXT("布料动作需要 SkeletalMesh。"));
			}
			if (Action == TEXT("read_cloth_data"))
			{
				TArray<TSharedPtr<FJsonValue>> Assets;
				for (const UClothingAssetBase* Cloth : Mesh->GetMeshClothingAssets())
				{
					if (Cloth)
					{
						Assets.Add(MakeShared<FJsonValueObject>(ClothAssetToJson(Cloth)));
					}
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("clothAssetCount"), Assets.Num());
				Result->SetArrayField(TEXT("clothAssets"), Assets);
				return SuccessJson(Result);
			}
			double IndexNumber = 0.0;
			Args->TryGetNumberField(TEXT("clothIndex"), IndexNumber);
			const int32 Index = static_cast<int32>(IndexNumber);
			const TArray<TObjectPtr<UClothingAssetBase>>& ClothAssets = Mesh->GetMeshClothingAssets();
			if (!ClothAssets.IsValidIndex(Index) || !ClothAssets[Index])
			{
				return ErrorJson(TEXT("clothIndex 无效。"));
			}
			const FString ConfigName = GetStringArgument(Args, { TEXT("configName"), TEXT("configClass") });
			const FString PropertyName = GetStringArgument(Args, { TEXT("propertyName"), TEXT("property") });
			const TSharedPtr<FJsonValue> Value = Args->TryGetField(TEXT("value"));
			TArray<UObject*> Candidates{ ClothAssets[Index] };
			GetObjectsWithOuter(ClothAssets[Index], Candidates, EGetObjectsFlags::IncludeNestedObjects, RF_NoFlags);
			for (UObject* Candidate : Candidates)
			{
				if (!Candidate || (!ConfigName.IsEmpty() && !Candidate->GetClass()->GetName().Contains(ConfigName, ESearchCase::IgnoreCase)))
				{
					continue;
				}
				FProperty* Property = FindFProperty<FProperty>(Candidate->GetClass(), *PropertyName);
				FString Error;
				if (Property && Value.IsValid())
				{
					Candidate->Modify();
					if (PropertyWriter::SetPropertyFromJson(Candidate, Property, Value, Error))
					{
						Candidate->PostEditChange();
						Mesh->MarkPackageDirty();
						TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
						Result->SetStringField(TEXT("configObject"), Candidate->GetPathName());
						Result->SetStringField(TEXT("propertyName"), PropertyName);
						return SuccessJson(Result);
					}
				}
			}
			return ErrorJson(TEXT("没有找到可写的布料配置属性。"));
		}

		if (Action == TEXT("create_interchange_pipeline"))
		{
			TSharedRef<FJsonObject> Normalized = CopyAdvancedArguments(Args);
			if (!Normalized->HasField(TEXT("class")))
			{
				Normalized->SetStringField(TEXT("class"),
					TEXT("/Script/InterchangePipelines."
						 "InterchangeGenericAssetsPipeline"));
			}
			return Tools::CreateAsset(Normalized);
		}

		if (Action == TEXT("reindex_fts"))
		{
			RebuildAssetSearchIndex();
			FScopeLock Guard(&AssetSearchIndexMutex);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("indexedAssetCount"), AssetSearchIndex.Num());
			Result->SetStringField(TEXT("builtAt"), AssetSearchIndexBuiltAt.ToIso8601());
			return SuccessJson(Result);
		}

		if (Action == TEXT("search_fts"))
		{
			bool bNeedsIndex = false;
			{
				FScopeLock Guard(&AssetSearchIndexMutex);
				bNeedsIndex = AssetSearchIndex.IsEmpty();
			}
			if (bNeedsIndex)
			{
				RebuildAssetSearchIndex();
			}
			const FString Query = GetStringArgument(Args, { TEXT("query"), TEXT("searchTerm") });
			double LimitNumber = 100.0;
			Args->TryGetNumberField(TEXT("limit"), LimitNumber);
			const int32 Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 1000);
			TArray<TSharedPtr<FJsonValue>> Items;
			int32 Matched = 0;
			FScopeLock Guard(&AssetSearchIndexMutex);
			for (const FAssetData& Asset : AssetSearchIndex)
			{
				if (!AssetMatchesSearch(Asset, Query))
				{
					continue;
				}
				++Matched;
				if (Items.Num() < Limit)
				{
					Items.Add(MakeShared<FJsonValueObject>(MakeAssetJson(Asset)));
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("query"), Query);
			Result->SetNumberField(TEXT("matchedCount"), Matched);
			Result->SetNumberField(TEXT("count"), Items.Num());
			Result->SetArrayField(TEXT("assets"), Items);
			return SuccessJson(Result);
		}

		if (Action == TEXT("migrate"))
		{
			TArray<FString> AssetPaths = GetStringArrayArgument(Args, { TEXT("assetPaths"), TEXT("assets"), TEXT("paths") });
			if (AssetPaths.IsEmpty() && !AssetPath.IsEmpty())
			{
				AssetPaths.Add(AssetPath);
			}
			const FString DestinationInput = GetStringArgument(Args, { TEXT("destinationPath"), TEXT("outputPath") });
			FString Error;
			const FString Destination = ResolveProjectFilePath(DestinationInput, false, Error);
			if (AssetPaths.IsEmpty() || Destination.IsEmpty())
			{
				return ErrorJson(Error.IsEmpty() ? TEXT("缺少待迁移资产。") : Error);
			}
			TArray<FName> Packages;
			for (const FString& Path : AssetPaths)
			{
				Packages.AddUnique(FName(*ToPackageName(Path)));
			}
			IFileManager::Get().MakeDirectory(*Destination, true);
			FAssetToolsModule::GetModule().Get().MigratePackages(Packages, Destination);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("packageCount"), Packages.Num());
			Result->SetStringField(TEXT("destinationPath"), Destination);
			return SuccessJson(Result);
		}

		if (Action == TEXT("diff"))
		{
			const FString OtherPath = GetStringArgument(Args, { TEXT("otherAssetPath"), TEXT("rightAssetPath"), TEXT("targetPath") });
			UObject* Left = Subsystem->LoadAsset(AssetPath);
			UObject* Right = Subsystem->LoadAsset(OtherPath);
			if (!Left || !Right)
			{
				return ErrorJson(TEXT("差异比较两侧资产必须存在。"));
			}
			const FString LeftJson = JsonObjectToString(SerializeObjectForDiff(Left));
			const FString RightJson = JsonObjectToString(SerializeObjectForDiff(Right));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("left"), AssetPath);
			Result->SetStringField(TEXT("right"), OtherPath);
			Result->SetStringField(TEXT("leftHash"), FMD5::HashAnsiString(*LeftJson));
			Result->SetStringField(TEXT("rightHash"), FMD5::HashAnsiString(*RightJson));
			Result->SetBoolField(TEXT("identical"), LeftJson == RightJson);
			Result->SetBoolField(TEXT("sameClass"), Left->GetClass() == Right->GetClass());
			return SuccessJson(Result);
		}

		return ErrorJson(FString::Printf(TEXT("高级资产动作未进入有效分支：%s"), *Action));
	}
}
