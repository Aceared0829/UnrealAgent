// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPClientConfigurationTests.cpp
 * @brief MCP 客户端配置合并契约测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Infrastructure/Configuration/UnrealAgentMCPClientConfiguration.h"

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		ClientConfiguration::FMcpClientConnectionDescriptor MakeTestConnection()
		{
			ClientConfiguration::FMcpClientConnectionDescriptor Connection;
			Connection.ProjectName = TEXT("TestProject");
			Connection.ProjectId = TEXT("test_project_12345678");
			Connection.ServerName = TEXT("world_data_test_project_12345678");
			Connection.Url = TEXT("http://127.0.0.1:5753/mcp");
			Connection.ProtocolVersion = TEXT("2025-06-18");
			Connection.AccessTokenHeaderName = TEXT("X-WorldData-MCP-Token");
			Connection.AccessToken = TEXT("0123456789abcdef0123456789abcdef");
			Connection.Command = TEXT("D:\\Project\\Plugins\\UnrealAgent\\Binaries\\Win64\\UnrealAgentMCPHost.exe");
			Connection.Arguments.Add(TEXT("--project=D:\\Project\\TestProject.uproject"));
			return Connection;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPJsonClientConfigurationMergeTest, "WorldData.UnrealAgent.Configuration.JsonClientMerge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPJsonClientConfigurationMergeTest::RunTest(const FString& Parameters)
	{
		const ClientConfiguration::FMcpClientConnectionDescriptor Connection = MakeTestConnection();
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("userSetting"), TEXT("keep"));
		TSharedRef<FJsonObject> Servers = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> UserServer = MakeShared<FJsonObject>();
		UserServer->SetStringField(TEXT("url"), TEXT("https://user.example/mcp"));
		Servers->SetObjectField(TEXT("user_server"), UserServer);
		Servers->SetObjectField(TEXT("world_data_old_project_deadbeef"), MakeShared<FJsonObject>());
		TSharedRef<FJsonObject> GeneratedServer = MakeShared<FJsonObject>();
		GeneratedServer->SetStringField(TEXT("generatedBy"), TEXT("UnrealAgentMCP"));
		Servers->SetObjectField(TEXT("custom_generated_name"), GeneratedServer);
		Root->SetObjectField(TEXT("mcpServers"), Servers);

		const TSharedRef<FJsonObject> Merged = ClientConfiguration::MergeJsonClientConfiguration(Root, Connection);
		TestEqual(TEXT("保留用户根字段"), Merged->GetStringField(TEXT("userSetting")), FString(TEXT("keep")));

		const TSharedPtr<FJsonObject>* MergedServers = nullptr;
		TestTrue(TEXT("保留 mcpServers 对象"), Merged->TryGetObjectField(TEXT("mcpServers"), MergedServers) && MergedServers != nullptr && MergedServers->IsValid());
		if (MergedServers != nullptr && MergedServers->IsValid())
		{
			TestTrue(TEXT("保留用户服务器"), (*MergedServers)->HasField(TEXT("user_server")));
			TestFalse(TEXT("移除旧 world_data 条目"), (*MergedServers)->HasField(TEXT("world_data_old_project_deadbeef")));
			TestFalse(TEXT("移除 generatedBy 受管条目"), (*MergedServers)->HasField(TEXT("custom_generated_name")));
			TestTrue(TEXT("写入当前服务器"), (*MergedServers)->HasField(Connection.ServerName));
			TestEqual(TEXT("合并后仅两个服务器"), (*MergedServers)->Values.Num(), 2);
			const TSharedPtr<FJsonObject>* CurrentServer = nullptr;
			if ((*MergedServers)->TryGetObjectField(Connection.ServerName, CurrentServer) && CurrentServer != nullptr && CurrentServer->IsValid())
			{
				TestEqual(TEXT("使用稳定 stdio Host"), (*CurrentServer)->GetStringField(TEXT("command")), Connection.Command);
				TestFalse(TEXT("stdio 配置不固化动态端口"), (*CurrentServer)->HasField(TEXT("url")));
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPCodexConfigurationMergeTest, "WorldData.UnrealAgent.Configuration.CodexTomlMerge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPCodexConfigurationMergeTest::RunTest(const FString& Parameters)
	{
		const ClientConfiguration::FMcpClientConnectionDescriptor Connection = MakeTestConnection();
		const FString ExistingConfiguration =
			TEXT("model = \"gpt-test\"\n") TEXT("\n") TEXT("# Managed by UnrealAgentMCP (Legacy). Do not edit.\n") TEXT("# Managed by UnrealAgentMCP (Legacy). Do not edit.\n")
				TEXT("[mcp_servers.user_server]\n") TEXT("url = \"https://user.example/mcp\"\n") TEXT("\n") TEXT("[mcp_servers.world_data_old_project_deadbeef]\n")
					TEXT("url = \"http://127.0.0.1:1111/mcp\"\n") TEXT("\n") TEXT("[mcp_servers.world_data_old_project_deadbeef.env]\n") TEXT("OLD = \"value\"\n");

		const FString Merged = ClientConfiguration::MergeCodexTomlConfiguration(ExistingConfiguration, Connection);
		TestTrue(TEXT("保留用户模型设置"), Merged.Contains(TEXT("model = \"gpt-test\"")));
		TestTrue(TEXT("保留用户 MCP 服务器"), Merged.Contains(TEXT("[mcp_servers.user_server]")));
		TestFalse(TEXT("移除遗留服务器区段"), Merged.Contains(TEXT("world_data_old_project_deadbeef")));
		const FString ManagedComment = TEXT("# Managed by Unreal Agent");
		TestEqual(TEXT("只保留一条托管注释"), Merged.Find(ManagedComment), Merged.Find(ManagedComment, ESearchCase::CaseSensitive, ESearchDir::FromEnd));
		TestTrue(TEXT("写入当前服务器区段"), Merged.Contains(FString::Printf(TEXT("[mcp_servers.%s]"), *Connection.ServerName)));
		FString EscapedCommand = Connection.Command;
		EscapedCommand.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		TestTrue(TEXT("写入稳定 Host 命令"), Merged.Contains(EscapedCommand));
		TestTrue(TEXT("写入项目参数"), Merged.Contains(TEXT("--project=")));
		TestFalse(TEXT("stdio 配置不固化访问令牌"), Merged.Contains(Connection.AccessToken));

		const FString MergedAgain = ClientConfiguration::MergeCodexTomlConfiguration(Merged, Connection);
		int32 FirstManagedSectionIndex = INDEX_NONE;
		int32 LastManagedSectionIndex = INDEX_NONE;
		const FString ManagedSection = FString::Printf(TEXT("[mcp_servers.%s]"), *Connection.ServerName);
		FirstManagedSectionIndex = MergedAgain.Find(ManagedSection);
		LastManagedSectionIndex = MergedAgain.Find(ManagedSection, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		TestEqual(TEXT("重复合并仅保留一个当前区段"), FirstManagedSectionIndex, LastManagedSectionIndex);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPClaudeConfigurationMergeTest, "WorldData.UnrealAgent.Configuration.ClaudeSettingsMerge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPClaudeConfigurationMergeTest::RunTest(const FString& Parameters)
	{
		TSharedRef<FJsonObject> Existing = MakeShared<FJsonObject>();
		Existing->SetStringField(TEXT("userSetting"), TEXT("keep"));
		const TSharedRef<FJsonObject> Merged = ClientConfiguration::MergeClaudeProjectSettings(Existing);
		TestEqual(TEXT("保留 Claude 用户设置"), Merged->GetStringField(TEXT("userSetting")), FString(TEXT("keep")));
		TestTrue(TEXT("启用工程 MCP 服务器"), Merged->GetBoolField(TEXT("enableAllProjectMcpServers")));
		return true;
	}
}

#endif
