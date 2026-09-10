// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAcpToolResultPresentationTests.cpp
 * @brief Codex/Cursor ACP MCP 工具结果到面板诊断字段的协议集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Application/ACP/UnrealAgentCodexACPClientRules.h"
#include "Core/Conversation/UnrealAgentMCPConversationModel.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		bool HasHeader(const TSharedRef<FJsonObject>& Server, const FString& ExpectedName, const FString& ExpectedValue)
		{
			const TArray<TSharedPtr<FJsonValue>>* Headers = nullptr;
			if (!Server->TryGetArrayField(TEXT("headers"), Headers) || Headers == nullptr)
			{
				return false;
			}

			for (const TSharedPtr<FJsonValue>& Value : *Headers)
			{
				const TSharedPtr<FJsonObject> Header = Value.IsValid() ? Value->AsObject() : nullptr;
				FString Name;
				FString HeaderValue;
				if (Header.IsValid() && Header->TryGetStringField(TEXT("name"), Name) && Header->TryGetStringField(TEXT("value"), HeaderValue) && Name == ExpectedName &&
					HeaderValue == ExpectedValue)
				{
					return true;
				}
			}
			return false;
		}

		TSharedRef<FJsonObject> MakeToolCallUpdate(const FString& ToolName, const bool bFailed, const FString& TraceId)
		{
			TSharedRef<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetBoolField(TEXT("success"), !bFailed);
			StructuredContent->SetStringField(TEXT("tool"), ToolName);
			if (bFailed)
			{
				StructuredContent->SetStringField(TEXT("code"), TEXT("invalid_arguments"));
				TSharedRef<FJsonObject> MissingSpacing = MakeShared<FJsonObject>();
				MissingSpacing->SetStringField(TEXT("path"), TEXT("$.spacing"));
				TSharedRef<FJsonObject> MissingCountX = MakeShared<FJsonObject>();
				MissingCountX->SetStringField(TEXT("path"), TEXT("$.countX"));
				StructuredContent->SetArrayField(TEXT("schemaErrors"), { MakeShared<FJsonValueObject>(MissingSpacing), MakeShared<FJsonValueObject>(MissingCountX) });
			}
			else
			{
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("sampleCount"), 4);
				StructuredContent->SetObjectField(TEXT("result"), Result);
			}

			TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
			Meta->SetStringField(TEXT("traceId"), TraceId);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("structuredContent"), StructuredContent);
			Result->SetObjectField(TEXT("_meta"), Meta);
			TSharedRef<FJsonObject> RawOutput = MakeShared<FJsonObject>();
			RawOutput->SetObjectField(TEXT("result"), Result);
			TSharedRef<FJsonObject> Update = MakeShared<FJsonObject>();
			Update->SetStringField(TEXT("sessionUpdate"), TEXT("tool_call_update"));
			Update->SetStringField(TEXT("toolCallId"), TEXT("fixture-tool-call"));
			Update->SetStringField(TEXT("status"), bFailed ? TEXT("failed") : TEXT("completed"));
			Update->SetObjectField(TEXT("rawOutput"), RawOutput);
			return Update;
		}

		bool RunProviderFixture(FAutomationTestBase& Test, const FString& ProviderId)
		{
			const TSharedRef<FJsonObject> Server = WorldDataCodexAcpRules::BuildHttpMcpServer(TEXT("world_data_project_test"), TEXT("http://127.0.0.1:25072/mcp"),
				TEXT("X-WorldData-Token"), TEXT("test-token"), TEXT("approval-client"), ProviderId);
			Test.TestTrue(*FString::Printf(TEXT("%s ACP 注入 provider 审计头"), *ProviderId), HasHeader(Server, TEXT("X-WorldData-ACP-Provider"), ProviderId));

			const FString FailureTraceId = TEXT("7ccf2f97-75a1-4cac-a3e3-92686d3952c3");
			FUnrealAgentAcpToolCallUpdate FailureDetails;
			WorldDataCodexAcpRules::ExtractToolCallResultDetails(MakeToolCallUpdate(TEXT("worlddata.landscape.sample_grid"), true, FailureTraceId), FailureDetails);
			Test.TestEqual(*FString::Printf(TEXT("%s ACP Schema 失败保留 canonical action"), *ProviderId), FailureDetails.CanonicalAction,
				FString(TEXT("worlddata.landscape.sample_grid")));
			Test.TestEqual(*FString::Printf(TEXT("%s ACP Schema 失败保留 invalid_arguments"), *ProviderId), FailureDetails.Code, FString(TEXT("invalid_arguments")));
			Test.TestTrue(*FString::Printf(TEXT("%s ACP Schema 失败只保留字段路径"), *ProviderId),
				FailureDetails.SchemaErrorPaths.Contains(TEXT("$.spacing")) && FailureDetails.SchemaErrorPaths.Contains(TEXT("$.countX")));
			Test.TestEqual(*FString::Printf(TEXT("%s ACP Schema 失败保留 traceId"), *ProviderId), FailureDetails.TraceId, FailureTraceId);
			const FString FailureCard = UnrealAgentMCPConversationModel::BuildToolCallDisplayText(TEXT("Landscape sample grid"), EWorldDataConversationToolState::Failed) +
				UnrealAgentMCPConversationModel::BuildToolCallFailureDetails(FailureDetails.CanonicalAction, FailureDetails.Code, FailureDetails.SchemaErrorPaths,
					FailureDetails.TraceId);
			Test.TestTrue(*FString::Printf(TEXT("%s 面板失败卡显示完整结构化诊断"), *ProviderId),
				FailureCard.Contains(TEXT("canonical action")) && FailureCard.Contains(TEXT("invalid_arguments")) && FailureCard.Contains(TEXT("schemaErrors")) &&
					FailureCard.Contains(FailureTraceId));

			FUnrealAgentAcpToolCallUpdate CompletedDetails;
			WorldDataCodexAcpRules::ExtractToolCallResultDetails(MakeToolCallUpdate(TEXT("worlddata.landscape.sample_grid"), false, TEXT("9c58779b-8d60-44b8-b07a-a18ff7af6b07")),
				CompletedDetails);
			Test.TestEqual(*FString::Printf(TEXT("%s ACP 合法 sample_grid 完成"), *ProviderId), CompletedDetails.CanonicalAction, FString(TEXT("worlddata.landscape.sample_grid")));
			Test.TestTrue(*FString::Printf(TEXT("%s ACP 合法 sample_grid 没有 Schema 失败码"), *ProviderId),
				CompletedDetails.Code.IsEmpty() && CompletedDetails.SchemaErrorPaths.IsEmpty());
			return true;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAcpCodexToolResultPresentationTest, "WorldData.UnrealAgent.ACP.Codex.McpToolResultPresentation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAcpCodexToolResultPresentationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		return RunProviderFixture(*this, TEXT("codex"));
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAcpCursorToolResultPresentationTest, "WorldData.UnrealAgent.ACP.Cursor.McpToolResultPresentation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAcpCursorToolResultPresentationTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		return RunProviderFixture(*this, TEXT("cursor"));
	}
}

#endif
