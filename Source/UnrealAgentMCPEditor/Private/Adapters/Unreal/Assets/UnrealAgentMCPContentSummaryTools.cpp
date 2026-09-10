// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPContentSummaryTools.cpp
 * @brief 内容类型摘要与 MCP 启动上下文构建工具。
 */

#include "Adapters/Tooling/UnrealAgentMCPTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/EngineVersion.h"
#include "UObject/Package.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Assets/UnrealAgentMCPAssetSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"

namespace UnrealAgentMCP::Tools
{
	using namespace ActorSupport;
	using namespace AssetSupport;

	FString GetContentSummary(const TSharedPtr<FJsonObject>& Args)
	{
		FString SearchRoot = TEXT("/Game");
		Args->TryGetStringField(TEXT("path"), SearchRoot);
		if (SearchRoot.IsEmpty())
		{
			SearchRoot = TEXT("/Game");
		}

		double MaxClassesNumber = 30.0;
		Args->TryGetNumberField(TEXT("maxClasses"), MaxClassesNumber);
		const int32 MaxClasses = FMath::Clamp(static_cast<int32>(MaxClassesNumber), 1, 200);

		int32 TotalAssets = 0;
		TMap<FString, int32> CountsByClass;
		ComputeContentCounts(SearchRoot, TotalAssets, CountsByClass);

		CountsByClass.ValueSort(
			[](const int32& A, const int32& B)
			{
				return A > B;
			});

		TArray<TSharedPtr<FJsonValue>> Classes;
		for (const TPair<FString, int32>& Pair : CountsByClass)
		{
			if (Classes.Num() >= MaxClasses)
			{
				break;
			}
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("class"), Pair.Key);
			Entry->SetNumberField(TEXT("count"), Pair.Value);
			Classes.Add(MakeShared<FJsonValueObject>(Entry));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), SearchRoot);
		Result->SetNumberField(TEXT("totalAssets"), TotalAssets);
		Result->SetNumberField(TEXT("classCount"), CountsByClass.Num());
		Result->SetBoolField(TEXT("truncated"), CountsByClass.Num() > Classes.Num());
		Result->SetArrayField(TEXT("byClass"), Classes);
		return SuccessJson(Result);
	}

	FString GetBootstrapContextJson()
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("purpose"),
			TEXT("Read this first. It gives an agent a compact, read-only strategy for understanding this Unreal project before taking action."));
		Result->SetStringField(TEXT("projectName"), GetProjectName());
		Result->SetStringField(TEXT("projectId"), FUnrealAgentMCPServer::GetProjectId());
		Result->SetStringField(TEXT("serverName"), FUnrealAgentMCPServer::GetServerName());
		Result->SetStringField(TEXT("mcpUrl"), FUnrealAgentMCPServer::GetMcpUrl());
		Result->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());

		TSharedRef<FJsonObject> Editor = MakeShared<FJsonObject>();
		UWorld* World = GetEditorWorld();
		if (World)
		{
			Editor->SetStringField(TEXT("levelName"), World->GetMapName());
			Editor->SetStringField(TEXT("levelPackage"), World->GetOutermost()->GetName());

			int32 ActorCount = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				++ActorCount;
			}
			Editor->SetNumberField(TEXT("actorCount"), ActorCount);
		}
		Editor->SetBoolField(TEXT("isPlayInEditor"), GEditor ? (GEditor->PlayWorld != nullptr) : false);
		Editor->SetNumberField(TEXT("selectedActorCount"), GEditor ? GEditor->GetSelectedActorCount() : 0);
		Result->SetObjectField(TEXT("editor"), Editor);

		// 提供紧凑的内容类型直方图，让调用方快速了解项目中已有的资产类型。
		{
			int32 TotalAssets = 0;
			TMap<FString, int32> CountsByClass;
			ComputeContentCounts(TEXT("/Game"), TotalAssets, CountsByClass);
			CountsByClass.ValueSort(
				[](const int32& A, const int32& B)
				{
					return A > B;
				});

			TArray<TSharedPtr<FJsonValue>> TopClasses;
			for (const TPair<FString, int32>& Pair : CountsByClass)
			{
				if (TopClasses.Num() >= 10)
				{
					break;
				}
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("class"), Pair.Key);
				Entry->SetNumberField(TEXT("count"), Pair.Value);
				TopClasses.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedRef<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("path"), TEXT("/Game"));
			Content->SetNumberField(TEXT("totalAssets"), TotalAssets);
			Content->SetNumberField(TEXT("classCount"), CountsByClass.Num());
			Content->SetArrayField(TEXT("topClasses"), TopClasses);
			Result->SetObjectField(TEXT("content"), Content);
		}

		TArray<TSharedPtr<FJsonValue>> ReadOrder;
		auto AddStep = [&ReadOrder](const FString& Uri, const FString& Why)
		{
			TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
			Step->SetStringField(TEXT("uri"), Uri);
			Step->SetStringField(TEXT("why"), Why);
			ReadOrder.Add(MakeShared<FJsonValueObject>(Step));
		};

		AddStep(TEXT("worlddata://project/info"), TEXT("Verify project identity and the active MCP endpoint."));
		AddStep(TEXT("worlddata://tools/catalog"), TEXT("Discover the complete top-level MCP tool catalog generated from the live runtime registry."));
		AddStep(TEXT("worlddata://codex/policy-snapshot"), TEXT("Inspect explicit local Codex approval, sandbox, model, and MCP configuration, if present."));
		AddStep(TEXT("worlddata://editor/selection"), TEXT("See what the user currently has selected to act in context."));
		AddStep(TEXT("worlddata://level/actors"), TEXT("Understand the current editor world before spawning or selecting actors."));
		AddStep(TEXT("worlddata://content/summary"), TEXT("Survey asset types before browsing details or creating duplicates."));
		AddStep(TEXT("worlddata://content/assets"), TEXT("List concrete asset paths when you need exact references."));
		Result->SetArrayField(TEXT("recommendedReadOrder"), ReadOrder);

		TArray<TSharedPtr<FJsonValue>> Notes;
		Notes.Add(MakeShared<FJsonValueString>(TEXT("Resources are read-only and compact by design.")));
		Notes.Add(MakeShared<FJsonValueString>(TEXT("Use tools for mutations only after reading enough context.")));
		Notes.Add(MakeShared<FJsonValueString>(
			TEXT("Landscape is a top-level MCP tool returned by tools/list; do not route it through Rider MCP or the reflection-only toolsets catalog.")));
		Notes.Add(MakeShared<FJsonValueString>(TEXT(
			"Whitebox is a top-level action-routed MCP tool for repeatable massing workflows such as build_city_wall; invoke it through tools/call rather than execute_python.")));
		Notes.Add(MakeShared<FJsonValueString>(
			TEXT("worlddata://toolsets/catalog lists reflected C++ toolsets only and is not a substitute for tools/list or worlddata://tools/catalog.")));
		Notes.Add(MakeShared<FJsonValueString>(TEXT("Prefer get_actor_details/get_selected_actors for focused inspection over listing all actors.")));
		Result->SetArrayField(TEXT("notes"), Notes);

		return JsonObjectToString(Result);
	}
}
