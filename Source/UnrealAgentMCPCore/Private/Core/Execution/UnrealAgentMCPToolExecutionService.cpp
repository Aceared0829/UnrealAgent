// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPToolExecutionService.cpp
 * @brief 工具契约校验、线程调度、任务回执与同步等待实现。
 */

#include "Core/Execution/UnrealAgentMCPToolExecutionService.h"

#include "Core/Schema/UnrealAgentMCPJsonSchema.h"
#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAgentMCPExecution, Log, All);

namespace UnrealAgentMCP::Execution
{
	namespace
	{
		FString JsonObjectToString(const TSharedRef<FJsonObject>& Object)
		{
			FString Json;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
			FJsonSerializer::Serialize(Object, Writer);
			return Json;
		}

		FString ErrorJson(const FString& Error)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), Error);
			return JsonObjectToString(Result);
		}

		FString ContractErrorJson(const FString& Code, const FString& Error)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("code"), Code);
			Result->SetStringField(TEXT("error"), Error);
			Result->SetBoolField(TEXT("retryable"), false);
			return JsonObjectToString(Result);
		}

		FString PolicyErrorJson(const Policy::FMcpPolicyDecision& Decision)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), Decision.Reason);
			Result->SetStringField(TEXT("policyCode"), Decision.Code);
			Result->SetStringField(TEXT("policyOutcome"), Policy::PolicyOutcomeToString(Decision.Outcome));
			return JsonObjectToString(Result);
		}

		FString ToolUnavailableJson(const FString& ToolName)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("code"), TEXT("provider_unavailable"));
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("工具 %s 所属 Provider 已卸载或被替换。"), *ToolName));
			return JsonObjectToString(Result);
		}

		FString TaskReceiptJson(const FMcpTaskSnapshot& Snapshot)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetBoolField(TEXT("async"), true);
			Result->SetObjectField(TEXT("task"), Snapshot.ToJsonObject());
			return JsonObjectToString(Result);
		}

		FMcpTaskWorkResult InterpretToolResult(FString ResultJson, const TSharedPtr<FJsonObject>& OutputSchema, const bool bOutputSchemaExplicit)
		{
			TSharedPtr<FJsonObject> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
			if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
			{
				const FString Error = TEXT("工具返回值不是有效的 JSON 对象。");
				FMcpTaskWorkResult FailedResult = FMcpTaskWorkResult::Failed(Error);
				FailedResult.ValueJson = ContractErrorJson(TEXT("invalid_result_json"), Error);
				return FailedResult;
			}

			bool bSuccess = true;
			if (Parsed->TryGetBoolField(TEXT("success"), bSuccess) && !bSuccess)
			{
				FString Error;
				if (!Parsed->TryGetStringField(TEXT("error"), Error))
				{
					Error = ResultJson;
				}
				FMcpTaskWorkResult FailedResult = FMcpTaskWorkResult::Failed(MoveTemp(Error));
				FailedResult.ValueJson = MoveTemp(ResultJson);
				return FailedResult;
			}

			if (bOutputSchemaExplicit && OutputSchema.IsValid())
			{
				const JsonSchema::FValidationResult Validation = JsonSchema::ValidateObject(OutputSchema.ToSharedRef(), Parsed);
				if (!Validation.IsValid())
				{
					const FString Error = TEXT("工具成功结果违反已发布的输出 Schema。");
					FMcpTaskWorkResult FailedResult = FMcpTaskWorkResult::Failed(Error);
					FailedResult.ValueJson = JsonSchema::MakeOutputValidationErrorJson(TEXT("tool_output"), Validation);
					return FailedResult;
				}
			}
			return FMcpTaskWorkResult::Succeeded(MoveTemp(ResultJson));
		}

		struct FCompensationEntry
		{
			FString Description;
			FMcpCompensationHandler Handler;
		};

		class FCompensatingToolTransactionScope final : public IMcpToolTransactionScope
		{
		public:
			explicit FCompensatingToolTransactionScope(FMcpCompensationMetricsReporter InMetricsReporter = {}) : MetricsReporter(MoveTemp(InMetricsReporter))
			{
			}

			virtual ~FCompensatingToolTransactionScope() override
			{
				if (!bFinalized)
				{
					FString IgnoredError;
					Finalize(false, IgnoredError);
				}
			}

			bool Register(FString Description, FMcpCompensationHandler Handler, FString& OutError)
			{
				OutError.Reset();
				if (bFinalized)
				{
					OutError = TEXT("补偿事务已经完成，不能继续登记动作。");
					return false;
				}
				FCompensationEntry& Entry = Entries.AddDefaulted_GetRef();
				Entry.Description = MoveTemp(Description);
				Entry.Handler = MoveTemp(Handler);
				if (MetricsReporter)
				{
					MetricsReporter(1, 0, 0, 0.0);
				}
				return true;
			}

			virtual bool Finalize(const bool bInvocationSucceeded, FString& OutError) override
			{
				OutError.Reset();
				if (bFinalized)
				{
					OutError = TEXT("补偿事务已经完成，不能重复收尾。");
					return false;
				}
				bFinalized = true;
				if (bInvocationSucceeded)
				{
					Entries.Reset();
					return true;
				}

				TArray<FString> Failures;
				for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
				{
					FCompensationEntry& Entry = Entries[Index];
					FString CompensationError;
					const double CompensationStartSeconds = FPlatformTime::Seconds();
					const bool bCompensated = Entry.Handler(CompensationError);
					const double CompensationDurationMs = (FPlatformTime::Seconds() - CompensationStartSeconds) * 1000.0;
					if (MetricsReporter)
					{
						MetricsReporter(0, 1, bCompensated ? 0 : 1, CompensationDurationMs);
					}
					if (!bCompensated)
					{
						if (CompensationError.IsEmpty())
						{
							CompensationError = TEXT("未提供失败原因。");
						}
						Failures.Add(FString::Printf(TEXT("%s：%s"), *Entry.Description, *CompensationError));
					}
				}
				Entries.Reset();
				if (!Failures.IsEmpty())
				{
					OutError = FString::Printf(TEXT("补偿事务有 %d 个动作失败：%s"), Failures.Num(), *FString::Join(Failures, TEXT("；")));
					return false;
				}
				return true;
			}

		private:
			TArray<FCompensationEntry> Entries;
			FMcpCompensationMetricsReporter MetricsReporter;
			bool bFinalized = false;
		};

	}

	class FTransactionalTaskStepper final : public IMcpTaskStepper
	{
	public:
		FTransactionalTaskStepper(TSharedPtr<IMcpTaskStepper> InInner, FMcpToolRuntimeRegistry& InRegistry, FString InRegistryToolName, FString InEffectiveToolId,
			const uint64 InExpectedGeneration, const EMcpToolTransactionPolicy InTransactionPolicy,
			TSharedPtr<IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> InTransactionCoordinator)
			: Inner(MoveTemp(InInner)), Registry(InRegistry), RegistryToolName(MoveTemp(InRegistryToolName)), EffectiveToolId(MoveTemp(InEffectiveToolId)),
			  ExpectedGeneration(InExpectedGeneration), TransactionPolicy(InTransactionPolicy), TransactionCoordinator(MoveTemp(InTransactionCoordinator))
		{
		}

		virtual ~FTransactionalTaskStepper() override
		{
			Inner.Reset();
			if (TransactionScope.IsValid() && !bFinalized)
			{
				FString IgnoredError;
				TransactionScope->Finalize(false, IgnoredError);
			}
		}

		virtual bool HasWorkerPreparation() const override
		{
			return Inner.IsValid() && Inner->HasWorkerPreparation();
		}

		virtual FMcpTaskPrepareResult PrepareOnWorker(FMcpTaskExecutionContext& Context) override
		{
			FMcpToolDescriptor CurrentDescriptor;
			if (!Registry.TryGetDescriptor(RegistryToolName, CurrentDescriptor) || CurrentDescriptor.RegistrationGeneration != ExpectedGeneration)
			{
				return FMcpTaskPrepareResult::Failed(TEXT("provider_unavailable"));
			}
			return Inner.IsValid() ? Inner->PrepareOnWorker(Context) : FMcpTaskPrepareResult::Failed(TEXT("task_stepper_unavailable"));
		}

		virtual FMcpTaskStepResult Step(FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			FMcpToolDescriptor CurrentDescriptor;
			if (!Registry.TryGetDescriptor(RegistryToolName, CurrentDescriptor) || CurrentDescriptor.RegistrationGeneration != ExpectedGeneration)
			{
				return Finalize(FMcpTaskStepResult::Failed(TEXT("provider_unavailable")), false);
			}

			FString BeginError;
			if (!BeginTransaction(Context, BeginError))
			{
				FMcpTaskStepResult Result = FMcpTaskStepResult::Failed(BeginError);
				Result.ValueJson = ErrorJson(BeginError);
				return Result;
			}

			BindCompensation(Context);
			FMcpTaskStepResult Result = Inner->Step(Context, FrameBudget);
			Context.SetCompensationRegistrar(FMcpCompensationRegistrar());
			if (Result.State == EMcpTaskStepState::Continue)
			{
				return Result;
			}
			if (Result.State == EMcpTaskStepState::Succeeded &&
				(TransactionPolicy == EMcpToolTransactionPolicy::Atomic || TransactionPolicy == EMcpToolTransactionPolicy::Compensating) && Context.ShouldStop())
			{
				FString StopError = Context.IsDeadlineExceeded() ? TEXT("任务执行超时。") : Context.GetCancellationReason();
				Result = FMcpTaskStepResult::Failed(MoveTemp(StopError));
			}
			const bool bInvocationSucceeded = Result.State == EMcpTaskStepState::Succeeded;
			return Finalize(MoveTemp(Result), bInvocationSucceeded);
		}

		virtual FMcpTaskStepResult Abort(FMcpTaskExecutionContext& Context, FString Reason) override
		{
			BindCompensation(Context);
			FMcpTaskStepResult Result = Inner->Abort(Context, MoveTemp(Reason));
			Context.SetCompensationRegistrar(FMcpCompensationRegistrar());
			if (Result.State == EMcpTaskStepState::Continue)
			{
				return Result;
			}
			return Finalize(MoveTemp(Result), false);
		}

	private:
		bool BeginTransaction(const FMcpTaskExecutionContext& Context, FString& OutError)
		{
			OutError.Reset();
			if (bTransactionStarted)
			{
				return true;
			}
			bTransactionStarted = true;
			if (TransactionPolicy == EMcpToolTransactionPolicy::Compensating)
			{
				TUniquePtr<FCompensatingToolTransactionScope> NewScope = MakeUnique<FCompensatingToolTransactionScope>(Context.GetCompensationMetricsReporter());
				CompensationScope = NewScope.Get();
				TransactionScope = MoveTemp(NewScope);
				return true;
			}
			if (!TransactionCoordinator.IsValid() || (TransactionPolicy != EMcpToolTransactionPolicy::ScopedTransaction && TransactionPolicy != EMcpToolTransactionPolicy::Atomic))
			{
				return true;
			}
			TransactionScope = TransactionCoordinator->Begin(EffectiveToolId, TransactionPolicy, OutError);
			return TransactionScope.IsValid() || OutError.IsEmpty();
		}

		void BindCompensation(FMcpTaskExecutionContext& Context) const
		{
			if (!CompensationScope)
			{
				return;
			}
			Context.SetCompensationRegistrar(
				[Scope = CompensationScope](FString Description, FMcpCompensationHandler Handler, FString& OutError)
				{
					return Scope->Register(MoveTemp(Description), MoveTemp(Handler), OutError);
				});
		}

		FMcpTaskStepResult Finalize(FMcpTaskStepResult Result, const bool bInvocationSucceeded)
		{
			if (!TransactionScope.IsValid() || bFinalized)
			{
				bFinalized = true;
				return Result;
			}
			bFinalized = true;
			FString TransactionError;
			if (TransactionScope->Finalize(bInvocationSucceeded, TransactionError))
			{
				return Result;
			}
			if (TransactionError.IsEmpty())
			{
				TransactionError = TEXT("事务收尾失败。");
			}
			FMcpTaskStepResult FailedResult = FMcpTaskStepResult::Failed(TransactionError);
			FailedResult.ValueJson = ErrorJson(TransactionError);
			return FailedResult;
		}

		TSharedPtr<IMcpTaskStepper> Inner;
		FMcpToolRuntimeRegistry& Registry;
		FString RegistryToolName;
		FString EffectiveToolId;
		uint64 ExpectedGeneration = 0;
		EMcpToolTransactionPolicy TransactionPolicy = EMcpToolTransactionPolicy::None;
		TSharedPtr<IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> TransactionCoordinator;
		TUniquePtr<IMcpToolTransactionScope> TransactionScope;
		FCompensatingToolTransactionScope* CompensationScope = nullptr;
		bool bTransactionStarted = false;
		bool bFinalized = false;
	};

	FMcpToolExecutionService::FMcpToolExecutionService(FMcpToolRuntimeRegistry& InRegistry, const FTimespan TaskRetention, Policy::FMcpServerPolicy InPolicy)
		: FMcpToolExecutionService(InRegistry, TaskRetention, MoveTemp(InPolicy), nullptr, nullptr)
	{
	}

	FMcpToolExecutionService::FMcpToolExecutionService(FMcpToolRuntimeRegistry& InRegistry, const FTimespan TaskRetention, Policy::FMcpServerPolicy InPolicy,
		TSharedPtr<IMcpToolTransactionCoordinator, ESPMode::ThreadSafe> InTransactionCoordinator, TSharedPtr<IMcpExecutionLedger, ESPMode::ThreadSafe> InExecutionLedger)
		: Registry(InRegistry), TaskManager(TaskRetention,
									[Ledger = InExecutionLedger](const FMcpTaskSnapshot& Snapshot, FString& OutError)
									{
										return !Ledger.IsValid() || Ledger->AppendTaskSnapshot(Snapshot, OutError);
									}),
		  PolicyEngine(MoveTemp(InPolicy)), AuditLog(2048), TransactionCoordinator(MoveTemp(InTransactionCoordinator)), ExecutionLedger(MoveTemp(InExecutionLedger))
	{
	}

	bool FMcpToolExecutionService::Execute(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FString& OutResultJson, const FMcpToolExecutionOptions& Options)
	{
		FMcpToolDescriptor Descriptor;
		if (!Registry.TryGetDescriptor(ToolName, Descriptor))
		{
			return false;
		}

		const TSharedPtr<FJsonObject> SafeArguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
		const FGuid RequestId = Options.RequestId.IsValid() ? Options.RequestId : FGuid::NewGuid();
		const FGuid TraceId = Options.TraceId.IsValid() ? Options.TraceId : RequestId;
		if (Descriptor.RiskResolver)
		{
			Descriptor.Risk = Descriptor.RiskResolver(SafeArguments);
		}
		if (Descriptor.ThreadPolicyResolver)
		{
			Descriptor.ThreadPolicy = Descriptor.ThreadPolicyResolver(SafeArguments);
		}
		if (Descriptor.ExecutionModeResolver)
		{
			Descriptor.ExecutionMode = Descriptor.ExecutionModeResolver(SafeArguments);
		}
		if (Descriptor.TransactionPolicyResolver)
		{
			Descriptor.TransactionPolicy = Descriptor.TransactionPolicyResolver(SafeArguments);
		}
		if (Descriptor.CancelableResolver)
		{
			Descriptor.bCancelable = Descriptor.CancelableResolver(SafeArguments);
		}
		if (Descriptor.FilePathArgumentsResolver)
		{
			Descriptor.FilePathArguments = Descriptor.FilePathArgumentsResolver(SafeArguments);
		}
		if (Descriptor.AssetPathArgumentsResolver)
		{
			Descriptor.AssetPathArguments = Descriptor.AssetPathArgumentsResolver(SafeArguments);
		}
		if (Descriptor.ActionContractResolver)
		{
			FMcpResolvedToolContract ResolvedContract;
			if (Descriptor.ActionContractResolver(SafeArguments, ResolvedContract))
			{
				Descriptor.QualifiedName = MoveTemp(ResolvedContract.CanonicalToolId);
				if (ResolvedContract.InputSchema.IsValid())
				{
					Descriptor.InputSchema = MoveTemp(ResolvedContract.InputSchema);
				}
				if (ResolvedContract.OutputSchema.IsValid())
				{
					Descriptor.OutputSchema = MoveTemp(ResolvedContract.OutputSchema);
					Descriptor.bOutputSchemaExplicit = true;
				}
				Descriptor.Risk = ResolvedContract.Risk;
				Descriptor.bReadOnly = ResolvedContract.Risk == EMcpToolRisk::ReadOnly;
				Descriptor.ExecutionMode = ResolvedContract.ExecutionMode;
				Descriptor.TransactionPolicy = ResolvedContract.TransactionPolicy;
				Descriptor.bCancelable = ResolvedContract.bCancelable;
			}
		}
		const FString EffectiveToolId = Descriptor.GetEffectiveToolId();
		const bool bRequiresResumableTask = Descriptor.RequiresResumableTaskResolver ? Descriptor.RequiresResumableTaskResolver(SafeArguments) : Descriptor.bRequiresResumableTask;
		if (Descriptor.bInputSchemaEnforced)
		{
			const JsonSchema::FValidationResult Validation = JsonSchema::ValidateObject(Descriptor.InputSchema.ToSharedRef(), SafeArguments);
			if (!Validation.IsValid())
			{
				Policy::FMcpAuditRecord AuditRecord;
				AuditRecord.TraceId = TraceId;
				AuditRecord.RequestId = RequestId;
				AuditRecord.Timestamp = FDateTime::UtcNow();
				AuditRecord.ClientId = Options.PolicyContext.ClientId;
				AuditRecord.SessionId = Options.PolicyContext.SessionId;
				AuditRecord.Source = Options.PolicyContext.Source;
				AuditRecord.Provider = Options.PolicyContext.Provider;
				AuditRecord.ToolName = EffectiveToolId;
				AuditRecord.Risk = Descriptor.Risk;
				AuditRecord.Outcome = Policy::EMcpPolicyOutcome::Denied;
				AuditRecord.DecisionCode = TEXT("input_schema_invalid");
				for (const JsonSchema::FValidationError& Error : Validation.Errors)
				{
					if (!Error.Path.IsEmpty())
					{
						AuditRecord.SchemaErrorPaths.Add(Error.Path.Left(256));
					}
				}
				AuditRecord.bAccepted = false;
				FString PersistenceError;
				if (!AppendAuditRecord(AuditRecord, PersistenceError))
				{
					OutResultJson = ErrorJson(FString::Printf(TEXT("audit_persistence_unavailable: %s"), *PersistenceError));
					return true;
				}
				OutResultJson = JsonSchema::MakeValidationErrorJson(EffectiveToolId, Validation);
				return true;
			}
		}

		const Policy::FMcpPolicyDecision PolicyDecision = PolicyEngine.Evaluate(Descriptor, SafeArguments, Options.PolicyContext);
		Policy::FMcpAuditRecord AuditRecord;
		AuditRecord.TraceId = TraceId;
		AuditRecord.RequestId = RequestId;
		AuditRecord.Timestamp = FDateTime::UtcNow();
		AuditRecord.ClientId = Options.PolicyContext.ClientId;
		AuditRecord.SessionId = Options.PolicyContext.SessionId;
		AuditRecord.Source = Options.PolicyContext.Source;
		AuditRecord.Provider = Options.PolicyContext.Provider;
		AuditRecord.ToolName = EffectiveToolId;
		AuditRecord.Risk = Descriptor.Risk;
		AuditRecord.Outcome = PolicyDecision.Outcome;
		AuditRecord.DecisionCode = PolicyDecision.Code;
		AuditRecord.bAccepted = PolicyDecision.IsAllowed();
		FString PersistenceError;
		if (!AppendAuditRecord(AuditRecord, PersistenceError))
		{
			OutResultJson = ErrorJson(FString::Printf(TEXT("audit_persistence_unavailable: %s"), *PersistenceError));
			return true;
		}
		if (!PolicyDecision.IsAllowed())
		{
			OutResultJson = PolicyErrorJson(PolicyDecision);
			return true;
		}

		const bool bReturnTask = Options.bForceTask || Descriptor.ExecutionMode != EMcpToolExecutionMode::Synchronous ||
			Descriptor.ThreadPolicy == EMcpToolThreadPolicy::LongRunning || Descriptor.ThreadPolicy == EMcpToolThreadPolicy::StagedGameThread ||
			Descriptor.ThreadPolicy == EMcpToolThreadPolicy::NativeAsync;

		FMcpTaskRequest Request;
		Request.ToolName = EffectiveToolId;
		Request.Owner = Options.Owner.IsEmpty() ? Descriptor.Provider.ToString() : Options.Owner;
		Request.ThreadPolicy = Descriptor.ThreadPolicy;
		if (bReturnTask && Request.ThreadPolicy == EMcpToolThreadPolicy::GameThread)
		{
			// GameThread 工作开始前必须先让调用方拿到 Task 回执。
			// 否则 HTTP 调用会在 Submit 内执行到底，不仅无法取消，
			// 还会让请求处理器一直占用游戏线程。
			Request.ThreadPolicy = EMcpToolThreadPolicy::StagedGameThread;
		}
		else if (IsInGameThread() && Request.ThreadPolicy == EMcpToolThreadPolicy::GameThread)
		{
			Request.ThreadPolicy = EMcpToolThreadPolicy::Inline;
		}
		Request.bCancelable = Descriptor.bCancelable;
		Request.Timeout = Options.Timeout;
		Request.RequestId = RequestId;
		Request.TraceId = TraceId;
		Request.ProtocolRequestId = Options.ProtocolRequestId;
		Request.ClientId = Options.PolicyContext.ClientId;
		Request.SessionId = Options.PolicyContext.SessionId;
		Request.Source = Options.PolicyContext.Source;
		Request.Provider = Options.PolicyContext.Provider;
		if (Request.ThreadPolicy == EMcpToolThreadPolicy::StagedGameThread && Descriptor.StagedTaskFactory)
		{
			TSharedPtr<IMcpTaskStepper> InnerStepper = Descriptor.StagedTaskFactory(SafeArguments);
			if (InnerStepper.IsValid())
			{
				Request.Stepper = MakeShared<FTransactionalTaskStepper>(MoveTemp(InnerStepper), Registry, ToolName, EffectiveToolId, Descriptor.RegistrationGeneration,
					Descriptor.TransactionPolicy, TransactionCoordinator);
			}
		}
		if (bReturnTask && Descriptor.ExecutionMode == EMcpToolExecutionMode::Task && Request.ThreadPolicy == EMcpToolThreadPolicy::StagedGameThread && bRequiresResumableTask &&
			!Request.Stepper.IsValid())
		{
			const FString Error = FString::Printf(TEXT("Task tool '%s' has no resumable executor for this action."), *EffectiveToolId);
			UE_LOG(LogUnrealAgentMCPExecution, Error, TEXT("%s"), *Error);
			OutResultJson = ErrorJson(Error);
			return true;
		}

		if (!Request.Stepper.IsValid())
		{
			if (bReturnTask && Request.ThreadPolicy == EMcpToolThreadPolicy::StagedGameThread)
			{
				UE_LOG(LogUnrealAgentMCPExecution, Warning, TEXT("Task tool '%s' uses deferred monolithic GameThread work; cancellation cannot interrupt it after start."),
					*EffectiveToolId);
			}
			Request.Work = [RegistryPtr = &Registry, RegistryToolName = ToolName, EffectiveToolId, ExpectedGeneration = Descriptor.RegistrationGeneration,
							   TransactionPolicy = Descriptor.TransactionPolicy, ActiveTransactionCoordinator = TransactionCoordinator, OutputSchema = Descriptor.OutputSchema,
							   bOutputSchemaExplicit = Descriptor.bOutputSchemaExplicit, SafeArguments](FMcpTaskExecutionContext& Context)
			{
				if (Context.IsCancellationRequested())
				{
					return FMcpTaskWorkResult::Failed(Context.GetCancellationReason());
				}
				TUniquePtr<IMcpToolTransactionScope> TransactionScope;
				FCompensatingToolTransactionScope* CompensationScope = nullptr;
				if (TransactionPolicy == EMcpToolTransactionPolicy::Compensating)
				{
					TUniquePtr<FCompensatingToolTransactionScope> NewScope = MakeUnique<FCompensatingToolTransactionScope>(Context.GetCompensationMetricsReporter());
					CompensationScope = NewScope.Get();
					TransactionScope = MoveTemp(NewScope);
				}
				else if (ActiveTransactionCoordinator.IsValid() &&
					(TransactionPolicy == EMcpToolTransactionPolicy::ScopedTransaction || TransactionPolicy == EMcpToolTransactionPolicy::Atomic))
				{
					FString TransactionError;
					TransactionScope = ActiveTransactionCoordinator->Begin(EffectiveToolId, TransactionPolicy, TransactionError);
					if (!TransactionScope.IsValid() && !TransactionError.IsEmpty())
					{
						FMcpTaskWorkResult FailedResult = FMcpTaskWorkResult::Failed(TransactionError);
						FailedResult.ValueJson = ErrorJson(TransactionError);
						return FailedResult;
					}
				}
				if (CompensationScope)
				{
					Context.SetCompensationRegistrar(
						[CompensationScope](FString Description, FMcpCompensationHandler Handler, FString& OutError)
						{
							return CompensationScope->Register(MoveTemp(Description), MoveTemp(Handler), OutError);
						});
				}

				FString ResultJson;
				FMcpTaskWorkResult WorkResult;
				if (!RegistryPtr->TryDispatch(RegistryToolName, SafeArguments, ResultJson, ExpectedGeneration))
				{
					WorkResult = FMcpTaskWorkResult::Failed(TEXT("provider_unavailable"));
					WorkResult.ValueJson = ToolUnavailableJson(EffectiveToolId);
				}
				else
				{
					WorkResult = InterpretToolResult(MoveTemp(ResultJson), OutputSchema, bOutputSchemaExplicit);
				}
				Context.SetCompensationRegistrar(FMcpCompensationRegistrar());

				if (WorkResult.bSucceeded && (TransactionPolicy == EMcpToolTransactionPolicy::Atomic || TransactionPolicy == EMcpToolTransactionPolicy::Compensating) &&
					Context.ShouldStop())
				{
					const FString StopError = Context.IsDeadlineExceeded() ? TEXT("任务执行超时。") : Context.GetCancellationReason();
					WorkResult = FMcpTaskWorkResult::Failed(StopError);
					WorkResult.ValueJson = ErrorJson(StopError);
				}

				if (TransactionScope.IsValid())
				{
					FString TransactionError;
					if (!TransactionScope->Finalize(WorkResult.bSucceeded, TransactionError))
					{
						if (TransactionError.IsEmpty())
						{
							TransactionError = TEXT("事务收尾失败。");
						}
						FMcpTaskWorkResult FailedResult = FMcpTaskWorkResult::Failed(TransactionError);
						FailedResult.ValueJson = ErrorJson(TransactionError);
						return FailedResult;
					}
				}
				return WorkResult;
			};
		}

		FGuid TaskId;
		FString SubmitError;
		if (!TaskManager.Submit(MoveTemp(Request), TaskId, SubmitError))
		{
			OutResultJson = ErrorJson(SubmitError);
			return true;
		}

		FMcpTaskSnapshot Snapshot;
		if (bReturnTask)
		{
			TaskManager.TryRead(TaskId, Snapshot);
			OutResultJson = TaskReceiptJson(Snapshot);
			return true;
		}

		const double WaitSeconds = Options.Timeout > FTimespan::Zero() ? Options.Timeout.GetTotalSeconds() + 0.1 : 60.0;
		const double WaitDeadline = FPlatformTime::Seconds() + WaitSeconds;
		do
		{
			TaskManager.Tick();
			if (TaskManager.TryRead(TaskId, Snapshot) && Snapshot.IsTerminal())
			{
				TaskManager.TryRead(TaskId, Snapshot, true);
				if (Snapshot.State == EMcpTaskState::Completed)
				{
					OutResultJson = Snapshot.ValueJson;
				}
				else
				{
					OutResultJson = Snapshot.ValueJson.IsEmpty() ? ErrorJson(Snapshot.Error) : Snapshot.ValueJson;
				}
				return true;
			}
			FPlatformProcess::SleepNoStats(0.001f);
		} while (FPlatformTime::Seconds() < WaitDeadline);

		TaskManager.Tick(FDateTime::UtcNow() + Options.Timeout);
		if (TaskManager.TryRead(TaskId, Snapshot, true))
		{
			OutResultJson = ErrorJson(Snapshot.Error.IsEmpty() ? TEXT("工具等待超时。") : Snapshot.Error);
		}
		else
		{
			OutResultJson = ErrorJson(TEXT("工具等待超时。"));
		}
		return true;
	}

	FMcpToolInvocationResult FMcpToolExecutionService::ExecuteTyped(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, const FMcpToolExecutionOptions& Options)
	{
		FString ResultJson;
		const bool bToolFound = Execute(ToolName, Arguments, ResultJson, Options);
		FMcpToolInvocationResult Result = InterpretResult(bToolFound, MoveTemp(ResultJson));
		if (!Result.TraceId.IsValid())
		{
			Result.TraceId = Options.TraceId;
		}
		return Result;
	}

	FMcpToolInvocationResult FMcpToolExecutionService::InterpretResult(const bool bToolFound, FString ResultJson)
	{
		FMcpToolInvocationResult Result;
		Result.ResultJson = MoveTemp(ResultJson);
		if (!bToolFound)
		{
			Result.Outcome = EMcpToolInvocationOutcome::UnknownTool;
			Result.Code = TEXT("unknown_tool");
			Result.Message = TEXT("The requested tool is not registered.");
			return Result;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.ResultJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Result.Outcome = EMcpToolInvocationOutcome::ContractViolation;
			Result.Code = TEXT("invalid_result_json");
			Result.Message = TEXT("Tool returned a non-object or invalid JSON result.");
			return Result;
		}

		Root->TryGetStringField(TEXT("code"), Result.Code);
		Root->TryGetStringField(TEXT("error"), Result.Message);
		Root->TryGetBoolField(TEXT("retryable"), Result.bRetryable);
		FString TraceIdText;
		if (Root->TryGetStringField(TEXT("traceId"), TraceIdText))
		{
			FGuid::Parse(TraceIdText, Result.TraceId);
		}

		bool bSucceeded = true;
		if (Root->TryGetBoolField(TEXT("success"), bSucceeded) && !bSucceeded)
		{
			FString PolicyOutcome;
			FString PolicyCode;
			Root->TryGetStringField(TEXT("policyOutcome"), PolicyOutcome);
			Root->TryGetStringField(TEXT("policyCode"), PolicyCode);
			if (Result.Code.IsEmpty())
			{
				Result.Code = PolicyCode.IsEmpty() ? TEXT("tool_failed") : PolicyCode;
			}
			Result.Outcome = !PolicyOutcome.IsEmpty() ? EMcpToolInvocationOutcome::Denied : EMcpToolInvocationOutcome::Failed;
			if (Result.Message.IsEmpty())
			{
				Result.Message = TEXT("Tool execution failed.");
			}
			return Result;
		}

		bool bAsync = false;
		const TSharedPtr<FJsonObject>* TaskObject = nullptr;
		Root->TryGetBoolField(TEXT("async"), bAsync);
		if (bAsync)
		{
			if (!Root->TryGetObjectField(TEXT("task"), TaskObject) || TaskObject == nullptr || !TaskObject->IsValid())
			{
				Result.Outcome = EMcpToolInvocationOutcome::ContractViolation;
				Result.Code = TEXT("invalid_task_receipt");
				Result.Message = TEXT("Async tool receipt did not contain a task object.");
				return Result;
			}
			FString TaskIdText;
			(*TaskObject)->TryGetStringField(TEXT("taskId"), TaskIdText);
			FGuid::Parse(TaskIdText, Result.TaskId);
			if (!Result.TraceId.IsValid())
			{
				(*TaskObject)->TryGetStringField(TEXT("traceId"), TraceIdText);
				FGuid::Parse(TraceIdText, Result.TraceId);
			}
			if (!Result.TaskId.IsValid())
			{
				Result.Outcome = EMcpToolInvocationOutcome::ContractViolation;
				Result.Code = TEXT("invalid_task_receipt");
				Result.Message = TEXT("Async tool receipt did not contain a valid taskId.");
				return Result;
			}
			Result.Outcome = EMcpToolInvocationOutcome::AcceptedAsync;
			Result.Code = TEXT("task_accepted");
			return Result;
		}

		Result.Outcome = EMcpToolInvocationOutcome::Succeeded;
		if (Result.Code.IsEmpty())
		{
			Result.Code = TEXT("ok");
		}
		return Result;
	}

	bool FMcpToolExecutionService::TryReadTask(const FGuid& TaskId, FMcpTaskSnapshot& OutSnapshot, const bool bConsumeTerminal)
	{
		TaskManager.Tick();
		return TaskManager.TryRead(TaskId, OutSnapshot, bConsumeTerminal);
	}

	EMcpTaskCancelResult FMcpToolExecutionService::CancelTask(const FGuid& TaskId, FString Reason)
	{
		return TaskManager.Cancel(TaskId, MoveTemp(Reason));
	}

	int32 FMcpToolExecutionService::CancelTasksByClientId(const FString& ClientId, FString Reason)
	{
		return TaskManager.CancelByClientId(ClientId, MoveTemp(Reason));
	}

	TArray<FMcpTaskSnapshot> FMcpToolExecutionService::ListTasks()
	{
		TaskManager.Tick();
		TaskManager.PurgeExpired();
		return TaskManager.List();
	}

	TArray<Policy::FMcpAuditRecord> FMcpToolExecutionService::ListAudit(const int32 MaxResults) const
	{
		return AuditLog.List(MaxResults);
	}

	bool FMcpToolExecutionService::AppendAuditRecord(const Policy::FMcpAuditRecord& Record, FString& OutError)
	{
		AuditLog.Append(Record);
		OutError.Reset();
		return !ExecutionLedger.IsValid() || ExecutionLedger->AppendAuditRecord(Record, OutError);
	}

	const Policy::FMcpServerPolicy& FMcpToolExecutionService::GetPolicy() const
	{
		return PolicyEngine.GetPolicy();
	}

	bool FMcpToolExecutionService::IsExecutionLedgerHealthy(FString& OutError) const
	{
		OutError.Reset();
		return !ExecutionLedger.IsValid() || ExecutionLedger->IsHealthy(OutError);
	}

	FString FMcpToolExecutionService::GetExecutionLedgerLocation() const
	{
		return ExecutionLedger.IsValid() ? ExecutionLedger->GetLocation() : FString();
	}

	int32 FMcpToolExecutionService::Tick(const FDateTime Now)
	{
		return TaskManager.Tick(Now);
	}

	void FMcpToolExecutionService::Shutdown(FString Reason)
	{
		TaskManager.Shutdown(MoveTemp(Reason));
	}

	bool FMcpToolExecutionService::ShutdownAndWait(FString Reason, const FTimespan Timeout)
	{
		return TaskManager.ShutdownAndWait(MoveTemp(Reason), Timeout);
	}
}
