// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPFeedbackMigrationTests.cpp
 * @brief Feedback 应用服务的本地路由、归档和清理集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Feedback/UnrealAgentMCPUnrealFeedbackAdapter.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPFeedbackMigrationIntegrationTest, "WorldData.UnrealAgent.Feedback.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPFeedbackMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		FUnrealAgentMCPUnrealFeedbackAdapter Adapter;
		auto MakeFeedback = []()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("title"), TEXT("中文输入焦点回归"));
			Args->SetStringField(TEXT("summary"), TEXT("输入法候选窗需要稳定绑定编辑器面板。"));
			Args->SetStringField(TEXT("pythonWorkaround"), TEXT(""));
			Args->SetStringField(TEXT("idealTool"), TEXT("IME panel input"));
			return Args;
		};

		TSharedRef<FJsonObject> RouteArgs = MakeFeedback();
		RouteArgs->SetStringField(TEXT("action"), TEXT("route"));
		const FString RouteResult = Adapter.Execute(RouteArgs);
		TestTrue(TEXT("反馈路由成功并指向 Editor UI"), RouteResult.Contains(TEXT("\"success\":true")) && RouteResult.Contains(TEXT("editor-ui")));

		TSharedRef<FJsonObject> SubmitArgs = MakeFeedback();
		SubmitArgs->SetStringField(TEXT("action"), TEXT("submit"));
		const FString SubmitResult = Adapter.Execute(SubmitArgs);
		TestTrue(TEXT("反馈提交成功且仅本地归档"), SubmitResult.Contains(TEXT("\"success\":true")) && SubmitResult.Contains(TEXT("\"externalSubmission\":false")));

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SubmitResult);
		FString ArchivePath;
		if (TestTrue(TEXT("反馈响应可以解析"), FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid()))
		{
			Parsed->TryGetStringField(TEXT("archivePath"), ArchivePath);
		}
		const FString ExpectedRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("Feedback")));
		TestTrue(TEXT("归档路径受限于项目 Saved 目录"), !ArchivePath.IsEmpty() && ArchivePath.StartsWith(ExpectedRoot, ESearchCase::IgnoreCase));
		TestTrue(TEXT("反馈归档文件真实存在"), IFileManager::Get().FileExists(*ArchivePath));
		FString ArchivedContent;
		FFileHelper::LoadFileToString(ArchivedContent, *ArchivePath);
		TestTrue(TEXT("归档保留中文标题与路由"), ArchivedContent.Contains(TEXT("中文输入焦点回归")) && ArchivedContent.Contains(TEXT("editor-ui")));
		if (!ArchivePath.IsEmpty() && ArchivePath.StartsWith(ExpectedRoot, ESearchCase::IgnoreCase))
		{
			IFileManager::Get().Delete(*ArchivePath, false, true, true);
		}
		return true;
	}
}

#endif
