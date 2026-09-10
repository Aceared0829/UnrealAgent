// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPServerEnvironmentTests.cpp
 * @brief MCP 工程标识、端口、令牌与配置路径规则测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPProjectIdentityRulesTest, "WorldData.UnrealAgent.Environment.ProjectIdentityAndPort",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPProjectIdentityRulesTest::RunTest(const FString& Parameters)
	{
		TestEqual(TEXT("工程名规范化为小写下划线"), ServerEnvironment::SanitizeNamePart(TEXT("My Project--Demo")), FString(TEXT("my_project_demo")));
		TestEqual(TEXT("空工程名使用稳定回退值"), ServerEnvironment::SanitizeNamePart(TEXT("---")), FString(TEXT("project")));
		TestEqual(TEXT("连续分隔符合并"), ServerEnvironment::SanitizeNamePart(TEXT("_A / B_")), FString(TEXT("a_b")));

		const int32 DefaultPort = ServerEnvironment::GetDefaultPort();
		TestTrue(TEXT("默认端口不低于工程端口基值"), DefaultPort >= 5753);
		TestTrue(TEXT("默认端口位于工程哈希窗口内"), DefaultPort < 25753);
		TestEqual(TEXT("工程哈希固定为八位十六进制"), ServerEnvironment::GetProjectHashString().Len(), 8);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPEnvironmentTokenAndPathRulesTest, "WorldData.UnrealAgent.Environment.TokenAndConfigurationPaths",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPEnvironmentTokenAndPathRulesTest::RunTest(const FString& Parameters)
	{
		const FString GeneratedToken = ServerEnvironment::GenerateAccessToken();
		TestTrue(TEXT("生成令牌满足强度要求"), ServerEnvironment::IsStrongAccessToken(GeneratedToken));
		TestEqual(TEXT("生成令牌由两组无分隔 GUID 组成"), GeneratedToken.Len(), 64);
		TestFalse(TEXT("拒绝过短令牌"), ServerEnvironment::IsStrongAccessToken(TEXT("short")));

		const FString SavedConfigPath = ServerEnvironment::GetSavedConfigPath();
		const FString ConnectionPath = ServerEnvironment::GetConnectionPath();
		const FString CursorConfigPath = ServerEnvironment::GetCursorClientConfigPath();
		TestEqual(TEXT("持久配置文件名"), FPaths::GetCleanFilename(SavedConfigPath), FString(TEXT("config.json")));
		TestTrue(TEXT("连接文件使用主路径或 ACL 恢复路径"),
			FPaths::GetCleanFilename(ConnectionPath) == TEXT("mcp.json") || FPaths::GetCleanFilename(ConnectionPath) == TEXT("mcp.recovered.json"));
		TestEqual(TEXT("Cursor 配置文件名"), FPaths::GetCleanFilename(CursorConfigPath), FString(TEXT("mcp.json")));
		TestTrue(TEXT("持久配置位于 Unreal Agent 目录"), SavedConfigPath.Contains(TEXT("UnrealAgent")));
		TestTrue(TEXT("Cursor 配置位于 .cursor 目录"), CursorConfigPath.Contains(TEXT(".cursor")));
		TestFalse(TEXT("Cursor 配置不写入项目仓库"), CursorConfigPath.StartsWith(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), ESearchCase::IgnoreCase));

		const FString PrivateFilePath =
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("Tests"), FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
		TestTrue(TEXT("私密配置可以原子写入"), ServerEnvironment::WriteStringAtomically(TEXT("{\"secret\":\"redacted-test-value\"}"), PrivateFilePath, true));
		FString WriteError;
		TestTrue(TEXT("已有私密配置可以原子覆盖并重新验证 ACL"),
			ServerEnvironment::WriteStringAtomically(TEXT("{\"secret\":\"rotated-test-value\"}"), PrivateFilePath, true, WriteError));
		if (!WriteError.IsEmpty())
		{
			AddError(WriteError);
		}
		FString WrittenContent;
		TestTrue(TEXT("覆盖后的私密配置可读"), FFileHelper::LoadFileToString(WrittenContent, *PrivateFilePath));
		TestEqual(TEXT("覆盖后的私密配置内容完整"), WrittenContent, FString(TEXT("{\"secret\":\"rotated-test-value\"}")));
		FString AccessError;
		TestTrue(TEXT("私密配置仅允许当前用户访问"), ServerEnvironment::IsFileAccessRestrictedToCurrentUser(PrivateFilePath, AccessError));
		if (!AccessError.IsEmpty())
		{
			AddError(AccessError);
		}
		IFileManager::Get().Delete(*PrivateFilePath);
		return true;
	}
}

#endif
