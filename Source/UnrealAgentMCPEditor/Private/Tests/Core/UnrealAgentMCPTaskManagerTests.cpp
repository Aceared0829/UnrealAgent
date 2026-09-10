// Copyright ZhaoZining. All Rights Reserved.

#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Execution/UnrealAgentMCPToolExecutionService.h"
#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FTransactionProbe
	{
		int32 BeginCount = 0;
		int32 FinalizeCount = 0;
		FString ToolId;
		UnrealAgentMCP::EMcpToolTransactionPolicy Policy = UnrealAgentMCP::EMcpToolTransactionPolicy::None;
		bool bInvocationSucceeded = false;
		bool bFinalizeResult = true;
	};

	struct FCompensationProbe
	{
		TArray<int32> ExecutionOrder;
	};

	struct FStepperProbe
	{
		int32 StepCount = 0;
		int32 AbortCount = 0;
		bool bLegacyInvokerRan = false;
	};

	struct FPreparedStepperProbe
	{
		FThreadSafeCounter PrepareCount;
		FThreadSafeCounter StepCount;
		FThreadSafeBool bPrepareRanOnGameThread = false;
		FThreadSafeBool bStepRanOffGameThread = false;
	};

	class FPreparedTaskStepper final : public UnrealAgentMCP::Execution::IMcpTaskStepper
	{
	public:
		explicit FPreparedTaskStepper(TSharedRef<FPreparedStepperProbe, ESPMode::ThreadSafe> InProbe) : Probe(MoveTemp(InProbe))
		{
		}

		virtual bool HasWorkerPreparation() const override
		{
			return true;
		}

		virtual UnrealAgentMCP::Execution::FMcpTaskPrepareResult PrepareOnWorker(UnrealAgentMCP::Execution::FMcpTaskExecutionContext& Context) override
		{
			Probe->PrepareCount.Increment();
			Probe->bPrepareRanOnGameThread.AtomicSet(IsInGameThread());
			FPlatformProcess::SleepNoStats(0.001f);
			return Context.ShouldStop() ? UnrealAgentMCP::Execution::FMcpTaskPrepareResult::Failed(TEXT("prepared task stopped"))
										: UnrealAgentMCP::Execution::FMcpTaskPrepareResult::Succeeded();
		}

		virtual UnrealAgentMCP::Execution::FMcpTaskStepResult Step(UnrealAgentMCP::Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			(void)FrameBudget;
			Probe->bStepRanOffGameThread.AtomicSet(!IsInGameThread());
			if (Context.ShouldStop())
			{
				return UnrealAgentMCP::Execution::FMcpTaskStepResult::Failed(TEXT("prepared task stopped"));
			}
			FPlatformProcess::SleepNoStats(0.001f);
			const int32 CurrentStepCount = Probe->StepCount.Increment();
			return CurrentStepCount < 3 ? UnrealAgentMCP::Execution::FMcpTaskStepResult::Continue()
										: UnrealAgentMCP::Execution::FMcpTaskStepResult::Succeeded(TEXT("{\"success\":true}"));
		}

	private:
		TSharedRef<FPreparedStepperProbe, ESPMode::ThreadSafe> Probe;
	};

	class FBlockingPreparationStepper final : public UnrealAgentMCP::Execution::IMcpTaskStepper
	{
	public:
		FBlockingPreparationStepper(FEvent* InStartedEvent, FEvent* InReleaseEvent, TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> InDestructionCount)
			: StartedEvent(InStartedEvent), ReleaseEvent(InReleaseEvent), DestructionCount(MoveTemp(InDestructionCount))
		{
		}

		virtual ~FBlockingPreparationStepper() override
		{
			DestructionCount->Increment();
		}

		virtual bool HasWorkerPreparation() const override
		{
			return true;
		}

		virtual UnrealAgentMCP::Execution::FMcpTaskPrepareResult PrepareOnWorker(UnrealAgentMCP::Execution::FMcpTaskExecutionContext& Context) override
		{
			StartedEvent->Trigger();
			ReleaseEvent->Wait();
			Context.ShouldStop();
			return UnrealAgentMCP::Execution::FMcpTaskPrepareResult::Failed(TEXT("shutdown preparation released"));
		}

		virtual UnrealAgentMCP::Execution::FMcpTaskStepResult Step(UnrealAgentMCP::Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			(void)Context;
			(void)FrameBudget;
			return UnrealAgentMCP::Execution::FMcpTaskStepResult::Failed(TEXT("blocking preparation must not reach apply"));
		}

	private:
		FEvent* StartedEvent = nullptr;
		FEvent* ReleaseEvent = nullptr;
		TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> DestructionCount;
	};

	class FCountingTaskStepper final : public UnrealAgentMCP::Execution::IMcpTaskStepper
	{
	public:
		FCountingTaskStepper(TSharedRef<FStepperProbe, ESPMode::ThreadSafe> InProbe, const int32 InTargetStepCount) : Probe(MoveTemp(InProbe)), TargetStepCount(InTargetStepCount)
		{
		}

		virtual UnrealAgentMCP::Execution::FMcpTaskStepResult Step(UnrealAgentMCP::Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (FrameBudget <= FTimespan::Zero())
			{
				return UnrealAgentMCP::Execution::FMcpTaskStepResult::Failed(TEXT("invalid frame budget"));
			}
			++LocalStepCount;
			++Probe->StepCount;
			Context.ReportProgress(static_cast<double>(LocalStepCount) / TargetStepCount, TEXT("advancing resumable test task"));
			if (LocalStepCount < TargetStepCount)
			{
				return UnrealAgentMCP::Execution::FMcpTaskStepResult::Continue();
			}
			return UnrealAgentMCP::Execution::FMcpTaskStepResult::Succeeded(TEXT("{\"success\":true,\"stepCount\":3}"));
		}

		virtual UnrealAgentMCP::Execution::FMcpTaskStepResult Abort(UnrealAgentMCP::Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			(void)Context;
			++Probe->AbortCount;
			return UnrealAgentMCP::Execution::FMcpTaskStepResult::Failed(MoveTemp(Reason));
		}

	private:
		TSharedRef<FStepperProbe, ESPMode::ThreadSafe> Probe;
		int32 TargetStepCount = 0;
		int32 LocalStepCount = 0;
	};

	class FTestTransactionScope final : public UnrealAgentMCP::Execution::IMcpToolTransactionScope
	{
	public:
		explicit FTestTransactionScope(TSharedRef<FTransactionProbe, ESPMode::ThreadSafe> InProbe) : Probe(MoveTemp(InProbe))
		{
		}

		virtual bool Finalize(const bool bInvocationSucceeded, FString& OutError) override
		{
			++Probe->FinalizeCount;
			Probe->bInvocationSucceeded = bInvocationSucceeded;
			if (!Probe->bFinalizeResult)
			{
				OutError = TEXT("测试事务收尾失败。");
				return false;
			}
			return true;
		}

	private:
		TSharedRef<FTransactionProbe, ESPMode::ThreadSafe> Probe;
	};

	class FTestTransactionCoordinator final : public UnrealAgentMCP::Execution::IMcpToolTransactionCoordinator
	{
	public:
		explicit FTestTransactionCoordinator(TSharedRef<FTransactionProbe, ESPMode::ThreadSafe> InProbe) : Probe(MoveTemp(InProbe))
		{
		}

		virtual TUniquePtr<UnrealAgentMCP::Execution::IMcpToolTransactionScope> Begin(const FString& ToolId, const UnrealAgentMCP::EMcpToolTransactionPolicy Policy,
			FString& OutError) override
		{
			++Probe->BeginCount;
			Probe->ToolId = ToolId;
			Probe->Policy = Policy;
			OutError.Reset();
			return MakeUnique<FTestTransactionScope>(Probe);
		}

	private:
		TSharedRef<FTransactionProbe, ESPMode::ThreadSafe> Probe;
	};

	bool WaitForTerminalTask(UnrealAgentMCP::Execution::FMcpTaskManager& Manager, const FGuid& TaskId, UnrealAgentMCP::Execution::FMcpTaskSnapshot& OutSnapshot,
		const double TimeoutSeconds = 3.0)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		do
		{
			Manager.Tick();
			if (Manager.TryRead(TaskId, OutSnapshot) && OutSnapshot.IsTerminal())
			{
				return true;
			}
			FPlatformProcess::SleepNoStats(0.005f);
		} while (FPlatformTime::Seconds() < Deadline);
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPreparedGameThreadTaskTest, "WorldData.UnrealAgent.Core.Execution.WorkerPrepareAndBoundedApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPPreparedGameThreadTaskTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	const TSharedRef<FPreparedStepperProbe, ESPMode::ThreadSafe> Probe = MakeShared<FPreparedStepperProbe, ESPMode::ThreadSafe>();
	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	FMcpTaskRequest Request;
	Request.ToolName = TEXT("worlddata.tests.prepared_game_thread_task");
	Request.ThreadPolicy = UnrealAgentMCP::EMcpToolThreadPolicy::StagedGameThread;
	Request.bCancelable = true;
	Request.Timeout = FTimespan::FromSeconds(3);
	Request.StepFrameBudget = FTimespan::FromMilliseconds(0.25);
	Request.Stepper = MakeShared<FPreparedTaskStepper>(Probe);

	FGuid TaskId;
	FString Error;
	if (!TestTrue(TEXT("Prepared task submission succeeds"), Manager.Submit(MoveTemp(Request), TaskId, Error)))
	{
		AddError(Error);
		return false;
	}

	FMcpTaskSnapshot Snapshot;
	const double DeadlineSeconds = FPlatformTime::Seconds() + 3.0;
	do
	{
		FTSTicker::GetCoreTicker().Tick(0.016f);
		Manager.Tick();
		if (Manager.TryRead(TaskId, Snapshot) && Snapshot.IsTerminal())
		{
			break;
		}
		FPlatformProcess::SleepNoStats(0.001f);
	} while (FPlatformTime::Seconds() < DeadlineSeconds);

	TestEqual(TEXT("Worker Prepare runs exactly once"), Probe->PrepareCount.GetValue(), 1);
	TestFalse(TEXT("Worker Prepare never runs on GameThread"), Probe->bPrepareRanOnGameThread);
	TestFalse(TEXT("Apply Step never leaves GameThread"), Probe->bStepRanOffGameThread);
	TestEqual(TEXT("Prepared task performs three apply steps"), Probe->StepCount.GetValue(), 3);
	TestEqual(TEXT("Prepared task completes"), Snapshot.State, EMcpTaskState::Completed);
	TestEqual(TEXT("Terminal snapshot reports completed execution phase"), Snapshot.ExecutionPhase, EMcpTaskExecutionPhase::Completed);
	TestTrue(TEXT("Prepare duration is measured"), Snapshot.PrepareDurationMs > 0.0);
	TestEqual(TEXT("Apply budget is preserved"), Snapshot.ApplyStepBudgetMs, 0.25);
	TestTrue(TEXT("Apply p95 is measured"), Snapshot.ApplyStepP95DurationMs > 0.0);
	TestTrue(TEXT("Apply p99 is measured"), Snapshot.ApplyStepP99DurationMs > 0.0);
	TestTrue(TEXT("Over-budget apply steps are counted"), Snapshot.BudgetOverrunCount >= 1);
	TestTrue(TEXT("Cancellation checkpoints are observable"), Snapshot.CancellationCheckpointCount >= 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPClientScopedCancellationTest, "WorldData.UnrealAgent.Core.Execution.ClientScopedCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPClientScopedCancellationTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	auto SubmitTask = [&Manager](const FString& ClientId, FGuid& OutTaskId)
	{
		FMcpTaskRequest Request;
		Request.ToolName = TEXT("worlddata.tests.client_scoped_cancel");
		Request.ClientId = ClientId;
		Request.ThreadPolicy = UnrealAgentMCP::EMcpToolThreadPolicy::StagedGameThread;
		Request.bCancelable = true;
		Request.Stepper = MakeShared<FCountingTaskStepper>(MakeShared<FStepperProbe, ESPMode::ThreadSafe>(), 3);
		FString Error;
		return Manager.Submit(MoveTemp(Request), OutTaskId, Error);
	};

	FGuid OwnedTaskId;
	FGuid OtherTaskId;
	TestTrue(TEXT("Owned task submits"), SubmitTask(TEXT("client-a"), OwnedTaskId));
	TestTrue(TEXT("Other task submits"), SubmitTask(TEXT("client-b"), OtherTaskId));
	TestEqual(TEXT("Only the matching client's task is cancelled"), Manager.CancelByClientId(TEXT("client-a"), TEXT("client detached")), 1);

	FMcpTaskSnapshot OwnedSnapshot;
	FMcpTaskSnapshot OtherSnapshot;
	TestTrue(TEXT("Owned snapshot remains readable"), Manager.TryRead(OwnedTaskId, OwnedSnapshot));
	TestTrue(TEXT("Other snapshot remains readable"), Manager.TryRead(OtherTaskId, OtherSnapshot));
	TestEqual(TEXT("Owned task is terminally cancelled before apply"), OwnedSnapshot.State, EMcpTaskState::Cancelled);
	TestEqual(TEXT("Unrelated task stays queued"), OtherSnapshot.State, EMcpTaskState::Queued);
	Manager.Shutdown(TEXT("client scoped cancellation test cleanup"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPreparationShutdownDrainTest, "WorldData.UnrealAgent.Core.Execution.PreparationShutdownDrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPPreparationShutdownDrainTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	FEvent* StartedEvent = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* ReleaseEvent = FPlatformProcess::GetSynchEventFromPool(true);
	ON_SCOPE_EXIT
	{
		ReleaseEvent->Trigger();
		FPlatformProcess::ReturnSynchEventToPool(StartedEvent);
		FPlatformProcess::ReturnSynchEventToPool(ReleaseEvent);
	};
	const TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> DestructionCount = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	{
		FMcpTaskManager Manager(FTimespan::FromSeconds(5));
		FMcpTaskRequest Request;
		Request.ToolName = TEXT("worlddata.tests.preparation_shutdown_drain");
		Request.ThreadPolicy = UnrealAgentMCP::EMcpToolThreadPolicy::StagedGameThread;
		Request.bCancelable = true;
		Request.Stepper = MakeShared<FBlockingPreparationStepper>(StartedEvent, ReleaseEvent, DestructionCount);
		FGuid TaskId;
		FString Error;
		TestTrue(TEXT("Blocking preparation task submits"), Manager.Submit(MoveTemp(Request), TaskId, Error));
		TestTrue(TEXT("Worker preparation starts"), StartedEvent->Wait(1000));
		TestFalse(TEXT("Shutdown does not report a blocked preparation as drained"), Manager.ShutdownAndWait(TEXT("preparation shutdown test"), FTimespan::FromMilliseconds(10)));
		ReleaseEvent->Trigger();
		TestTrue(TEXT("Shutdown drains after Worker Prepare exits"), Manager.ShutdownAndWait(TEXT("preparation shutdown test"), FTimespan::FromSeconds(1)));
	}
	TestEqual(TEXT("Prepared stepper is released without a SharedState reference cycle"), DestructionCount->GetValue(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPToolExecutionServiceTest, "WorldData.UnrealAgent.Core.Execution.UnifiedService",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPToolExecutionServiceTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;

	FMcpToolRuntimeRegistry Registry;
	FMcpToolDescriptor Descriptor;
	Descriptor.Provider = TEXT("Automation");
	Descriptor.Toolset = TEXT("Tests.Execution");
	Descriptor.Name = TEXT("execution_echo");
	Descriptor.QualifiedName = TEXT("Tests.Execution.execution_echo");
	Descriptor.Description = TEXT("统一执行服务测试工具。");
	Descriptor.InputSchema = MakeShared<FJsonObject>();
	Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> ValueSchema = MakeShared<FJsonObject>();
	ValueSchema->SetStringField(TEXT("type"), TEXT("string"));
	Properties->SetObjectField(TEXT("value"), ValueSchema);
	Descriptor.InputSchema->SetObjectField(TEXT("properties"), Properties);
	TArray<TSharedPtr<FJsonValue>> RequiredValues;
	RequiredValues.Add(MakeShared<FJsonValueString>(TEXT("value")));
	Descriptor.InputSchema->SetArrayField(TEXT("required"), RequiredValues);
	Descriptor.OutputSchema = MakeShared<FJsonObject>();
	Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
	Descriptor.Risk = EMcpToolRisk::ReadOnly;
	Descriptor.bReadOnly = true;
	const TSharedPtr<FJsonObject> EchoInputSchema = Descriptor.InputSchema;
	const TSharedPtr<FJsonObject> EchoOutputSchema = Descriptor.OutputSchema;
	Descriptor.InputSchema = MakeShared<FJsonObject>();
	Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
	Descriptor.ActionContractResolver = [EchoInputSchema, EchoOutputSchema](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
	{
		OutContract.CanonicalToolId = TEXT("worlddata.tests.execution_echo");
		OutContract.InputSchema = EchoInputSchema;
		OutContract.OutputSchema = EchoOutputSchema;
		OutContract.Risk = EMcpToolRisk::ReadOnly;
		OutContract.TransactionPolicy = EMcpToolTransactionPolicy::ReadOnly;
		return true;
	};
	Descriptor.Invoker = BindInvocationContext(
		[](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext& Context)
		{
			Context.ReportProgress(0.5, TEXT("执行测试工具"));
			return FString::Printf(TEXT("{\"success\":true,\"value\":\"%s\","
										"\"requestId\":\"%s\",\"protocolRequestId\":\"%s\","
										"\"clientId\":\"%s\",\"sessionId\":\"%s\","
										"\"source\":\"%s\"}"),
				*Arguments->GetStringField(TEXT("value")), *Context.GetRequestId().ToString(EGuidFormats::DigitsWithHyphensLower), *Context.GetProtocolRequestId(),
				*Context.GetClientId(), *Context.GetSessionId(), *Context.GetSource());
		});

	FString Error;
	TestTrue(TEXT("测试描述符注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
	FMcpToolExecutionService Service(Registry);

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("value"), TEXT("城墙"));
	FString ResultJson;
	TestTrue(TEXT("统一执行服务找到工具"), Service.Execute(TEXT("execution_echo"), Arguments, ResultJson));
	TestTrue(TEXT("同步工具返回原始结果"), ResultJson.Contains(TEXT("\"value\":\"城墙\"")));

	FMcpToolDescriptor InvalidOutputDescriptor;
	InvalidOutputDescriptor.Provider = TEXT("Automation");
	InvalidOutputDescriptor.Toolset = TEXT("Tests.Execution");
	InvalidOutputDescriptor.Name = TEXT("execution_invalid_output");
	InvalidOutputDescriptor.QualifiedName = TEXT("Tests.Execution.execution_invalid_output");
	InvalidOutputDescriptor.Description = TEXT("输出契约失败测试工具。");
	InvalidOutputDescriptor.InputSchema = MakeShared<FJsonObject>();
	InvalidOutputDescriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	InvalidOutputDescriptor.OutputSchema = MakeShared<FJsonObject>();
	InvalidOutputDescriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
	TSharedRef<FJsonObject> InvalidOutputProperties = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> ExpectedStringSchema = MakeShared<FJsonObject>();
	ExpectedStringSchema->SetStringField(TEXT("type"), TEXT("string"));
	InvalidOutputProperties->SetObjectField(TEXT("value"), ExpectedStringSchema);
	InvalidOutputDescriptor.OutputSchema->SetObjectField(TEXT("properties"), InvalidOutputProperties);
	TArray<TSharedPtr<FJsonValue>> InvalidOutputRequired;
	InvalidOutputRequired.Add(MakeShared<FJsonValueString>(TEXT("value")));
	InvalidOutputDescriptor.OutputSchema->SetArrayField(TEXT("required"), InvalidOutputRequired);
	InvalidOutputDescriptor.Risk = EMcpToolRisk::ReadOnly;
	InvalidOutputDescriptor.bReadOnly = true;
	InvalidOutputDescriptor.Invoker = [](const TSharedPtr<FJsonObject>&)
	{
		return FString(TEXT("{\"success\":true,\"value\":7}"));
	};
	TestTrue(TEXT("输出契约失败描述符注册成功"), Registry.RegisterDescriptor(MoveTemp(InvalidOutputDescriptor), Error));
	TestTrue(TEXT("输出契约失败仍返回结构化结果"), Service.Execute(TEXT("execution_invalid_output"), MakeShared<FJsonObject>(), ResultJson));
	TestTrue(TEXT("成功载荷违反输出 Schema 时 fail-closed"), ResultJson.Contains(TEXT("\"success\":false")) && ResultJson.Contains(TEXT("output_schema_invalid")));

	const FGuid ContextRequestId = FGuid::NewGuid();
	FMcpToolExecutionOptions ContextOptions;
	ContextOptions.RequestId = ContextRequestId;
	ContextOptions.ProtocolRequestId = TEXT("jsonrpc:number:42");
	ContextOptions.PolicyContext.ClientId = TEXT("AutomationClient");
	ContextOptions.PolicyContext.SessionId = TEXT("AutomationSession");
	ContextOptions.PolicyContext.Source = TEXT("AutomationTransport");
	ContextOptions.PolicyContext.Provider = TEXT("codex");
	TestTrue(TEXT("统一服务接受显式调用身份"), Service.Execute(TEXT("execution_echo"), Arguments, ResultJson, ContextOptions));
	TestTrue(TEXT("处理器收到内部请求标识"), ResultJson.Contains(ContextRequestId.ToString(EGuidFormats::DigitsWithHyphensLower)));
	TestTrue(TEXT("处理器收到协议请求标识"), ResultJson.Contains(TEXT("jsonrpc:number:42")));
	TestTrue(TEXT("处理器收到客户端、会话与来源"),
		ResultJson.Contains(TEXT("AutomationClient")) && ResultJson.Contains(TEXT("AutomationSession")) && ResultJson.Contains(TEXT("AutomationTransport")));
	const TArray<Policy::FMcpAuditRecord> ContextAudit = Service.ListAudit();
	TestEqual(TEXT("审计与工具上下文共享请求标识"), ContextAudit.Last().RequestId, ContextRequestId);
	TestEqual(TEXT("审计与工具上下文共享会话标识"), ContextAudit.Last().SessionId, FString(TEXT("AutomationSession")));

	TSharedPtr<FJsonObject> InvalidArguments = MakeShared<FJsonObject>();
	TestTrue(TEXT("参数校验错误仍由执行服务处理"), Service.Execute(TEXT("execution_echo"), InvalidArguments, ResultJson, ContextOptions));
	TestTrue(TEXT("无效参数被 Schema 拒绝"), ResultJson.Contains(TEXT("\"success\":false")));
	const Policy::FMcpAuditRecord SchemaFailureAudit = Service.ListAudit().Last();
	TestTrue(TEXT("Schema 审计记录 codex provider 与字段路径，不记录参数值"),
		SchemaFailureAudit.Provider == TEXT("codex") && SchemaFailureAudit.DecisionCode == TEXT("input_schema_invalid") &&
			SchemaFailureAudit.SchemaErrorPaths.Contains(TEXT("$.value")));

	FMcpToolExecutionOptions UnauthenticatedOptions;
	UnauthenticatedOptions.PolicyContext.bAuthenticated = false;
	TestTrue(TEXT("未认证调用仍由统一服务返回策略结果"), Service.Execute(TEXT("execution_echo"), Arguments, ResultJson, UnauthenticatedOptions));
	TestTrue(TEXT("统一执行服务不能绕过认证策略"), ResultJson.Contains(TEXT("authentication_required")));
	TestTrue(TEXT("策略决策已写入脱敏审计"), !Service.ListAudit().IsEmpty());
	TestEqual(TEXT("审计使用 Action 的 Canonical Tool ID"), Service.ListAudit().Last().ToolName, FString(TEXT("worlddata.tests.execution_echo")));

	FMcpToolDescriptor AsyncDescriptor;
	AsyncDescriptor.Provider = TEXT("Automation");
	AsyncDescriptor.Toolset = TEXT("Tests.Execution");
	AsyncDescriptor.Name = TEXT("execution_task");
	AsyncDescriptor.QualifiedName = TEXT("Tests.Execution.execution_task");
	AsyncDescriptor.Description = TEXT("统一异步任务测试工具。");
	AsyncDescriptor.InputSchema = MakeShared<FJsonObject>();
	AsyncDescriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	AsyncDescriptor.OutputSchema = MakeShared<FJsonObject>();
	AsyncDescriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
	AsyncDescriptor.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
	AsyncDescriptor.ExecutionMode = EMcpToolExecutionMode::Task;
	AsyncDescriptor.bCancelable = true;
	const TSharedPtr<FJsonObject> AsyncInputSchema = AsyncDescriptor.InputSchema;
	const TSharedPtr<FJsonObject> AsyncOutputSchema = AsyncDescriptor.OutputSchema;
	AsyncDescriptor.ActionContractResolver = [AsyncInputSchema, AsyncOutputSchema](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
	{
		OutContract.CanonicalToolId = TEXT("worlddata.tests.execution_task");
		OutContract.InputSchema = AsyncInputSchema;
		OutContract.OutputSchema = AsyncOutputSchema;
		OutContract.ExecutionMode = EMcpToolExecutionMode::Task;
		OutContract.bCancelable = true;
		return true;
	};
	AsyncDescriptor.Invoker = [](const TSharedPtr<FJsonObject>&)
	{
		return FString(TEXT("{\"success\":true,\"taskValue\":7}"));
	};
	TestTrue(TEXT("异步描述符注册成功"), Registry.RegisterDescriptor(MoveTemp(AsyncDescriptor), Error));
	TestTrue(TEXT("异步工具进入统一执行服务"), Service.Execute(TEXT("execution_task"), MakeShared<FJsonObject>(), ResultJson));

	TSharedPtr<FJsonObject> Receipt;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
	TestTrue(TEXT("任务回执为有效 JSON"), FJsonSerializer::Deserialize(Reader, Receipt) && Receipt.IsValid());
	const TSharedPtr<FJsonObject>* TaskObject = nullptr;
	TestTrue(TEXT("任务回执包含结构化任务"), Receipt.IsValid() && Receipt->TryGetObjectField(TEXT("task"), TaskObject));
	if (!TaskObject || !TaskObject->IsValid())
	{
		return false;
	}
	TestEqual(TEXT("任务回执使用 Action 的 Canonical Tool ID"), (*TaskObject)->GetStringField(TEXT("tool")), FString(TEXT("worlddata.tests.execution_task")));
	FGuid TaskId;
	TestTrue(TEXT("任务标识可解析"), FGuid::Parse((*TaskObject)->GetStringField(TEXT("taskId")), TaskId));
	FMcpTaskSnapshot Snapshot;
	TestTrue(TEXT("异步工具在统一状态机中完成"),
		[&Service, &TaskId, &Snapshot]()
		{
			const double Deadline = FPlatformTime::Seconds() + 2.0;
			do
			{
				Service.Tick();
				if (Service.TryReadTask(TaskId, Snapshot) && Snapshot.IsTerminal())
				{
					return true;
				}
				FPlatformProcess::SleepNoStats(0.005f);
			} while (FPlatformTime::Seconds() < Deadline);
			return false;
		}());
	TestEqual(TEXT("异步工具最终成功"), Snapshot.State, EMcpTaskState::Completed);

	TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> bGameThreadTaskRan = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	FMcpToolDescriptor GameThreadTaskDescriptor;
	GameThreadTaskDescriptor.Provider = TEXT("Automation");
	GameThreadTaskDescriptor.Toolset = TEXT("Tests.Execution");
	GameThreadTaskDescriptor.Name = TEXT("execution_game_thread_task");
	GameThreadTaskDescriptor.QualifiedName = TEXT("Tests.Execution.execution_game_thread_task");
	GameThreadTaskDescriptor.Description = TEXT("Verifies deferred game-thread task execution.");
	GameThreadTaskDescriptor.InputSchema = MakeShared<FJsonObject>();
	GameThreadTaskDescriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	GameThreadTaskDescriptor.OutputSchema = MakeShared<FJsonObject>();
	GameThreadTaskDescriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
	GameThreadTaskDescriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
	GameThreadTaskDescriptor.ExecutionMode = EMcpToolExecutionMode::Task;
	GameThreadTaskDescriptor.bCancelable = true;
	GameThreadTaskDescriptor.bRequiresResumableTask = true;
	const TSharedPtr<FJsonObject> GameThreadTaskInputSchema = GameThreadTaskDescriptor.InputSchema;
	const TSharedPtr<FJsonObject> GameThreadTaskOutputSchema = GameThreadTaskDescriptor.OutputSchema;
	GameThreadTaskDescriptor.ActionContractResolver = [GameThreadTaskInputSchema, GameThreadTaskOutputSchema](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
	{
		OutContract.CanonicalToolId = TEXT("worlddata.tests.execution_game_thread_task");
		OutContract.InputSchema = GameThreadTaskInputSchema;
		OutContract.OutputSchema = GameThreadTaskOutputSchema;
		OutContract.ExecutionMode = EMcpToolExecutionMode::Task;
		OutContract.bCancelable = true;
		return true;
	};
	GameThreadTaskDescriptor.Invoker = [bGameThreadTaskRan](const TSharedPtr<FJsonObject>&)
	{
		bGameThreadTaskRan->AtomicSet(true);
		return FString(TEXT("{\"success\":true}"));
	};
	TestFalse(TEXT("Monolithic GameThread task descriptor is rejected"), Registry.RegisterDescriptor(MoveTemp(GameThreadTaskDescriptor), Error));
	TestTrue(TEXT("Registration error requires a resumable task factory"), Error.Contains(TEXT("resumable task factory")));
	TestFalse(TEXT("Rejected GameThread task never invokes tool work"), static_cast<bool>(*bGameThreadTaskRan));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPResumableGameThreadTaskTest, "WorldData.UnrealAgent.Core.Execution.ResumableGameThreadTask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPResumableGameThreadTaskTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	const TSharedRef<FStepperProbe, ESPMode::ThreadSafe> StepperProbe = MakeShared<FStepperProbe, ESPMode::ThreadSafe>();
	const TSharedRef<FTransactionProbe, ESPMode::ThreadSafe> TransactionProbe = MakeShared<FTransactionProbe, ESPMode::ThreadSafe>();
	const TSharedRef<FTestTransactionCoordinator, ESPMode::ThreadSafe> Coordinator = MakeShared<FTestTransactionCoordinator, ESPMode::ThreadSafe>(TransactionProbe);

	FMcpToolDescriptor Descriptor;
	Descriptor.Provider = TEXT("Automation");
	Descriptor.Toolset = TEXT("Tests.Execution");
	Descriptor.Name = TEXT("execution_resumable_task");
	Descriptor.QualifiedName = TEXT("worlddata.tests.execution_resumable_task");
	Descriptor.Description = TEXT("Verifies resumable game-thread execution.");
	Descriptor.InputSchema = MakeShared<FJsonObject>();
	Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
	Descriptor.OutputSchema = MakeShared<FJsonObject>();
	Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.Risk = EMcpToolRisk::EditorState;
	Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
	Descriptor.ExecutionMode = EMcpToolExecutionMode::Task;
	Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::ScopedTransaction;
	Descriptor.bCancelable = true;
	Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
	{
		OutContract.CanonicalToolId = TEXT("worlddata.tests.execution_resumable_task");
		OutContract.Risk = EMcpToolRisk::EditorState;
		OutContract.ExecutionMode = EMcpToolExecutionMode::Task;
		OutContract.TransactionPolicy = EMcpToolTransactionPolicy::ScopedTransaction;
		OutContract.bCancelable = true;
		return true;
	};
	Descriptor.StagedTaskFactory = [StepperProbe](const TSharedPtr<FJsonObject>&)
	{
		return MakeShared<FCountingTaskStepper>(StepperProbe, 3);
	};
	Descriptor.Invoker = [StepperProbe](const TSharedPtr<FJsonObject>&)
	{
		StepperProbe->bLegacyInvokerRan = true;
		return FString(TEXT("{\"success\":false,\"error\":\"legacy invoker ran\"}"));
	};

	FMcpToolRuntimeRegistry Registry;
	FString Error;
	TestTrue(TEXT("Resumable task descriptor registers"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
	FMcpToolExecutionService Service(Registry, FTimespan::FromMinutes(1), Policy::FMcpServerPolicy(), Coordinator);

	auto ExecuteTask = [this, &Service](FGuid& OutTaskId)
	{
		FString ResultJson;
		if (!TestTrue(TEXT("Resumable task enters the execution service"), Service.Execute(TEXT("execution_resumable_task"), MakeShared<FJsonObject>(), ResultJson)))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Receipt;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
		const TSharedPtr<FJsonObject>* TaskObject = nullptr;
		return TestTrue(TEXT("Resumable task receipt is valid JSON"), FJsonSerializer::Deserialize(Reader, Receipt) && Receipt.IsValid()) &&
			TestTrue(TEXT("Resumable task receipt contains task state"), Receipt->TryGetObjectField(TEXT("task"), TaskObject) && TaskObject && TaskObject->IsValid()) &&
			TestTrue(TEXT("Resumable task identifier parses"), FGuid::Parse((*TaskObject)->GetStringField(TEXT("taskId")), OutTaskId));
	};

	FGuid TaskId;
	if (!ExecuteTask(TaskId))
	{
		return false;
	}
	TestEqual(TEXT("Task receipt precedes all step work"), StepperProbe->StepCount, 0);
	TestEqual(TEXT("Transaction has not started at receipt time"), TransactionProbe->BeginCount, 0);
	TestFalse(TEXT("Legacy invoker is bypassed"), StepperProbe->bLegacyInvokerRan);

	FMcpTaskSnapshot Snapshot;
	FTSTicker::GetCoreTicker().Tick(0.016f);
	Service.TryReadTask(TaskId, Snapshot);
	TestEqual(TEXT("First frame advances exactly one step"), StepperProbe->StepCount, 1);
	TestTrue(TEXT("Task snapshot reports resumable execution"), Snapshot.bResumable);
	TestEqual(TEXT("Task snapshot counts the first step"), Snapshot.StepCount, static_cast<int64>(1));
	TestTrue(TEXT("Task snapshot records step duration"), Snapshot.LastStepDurationMs >= 0.0);
	TestFalse(TEXT("Task remains resumable after first frame"), Snapshot.IsTerminal());
	TestEqual(TEXT("Transaction starts once"), TransactionProbe->BeginCount, 1);
	TestEqual(TEXT("Transaction stays open between frames"), TransactionProbe->FinalizeCount, 0);

	FTSTicker::GetCoreTicker().Tick(0.016f);
	Service.TryReadTask(TaskId, Snapshot);
	TestEqual(TEXT("Second frame advances exactly one step"), StepperProbe->StepCount, 2);
	TestFalse(TEXT("Task remains active after second frame"), Snapshot.IsTerminal());

	FTSTicker::GetCoreTicker().Tick(0.016f);
	Service.TryReadTask(TaskId, Snapshot);
	TestEqual(TEXT("Third frame completes the final step"), StepperProbe->StepCount, 3);
	TestEqual(TEXT("Task snapshot counts all steps"), Snapshot.StepCount, static_cast<int64>(3));
	TestEqual(TEXT("Resumable task completes"), Snapshot.State, EMcpTaskState::Completed);
	TestTrue(TEXT("Stepper result is preserved"), Snapshot.ValueJson.Contains(TEXT("\"stepCount\":3")));
	TestEqual(TEXT("Transaction finalizes once"), TransactionProbe->FinalizeCount, 1);
	TestTrue(TEXT("Successful task commits transaction"), TransactionProbe->bInvocationSucceeded);
	TestNull(TEXT("Invocation context is cleared between frames"), FMcpTaskExecutionContext::GetCurrent());

	FGuid CancelledTaskId;
	if (!ExecuteTask(CancelledTaskId))
	{
		return false;
	}
	FTSTicker::GetCoreTicker().Tick(0.016f);
	TestEqual(TEXT("Second task advances before cancellation"), StepperProbe->StepCount, 4);
	TestEqual(TEXT("Running resumable task accepts cancellation"), Service.CancelTask(CancelledTaskId, TEXT("cancel resumable test")), EMcpTaskCancelResult::CancellationRequested);
	FTSTicker::GetCoreTicker().Tick(0.016f);
	Service.TryReadTask(CancelledTaskId, Snapshot);
	TestEqual(TEXT("Cancellation invokes stepper abort"), StepperProbe->AbortCount, 1);
	TestEqual(TEXT("Cancelled stepper reaches terminal state"), Snapshot.State, EMcpTaskState::Cancelled);
	TestEqual(TEXT("Cancelled transaction finalizes once"), TransactionProbe->FinalizeCount, 2);
	TestFalse(TEXT("Cancelled transaction rolls back"), TransactionProbe->bInvocationSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPInvocationContextTest, "WorldData.UnrealAgent.Core.Execution.InvocationContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPInvocationContextTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	TestNull(TEXT("工具调用外没有泄漏上下文"), FMcpTaskExecutionContext::GetCurrent());

	const FGuid RequestId = FGuid::NewGuid();
	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	FMcpTaskRequest Request;
	Request.ToolName = TEXT("worlddata.tests.invocation_context");
	Request.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
	Request.Timeout = FTimespan::FromSeconds(2);
	Request.RequestId = RequestId;
	Request.ProtocolRequestId = TEXT("jsonrpc:string:context-test");
	Request.ClientId = TEXT("AutomationClient");
	Request.SessionId = TEXT("AutomationSession");
	Request.Source = TEXT("Automation");
	Request.Work = [RequestId](FMcpTaskExecutionContext& Context)
	{
		if (FMcpTaskExecutionContext::GetCurrent() != &Context)
		{
			return FMcpTaskWorkResult::Failed(TEXT("当前调用上下文没有绑定到工作线程。"));
		}
		if (Context.GetRequestId() != RequestId || Context.GetProtocolRequestId() != TEXT("jsonrpc:string:context-test") || Context.GetClientId() != TEXT("AutomationClient") ||
			Context.GetSessionId() != TEXT("AutomationSession") || Context.GetSource() != TEXT("Automation"))
		{
			return FMcpTaskWorkResult::Failed(TEXT("调用身份没有完整传递。"));
		}
		const TOptional<FTimespan> RemainingTime = Context.GetRemainingTime();
		if (!Context.GetTaskId().IsValid() || !Context.HasDeadline() || !RemainingTime.IsSet() || RemainingTime.GetValue() <= FTimespan::Zero() || Context.ShouldStop())
		{
			return FMcpTaskWorkResult::Failed(TEXT("任务标识或截止时间无效。"));
		}
		Context.ReportProgress(0.5, TEXT("上下文已验证"));
		return FMcpTaskWorkResult::Succeeded(Context.GetTaskId().ToString(EGuidFormats::DigitsWithHyphensLower));
	};

	FGuid TaskId;
	FString Error;
	TestTrue(TEXT("携带调用上下文的任务提交成功"), Manager.Submit(MoveTemp(Request), TaskId, Error));
	FMcpTaskSnapshot Snapshot;
	TestTrue(TEXT("调用上下文任务正常结束"), WaitForTerminalTask(Manager, TaskId, Snapshot));
	TestEqual(TEXT("调用上下文校验通过"), Snapshot.State, EMcpTaskState::Completed);
	TestEqual(TEXT("上下文中的任务标识与任务回执一致"), Snapshot.ValueJson, TaskId.ToString(EGuidFormats::DigitsWithHyphensLower));
	TestEqual(TEXT("进度消息由上下文进入任务快照"), Snapshot.Progress.Message, FString(TEXT("上下文已验证")));
	TestNull(TEXT("任务结束后清除线程调用上下文"), FMcpTaskExecutionContext::GetCurrent());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTransactionOrchestrationTest, "WorldData.UnrealAgent.Core.Execution.TransactionOrchestration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPTransactionOrchestrationTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	FMcpToolRuntimeRegistry Registry;
	FMcpToolDescriptor Descriptor;
	Descriptor.Provider = TEXT("Automation");
	Descriptor.Toolset = TEXT("Tests.Execution");
	Descriptor.Name = TEXT("execution_transaction");
	Descriptor.QualifiedName = TEXT("worlddata.tests.execution_transaction");
	Descriptor.Description = TEXT("事务编排测试工具。");
	Descriptor.InputSchema = MakeShared<FJsonObject>();
	Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.OutputSchema = MakeShared<FJsonObject>();
	Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.Risk = EMcpToolRisk::EditorState;
	Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
	Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::Atomic;
	Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
	{
		OutContract.CanonicalToolId = TEXT("worlddata.tests.execution_transaction");
		OutContract.Risk = EMcpToolRisk::EditorState;
		OutContract.TransactionPolicy = EMcpToolTransactionPolicy::Atomic;
		return true;
	};
	Descriptor.Invoker = [](const TSharedPtr<FJsonObject>&)
	{
		return FString(TEXT("{\"success\":false,\"error\":\"synthetic failure\"}"));
	};

	FString Error;
	TestTrue(TEXT("事务测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
	const TSharedRef<FTransactionProbe, ESPMode::ThreadSafe> Probe = MakeShared<FTransactionProbe, ESPMode::ThreadSafe>();
	const TSharedRef<FTestTransactionCoordinator, ESPMode::ThreadSafe> Coordinator = MakeShared<FTestTransactionCoordinator, ESPMode::ThreadSafe>(Probe);
	FMcpToolExecutionService Service(Registry, FTimespan::FromMinutes(1), Policy::FMcpServerPolicy(), Coordinator);

	FString ResultJson;
	TestTrue(TEXT("事务测试工具进入统一执行服务"), Service.Execute(TEXT("execution_transaction"), MakeShared<FJsonObject>(), ResultJson));
	TestEqual(TEXT("事务只开始一次"), Probe->BeginCount, 1);
	TestEqual(TEXT("事务只收尾一次"), Probe->FinalizeCount, 1);
	TestEqual(TEXT("事务使用工具语义标识"), Probe->ToolId, FString(TEXT("worlddata.tests.execution_transaction")));
	TestEqual(TEXT("事务使用解析后的策略"), Probe->Policy, EMcpToolTransactionPolicy::Atomic);
	TestFalse(TEXT("工具失败状态传入事务收尾"), Probe->bInvocationSucceeded);
	TestTrue(TEXT("工具原始失败结果保持可见"), ResultJson.Contains(TEXT("synthetic failure")));

	Probe->bFinalizeResult = false;
	TestTrue(TEXT("事务收尾失败仍由执行服务返回"), Service.Execute(TEXT("execution_transaction"), MakeShared<FJsonObject>(), ResultJson));
	TestTrue(TEXT("事务收尾错误覆盖不可信的工具结果"), ResultJson.Contains(TEXT("测试事务收尾失败")));
	TestEqual(TEXT("第二次调用独立开始事务"), Probe->BeginCount, 2);
	TestEqual(TEXT("第二次调用独立收尾事务"), Probe->FinalizeCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPCompensatingTransactionTest, "WorldData.UnrealAgent.Core.Execution.CompensatingTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPCompensatingTransactionTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	const TSharedRef<FCompensationProbe, ESPMode::ThreadSafe> Probe = MakeShared<FCompensationProbe, ESPMode::ThreadSafe>();
	FMcpToolRuntimeRegistry Registry;
	FMcpToolDescriptor Descriptor;
	Descriptor.Provider = TEXT("Automation");
	Descriptor.Toolset = TEXT("Tests.Execution");
	Descriptor.Name = TEXT("execution_compensating");
	Descriptor.QualifiedName = TEXT("worlddata.tests.execution_compensating");
	Descriptor.Description = TEXT("补偿事务编排测试工具。");
	Descriptor.InputSchema = MakeShared<FJsonObject>();
	Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
	Descriptor.OutputSchema = MakeShared<FJsonObject>();
	Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
	Descriptor.Risk = EMcpToolRisk::FileMutation;
	Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
	Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
	Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
	{
		OutContract.CanonicalToolId = TEXT("worlddata.tests.execution_compensating");
		OutContract.Risk = EMcpToolRisk::FileMutation;
		OutContract.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
		return true;
	};
	Descriptor.Invoker = BindInvocationContext(
		[Probe](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext& Context)
		{
			bool bInvocationSucceeds = false;
			bool bCompensationFails = false;
			Arguments->TryGetBoolField(TEXT("invocationSucceeds"), bInvocationSucceeds);
			Arguments->TryGetBoolField(TEXT("compensationFails"), bCompensationFails);
			for (int32 Step = 1; Step <= 3; ++Step)
			{
				FString RegisterError;
				if (!Context.RegisterCompensation(
						FString::Printf(TEXT("补偿步骤 %d"), Step),
						[Probe, Step, bCompensationFails](FString& OutError)
						{
							Probe->ExecutionOrder.Add(Step);
							if (bCompensationFails && Step == 2)
							{
								OutError = TEXT("合成补偿失败");
								return false;
							}
							return true;
						},
						RegisterError))
				{
					return FString::Printf(TEXT("{\"success\":false,\"error\":\"%s\"}"), *RegisterError);
				}
			}
			return bInvocationSucceeds ? FString(TEXT("{\"success\":true}")) : FString(TEXT("{\"success\":false,\"error\":\"synthetic failure\"}"));
		});

	FString Error;
	TestTrue(TEXT("补偿事务测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
	FMcpToolExecutionService Service(Registry);
	FString ResultJson;
	TestTrue(TEXT("失败调用进入补偿事务"), Service.Execute(TEXT("execution_compensating"), MakeShared<FJsonObject>(), ResultJson));
	TestTrue(TEXT("补偿成功时保留工具原始失败"), ResultJson.Contains(TEXT("synthetic failure")));
	TestEqual(TEXT("三个补偿动作都已执行"), Probe->ExecutionOrder.Num(), 3);
	if (Probe->ExecutionOrder.Num() == 3)
	{
		TestEqual(TEXT("最后登记的补偿最先执行"), Probe->ExecutionOrder[0], 3);
		TestEqual(TEXT("补偿保持逆序执行"), Probe->ExecutionOrder[1], 2);
		TestEqual(TEXT("最先登记的补偿最后执行"), Probe->ExecutionOrder[2], 1);
	}

	Probe->ExecutionOrder.Reset();
	TSharedPtr<FJsonObject> SuccessArguments = MakeShared<FJsonObject>();
	SuccessArguments->SetBoolField(TEXT("invocationSucceeds"), true);
	TestTrue(TEXT("成功调用完成补偿事务"), Service.Execute(TEXT("execution_compensating"), SuccessArguments, ResultJson));
	TestTrue(TEXT("成功调用保留成功结果"), ResultJson.Contains(TEXT("\"success\": true")) || ResultJson.Contains(TEXT("\"success\":true")));
	TestTrue(TEXT("成功调用不会执行补偿"), Probe->ExecutionOrder.IsEmpty());

	TSharedPtr<FJsonObject> FailedCompensationArguments = MakeShared<FJsonObject>();
	FailedCompensationArguments->SetBoolField(TEXT("compensationFails"), true);
	TestTrue(TEXT("补偿失败仍由统一执行服务返回"), Service.Execute(TEXT("execution_compensating"), FailedCompensationArguments, ResultJson));
	TestTrue(TEXT("补偿失败覆盖不可信的工具结果"), ResultJson.Contains(TEXT("补偿事务有 1 个动作失败")) && ResultJson.Contains(TEXT("补偿步骤 2")));

	Probe->ExecutionOrder.Reset();
	FMcpToolExecutionOptions TaskOptions;
	TaskOptions.bForceTask = true;
	TestTrue(TEXT("补偿指标调用返回 Task 回执"), Service.Execute(TEXT("execution_compensating"), FailedCompensationArguments, ResultJson, TaskOptions));
	TSharedPtr<FJsonObject> Receipt;
	const TSharedRef<TJsonReader<>> ReceiptReader = TJsonReaderFactory<>::Create(ResultJson);
	const TSharedPtr<FJsonObject>* TaskObject = nullptr;
	FGuid TaskId;
	TestTrue(TEXT("补偿指标 Task 回执可解析"),
		FJsonSerializer::Deserialize(ReceiptReader, Receipt) && Receipt.IsValid() && Receipt->TryGetObjectField(TEXT("task"), TaskObject) && TaskObject != nullptr &&
			FGuid::Parse((*TaskObject)->GetStringField(TEXT("taskId")), TaskId));
	FTSTicker::GetCoreTicker().Tick(0.016f);
	FMcpTaskSnapshot CompensationSnapshot;
	TestTrue(TEXT("补偿指标 Task 终态可读取"), Service.TryReadTask(TaskId, CompensationSnapshot) && CompensationSnapshot.IsTerminal());
	TestEqual(TEXT("补偿注册数进入任务指标"), CompensationSnapshot.CompensationRegisteredCount, static_cast<int64>(3));
	TestEqual(TEXT("补偿执行数进入任务指标"), CompensationSnapshot.CompensationExecutedCount, static_cast<int64>(3));
	TestEqual(TEXT("补偿失败数进入任务指标"), CompensationSnapshot.CompensationFailureCount, static_cast<int64>(1));
	TestTrue(TEXT("补偿耗时进入任务指标"), CompensationSnapshot.CompensationDurationMs >= 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTaskManagerLifecycleTest, "WorldData.UnrealAgent.Core.Execution.TaskLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPTaskManagerLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;

	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	FMcpTaskRequest Request;
	Request.ToolName = TEXT("Tests.Execution.progress");
	Request.Owner = TEXT("Automation");
	Request.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
	Request.bCancelable = true;
	Request.Timeout = FTimespan::FromSeconds(2);
	Request.Work = [](FMcpTaskExecutionContext& Context)
	{
		Context.ReportProgress(0.25, TEXT("读取输入"));
		Context.EnterWaiting();
		Context.ResumeRunning();
		Context.ReportProgress(0.75, TEXT("写入结果"));
		return FMcpTaskWorkResult::Succeeded(TEXT("{\"success\":true}"));
	};

	FGuid TaskId;
	FString Error;
	TestTrue(TEXT("后台任务提交成功"), Manager.Submit(MoveTemp(Request), TaskId, Error));
	TestTrue(TEXT("任务标识有效"), TaskId.IsValid());

	FMcpTaskSnapshot Snapshot;
	TestTrue(TEXT("任务在超时前进入终态"), WaitForTerminalTask(Manager, TaskId, Snapshot));
	TestEqual(TEXT("任务成功完成"), Snapshot.State, EMcpTaskState::Completed);
	TestEqual(TEXT("任务结果保持不变"), Snapshot.ValueJson, FString(TEXT("{\"success\":true}")));
	TestEqual(TEXT("最终进度为一"), Snapshot.Progress.Fraction, 1.0);
	TestTrue(TEXT("状态历史包含等待阶段"),
		Snapshot.History.ContainsByPredicate(
			[](const FMcpTaskTransition& Transition)
			{
				return Transition.State == EMcpTaskState::Waiting;
			}));
	TestTrue(TEXT("任务快照可生成结构化 JSON"), Snapshot.ToJsonObject()->HasField(TEXT("history")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTaskManagerCancellationTest, "WorldData.UnrealAgent.Core.Execution.CancellationAndTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPTaskManagerCancellationTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;

	FMcpTaskManager Manager(FTimespan::FromSeconds(1));
	FEvent* StartedEvent = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* ReleaseEvent = FPlatformProcess::GetSynchEventFromPool(true);
	ON_SCOPE_EXIT
	{
		ReleaseEvent->Trigger();
		FPlatformProcess::ReturnSynchEventToPool(StartedEvent);
		FPlatformProcess::ReturnSynchEventToPool(ReleaseEvent);
	};

	FMcpTaskRequest RunningRequest;
	RunningRequest.ToolName = TEXT("Tests.Execution.cancel");
	RunningRequest.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
	RunningRequest.bCancelable = true;
	RunningRequest.Timeout = FTimespan::FromSeconds(2);
	RunningRequest.Work = [StartedEvent, ReleaseEvent](FMcpTaskExecutionContext& Context)
	{
		StartedEvent->Trigger();
		while (!Context.IsCancellationRequested() && !ReleaseEvent->Wait(5))
		{
		}
		return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};

	FGuid RunningTaskId;
	FString Error;
	TestTrue(TEXT("可取消任务提交成功"), Manager.Submit(MoveTemp(RunningRequest), RunningTaskId, Error));
	TestTrue(TEXT("任务已开始执行"), StartedEvent->Wait(1000));
	TestEqual(TEXT("运行中任务收到协作取消请求"), Manager.Cancel(RunningTaskId, TEXT("测试取消")), EMcpTaskCancelResult::CancellationRequested);

	FMcpTaskSnapshot Snapshot;
	TestTrue(TEXT("取消任务在安全点结束"), WaitForTerminalTask(Manager, RunningTaskId, Snapshot));
	TestEqual(TEXT("运行中任务最终为已取消"), Snapshot.State, EMcpTaskState::Cancelled);
	TestTrue(TEXT("取消请求已记录"), Snapshot.bCancellationRequested);

	FMcpTaskRequest TimeoutRequest;
	TimeoutRequest.ToolName = TEXT("Tests.Execution.timeout");
	TimeoutRequest.ThreadPolicy = EMcpToolThreadPolicy::Inline;
	TimeoutRequest.bCancelable = true;
	TimeoutRequest.Timeout = FTimespan::FromMilliseconds(100);
	TimeoutRequest.Work = [](FMcpTaskExecutionContext& Context)
	{
		return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};
	FGuid TimeoutTaskId;
	TestTrue(TEXT("即时任务提交成功"), Manager.Submit(MoveTemp(TimeoutRequest), TimeoutTaskId, Error));
	TestTrue(TEXT("即时任务可读取"), Manager.TryRead(TimeoutTaskId, Snapshot));
	TestEqual(TEXT("即时任务不会被误判为超时"), Snapshot.State, EMcpTaskState::Completed);

	FMcpTaskRequest LateRequest;
	LateRequest.ToolName = TEXT("Tests.Execution.late_timeout");
	LateRequest.ThreadPolicy = EMcpToolThreadPolicy::Inline;
	LateRequest.bCancelable = true;
	LateRequest.Timeout = FTimespan::FromMilliseconds(1);
	LateRequest.Work = [](FMcpTaskExecutionContext& Context)
	{
		FPlatformProcess::SleepNoStats(0.01f);
		return Context.IsDeadlineExceeded() ? FMcpTaskWorkResult::Failed(TEXT("deadline_exceeded")) : FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};
	FGuid LateTaskId;
	TestTrue(TEXT("超时即时任务提交成功"), Manager.Submit(MoveTemp(LateRequest), LateTaskId, Error));
	TestTrue(TEXT("超时即时任务可读取"), Manager.TryRead(LateTaskId, Snapshot));
	TestEqual(TEXT("超过截止时间的即时任务不能提交成功结果"), Snapshot.State, EMcpTaskState::TimedOut);

	FMcpTaskRequest QueuedRequest;
	QueuedRequest.ToolName = TEXT("Tests.Execution.queued");
	QueuedRequest.ThreadPolicy = EMcpToolThreadPolicy::StagedGameThread;
	QueuedRequest.bCancelable = true;
	QueuedRequest.Work = [](FMcpTaskExecutionContext& Context)
	{
		return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};
	FGuid QueuedTaskId;
	TestTrue(TEXT("主线程排队任务提交成功"), Manager.Submit(MoveTemp(QueuedRequest), QueuedTaskId, Error));
	TestEqual(TEXT("未开始任务保证取消"), Manager.Cancel(QueuedTaskId, TEXT("排队取消")), EMcpTaskCancelResult::CancelledBeforeStart);
	TestTrue(TEXT("排队取消任务可读取"), Manager.TryRead(QueuedTaskId, Snapshot));
	TestEqual(TEXT("排队任务保持已取消"), Snapshot.State, EMcpTaskState::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTaskManagerCancellationRaceTest, "WorldData.UnrealAgent.Core.Execution.CancellationRaceDurability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPTaskManagerCancellationRaceTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	TArray<FGuid> TaskIds;
	TaskIds.Reserve(256);
	FString Error;
	for (int32 Index = 0; Index < 256; ++Index)
	{
		FMcpTaskRequest Request;
		Request.ToolName = TEXT("Tests.Execution.cancel_race");
		Request.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
		Request.bCancelable = true;
		Request.Timeout = FTimespan::FromSeconds(2);
		Request.Work = [](FMcpTaskExecutionContext& Context)
		{
			const double Deadline = FPlatformTime::Seconds() + 0.25;
			while (!Context.IsCancellationRequested() && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::SleepNoStats(0.0005f);
			}
			return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
		};
		FGuid TaskId;
		if (!Manager.Submit(MoveTemp(Request), TaskId, Error))
		{
			AddError(FString::Printf(TEXT("取消竞态任务提交失败：%s"), *Error));
			return false;
		}
		TaskIds.Add(TaskId);
		const EMcpTaskCancelResult CancelResult = Manager.Cancel(TaskId, TEXT("P4 cancel race"));
		TestTrue(TEXT("立即取消只返回已取消或已请求"), CancelResult == EMcpTaskCancelResult::CancelledBeforeStart || CancelResult == EMcpTaskCancelResult::CancellationRequested);
	}

	int32 NonCancelled = 0;
	for (const FGuid& TaskId : TaskIds)
	{
		FMcpTaskSnapshot Snapshot;
		if (!WaitForTerminalTask(Manager, TaskId, Snapshot, 5.0) || Snapshot.State != EMcpTaskState::Cancelled)
		{
			++NonCancelled;
		}
	}
	TestEqual(TEXT("256 次提交/取消竞态全部收敛为 Cancelled"), NonCancelled, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTaskManagerSchedulingSemanticsTest, "WorldData.UnrealAgent.Core.Execution.SchedulingSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPTaskManagerSchedulingSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	TestTrue(TEXT("自动化测试从游戏线程运行"), IsInGameThread());
	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	bool bGameThreadWorkRan = false;
	bool bWorkObservedGameThread = false;
	FMcpTaskRequest GameThreadRequest;
	GameThreadRequest.ToolName = TEXT("Tests.Execution.game_thread_immediate");
	GameThreadRequest.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
	GameThreadRequest.Work = [&bGameThreadWorkRan, &bWorkObservedGameThread](FMcpTaskExecutionContext& Context)
	{
		(void)Context;
		bGameThreadWorkRan = true;
		bWorkObservedGameThread = IsInGameThread();
		return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};

	FGuid GameThreadTaskId;
	FString Error;
	TestTrue(TEXT("游戏线程任务提交成功"), Manager.Submit(MoveTemp(GameThreadRequest), GameThreadTaskId, Error));
	TestTrue(TEXT("已在游戏线程时任务立即执行"), bGameThreadWorkRan);
	TestTrue(TEXT("游戏线程策略确实在游戏线程运行"), bWorkObservedGameThread);
	FMcpTaskSnapshot Snapshot;
	TestTrue(TEXT("游戏线程任务快照可读"), Manager.TryRead(GameThreadTaskId, Snapshot));
	TestEqual(TEXT("立即执行任务已经完成"), Snapshot.State, EMcpTaskState::Completed);

	TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> bStagedWorkRan = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	FMcpTaskRequest StagedRequest;
	StagedRequest.ToolName = TEXT("Tests.Execution.staged_game_thread");
	StagedRequest.ThreadPolicy = EMcpToolThreadPolicy::StagedGameThread;
	StagedRequest.bCancelable = true;
	StagedRequest.Work = [bStagedWorkRan](FMcpTaskExecutionContext& Context)
	{
		(void)Context;
		bStagedWorkRan->AtomicSet(true);
		return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};
	FGuid StagedTaskId;
	TestTrue(TEXT("分阶段游戏线程任务提交成功"), Manager.Submit(MoveTemp(StagedRequest), StagedTaskId, Error));
	TestFalse(TEXT("分阶段策略不会在提交栈内重入"), static_cast<bool>(*bStagedWorkRan));
	TestEqual(TEXT("分阶段任务可在开始前取消"), Manager.Cancel(StagedTaskId, TEXT("cancel staged task")), EMcpTaskCancelResult::CancelledBeforeStart);
	TestFalse(TEXT("取消后分阶段工作仍未执行"), static_cast<bool>(*bStagedWorkRan));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTaskManagerTimeoutDurabilityTest, "WorldData.UnrealAgent.Core.Execution.TimeoutDurability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPTaskManagerTimeoutDurabilityTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	FEvent* StartedEvent = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* ReleaseEvent = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* WorkReturnedEvent = FPlatformProcess::GetSynchEventFromPool(true);
	ON_SCOPE_EXIT
	{
		ReleaseEvent->Trigger();
		WorkReturnedEvent->Wait(1000);
		FPlatformProcess::ReturnSynchEventToPool(StartedEvent);
		FPlatformProcess::ReturnSynchEventToPool(ReleaseEvent);
		FPlatformProcess::ReturnSynchEventToPool(WorkReturnedEvent);
	};

	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	FMcpTaskRequest Request;
	Request.ToolName = TEXT("Tests.Execution.timeout_durability");
	Request.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
	Request.bCancelable = true;
	Request.Timeout = FTimespan::FromSeconds(30);
	Request.Work = [StartedEvent, ReleaseEvent, WorkReturnedEvent](FMcpTaskExecutionContext& Context)
	{
		(void)Context;
		StartedEvent->Trigger();
		ReleaseEvent->Wait();
		WorkReturnedEvent->Trigger();
		return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};

	FGuid TaskId;
	FString Error;
	TestTrue(TEXT("运行中超时测试任务提交成功"), Manager.Submit(MoveTemp(Request), TaskId, Error));
	TestTrue(TEXT("运行中超时测试任务已经开始"), StartedEvent->Wait(1000));
	TestEqual(TEXT("Tick 将超过截止时间的运行任务标记为超时"), Manager.Tick(FDateTime::UtcNow() + FTimespan::FromMinutes(1)), 1);

	FMcpTaskSnapshot Snapshot;
	TestTrue(TEXT("超时任务快照可读"), Manager.TryRead(TaskId, Snapshot));
	TestEqual(TEXT("任务立即进入稳定超时终态"), Snapshot.State, EMcpTaskState::TimedOut);
	TestEqual(TEXT("后台工作尚未退出时副作用状态可观察"), Snapshot.SideEffectState, FString(TEXT("may_continue")));

	ReleaseEvent->Trigger();
	TestTrue(TEXT("超时后的后台工作最终退出"), WorkReturnedEvent->Wait(1000));
	const double WaitDeadline = FPlatformTime::Seconds() + 1.0;
	do
	{
		Manager.TryRead(TaskId, Snapshot);
		if (Snapshot.SideEffectState == TEXT("completed_after_timeout"))
		{
			break;
		}
		FPlatformProcess::SleepNoStats(0.001f);
	} while (FPlatformTime::Seconds() < WaitDeadline);
	TestEqual(TEXT("后台退出后副作用状态被持久化"), Snapshot.SideEffectState, FString(TEXT("completed_after_timeout")));
	TestEqual(TEXT("后台退出不会覆盖超时终态"), Snapshot.State, EMcpTaskState::TimedOut);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTaskManagerShutdownDrainTest, "WorldData.UnrealAgent.Core.Execution.ShutdownDrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPTaskManagerShutdownDrainTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCP;
	using namespace UnrealAgentMCP::Execution;
	(void)Parameters;

	FEvent* StartedEvent = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* ReleaseEvent = FPlatformProcess::GetSynchEventFromPool(true);
	ON_SCOPE_EXIT
	{
		ReleaseEvent->Trigger();
		FPlatformProcess::ReturnSynchEventToPool(StartedEvent);
		FPlatformProcess::ReturnSynchEventToPool(ReleaseEvent);
	};

	FMcpTaskManager Manager(FTimespan::FromSeconds(5));
	FMcpTaskRequest BackgroundRequest;
	BackgroundRequest.ToolName = TEXT("Tests.Execution.shutdown_drain");
	BackgroundRequest.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
	BackgroundRequest.bCancelable = true;
	BackgroundRequest.Work = [StartedEvent, ReleaseEvent](FMcpTaskExecutionContext& Context)
	{
		StartedEvent->Trigger();
		ReleaseEvent->Wait();
		Context.ShouldStop();
		return FMcpTaskWorkResult::Failed(TEXT("stopped"));
	};
	FGuid BackgroundTaskId;
	FString Error;
	TestTrue(TEXT("后台排空测试任务提交成功"), Manager.Submit(MoveTemp(BackgroundRequest), BackgroundTaskId, Error));
	TestTrue(TEXT("后台排空测试任务已经开始"), StartedEvent->Wait(1000));
	TestFalse(TEXT("尚未退出的后台工作不会被虚假报告为已排空"), Manager.ShutdownAndWait(TEXT("test shutdown"), FTimespan::FromMilliseconds(10)));
	ReleaseEvent->Trigger();
	TestTrue(TEXT("后台工作退出后排空成功"), Manager.ShutdownAndWait(TEXT("test shutdown"), FTimespan::FromSeconds(1)));

	FMcpTaskSnapshot BackgroundSnapshot;
	TestTrue(TEXT("停止后的后台任务快照仍可读取"), Manager.TryRead(BackgroundTaskId, BackgroundSnapshot));
	TestEqual(TEXT("停止后的后台任务保持取消终态"), BackgroundSnapshot.State, EMcpTaskState::Cancelled);

	FMcpTaskManager StagedManager(FTimespan::FromSeconds(5));
	TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> bStagedWorkRan = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	FMcpTaskRequest StagedRequest;
	StagedRequest.ToolName = TEXT("Tests.Execution.shutdown_staged");
	StagedRequest.ThreadPolicy = EMcpToolThreadPolicy::StagedGameThread;
	StagedRequest.bCancelable = true;
	StagedRequest.Work = [bStagedWorkRan](FMcpTaskExecutionContext& Context)
	{
		(void)Context;
		bStagedWorkRan->AtomicSet(true);
		return FMcpTaskWorkResult::Succeeded(TEXT("{}"));
	};
	FGuid StagedTaskId;
	TestTrue(TEXT("待调度任务提交成功"), StagedManager.Submit(MoveTemp(StagedRequest), StagedTaskId, Error));
	TestTrue(TEXT("停止会移除待调度 Ticker 并完成排空"), StagedManager.ShutdownAndWait(TEXT("test staged shutdown"), FTimespan::FromSeconds(1)));
	TestFalse(TEXT("被移除的待调度工作没有执行"), static_cast<bool>(*bStagedWorkRan));
	return true;
}
