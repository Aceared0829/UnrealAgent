// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPluginsMigrationTests.cpp
 * @brief Plugins 应用服务与 UE 原生插件目录的真实集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Plugins/UnrealAgentMCPUnrealPluginsAdapter.h"
#include "Dom/JsonObject.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPluginsMigrationIntegrationTest, "WorldData.UnrealAgent.Plugins.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPPluginsMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		FUnrealAgentMCPUnrealPluginsAdapter Adapter;
		auto Execute = [&Adapter, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Adapter.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1000)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		TSharedRef<FJsonObject> ListArgs = MakeShared<FJsonObject>();
		ListArgs->SetBoolField(TEXT("enabledOnly"), true);
		ListArgs->SetStringField(TEXT("nameFilter"), TEXT("UnrealAgent"));
		const FString ListResult = Execute(TEXT("list"), ListArgs);
		TestTrue(TEXT("插件目录包含当前 UnrealAgentMCP"), ListResult.Contains(TEXT("UnrealAgent")) && ListResult.Contains(TEXT("Unreal IPluginManager")));

		TSharedRef<FJsonObject> DescribeArgs = MakeShared<FJsonObject>();
		DescribeArgs->SetStringField(TEXT("name"), TEXT("UnrealAgent"));
		const FString DescribeResult = Execute(TEXT("describe"), DescribeArgs);
		TestTrue(TEXT("插件详情包含自有 Core 与 Editor 模块"), DescribeResult.Contains(TEXT("UnrealAgentMCPCore")) && DescribeResult.Contains(TEXT("UnrealAgentMCPEditor")));
		TestTrue(TEXT("插件详情包含描述文件和依赖数组"), DescribeResult.Contains(TEXT("descriptorFile")) && DescribeResult.Contains(TEXT("dependencies")));
		return true;
	}
}

#endif
