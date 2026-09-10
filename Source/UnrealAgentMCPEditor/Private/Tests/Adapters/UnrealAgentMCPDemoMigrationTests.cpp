// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPDemoMigrationTests.cpp
 * @brief Demo 四项入口与十九步编排目录黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Demo/UnrealAgentMCPUnrealDemoAdapter.h"
#include "Dom/JsonObject.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPDemoMigrationIntegrationTest, "WorldData.UnrealAgent.Demo.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPDemoMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPUnrealDemoAdapter::GetImplementedActions();
		TestEqual(TEXT("Demo action 数量"), Actions.Num(), 4);
		FUnrealAgentMCPUnrealDemoAdapter Adapter;
		TSet<FString> Invoked;
		auto Execute = [&Adapter, &Invoked, this](const FString& Action, const TSharedRef<FJsonObject>& Args)
		{
			Invoked.Add(Action);
			Args->SetStringField(TEXT("action"), Action);
			const FString Json = Adapter.Execute(Args);
			TestTrue(*FString::Printf(TEXT("%s 执行成功：%s"), *Action, *Json.Left(500)), Json.Contains(TEXT("\"success\":true")) && Json.Contains(TEXT("\"domain\":\"demo\"")));
			return Json;
		};

		const FString StepsJson = Execute(TEXT("get_steps"), MakeShared<FJsonObject>());
		TestTrue(TEXT("Demo 包含十九步"), StepsJson.Contains(TEXT("\"count\":19")));
		const FString StepFallback = Execute(TEXT("step"), MakeShared<FJsonObject>());
		TestTrue(TEXT("step 未指定索引时返回目录"), StepFallback.Contains(TEXT("\"count\":19")));

		TSharedRef<FJsonObject> Cleanup = MakeShared<FJsonObject>();
		Cleanup->SetBoolField(TEXT("dryRun"), true);
		Cleanup->SetStringField(TEXT("rootPath"), TEXT("/Game/UnrealAgentAutomation/DemoIsolation"));
		Cleanup->SetStringField(TEXT("actorPrefix"), TEXT("MCP_DemoIsolation_"));
		Execute(TEXT("cleanup"), Cleanup);

		TSharedRef<FJsonObject> Home = MakeShared<FJsonObject>();
		Home->SetBoolField(TEXT("dryRun"), true);
		Home->SetStringField(TEXT("homeLevelPath"), TEXT("/Game/UnrealAgentAutomation/MCP_Home_Isolation"));
		Execute(TEXT("go_home"), Home);

		TestEqual(TEXT("所有 Demo action 均已执行"), Invoked.Num(), Actions.Num());
		return true;
	}
}

#endif
