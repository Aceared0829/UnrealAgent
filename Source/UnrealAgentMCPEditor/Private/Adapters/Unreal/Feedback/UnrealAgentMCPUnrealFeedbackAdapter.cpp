// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealFeedbackAdapter.cpp
 * @brief 无外部网络依赖的反馈分类、JSON 归档和回读信息实现。
 */

#include "Adapters/Unreal/Feedback/UnrealAgentMCPUnrealFeedbackAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace UnrealAgentMCP
{
	TArray<FString> FUnrealAgentMCPUnrealFeedbackAdapter::GetImplementedActions()
	{
		return { TEXT("submit"), TEXT("route") };
	}

	FString FUnrealAgentMCPUnrealFeedbackAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		if (Action == TEXT("submit"))
		{
			return Submit(SafeArgs);
		}
		if (Action == TEXT("route"))
		{
			return Route(SafeArgs);
		}

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
		{
			Actions.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("feedback"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Feedback action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealFeedbackAdapter::DetermineRoute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Category;
		FString IdealTool;
		FString Summary;
		Args->TryGetStringField(TEXT("category"), Category);
		Args->TryGetStringField(TEXT("idealTool"), IdealTool);
		Args->TryGetStringField(TEXT("summary"), Summary);
		const FString Search = (Category + TEXT(" ") + IdealTool + TEXT(" ") + Summary).ToLower();
		if (Search.Contains(TEXT("host")) || Search.Contains(TEXT("stdio")) || Search.Contains(TEXT("transport")) || Search.Contains(TEXT("http")))
		{
			return TEXT("host");
		}
		if (Search.Contains(TEXT("engine")) || Search.Contains(TEXT("toolset")) || Search.Contains(TEXT("reflection")))
		{
			return TEXT("engine");
		}
		if (Search.Contains(TEXT("ui")) || Search.Contains(TEXT("panel")) || Search.Contains(TEXT("ime")) || Search.Contains(TEXT("input")))
		{
			return TEXT("editor-ui");
		}
		return TEXT("plugin");
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealFeedbackAdapter::MakeRouteJson(const TSharedPtr<FJsonObject>& Args)
	{
		const FString RouteName = DetermineRoute(Args);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("route"), RouteName);
		Result->SetStringField(TEXT("owner"),
			RouteName == TEXT("host")            ? TEXT("UnrealAgentMCPHost")
				: RouteName == TEXT("engine")    ? TEXT("Unreal Agent Reflection Infrastructure")
				: RouteName == TEXT("editor-ui") ? TEXT("Unreal Agent Editor UI")
												 : TEXT("Unreal Agent Domain Adapters"));
		Result->SetStringField(TEXT("delivery"), TEXT("local-archive"));
		Result->SetBoolField(TEXT("externalSubmission"), false);
		return Result;
	}

	FString FUnrealAgentMCPUnrealFeedbackAdapter::Route(const TSharedPtr<FJsonObject>& Args)
	{
		return SuccessJson(MakeRouteJson(Args));
	}

	FString FUnrealAgentMCPUnrealFeedbackAdapter::Submit(const TSharedPtr<FJsonObject>& Args)
	{
		FString Title;
		FString Summary;
		if (!Args->TryGetStringField(TEXT("title"), Title) || Title.TrimStartAndEnd().IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 title。"));
		}
		if (!Args->TryGetStringField(TEXT("summary"), Summary) || Summary.TrimStartAndEnd().IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 summary。"));
		}

		const FString FeedbackId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("Feedback")));
		FPaths::NormalizeDirectoryName(Directory);
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			return ErrorJson(TEXT("反馈归档目录创建失败。"));
		}
		const FString FilePath = FPaths::Combine(Directory, FeedbackId + TEXT(".json"));

		TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>(*Args);
		Document->RemoveField(TEXT("action"));
		Document->SetStringField(TEXT("id"), FeedbackId);
		Document->SetStringField(TEXT("createdAtUtc"), FDateTime::UtcNow().ToIso8601());
		Document->SetObjectField(TEXT("routing"), MakeRouteJson(Args));
		const FString Content = JsonObjectToString(Document, true);
		if (!FFileHelper::SaveStringToFile(Content, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return ErrorJson(TEXT("反馈归档写入失败。"));
		}

		TSharedRef<FJsonObject> Result = MakeRouteJson(Args);
		Result->SetStringField(TEXT("id"), FeedbackId);
		Result->SetStringField(TEXT("archivePath"), FilePath);
		Result->SetBoolField(TEXT("archived"), true);
		return SuccessJson(Result);
	}
}
