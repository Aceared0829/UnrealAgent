// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedResources.cpp
 * @brief Unreal Agent 扩展资源目录与资源读取实现。
 */

#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"

#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"

#include "Adapters/Tooling/UnrealAgentMCPExtractedToolSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"
#include "Adapters/Tooling/UnrealAgentMCPTools.h"

namespace UnrealAgentMCP::ExtractedTools
{
	namespace
	{
		using namespace ExtractedToolSupport;

		void AddResource(TArray<TSharedPtr<FJsonValue>>& Resources, const FString& Uri, const FString& Name, const FString& Description)
		{
			TSharedRef<FJsonObject> Resource = MakeShared<FJsonObject>();
			Resource->SetStringField(TEXT("uri"), Uri);
			Resource->SetStringField(TEXT("name"), Name);
			Resource->SetStringField(TEXT("description"), Description);
			Resource->SetStringField(TEXT("mimeType"), TEXT("application/json"));
			Resources.Add(MakeShared<FJsonValueObject>(Resource));
		}

		FString GetProjectPluginsResource()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);

			TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
			Plugins.Sort(
				[](const TSharedRef<IPlugin>& Left, const TSharedRef<IPlugin>& Right)
				{
					return Left->GetName() < Right->GetName();
				});

			TArray<TSharedPtr<FJsonValue>> Rows;
			for (int32 Index = 0; Index < Plugins.Num() && Index < MaximumResourceRows; ++Index)
			{
				const TSharedRef<IPlugin>& Plugin = Plugins[Index];
				const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();

				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("name"), Plugin->GetName());
				Row->SetStringField(TEXT("friendlyName"), Descriptor.FriendlyName);
				Row->SetStringField(TEXT("description"), Descriptor.Description);
				Row->SetStringField(TEXT("category"), Descriptor.Category);
				Row->SetStringField(TEXT("versionName"), Descriptor.VersionName);
				Row->SetBoolField(TEXT("canContainContent"), Descriptor.bCanContainContent);
				Row->SetBoolField(TEXT("isBeta"), Descriptor.bIsBetaVersion);
				Row->SetBoolField(TEXT("isExperimental"), Descriptor.bIsExperimentalVersion);
				Row->SetBoolField(TEXT("installed"), Descriptor.bInstalled);
				Row->SetStringField(TEXT("rootDir"), MakeProjectRelative(Plugin->GetBaseDir()));
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}

			Result->SetNumberField(TEXT("enabledPluginCount"), Plugins.Num());
			Result->SetBoolField(TEXT("truncated"), Plugins.Num() > MaximumResourceRows);
			Result->SetArrayField(TEXT("plugins"), Rows);
			return SerializeObject(Result);
		}

		FString GetProjectSourceIndexResource()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);

			TArray<FString> Roots;
			const FString SourceRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")));
			const FString PluginsRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
			if (IFileManager::Get().DirectoryExists(*SourceRoot))
			{
				Roots.Add(SourceRoot);
			}
			if (IFileManager::Get().DirectoryExists(*PluginsRoot))
			{
				Roots.Add(PluginsRoot);
			}

			TArray<FString> Files;
			for (const FString& Root : Roots)
			{
				for (const TCHAR* Pattern : { TEXT("*.h"), TEXT("*.hpp"), TEXT("*.cpp"), TEXT("*.cs"), TEXT("*.uplugin") })
				{
					TArray<FString> Found;
					IFileManager::Get().FindFilesRecursive(Found, *Root, Pattern, true, false);
					Files.Append(Found);
				}
			}
			Files.Sort();

			TArray<TSharedPtr<FJsonValue>> Rows;
			for (int32 Index = 0; Index < Files.Num() && Index < MaximumSourceIndexFiles; ++Index)
			{
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("path"), MakeProjectRelative(Files[Index]));
				Row->SetStringField(TEXT("extension"), FPaths::GetExtension(Files[Index]));
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}

			Result->SetNumberField(TEXT("fileCount"), Files.Num());
			Result->SetBoolField(TEXT("truncated"), Files.Num() > MaximumSourceIndexFiles);
			Result->SetArrayField(TEXT("files"), Rows);
			return SerializeObject(Result);
		}

		FString GetCurrentLevelResource()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
			if (!World)
			{
				Result->SetStringField(TEXT("error"), TEXT("No editor world."));
				Result->SetBoolField(TEXT("success"), false);
				return SerializeObject(Result);
			}

			TMap<FString, int32> ClassCounts;
			int32 ActorCount = 0;
			for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
			{
				AActor* Actor = *ActorIterator;
				if (!IsValid(Actor))
				{
					continue;
				}

				++ActorCount;
				const FString ClassName = Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT("Unknown");
				ClassCounts.FindOrAdd(ClassName)++;
			}

			TArray<TSharedPtr<FJsonValue>> Counts;
			for (const TPair<FString, int32>& Pair : ClassCounts)
			{
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("class"), Pair.Key);
				Row->SetNumberField(TEXT("count"), Pair.Value);
				Counts.Add(MakeShared<FJsonValueObject>(Row));
			}

			Result->SetStringField(TEXT("levelName"), World->GetMapName());
			Result->SetNumberField(TEXT("actorCount"), ActorCount);
			Result->SetArrayField(TEXT("classCounts"), Counts);
			return SerializeObject(Result);
		}

		FString GetLevelComponentsResource()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
			if (!World)
			{
				return ErrorJson(TEXT("No editor world."));
			}

			TMap<FString, int32> ComponentCounts;
			for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
			{
				AActor* Actor = *ActorIterator;
				if (!IsValid(Actor))
				{
					continue;
				}

				TInlineComponentArray<UActorComponent*> Components(Actor);
				for (UActorComponent* Component : Components)
				{
					if (Component)
					{
						ComponentCounts.FindOrAdd(Component->GetClass()->GetName())++;
					}
				}
			}

			TArray<TSharedPtr<FJsonValue>> Rows;
			for (const TPair<FString, int32>& Pair : ComponentCounts)
			{
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("class"), Pair.Key);
				Row->SetNumberField(TEXT("count"), Pair.Value);
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}
			Result->SetArrayField(TEXT("components"), Rows);
			return SerializeObject(Result);
		}

		FString GetEditorPerformanceResource()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetNumberField(TEXT("memoryUsedMB"), FPlatformMemory::GetStats().UsedPhysical / 1024.0 / 1024.0);
			Result->SetNumberField(TEXT("memoryPeakMB"), FPlatformMemory::GetStats().PeakUsedPhysical / 1024.0 / 1024.0);

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
			int32 ActorCount = 0;
			if (World)
			{
				for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
				{
					++ActorCount;
				}
			}
			Result->SetNumberField(TEXT("actorCount"), ActorCount);
			return SerializeObject(Result);
		}

		FString GetViewportResource()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("message"), TEXT("Viewport camera details are not exposed by this standalone Unreal Agent resource yet."));
			return SerializeObject(Result);
		}

		FString GetBootstrapResource()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("source"), TEXT("Unreal Agent extracted tools"));
			Result->SetStringField(TEXT("purpose"), TEXT("Read this first for compact Unreal project context."));
			Result->SetStringField(TEXT("projectName"), FApp::GetProjectName());
			Result->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
			if (World)
			{
				Result->SetStringField(TEXT("levelName"), World->GetMapName());
			}

			USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
			Result->SetNumberField(TEXT("selectedActorCount"), Selection ? Selection->Num() : 0);

			TArray<TSharedPtr<FJsonValue>> ReadOrder;
			auto AddStep = [&ReadOrder](const FString& Uri, const FString& Why)
			{
				TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
				Step->SetStringField(TEXT("uri"), Uri);
				Step->SetStringField(TEXT("why"), Why);
				ReadOrder.Add(MakeShared<FJsonValueObject>(Step));
			};
			AddStep(TEXT("ubridge://project/info"), TEXT("Basic project identity and paths."));
			AddStep(TEXT("ubridge://project/plugins"), TEXT("Enabled plugin inventory."));
			AddStep(TEXT("ubridge://project/source-index"), TEXT("Project source file index."));
			AddStep(TEXT("ubridge://content/assets"), TEXT("Asset registry overview."));
			AddStep(TEXT("ubridge://level/current"), TEXT("Current level summary."));
			AddStep(TEXT("ubridge://level/actors"), TEXT("Detailed actor context."));
			AddStep(TEXT("ubridge://blueprints/index"), TEXT("Blueprint inventory."));
			AddStep(TEXT("ubridge://pcg/graphs"), TEXT("PCG graph inventory."));
			AddStep(TEXT("ubridge://editor/problems"), TEXT("Recent warnings and errors."));
			AddStep(TEXT("ubridge://editor/selection"), TEXT("Focused selection context."));
			Result->SetArrayField(TEXT("recommendedReadOrder"), ReadOrder);

			return SerializeObject(Result);
		}
	}

	FString ListResources()
	{
		TArray<TSharedPtr<FJsonValue>> Resources;
		AddResource(Resources, TEXT("ubridge://context/bootstrap"), TEXT("Bootstrap Context"), TEXT("Recommended first-read order and compact editor state."));
		AddResource(Resources, TEXT("ubridge://project/info"), TEXT("Project Info"), TEXT("Engine version, project name, paths, and MCP endpoint."));
		AddResource(Resources, TEXT("ubridge://project/plugins"), TEXT("Project Plugins"), TEXT("Enabled plugin inventory and plugin metadata."));
		AddResource(Resources, TEXT("ubridge://project/source-index"), TEXT("Project Source Index"), TEXT("Source and plugin code file index."));
		AddResource(Resources, TEXT("ubridge://content/assets"), TEXT("Content Assets"), TEXT("Asset registry survey under /Game."));
		AddResource(Resources, TEXT("ubridge://level/current"), TEXT("Current Level"), TEXT("Current map summary and actor class distribution."));
		AddResource(Resources, TEXT("ubridge://level/actors"), TEXT("Level Actors"), TEXT("Current editor-world actors and transforms."));
		AddResource(Resources, TEXT("ubridge://level/components"), TEXT("Level Components"), TEXT("Component class distribution in the current world."));
		AddResource(Resources, TEXT("ubridge://blueprints/index"), TEXT("Blueprint Index"), TEXT("Blueprint asset inventory."));
		AddResource(Resources, TEXT("ubridge://pcg/graphs"), TEXT("PCG Graphs"), TEXT("PCG graph asset inventory."));
		AddResource(Resources, TEXT("ubridge://editor/problems"), TEXT("Editor Problems"), TEXT("Recent warning/error log lines."));
		AddResource(Resources, TEXT("ubridge://editor/selection"), TEXT("Editor Selection"), TEXT("Currently selected actors."));
		AddResource(Resources, TEXT("ubridge://editor/performance"), TEXT("Performance Stats"), TEXT("Memory and actor-count snapshot."));
		AddResource(Resources, TEXT("ubridge://editor/log"), TEXT("Editor Log"), TEXT("Recent project log lines."));
		AddResource(Resources, TEXT("ubridge://editor/viewport"), TEXT("Viewport Info"), TEXT("Viewport availability summary."));

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("source"), TEXT("UnrealAgent"));
		Result->SetStringField(TEXT("recommendedFirstRead"), TEXT("ubridge://context/bootstrap"));
		Result->SetArrayField(TEXT("resources"), Resources);
		return SerializeObject(Result);
	}

	FString ReadResource(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Uri = GetStringField(Args, TEXT("uri"));
		if (Uri.IsEmpty())
		{
			return ErrorJson(TEXT("uri is required."));
		}

		if (Uri == TEXT("ubridge://context/bootstrap"))
			return GetBootstrapResource();
		if (Uri == TEXT("ubridge://project/info"))
			return FUnrealAgentMCPServer::GetProjectInfoJson();
		if (Uri == TEXT("ubridge://project/plugins"))
			return GetProjectPluginsResource();
		if (Uri == TEXT("ubridge://project/source-index"))
			return GetProjectSourceIndexResource();
		if (Uri == TEXT("ubridge://content/assets"))
			return FUnrealAgentMCPServer::ReadResource(TEXT("worlddata://content/assets"));
		if (Uri == TEXT("ubridge://level/current"))
			return GetCurrentLevelResource();
		if (Uri == TEXT("ubridge://level/actors"))
			return FUnrealAgentMCPServer::ReadResource(TEXT("worlddata://level/actors"));
		if (Uri == TEXT("ubridge://level/components"))
			return GetLevelComponentsResource();
		if (Uri == TEXT("ubridge://blueprints/index"))
		{
			TSharedPtr<FJsonObject> Query = MakeShared<FJsonObject>();
			Query->SetStringField(TEXT("classFilter"), TEXT("Blueprint"));
			Query->SetNumberField(TEXT("maxResults"), MaximumResourceRows);
			return UnrealAgentMCP::Tools::FindAssets(Query);
		}
		if (Uri == TEXT("ubridge://pcg/graphs"))
		{
			TSharedPtr<FJsonObject> Query = MakeShared<FJsonObject>();
			Query->SetStringField(TEXT("classFilter"), TEXT("PCG"));
			Query->SetNumberField(TEXT("maxResults"), MaximumResourceRows);
			return UnrealAgentMCP::Tools::FindAssets(Query);
		}
		if (Uri == TEXT("ubridge://editor/problems"))
		{
			TSharedPtr<FJsonObject> Query = MakeShared<FJsonObject>();
			Query->SetNumberField(TEXT("lines"), 80);
			return ReadLog(Query);
		}
		if (Uri == TEXT("ubridge://editor/selection"))
			return FUnrealAgentMCPServer::ReadResource(TEXT("worlddata://editor/selection"));
		if (Uri == TEXT("ubridge://editor/performance"))
			return GetEditorPerformanceResource();
		if (Uri == TEXT("ubridge://editor/log"))
			return ReadLog(MakeShared<FJsonObject>());
		if (Uri == TEXT("ubridge://editor/viewport"))
			return GetViewportResource();

		return ErrorJson(FString::Printf(TEXT("Unknown resource: %s"), *Uri));
	}
}
