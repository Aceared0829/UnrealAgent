// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedPcgLibrary.cpp
 * @brief PCG Recipe 与场景绑定 JSON 库查询工具。
 */

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Adapters/FileSystem/UnrealAgentMCPExtractedFileService.h"
#include "Adapters/Tooling/UnrealAgentMCPExtractedToolSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"

namespace UnrealAgentMCP::ExtractedTools
{
	namespace
	{
		using namespace ExtractedFileService;
		using namespace ExtractedToolSupport;

		void FindRecipeRoots(TArray<FString>& OutRoots)
		{
			OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("pcg_tool"), TEXT("recipe_library"))));
			OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Nwiro"), TEXT("pcg_tool"), TEXT("recipe_library"))));
			OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PCGRecipeLibrary"))));
		}

		FString GetActiveRecipeRoot()
		{
			TArray<FString> Roots;
			FindRecipeRoots(Roots);
			for (const FString& Root : Roots)
			{
				if (IFileManager::Get().DirectoryExists(*Root))
				{
					return Root;
				}
			}
			return Roots.Num() > 0 ? Roots[0] : FString();
		}

		void FindJsonFilesRecursive(const FString& Root, TArray<FString>& OutFiles)
		{
			if (IFileManager::Get().DirectoryExists(*Root))
			{
				IFileManager::Get().FindFilesRecursive(OutFiles, *Root, TEXT("*.json"), true, false);
				OutFiles.Sort();
			}
		}

		void GetStringArrayField(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* FieldName, TArray<FString>& OutValues)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Arguments.IsValid() || !Arguments->TryGetArrayField(FieldName, Values) || !Values)
			{
				return;
			}

			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const FString Text = Value.IsValid() ? Value->AsString() : FString();
				if (!Text.IsEmpty())
				{
					OutValues.Add(Text);
				}
			}
		}

		bool JsonObjectMatchesId(const TSharedPtr<FJsonObject>& Object, const FString& WantedId)
		{
			if (!Object.IsValid() || WantedId.IsEmpty())
			{
				return false;
			}

			for (const TCHAR* FieldName : { TEXT("id"), TEXT("recipe_id"), TEXT("binding_id"), TEXT("name") })
			{
				FString Value;
				if (Object->TryGetStringField(FieldName, Value) && Value.Equals(WantedId, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		FString FindJsonFileByIdOrName(const FString& Wanted, const TArray<FString>& Files)
		{
			if (Wanted.IsEmpty())
			{
				return FString();
			}

			for (const FString& File : Files)
			{
				const FString BaseName = FPaths::GetBaseFilename(File);
				if (BaseName.Equals(Wanted, ESearchCase::IgnoreCase) || File.Contains(Wanted, ESearchCase::IgnoreCase))
				{
					return File;
				}

				const TSharedPtr<FJsonObject> Object = LoadJsonObjectFile(File);
				if (JsonObjectMatchesId(Object, Wanted))
				{
					return File;
				}
			}
			return FString();
		}

		FString ReadJsonFileAsResult(const FString& File, const FString& FieldName)
		{
			if (!FPaths::GetExtension(File, false).Equals(TEXT("json"), ESearchCase::IgnoreCase))
			{
				return ErrorJson(TEXT("Only .json files can be read by this helper."));
			}

			const int64 FileSize = IFileManager::Get().FileSize(*File);
			if (FileSize < 0)
			{
				return ErrorJson(FString::Printf(TEXT("Unable to stat file: %s"), *File));
			}
			if (FileSize > MaximumReadableFileBytes)
			{
				return ErrorJson(FString::Printf(TEXT("File is larger than the %d byte read limit."), MaximumReadableFileBytes));
			}

			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *File))
			{
				return ErrorJson(FString::Printf(TEXT("Failed to read file: %s"), *File));
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("file"), MakeProjectRelative(File));

			const TSharedPtr<FJsonObject> Object = ParseJsonObject(Content);
			if (Object.IsValid())
			{
				Result->SetObjectField(FieldName, Object);
			}
			else
			{
				Result->SetStringField(TEXT("content"), Content);
			}
			return SerializeObject(Result);
		}
	}

	FString PcgRecipeLibraryStatus(const TSharedPtr<FJsonObject>& Args)
	{
		TArray<FString> Roots;
		FindRecipeRoots(Roots);
		const FString ActiveRoot = GetActiveRecipeRoot();

		TArray<FString> Files;
		FindJsonFilesRecursive(ActiveRoot, Files);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("activeRoot"), ActiveRoot);
		Result->SetBoolField(TEXT("exists"), IFileManager::Get().DirectoryExists(*ActiveRoot));
		Result->SetNumberField(TEXT("jsonFileCount"), Files.Num());
		AddStringArray(Result, TEXT("candidateRoots"), Roots);

		TArray<FString> RelativeFiles;
		for (int32 Index = 0; Index < Files.Num() && Index < 50; ++Index)
		{
			RelativeFiles.Add(MakeProjectRelative(Files[Index]));
		}
		AddStringArray(Result, TEXT("sampleFiles"), RelativeFiles);

		Result->SetStringField(TEXT("guidance"), TEXT("Use search_pcg_recipes first, then read_pcg_recipe for the selected id/file."));
		return SerializeObject(Result);
	}

	FString SearchPcgRecipes(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Query = GetStringField(Args, TEXT("query")).ToLower();
		TArray<FString> Tags;
		TArray<FString> RequiredSceneInputs;
		TArray<FString> OutputLayers;
		GetStringArrayField(Args, TEXT("tags"), Tags);
		GetStringArrayField(Args, TEXT("required_scene_inputs"), RequiredSceneInputs);
		GetStringArrayField(Args, TEXT("output_layers"), OutputLayers);

		const int32 Limit = FMath::Clamp(static_cast<int32>(GetNumberField(Args, TEXT("limit"), 10)), 1, 100);
		const bool bIncludeRecipe = GetBoolField(Args, TEXT("include_recipe"), false);

		TArray<FString> Files;
		FindJsonFilesRecursive(GetActiveRecipeRoot(), Files);

		TArray<TSharedPtr<FJsonValue>> Hits;
		for (const FString& File : Files)
		{
			if (Hits.Num() >= Limit)
			{
				break;
			}

			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *File))
			{
				continue;
			}

			const FString LowerContent = Content.ToLower();
			bool bMatches = Query.IsEmpty() || LowerContent.Contains(Query);
			for (const FString& Tag : Tags)
			{
				bMatches = bMatches && LowerContent.Contains(Tag.ToLower());
			}
			for (const FString& SceneInput : RequiredSceneInputs)
			{
				bMatches = bMatches && LowerContent.Contains(SceneInput.ToLower());
			}
			for (const FString& OutputLayer : OutputLayers)
			{
				bMatches = bMatches && LowerContent.Contains(OutputLayer.ToLower());
			}
			if (!bMatches)
			{
				continue;
			}

			TSharedRef<FJsonObject> Hit = MakeShared<FJsonObject>();
			Hit->SetStringField(TEXT("file"), MakeProjectRelative(File));

			const TSharedPtr<FJsonObject> Object = ParseJsonObject(Content);
			if (Object.IsValid())
			{
				for (const TCHAR* FieldName : { TEXT("id"), TEXT("recipe_id"), TEXT("name"), TEXT("title") })
				{
					FString Value;
					if (Object->TryGetStringField(FieldName, Value))
					{
						Hit->SetStringField(FieldName, Value);
					}
				}
				if (bIncludeRecipe)
				{
					Hit->SetObjectField(TEXT("recipe"), Object);
				}
			}
			Hits.Add(MakeShared<FJsonValueObject>(Hit));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("activeRoot"), GetActiveRecipeRoot());
		Result->SetNumberField(TEXT("count"), Hits.Num());
		Result->SetArrayField(TEXT("recipes"), Hits);
		return SerializeObject(Result);
	}

	FString ReadPcgRecipe(const TSharedPtr<FJsonObject>& Args)
	{
		const FString FileArgument = GetStringField(Args, TEXT("file"));
		const FString Id = !GetStringField(Args, TEXT("id")).IsEmpty() ? GetStringField(Args, TEXT("id")) : GetStringField(Args, TEXT("recipe_id"));

		FString FullPath;
		FString Error;
		const FString ActiveRoot = GetActiveRecipeRoot();
		if (!FileArgument.IsEmpty())
		{
			if (!ResolvePcgJsonFilePath(FileArgument, ActiveRoot, ActiveRoot, FullPath, Error))
			{
				return ErrorJson(Error);
			}
		}
		else
		{
			TArray<FString> Files;
			FindJsonFilesRecursive(ActiveRoot, Files);
			FullPath = FindJsonFileByIdOrName(Id, Files);
			if (FullPath.IsEmpty())
			{
				return ErrorJson(TEXT("PCG recipe was not found."));
			}
			if (!ValidateReadableJsonFileWithinRoot(FullPath, ActiveRoot, FullPath, Error))
			{
				return ErrorJson(Error);
			}
		}

		return ReadJsonFileAsResult(FullPath, TEXT("recipe"));
	}

	FString ReadPcgSceneBinding(const TSharedPtr<FJsonObject>& Args)
	{
		const FString FileArgument = GetStringField(Args, TEXT("file"));
		const FString Wanted = !GetStringField(Args, TEXT("binding_id")).IsEmpty() ? GetStringField(Args, TEXT("binding_id")) : GetStringField(Args, TEXT("recipe_id"));

		FString FullPath;
		FString Error;
		const FString ActiveRoot = GetActiveRecipeRoot();
		const FString PcgRoot = FPaths::GetPath(ActiveRoot);
		if (!FileArgument.IsEmpty())
		{
			if (!ResolvePcgJsonFilePath(FileArgument, ActiveRoot, ActiveRoot, FullPath, Error))
			{
				return ErrorJson(Error);
			}
		}
		else
		{
			TArray<FString> Files;
			FindJsonFilesRecursive(PcgRoot, Files);
			FullPath = FindJsonFileByIdOrName(Wanted, Files);
			if (FullPath.IsEmpty())
			{
				return ErrorJson(TEXT("PCG scene binding was not found."));
			}
			if (!ValidateReadableJsonFileWithinRoot(FullPath, PcgRoot, FullPath, Error))
			{
				return ErrorJson(Error);
			}
		}

		return ReadJsonFileAsResult(FullPath, TEXT("sceneBinding"));
	}
}
