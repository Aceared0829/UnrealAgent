// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPToolExecutionResultTests.cpp
 * @brief 工具执行结果的强类型语义与 fail-closed 契约测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/Execution/UnrealAgentMCPToolExecutionService.h"

namespace UnrealAgentMCP::Tests
{
	using namespace Execution;

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolExecutionResultTest, "WorldData.UnrealAgent.Core.ToolExecution.TypedResult",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPToolExecutionResultTest::RunTest(const FString& Parameters)
	{
		const FMcpToolInvocationResult Unknown = FMcpToolExecutionService::InterpretResult(false, TEXT(""));
		TestEqual(TEXT("未注册工具映射为 UnknownTool"), Unknown.Outcome, EMcpToolInvocationOutcome::UnknownTool);
		TestEqual(TEXT("未注册工具有稳定错误码"), Unknown.Code, FString(TEXT("unknown_tool")));

		const FMcpToolInvocationResult Denied = FMcpToolExecutionService::InterpretResult(true,
			TEXT("{\"success\":false,\"error\":\"approval required\","
				 "\"policyOutcome\":\"Denied\","
				 "\"policyCode\":\"confirmation_required\"}"));
		TestEqual(TEXT("策略拒绝不伪装为执行失败"), Denied.Outcome, EMcpToolInvocationOutcome::Denied);
		TestEqual(TEXT("策略拒绝保留策略码"), Denied.Code, FString(TEXT("confirmation_required")));

		const FMcpToolInvocationResult Failed = FMcpToolExecutionService::InterpretResult(true,
			TEXT("{\"success\":false,\"code\":\"asset_not_found\","
				 "\"error\":\"missing\",\"retryable\":false}"));
		TestEqual(TEXT("领域失败映射为 Failed"), Failed.Outcome, EMcpToolInvocationOutcome::Failed);
		TestEqual(TEXT("领域失败保留错误码"), Failed.Code, FString(TEXT("asset_not_found")));

		const FGuid TaskId = FGuid::NewGuid();
		const FString Receipt = FString::Printf(TEXT("{\"success\":true,\"async\":true,\"task\":{\"taskId\":\"%s\"}}"), *TaskId.ToString(EGuidFormats::DigitsWithHyphens));
		const FMcpToolInvocationResult Accepted = FMcpToolExecutionService::InterpretResult(true, Receipt);
		TestEqual(TEXT("合法异步回执映射为 AcceptedAsync"), Accepted.Outcome, EMcpToolInvocationOutcome::AcceptedAsync);
		TestEqual(TEXT("合法异步回执保留任务 ID"), Accepted.TaskId, TaskId);

		const FMcpToolInvocationResult MissingTask = FMcpToolExecutionService::InterpretResult(true, TEXT("{\"success\":true,\"async\":true}"));
		TestEqual(TEXT("缺少 task 的异步回执 fail-closed"), MissingTask.Outcome, EMcpToolInvocationOutcome::ContractViolation);

		const FMcpToolInvocationResult InvalidJson = FMcpToolExecutionService::InterpretResult(true, TEXT("not-json"));
		TestEqual(TEXT("无效 JSON fail-closed"), InvalidJson.Outcome, EMcpToolInvocationOutcome::ContractViolation);

		const FMcpToolInvocationResult Succeeded = FMcpToolExecutionService::InterpretResult(true, TEXT("{\"success\":true}"));
		TestEqual(TEXT("同步成功映射为 Succeeded"), Succeeded.Outcome, EMcpToolInvocationOutcome::Succeeded);
		return true;
	}
}

#endif
