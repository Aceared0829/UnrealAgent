// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPEpicMigrationTests.cpp
 * @brief Epic 元工具到自有反射注册表的真实集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Tooling/Reflection/UnrealAgentMCPReflectedToolsetAdapter.h"
#include "Adapters/Unreal/Epic/UnrealAgentMCPUnrealEpicAdapter.h"
#include "Application/Domains/Epic/UnrealAgentMCPEpicService.h"
#include "Application/Ports/UnrealAgentMCPEpicPort.h"
#include "Core/Reflection/UnrealAgentMCPSystemToolset.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP::Tests
{
	class FVerifyDeferredToolCallCommand final : public IAutomationLatentCommand
	{
	public:
		FVerifyDeferredToolCallCommand(FAutomationTestBase* InTest, FString InCallId) : Test(InTest), CallId(MoveTemp(InCallId)), Deadline(FPlatformTime::Seconds() + 5.0)
		{
		}

		virtual bool Update() override
		{
			TSharedRef<FJsonObject> ReadArguments = MakeShared<FJsonObject>();
			ReadArguments->SetStringField(TEXT("callId"), CallId);
			const FString ReadResult = Toolsets::GetCallResult(ReadArguments);
			if (ReadResult.Contains(TEXT("\"pending\":true")) && FPlatformTime::Seconds() < Deadline)
			{
				return false;
			}

			Test->TestTrue(TEXT("Deferred reflected call completes through the result gateway"),
				ReadResult.Contains(TEXT("\"success\":true")) && ReadResult.Contains(TEXT("\"pending\":false")) && ReadResult.Contains(TEXT("\"state\":\"Succeeded\"")) &&
					ReadResult.Contains(TEXT("\"ReturnValue\":\"pong\"")));

			ReadArguments->SetBoolField(TEXT("consume"), true);
			const FString ConsumedResult = Toolsets::GetCallResult(ReadArguments);
			Test->TestTrue(TEXT("A terminal deferred result can be consumed explicitly"), ConsumedResult.Contains(TEXT("\"state\":\"Succeeded\"")));
			const FString MissingResult = Toolsets::GetCallResult(ReadArguments);
			Test->TestTrue(TEXT("A consumed deferred result is no longer addressable"),
				MissingResult.Contains(TEXT("\"success\":false")) && MissingResult.Contains(TEXT("Unknown or expired")));
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		FString CallId;
		double Deadline = 0.0;
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPEpicMigrationIntegrationTest, "WorldData.UnrealAgent.Epic.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPEpicMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		UUnrealAgentMCPSystemToolset::StaticClass();
		TSharedRef<IUnrealAgentMCPEpicPort> Port = MakeShared<FUnrealAgentMCPUnrealEpicAdapter>();
		FUnrealAgentMCPEpicService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1000)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		const FString StatusResult = Execute(TEXT("status"), MakeShared<FJsonObject>());
		TestTrue(TEXT("状态明确使用 Unreal Agent 自有反射注册表"),
			StatusResult.Contains(TEXT("\"independent\":true")) && StatusResult.Contains(TEXT("WorldDataReflection")) && StatusResult.Contains(TEXT("\"toolsetCount\":1")));

		TSharedRef<FJsonObject> ListArgs = MakeShared<FJsonObject>();
		ListArgs->SetStringField(TEXT("nameFilter"), TEXT("UnrealAgentMCP.System"));
		ListArgs->SetBoolField(TEXT("includeSchemas"), true);
		const FString ListResult = Execute(TEXT("list_toolsets"), ListArgs);
		TestTrue(TEXT("目录能够发现生产系统 Toolset 及其 Schema"),
			ListResult.Contains(TEXT("UnrealAgentMCP.System")) && ListResult.Contains(TEXT("UnrealAgentMCP.System.ping")) && ListResult.Contains(TEXT("inputSchema")));

		TSharedRef<FJsonObject> DescribeArgs = MakeShared<FJsonObject>();
		DescribeArgs->SetStringField(TEXT("toolset"), TEXT("UnrealAgentMCP.System"));
		const FString DescribeResult = Execute(TEXT("describe_toolset"), DescribeArgs);
		TestTrue(TEXT("Toolset 详情包含完整工具描述"), DescribeResult.Contains(TEXT("\"shortName\":\"ping\"")) && DescribeResult.Contains(TEXT("outputSchema")));

		TSharedRef<FJsonObject> CallArgs = MakeShared<FJsonObject>();
		CallArgs->SetStringField(TEXT("toolset"), TEXT("UnrealAgentMCP.System"));
		CallArgs->SetStringField(TEXT("tool"), TEXT("ping"));
		CallArgs->SetObjectField(TEXT("input"), MakeShared<FJsonObject>());
		const FString CallResult = Execute(TEXT("call_tool"), CallArgs);
		TestTrue(TEXT("自有反射工具能够完成进程内调用"), CallResult.Contains(TEXT("\"pending\":false")) && CallResult.Contains(TEXT("\"ReturnValue\":\"pong\"")));

		TSharedRef<FJsonObject> InvalidCallIdArgs = MakeShared<FJsonObject>();
		InvalidCallIdArgs->SetStringField(TEXT("callId"), TEXT("not-a-guid"));
		TestTrue(TEXT("非法异步调用标识在查询前被拒绝"), Toolsets::GetCallResult(InvalidCallIdArgs).Contains(TEXT("callId is not a valid GUID")));

		TSharedRef<FJsonObject> DeferredArgs = MakeShared<FJsonObject>();
		DeferredArgs->SetStringField(TEXT("toolset"), TEXT("UnrealAgentMCP.System"));
		DeferredArgs->SetStringField(TEXT("tool"), TEXT("ping"));
		DeferredArgs->SetObjectField(TEXT("input"), MakeShared<FJsonObject>());
		DeferredArgs->SetBoolField(TEXT("defer"), true);
		const FString DeferredResult = Execute(TEXT("call_tool"), DeferredArgs);
		TSharedPtr<FJsonObject> DeferredResponse;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DeferredResult);
		const bool bResponseParsed = FJsonSerializer::Deserialize(Reader, DeferredResponse);
		TestTrue(TEXT("延迟反射调用返回结构化回执"), bResponseParsed && DeferredResponse.IsValid() && DeferredResponse->GetBoolField(TEXT("pending")));
		FString CallId;
		if (DeferredResponse.IsValid())
		{
			DeferredResponse->TryGetStringField(TEXT("callId"), CallId);
		}
		FGuid ParsedCallId;
		TestTrue(TEXT("延迟反射调用回执包含有效 GUID"), FGuid::Parse(CallId, ParsedCallId) && ParsedCallId.IsValid());
		if (!CallId.IsEmpty())
		{
			ADD_LATENT_AUTOMATION_COMMAND(FVerifyDeferredToolCallCommand(this, MoveTemp(CallId)));
		}
		return true;
	}
}

#endif
