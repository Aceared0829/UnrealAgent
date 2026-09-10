// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPExtractedFileServiceTests.cpp
 * @brief 扩展工具 JSON 解析与路径 containment 规则测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#include "Adapters/FileSystem/UnrealAgentMCPExtractedFileService.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPRestrictedPathRulesTest, "WorldData.UnrealAgent.ExtractedTools.RestrictedPathRules",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPRestrictedPathRulesTest::RunTest(const FString& Parameters)
	{
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ContainmentRoot")));
		const FString Child = FPaths::Combine(Root, TEXT("Nested"), TEXT("file.json"));
		const FString SiblingPrefix = Root + TEXT("_Sibling/Nested/file.json");
		TestTrue(TEXT("接受根目录内文件"), ExtractedFileService::IsPathInsideDirectory(Child, Root));
		TestFalse(TEXT("拒绝仅共享字符串前缀的兄弟目录"), ExtractedFileService::IsPathInsideDirectory(SiblingPrefix, Root));

		FString ResolvedPath;
		FString Error;
		TestTrue(TEXT("解析根目录内相对路径"), ExtractedFileService::ResolvePathWithinRoot(TEXT("Nested/file.json"), Root, Root, ResolvedPath, Error));
		TestTrue(TEXT("解析结果仍位于根目录"), ExtractedFileService::IsPathInsideDirectory(ResolvedPath, Root));

		Error.Empty();
		TestFalse(TEXT("拒绝通过父目录跳出根目录"), ExtractedFileService::ResolvePathWithinRoot(TEXT("../outside.json"), Root, Root, ResolvedPath, Error));
		TestTrue(TEXT("越界错误可读"), Error.Contains(TEXT("outside")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPJsonParsingRulesTest, "WorldData.UnrealAgent.ExtractedTools.JsonParsingRules",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPJsonParsingRulesTest::RunTest(const FString& Parameters)
	{
		const TSharedPtr<FJsonObject> ValidObject = ExtractedFileService::ParseJsonObject(TEXT("{\"id\":\"recipe_a\"}"));
		TestTrue(TEXT("解析有效 JSON 对象"), ValidObject.IsValid());
		if (ValidObject.IsValid())
		{
			TestEqual(TEXT("保留 JSON 字段"), ValidObject->GetStringField(TEXT("id")), FString(TEXT("recipe_a")));
		}
		TestFalse(TEXT("拒绝损坏 JSON"), ExtractedFileService::ParseJsonObject(TEXT("{invalid")).IsValid());
		TestFalse(TEXT("拒绝非对象 JSON 根"), ExtractedFileService::ParseJsonObject(TEXT("[1,2,3]")).IsValid());
		return true;
	}
}

#endif
