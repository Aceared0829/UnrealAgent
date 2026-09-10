// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPContentAuthoringTools.cpp
 * @brief 材质实例参数修改与 PCG Graph 配方创建工具。
 */

#include "Adapters/Tooling/UnrealAgentMCPTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Adapters/Unreal/Assets/UnrealAgentMCPAssetSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"

namespace UnrealAgentMCP::Tools
{
	using namespace AssetSupport;
	using namespace JsonConversion;

	FString ModifyMaterialInstance(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (!Args->TryGetStringField(TEXT("assetPath"), AssetPath) && !Args->TryGetStringField(TEXT("path"), AssetPath))
		{
			return ErrorJson(TEXT("Missing required field 'assetPath'."));
		}

		UMaterialInstanceConstant* MaterialInstance = LoadObject<UMaterialInstanceConstant>(nullptr, *NormalizeAssetObjectPath(AssetPath));
		if (!MaterialInstance)
		{
			return ErrorJson(FString::Printf(TEXT("MaterialInstanceConstant not found: %s"), *AssetPath));
		}

		int32 ChangedCount = 0;
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "ModifyMaterialInstance", "MCP Modify Material Instance"));
		MaterialInstance->Modify();

		const TSharedPtr<FJsonObject>* ScalarParams = nullptr;
		if (Args->TryGetObjectField(TEXT("scalarParameters"), ScalarParams) && ScalarParams && ScalarParams->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ScalarParams)->Values)
			{
				double Number = 0.0;
				if (!TryValueToNumber(Pair.Value, Number))
				{
					return ErrorJson(FString::Printf(TEXT("Scalar parameter '%s' must be numeric."), *Pair.Key));
				}
				MaterialInstance->SetScalarParameterValueEditorOnly(FName(*Pair.Key), static_cast<float>(Number));
				++ChangedCount;
			}
		}

		const TSharedPtr<FJsonObject>* VectorParams = nullptr;
		if (Args->TryGetObjectField(TEXT("vectorParameters"), VectorParams) && VectorParams && VectorParams->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*VectorParams)->Values)
			{
				FLinearColor Color = FLinearColor::White;
				if (!TryValueToLinearColor(Pair.Value, Color))
				{
					return ErrorJson(FString::Printf(TEXT("Vector parameter '%s' must be {r,g,b,a} or [r,g,b,a]."), *Pair.Key));
				}
				MaterialInstance->SetVectorParameterValueEditorOnly(FName(*Pair.Key), Color);
				++ChangedCount;
			}
		}

		MaterialInstance->PostEditChange();
		MaterialInstance->MarkPackageDirty();

		bool bSave = false;
		Args->TryGetBoolField(TEXT("save"), bSave);
		bool bSaved = false;
		if (bSave)
		{
			TArray<UPackage*> PackagesToSave;
			PackagesToSave.Add(MaterialInstance->GetOutermost());
			bSaved = SavePackages(PackagesToSave);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), MaterialInstance->GetPathName());
		Result->SetNumberField(TEXT("changedCount"), ChangedCount);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return SuccessJson(Result);
	}

	FString CreatePcgGraphFromRecipe(const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (!Args->TryGetStringField(TEXT("assetPath"), AssetPath) && !Args->TryGetStringField(TEXT("path"), AssetPath))
		{
			return ErrorJson(TEXT("Missing required field 'assetPath'."));
		}

		bool bSave = false;
		Args->TryGetBoolField(TEXT("save"), bSave);

		FString RecipeId;
		Args->TryGetStringField(TEXT("recipe_id"), RecipeId);
		if (RecipeId.IsEmpty())
		{
			Args->TryGetStringField(TEXT("id"), RecipeId);
		}

		FString SourceGraph;
		if (!Args->TryGetStringField(TEXT("sourceGraph"), SourceGraph))
		{
			Args->TryGetStringField(TEXT("source_graph"), SourceGraph);
		}
		const TSharedPtr<FJsonObject>* RecipeObject = nullptr;
		if (SourceGraph.IsEmpty() && Args->TryGetObjectField(TEXT("recipe"), RecipeObject) && RecipeObject && RecipeObject->IsValid())
		{
			if (!(*RecipeObject)->TryGetStringField(TEXT("source_graph"), SourceGraph))
			{
				(*RecipeObject)->TryGetStringField(TEXT("sourceGraph"), SourceGraph);
			}
			if (RecipeId.IsEmpty())
			{
				if (!(*RecipeObject)->TryGetStringField(TEXT("recipe_id"), RecipeId))
				{
					(*RecipeObject)->TryGetStringField(TEXT("id"), RecipeId);
				}
			}
		}

		if (!SourceGraph.IsEmpty())
		{
			UObject* SourceObject = StaticLoadObject(UObject::StaticClass(), nullptr, *NormalizeAssetObjectPath(SourceGraph));
			if (!SourceObject)
			{
				return ErrorJson(FString::Printf(TEXT("sourceGraph could not be loaded: %s"), *SourceGraph));
			}
			if (!SourceObject->GetClass()->GetName().Contains(TEXT("PCGGraph"), ESearchCase::IgnoreCase))
			{
				return ErrorJson(FString::Printf(TEXT("sourceGraph is not a PCGGraph asset: %s"), *SourceObject->GetPathName()));
			}

			FString PackageName;
			FString AssetName;
			FString Error;
			if (!SplitAssetPath(AssetPath, PackageName, AssetName, Error))
			{
				return ErrorJson(Error);
			}
			if (StaticFindObject(UObject::StaticClass(), nullptr, *NormalizeAssetObjectPath(PackageName)))
			{
				return ErrorJson(FString::Printf(TEXT("Asset already exists: %s"), *PackageName));
			}

			Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "CreatePcgGraphFromRecipe", "MCP Create PCG Graph From Recipe"));
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				return ErrorJson(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
			}
			Package->FullyLoad();

			UObject* DuplicatedGraph = StaticDuplicateObject(SourceObject, Package, FName(*AssetName));
			if (!DuplicatedGraph)
			{
				return ErrorJson(TEXT("Failed to duplicate source PCG graph."));
			}
			DuplicatedGraph->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
			DuplicatedGraph->ClearFlags(RF_Transient);
			FAssetRegistryModule::AssetCreated(DuplicatedGraph);
			Package->MarkPackageDirty();

			bool bSaved = false;
			if (bSave)
			{
				TArray<UPackage*> PackagesToSave;
				PackagesToSave.Add(Package);
				bSaved = SavePackages(PackagesToSave);
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("assetPath"), DuplicatedGraph->GetPathName());
			Result->SetStringField(TEXT("packageName"), PackageName);
			Result->SetStringField(TEXT("sourceGraph"), SourceObject->GetPathName());
			Result->SetStringField(TEXT("recipeId"), RecipeId);
			Result->SetBoolField(TEXT("recipeApplied"), true);
			Result->SetStringField(TEXT("buildMethod"), TEXT("duplicated_source_graph"));
			Result->SetBoolField(TEXT("sceneBindingApplied"), false);
			Result->SetBoolField(TEXT("saved"), bSaved);
			Result->SetStringField(TEXT("note"), TEXT("Duplicated the recipe source_graph PCGGraph. Scene input binding and generated node synthesis are still separate steps."));
			return JsonObjectToString(Result);
		}

		TSharedPtr<FJsonObject> CreateArgs = MakeShared<FJsonObject>();
		CreateArgs->SetStringField(TEXT("assetPath"), AssetPath);
		CreateArgs->SetStringField(TEXT("class"), TEXT("PCGGraph"));
		CreateArgs->SetBoolField(TEXT("save"), bSave);

		FString Created = CreateAsset(CreateArgs);
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Created);
		TSharedPtr<FJsonObject> Result;
		if (!FJsonSerializer::Deserialize(Reader, Result) || !Result.IsValid())
		{
			return Created;
		}

		bool bSuccess = true;
		if (Result->TryGetBoolField(TEXT("success"), bSuccess) && !bSuccess)
		{
			return Created;
		}

		Result->SetStringField(TEXT("recipeId"), RecipeId);
		Result->SetBoolField(TEXT("recipeApplied"), false);
		Result->SetStringField(TEXT("buildMethod"), TEXT("empty_pcg_graph_asset"));
		Result->SetBoolField(TEXT("sceneBindingApplied"), false);
		Result->SetStringField(TEXT("note"),
			TEXT(
				"Created a PCGGraph asset container because no sourceGraph/source_graph was supplied. Recipe-to-node graph synthesis and scene binding are not implemented in this plugin yet."));
		return JsonObjectToString(Result.ToSharedRef());
	}
}
