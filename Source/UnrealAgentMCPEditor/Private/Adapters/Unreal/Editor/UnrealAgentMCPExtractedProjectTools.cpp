// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedProjectTools.cpp
 * @brief 资产、关卡 Actor、工程模块与构建环境查询工具。
 */

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"

#include "Adapters/Tooling/UnrealAgentMCPExtractedToolSupport.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"
#include "Adapters/Tooling/UnrealAgentMCPTools.h"

namespace UnrealAgentMCP::ExtractedTools
{
	FString SearchAssets(const TSharedPtr<FJsonObject>& Args)
	{
		TSharedPtr<FJsonObject> Query = Args.IsValid() ? MakeShared<FJsonObject>(*Args.Get()) : MakeShared<FJsonObject>();
		FString QueryText;
		if (Query->TryGetStringField(TEXT("query"), QueryText) && !QueryText.IsEmpty() && !Query->HasField(TEXT("searchTerm")))
		{
			Query->SetStringField(TEXT("searchTerm"), QueryText);
		}
		return UnrealAgentMCP::Tools::FindAssets(Query);
	}

	FString FindStaticMeshes(const TSharedPtr<FJsonObject>& Args)
	{
		TSharedPtr<FJsonObject> Query = Args.IsValid() ? MakeShared<FJsonObject>(*Args.Get()) : MakeShared<FJsonObject>();
		FString QueryText;
		if (Query->TryGetStringField(TEXT("query"), QueryText) && !QueryText.IsEmpty() && !Query->HasField(TEXT("searchTerm")))
		{
			Query->SetStringField(TEXT("searchTerm"), QueryText);
		}
		Query->SetStringField(TEXT("classFilter"), TEXT("StaticMesh"));
		return UnrealAgentMCP::Tools::FindAssets(Query);
	}

	FString GetLevelActors(const TSharedPtr<FJsonObject>& Args)
	{
		return UnrealAgentMCP::Tools::ListLevelActors(Args.IsValid() ? Args : MakeShared<FJsonObject>());
	}

	FString GetProjectInfo(const TSharedPtr<FJsonObject>& Args)
	{
		return FUnrealAgentMCPServer::GetProjectInfoJson();
	}

	FString ListProjectModules(const TSharedPtr<FJsonObject>& Args)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);

		const FString SourceRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")));
		TArray<FString> BuildFiles;
		IFileManager::Get().FindFilesRecursive(BuildFiles, *SourceRoot, TEXT("*.Build.cs"), true, false);
		BuildFiles.Sort();

		TArray<TSharedPtr<FJsonValue>> Modules;
		for (const FString& File : BuildFiles)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), FPaths::GetBaseFilename(File).Replace(TEXT(".Build"), TEXT("")));
			Row->SetStringField(TEXT("buildFile"), ExtractedToolSupport::MakeProjectRelative(File));
			Modules.Add(MakeShared<FJsonValueObject>(Row));
		}
		Result->SetNumberField(TEXT("moduleCount"), Modules.Num());
		Result->SetArrayField(TEXT("modules"), Modules);
		return ExtractedToolSupport::SerializeObject(Result);
	}

	FString GetBuildConfiguration(const TSharedPtr<FJsonObject>& Args)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
#if UE_BUILD_DEBUG
		Result->SetStringField(TEXT("configuration"), TEXT("Debug"));
#elif UE_BUILD_DEVELOPMENT
		Result->SetStringField(TEXT("configuration"), TEXT("Development"));
#elif UE_BUILD_SHIPPING
		Result->SetStringField(TEXT("configuration"), TEXT("Shipping"));
#else
		Result->SetStringField(TEXT("configuration"), TEXT("Unknown"));
#endif
		Result->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
		Result->SetBoolField(TEXT("withEditor"), GIsEditor);
		Result->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
		return ExtractedToolSupport::SerializeObject(Result);
	}
}
