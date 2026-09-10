// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPTaskManager.cpp
 * @brief Unreal Agent 任务状态机、调度、进度、取消与超时实现。
 */

#include "Core/Execution/UnrealAgentMCPTaskManager.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAgentMCPTaskManager, Log, All);

namespace UnrealAgentMCP::Execution
{
	namespace
	{
		thread_local const FMcpTaskExecutionContext* GCurrentTaskExecutionContext = nullptr;

		bool IsTerminalState(const EMcpTaskState State)
		{
			return State == EMcpTaskState::Completed || State == EMcpTaskState::Failed || State == EMcpTaskState::Cancelled || State == EMcpTaskState::TimedOut;
		}

		double CalculateNearestRankPercentileFromSorted(const TArray<double>& SortedSamples, const double Percentile)
		{
			if (SortedSamples.IsEmpty())
			{
				return 0.0;
			}
			const int32 Rank = FMath::Clamp(FMath::CeilToInt(FMath::Clamp(Percentile, 0.0, 1.0) * SortedSamples.Num()), 1, SortedSamples.Num());
			return SortedSamples[Rank - 1];
		}

		bool AppendTransition(FMcpTaskSnapshot& Snapshot, const EMcpTaskState State, const FDateTime Timestamp, const FMcpTaskSnapshotSink& SnapshotSink)
		{
			Snapshot.State = State;
			FMcpTaskTransition& Transition = Snapshot.History.AddDefaulted_GetRef();
			Transition.State = State;
			Transition.Timestamp = Timestamp;
			if (!SnapshotSink)
			{
				return true;
			}
			FString Error;
			if (SnapshotSink(Snapshot, Error))
			{
				return true;
			}
			UE_LOG(LogUnrealAgentMCPTaskManager, Error, TEXT("Failed to persist MCP task %s transition %s: %s"), *Snapshot.Id.ToString(EGuidFormats::DigitsWithHyphensLower),
				*TaskStateToString(State), *Error);
			return false;
		}
	}

	struct FMcpTaskManager::FTaskRecord
	{
		FMcpTaskSnapshot Snapshot;
		TSharedRef<FMcpCancellationToken, ESPMode::ThreadSafe> Token = MakeShared<FMcpCancellationToken, ESPMode::ThreadSafe>();
		FGuid RequestId;
		FString ProtocolRequestId;
		FString ClientId;
		FString SessionId;
		FString Source;
		FMcpTaskWork Work;
		TSharedPtr<IMcpTaskStepper> Stepper;
		FTimespan StepFrameBudget = FTimespan::FromMilliseconds(4);
		TArray<double> ApplyStepDurationSamplesMs;
		FMcpTaskPrepareResult PreparationResult;
		bool bHasWorkerPreparation = false;
		bool bPreparationFinished = false;
		TUniquePtr<FMcpTaskExecutionContext> ExecutionContext;
	};

	struct FMcpTaskManager::FSharedState
	{
		struct FDispatchLease final
		{
			explicit FDispatchLease(TSharedRef<FSharedState, ESPMode::ThreadSafe> InState) : State(MoveTemp(InState))
			{
				FScopeLock Lock(&State->Mutex);
				if (State->ActiveDispatchCount++ == 0)
				{
					State->DispatchesFinishedEvent->Reset();
				}
			}

			~FDispatchLease()
			{
				FScopeLock Lock(&State->Mutex);
				check(State->ActiveDispatchCount > 0);
				if (--State->ActiveDispatchCount == 0)
				{
					State->DispatchesFinishedEvent->Trigger();
				}
			}

		private:
			TSharedRef<FSharedState, ESPMode::ThreadSafe> State;
		};

		FSharedState(const FTimespan InRetention, FMcpTaskSnapshotSink InSnapshotSink)
			: Retention(InRetention), SnapshotSink(MoveTemp(InSnapshotSink)), DispatchesFinishedEvent(FPlatformProcess::GetSynchEventFromPool(true))
		{
			DispatchesFinishedEvent->Trigger();
		}

		~FSharedState()
		{
			FPlatformProcess::ReturnSynchEventToPool(DispatchesFinishedEvent);
		}

		mutable FCriticalSection Mutex;
		TMap<FGuid, TSharedRef<FTaskRecord, ESPMode::ThreadSafe>> Tasks;
		TArray<FTSTicker::FDelegateHandle> TickerHandles;
		FTimespan Retention;
		FMcpTaskSnapshotSink SnapshotSink;
		FEvent* DispatchesFinishedEvent = nullptr;
		int32 ActiveDispatchCount = 0;
		bool bStopping = false;
	};

	bool FMcpTaskSnapshot::IsTerminal() const
	{
		return IsTerminalState(State);
	}

	TSharedRef<FJsonObject> FMcpTaskSnapshot::ToJsonObject() const
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("taskId"), Id.ToString(EGuidFormats::DigitsWithHyphensLower));
		Result->SetStringField(TEXT("traceId"), TraceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		Result->SetStringField(TEXT("requestId"), RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
		if (!ProtocolRequestId.IsEmpty())
		{
			Result->SetStringField(TEXT("protocolRequestId"), ProtocolRequestId);
		}
		Result->SetStringField(TEXT("clientId"), ClientId);
		Result->SetStringField(TEXT("sessionId"), SessionId);
		Result->SetStringField(TEXT("source"), Source);
		Result->SetStringField(TEXT("provider"), Provider);
		Result->SetStringField(TEXT("tool"), ToolName);
		Result->SetStringField(TEXT("owner"), Owner);
		Result->SetStringField(TEXT("state"), TaskStateToString(State));
		Result->SetStringField(TEXT("executionPhase"), TaskExecutionPhaseToString(ExecutionPhase));
		Result->SetBoolField(TEXT("terminal"), IsTerminal());
		Result->SetBoolField(TEXT("cancelable"), bCancelable);
		Result->SetBoolField(TEXT("resumable"), bResumable);
		Result->SetBoolField(TEXT("cancellationRequested"), bCancellationRequested);
		Result->SetBoolField(TEXT("cancellationDeferred"), bCancellationDeferred);
		Result->SetStringField(TEXT("sideEffectState"), SideEffectState);
		Result->SetStringField(TEXT("createdAt"), CreatedAt.ToIso8601());
		if (StartedAt != FDateTime())
		{
			Result->SetStringField(TEXT("startedAt"), StartedAt.ToIso8601());
			Result->SetNumberField(TEXT("queueDurationMs"), (StartedAt - CreatedAt).GetTotalMilliseconds());
		}
		if (CompletedAt != FDateTime())
		{
			Result->SetStringField(TEXT("completedAt"), CompletedAt.ToIso8601());
			if (StartedAt != FDateTime())
			{
				Result->SetNumberField(TEXT("executionDurationMs"), (CompletedAt - StartedAt).GetTotalMilliseconds());
			}
		}
		if (DeadlineAt != FDateTime())
		{
			Result->SetStringField(TEXT("deadlineAt"), DeadlineAt.ToIso8601());
		}
		if (!ValueJson.IsEmpty())
		{
			Result->SetStringField(TEXT("valueJson"), ValueJson);
		}
		if (!Error.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), Error);
		}

		TSharedRef<FJsonObject> ProgressObject = MakeShared<FJsonObject>();
		ProgressObject->SetNumberField(TEXT("fraction"), Progress.Fraction);
		ProgressObject->SetStringField(TEXT("message"), Progress.Message);
		ProgressObject->SetNumberField(TEXT("sequence"), static_cast<double>(Progress.Sequence));
		Result->SetObjectField(TEXT("progress"), ProgressObject);
		Result->SetNumberField(TEXT("stepCount"), static_cast<double>(StepCount));
		Result->SetNumberField(TEXT("applyStepBudgetMs"), ApplyStepBudgetMs);
		Result->SetNumberField(TEXT("prepareDurationMs"), PrepareDurationMs);
		Result->SetNumberField(TEXT("totalApplyDurationMs"), TotalApplyDurationMs);
		Result->SetNumberField(TEXT("lastStepDurationMs"), LastStepDurationMs);
		Result->SetNumberField(TEXT("maxStepDurationMs"), MaxStepDurationMs);
		Result->SetNumberField(TEXT("applyStepP95DurationMs"), ApplyStepP95DurationMs);
		Result->SetNumberField(TEXT("applyStepP99DurationMs"), ApplyStepP99DurationMs);
		Result->SetNumberField(TEXT("budgetOverrunCount"), static_cast<double>(BudgetOverrunCount));
		Result->SetNumberField(TEXT("cancellationCheckpointCount"), static_cast<double>(CancellationCheckpointCount));
		Result->SetNumberField(TEXT("compensationRegisteredCount"), static_cast<double>(CompensationRegisteredCount));
		Result->SetNumberField(TEXT("compensationExecutedCount"), static_cast<double>(CompensationExecutedCount));
		Result->SetNumberField(TEXT("compensationFailureCount"), static_cast<double>(CompensationFailureCount));
		Result->SetNumberField(TEXT("compensationDurationMs"), CompensationDurationMs);

		TArray<TSharedPtr<FJsonValue>> HistoryValues;
		HistoryValues.Reserve(History.Num());
		for (const FMcpTaskTransition& Transition : History)
		{
			TSharedRef<FJsonObject> TransitionObject = MakeShared<FJsonObject>();
			TransitionObject->SetStringField(TEXT("state"), TaskStateToString(Transition.State));
			TransitionObject->SetStringField(TEXT("timestamp"), Transition.Timestamp.ToIso8601());
			HistoryValues.Add(MakeShared<FJsonValueObject>(TransitionObject));
		}
		Result->SetArrayField(TEXT("history"), HistoryValues);
		return Result;
	}

	bool FMcpCancellationToken::Request(FString Reason)
	{
		if (Reason.IsEmpty())
		{
			Reason = TEXT("任务已请求取消。");
		}
		FScopeLock Lock(&ReasonMutex);
		if (bRequested.AtomicSet(true))
		{
			return false;
		}
		CancellationReason = MoveTemp(Reason);
		return true;
	}

	bool FMcpCancellationToken::IsRequested() const
	{
		return bRequested;
	}

	bool FMcpCancellationToken::ObserveRequest()
	{
		const bool bIsRequested = IsRequested();
		if (bIsRequested)
		{
			bObserved.AtomicSet(true);
		}
		return bIsRequested;
	}

	bool FMcpCancellationToken::WasObserved() const
	{
		return bObserved;
	}

	FString FMcpCancellationToken::GetReason() const
	{
		FScopeLock Lock(&ReasonMutex);
		return CancellationReason;
	}

	FMcpTaskExecutionContext::FMcpTaskExecutionContext(FGuid InTaskId, FGuid InTraceId, FGuid InRequestId, FString InProtocolRequestId, FString InClientId, FString InSessionId,
		FString InSource, const FDateTime InDeadline, TSharedRef<FMcpCancellationToken, ESPMode::ThreadSafe> InToken, TFunction<void(double, FString)> InProgressReporter,
		TFunction<void(bool)> InWaitingReporter, TFunction<void()> InCancellationCheckpointReporter, FMcpCompensationMetricsReporter InCompensationMetricsReporter)
		: TaskId(MoveTemp(InTaskId)), TraceId(MoveTemp(InTraceId)), RequestId(MoveTemp(InRequestId)), ProtocolRequestId(MoveTemp(InProtocolRequestId)),
		  ClientId(MoveTemp(InClientId)), SessionId(MoveTemp(InSessionId)), Source(MoveTemp(InSource)), Deadline(InDeadline), Token(MoveTemp(InToken)),
		  ProgressReporter(MoveTemp(InProgressReporter)), WaitingReporter(MoveTemp(InWaitingReporter)), CancellationCheckpointReporter(MoveTemp(InCancellationCheckpointReporter)),
		  CompensationMetricsReporter(MoveTemp(InCompensationMetricsReporter))
	{
	}

	const FMcpTaskExecutionContext* FMcpTaskExecutionContext::GetCurrent()
	{
		return GCurrentTaskExecutionContext;
	}

	const FGuid& FMcpTaskExecutionContext::GetTaskId() const
	{
		return TaskId;
	}

	const FGuid& FMcpTaskExecutionContext::GetTraceId() const
	{
		return TraceId;
	}

	const FGuid& FMcpTaskExecutionContext::GetRequestId() const
	{
		return RequestId;
	}

	const FString& FMcpTaskExecutionContext::GetProtocolRequestId() const
	{
		return ProtocolRequestId;
	}

	const FString& FMcpTaskExecutionContext::GetClientId() const
	{
		return ClientId;
	}

	const FString& FMcpTaskExecutionContext::GetSessionId() const
	{
		return SessionId;
	}

	const FString& FMcpTaskExecutionContext::GetSource() const
	{
		return Source;
	}

	bool FMcpTaskExecutionContext::HasDeadline() const
	{
		return Deadline != FDateTime();
	}

	FDateTime FMcpTaskExecutionContext::GetDeadline() const
	{
		return Deadline;
	}

	TOptional<FTimespan> FMcpTaskExecutionContext::GetRemainingTime() const
	{
		if (!HasDeadline())
		{
			return TOptional<FTimespan>();
		}
		return Deadline - FDateTime::UtcNow();
	}

	bool FMcpTaskExecutionContext::IsDeadlineExceeded() const
	{
		return HasDeadline() && FDateTime::UtcNow() >= Deadline;
	}

	bool FMcpTaskExecutionContext::ShouldStop() const
	{
		if (CancellationCheckpointReporter)
		{
			CancellationCheckpointReporter();
		}
		return Token->ObserveRequest() || IsDeadlineExceeded();
	}

	bool FMcpTaskExecutionContext::IsCancellationRequested() const
	{
		if (CancellationCheckpointReporter)
		{
			CancellationCheckpointReporter();
		}
		return Token->ObserveRequest();
	}

	FString FMcpTaskExecutionContext::GetCancellationReason() const
	{
		return Token->GetReason();
	}

	void FMcpTaskExecutionContext::ReportProgress(const double Fraction, FString Message) const
	{
		if (ProgressReporter)
		{
			ProgressReporter(Fraction, MoveTemp(Message));
		}
	}

	void FMcpTaskExecutionContext::EnterWaiting() const
	{
		if (WaitingReporter)
		{
			WaitingReporter(true);
		}
	}

	void FMcpTaskExecutionContext::ResumeRunning() const
	{
		if (WaitingReporter)
		{
			WaitingReporter(false);
		}
	}

	bool FMcpTaskExecutionContext::IsCompensationEnabled() const
	{
		return static_cast<bool>(CompensationRegistrar);
	}

	bool FMcpTaskExecutionContext::RegisterCompensation(FString Description, FMcpCompensationHandler Handler, FString& OutError) const
	{
		OutError.Reset();
		Description.TrimStartAndEndInline();
		if (Description.IsEmpty())
		{
			OutError = TEXT("补偿动作描述不能为空。");
			return false;
		}
		if (!Handler)
		{
			OutError = TEXT("补偿动作处理器不能为空。");
			return false;
		}
		if (!CompensationRegistrar)
		{
			OutError = TEXT("当前工具调用未启用 Compensating 事务。");
			return false;
		}
		return CompensationRegistrar(MoveTemp(Description), MoveTemp(Handler), OutError);
	}

	void FMcpTaskExecutionContext::SetCompensationRegistrar(FMcpCompensationRegistrar InRegistrar)
	{
		CompensationRegistrar = MoveTemp(InRegistrar);
	}

	FMcpCompensationMetricsReporter FMcpTaskExecutionContext::GetCompensationMetricsReporter() const
	{
		return CompensationMetricsReporter;
	}

	FMcpDynamicToolHandler BindInvocationContext(FMcpContextualToolHandler Handler)
	{
		if (!Handler)
		{
			return FMcpDynamicToolHandler();
		}
		return [Handler = MoveTemp(Handler)](const TSharedPtr<FJsonObject>& Arguments)
		{
			const FMcpTaskExecutionContext* Context = FMcpTaskExecutionContext::GetCurrent();
			if (!Context)
			{
				return FString(TEXT("{\"success\":false,"
									"\"error\":\"invocation_context_unavailable\"}"));
			}
			return Handler(Arguments, *Context);
		};
	}

	FMcpTaskWorkResult FMcpTaskWorkResult::Succeeded(FString ValueJson)
	{
		FMcpTaskWorkResult Result;
		Result.bSucceeded = true;
		Result.ValueJson = MoveTemp(ValueJson);
		return Result;
	}

	FMcpTaskWorkResult FMcpTaskWorkResult::Failed(FString Error)
	{
		FMcpTaskWorkResult Result;
		Result.Error = MoveTemp(Error);
		return Result;
	}

	FMcpTaskPrepareResult FMcpTaskPrepareResult::Succeeded()
	{
		FMcpTaskPrepareResult Result;
		Result.bSucceeded = true;
		return Result;
	}

	FMcpTaskPrepareResult FMcpTaskPrepareResult::Failed(FString Error)
	{
		FMcpTaskPrepareResult Result;
		Result.Error = MoveTemp(Error);
		return Result;
	}

	FMcpTaskStepResult FMcpTaskStepResult::Continue()
	{
		return FMcpTaskStepResult();
	}

	FMcpTaskStepResult FMcpTaskStepResult::Succeeded(FString ValueJson)
	{
		FMcpTaskStepResult Result;
		Result.State = EMcpTaskStepState::Succeeded;
		Result.ValueJson = MoveTemp(ValueJson);
		return Result;
	}

	FMcpTaskStepResult FMcpTaskStepResult::Failed(FString Error)
	{
		FMcpTaskStepResult Result;
		Result.State = EMcpTaskStepState::Failed;
		Result.Error = MoveTemp(Error);
		return Result;
	}

	FMcpTaskStepResult IMcpTaskStepper::Abort(FMcpTaskExecutionContext& Context, FString Reason)
	{
		(void)Context;
		return FMcpTaskStepResult::Failed(MoveTemp(Reason));
	}

	bool IMcpTaskStepper::HasWorkerPreparation() const
	{
		return false;
	}

	FMcpTaskPrepareResult IMcpTaskStepper::PrepareOnWorker(FMcpTaskExecutionContext& Context)
	{
		(void)Context;
		return FMcpTaskPrepareResult::Succeeded();
	}

	FMcpTaskManager::FMcpTaskManager(const FTimespan InRetention, FMcpTaskSnapshotSink InSnapshotSink)
		: SharedState(MakeShared<FSharedState, ESPMode::ThreadSafe>(InRetention, MoveTemp(InSnapshotSink)))
	{
	}

	FMcpTaskManager::~FMcpTaskManager()
	{
		const bool bWasDrained = ShutdownAndWait(TEXT("任务管理器已销毁。"), FTimespan::FromSeconds(30));
		ensureAlwaysMsgf(bWasDrained, TEXT("MCP task manager was destroyed while dispatched work was still running."));
	}

	FMcpTaskExecutionContext* FMcpTaskManager::StartTask(const TSharedRef<FSharedState, ESPMode::ThreadSafe>& State, const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>& Record)
	{
		FScopeLock Lock(&State->Mutex);
		const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* Found = State->Tasks.Find(Record->Snapshot.Id);
		if (!Found || (*Found)->Snapshot.IsTerminal() || State->bStopping)
		{
			return nullptr;
		}
		if (Record->ExecutionContext.IsValid())
		{
			return Record->ExecutionContext.Get();
		}

		const FDateTime StartedAt = FDateTime::UtcNow();
		if (Record->Snapshot.DeadlineAt != FDateTime() && StartedAt >= Record->Snapshot.DeadlineAt)
		{
			Record->Token->Request(TEXT("任务在开始执行前已经超时。"));
			Record->Snapshot.bCancellationRequested = true;
			Record->Snapshot.Error = TEXT("任务在开始执行前已经超时。");
			Record->Snapshot.SideEffectState = TEXT("not_started");
			Record->Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::Completed;
			Record->Snapshot.CompletedAt = StartedAt;
			AppendTransition(Record->Snapshot, EMcpTaskState::TimedOut, StartedAt, State->SnapshotSink);
			return nullptr;
		}

		Record->Snapshot.StartedAt = StartedAt;
		Record->Snapshot.ExecutionPhase = Record->bHasWorkerPreparation
			? EMcpTaskExecutionPhase::WorkerPrepare
			: (Record->Stepper.IsValid() ? EMcpTaskExecutionPhase::GameThreadApply : EMcpTaskExecutionPhase::DirectExecution);
		Record->Snapshot.SideEffectState = Record->bHasWorkerPreparation ? TEXT("not_started") : TEXT("running");
		AppendTransition(Record->Snapshot, EMcpTaskState::Running, StartedAt, State->SnapshotSink);
		const FGuid TaskId = Record->Snapshot.Id;
		const TWeakPtr<FSharedState, ESPMode::ThreadSafe> WeakState = State;
		Record->ExecutionContext = TUniquePtr<FMcpTaskExecutionContext>(new FMcpTaskExecutionContext(
			TaskId, Record->Snapshot.TraceId, Record->RequestId, MoveTemp(Record->ProtocolRequestId), MoveTemp(Record->ClientId), MoveTemp(Record->SessionId),
			MoveTemp(Record->Source), Record->Snapshot.DeadlineAt, Record->Token,
			[WeakState, TaskId](const double Fraction, FString Message)
			{
				const TSharedPtr<FSharedState, ESPMode::ThreadSafe> State = WeakState.Pin();
				if (!State.IsValid())
				{
					return;
				}
				FScopeLock ProgressLock(&State->Mutex);
				const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* ProgressRecord = State->Tasks.Find(TaskId);
				if (!ProgressRecord || (*ProgressRecord)->Snapshot.IsTerminal())
				{
					return;
				}
				FMcpTaskProgress& Progress = (*ProgressRecord)->Snapshot.Progress;
				Progress.Fraction = FMath::Clamp(Fraction, 0.0, 1.0);
				Progress.Message = MoveTemp(Message);
				++Progress.Sequence;
				Progress.UpdatedAt = FDateTime::UtcNow();
			},
			[WeakState, TaskId](const bool bWaiting)
			{
				const TSharedPtr<FSharedState, ESPMode::ThreadSafe> State = WeakState.Pin();
				if (!State.IsValid())
				{
					return;
				}
				FScopeLock WaitingLock(&State->Mutex);
				const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* WaitingRecord = State->Tasks.Find(TaskId);
				if (!WaitingRecord || (*WaitingRecord)->Snapshot.IsTerminal())
				{
					return;
				}
				const EMcpTaskState Desired = bWaiting ? EMcpTaskState::Waiting : EMcpTaskState::Running;
				if ((*WaitingRecord)->Snapshot.State != Desired)
				{
					AppendTransition((*WaitingRecord)->Snapshot, Desired, FDateTime::UtcNow(), State->SnapshotSink);
				}
			},
			[WeakState, TaskId]()
			{
				const TSharedPtr<FSharedState, ESPMode::ThreadSafe> State = WeakState.Pin();
				if (!State.IsValid())
				{
					return;
				}
				FScopeLock MetricsLock(&State->Mutex);
				const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* MetricsRecord = State->Tasks.Find(TaskId);
				if (MetricsRecord)
				{
					++(*MetricsRecord)->Snapshot.CancellationCheckpointCount;
				}
			},
			[WeakState, TaskId](const int64 RegisteredDelta, const int64 ExecutedDelta, const int64 FailureDelta, const double DurationMs)
			{
				const TSharedPtr<FSharedState, ESPMode::ThreadSafe> State = WeakState.Pin();
				if (!State.IsValid())
				{
					return;
				}
				FScopeLock MetricsLock(&State->Mutex);
				const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* MetricsRecord = State->Tasks.Find(TaskId);
				if (!MetricsRecord)
				{
					return;
				}
				FMcpTaskSnapshot& Metrics = (*MetricsRecord)->Snapshot;
				Metrics.CompensationRegisteredCount += RegisteredDelta;
				Metrics.CompensationExecutedCount += ExecutedDelta;
				Metrics.CompensationFailureCount += FailureDelta;
				Metrics.CompensationDurationMs += FMath::Max(0.0, DurationMs);
			}));
		return Record->ExecutionContext.Get();
	}

	void FMcpTaskManager::CompleteTask(const TSharedRef<FSharedState, ESPMode::ThreadSafe>& State, const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>& Record,
		FMcpTaskWorkResult WorkResult)
	{
		FScopeLock Lock(&State->Mutex);
		const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* Found = State->Tasks.Find(Record->Snapshot.Id);
		if (!Found)
		{
			return;
		}

		FMcpTaskSnapshot& Snapshot = (*Found)->Snapshot;
		if (Snapshot.IsTerminal())
		{
			if (Snapshot.State == EMcpTaskState::TimedOut)
			{
				Snapshot.SideEffectState =
					WorkResult.bSucceeded ? TEXT("completed_after_timeout") : ((*Found)->Token->WasObserved() ? TEXT("stopped_after_timeout") : TEXT("failed_after_timeout"));
			}
			else if (Snapshot.State == EMcpTaskState::Cancelled)
			{
				Snapshot.SideEffectState = WorkResult.bSucceeded ? TEXT("completed_after_cancellation")
																 : ((*Found)->Token->WasObserved() ? TEXT("stopped_after_cancellation") : TEXT("failed_after_cancellation"));
			}
			return;
		}

		const FDateTime CompletedAt = FDateTime::UtcNow();
		Snapshot.CompletedAt = CompletedAt;
		Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::Completed;
		if (Snapshot.DeadlineAt != FDateTime() && CompletedAt >= Snapshot.DeadlineAt)
		{
			(*Found)->Token->Request(TEXT("任务执行超时。"));
			Snapshot.bCancellationRequested = true;
			Snapshot.Error = TEXT("任务执行超时。");
			Snapshot.SideEffectState = TEXT("completed_after_timeout");
			AppendTransition(Snapshot, EMcpTaskState::TimedOut, CompletedAt, State->SnapshotSink);
		}
		else if ((*Found)->Token->IsRequested() && (*Found)->Token->WasObserved())
		{
			Snapshot.bCancellationRequested = true;
			Snapshot.Error = (*Found)->Token->GetReason();
			Snapshot.SideEffectState = TEXT("stopped");
			AppendTransition(Snapshot, EMcpTaskState::Cancelled, CompletedAt, State->SnapshotSink);
		}
		else if (WorkResult.bSucceeded)
		{
			Snapshot.ValueJson = MoveTemp(WorkResult.ValueJson);
			Snapshot.Progress.Fraction = 1.0;
			if ((*Found)->Token->IsRequested())
			{
				Snapshot.bCancellationRequested = true;
				Snapshot.bCancellationDeferred = true;
				Snapshot.Error = (*Found)->Token->GetReason();
				Snapshot.SideEffectState = TEXT("completed_after_cancellation");
			}
			else
			{
				Snapshot.SideEffectState = TEXT("completed");
			}
			AppendTransition(Snapshot, EMcpTaskState::Completed, CompletedAt, State->SnapshotSink);
		}
		else
		{
			Snapshot.ValueJson = MoveTemp(WorkResult.ValueJson);
			Snapshot.Error = MoveTemp(WorkResult.Error);
			if ((*Found)->Token->IsRequested())
			{
				Snapshot.bCancellationRequested = true;
				Snapshot.bCancellationDeferred = true;
				Snapshot.SideEffectState = TEXT("failed_after_cancellation");
			}
			else
			{
				Snapshot.SideEffectState = TEXT("failed");
			}
			AppendTransition(Snapshot, EMcpTaskState::Failed, CompletedAt, State->SnapshotSink);
		}
	}

	void FMcpTaskManager::RecordApplyStepMetrics(const TSharedRef<FSharedState, ESPMode::ThreadSafe>& State, const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>& Record,
		const double DurationMs, const double BudgetMs)
	{
		FScopeLock Lock(&State->Mutex);
		FMcpTaskSnapshot& Snapshot = Record->Snapshot;
		++Snapshot.StepCount;
		Snapshot.ApplyStepBudgetMs = FMath::Max(0.0, BudgetMs);
		Snapshot.TotalApplyDurationMs += FMath::Max(0.0, DurationMs);
		Snapshot.LastStepDurationMs = DurationMs;
		Snapshot.MaxStepDurationMs = FMath::Max(Snapshot.MaxStepDurationMs, DurationMs);
		if (BudgetMs > 0.0 && DurationMs > BudgetMs)
		{
			++Snapshot.BudgetOverrunCount;
		}

		constexpr int32 MaximumMetricSamples = 512;
		if (Record->ApplyStepDurationSamplesMs.Num() == MaximumMetricSamples)
		{
			Record->ApplyStepDurationSamplesMs.RemoveAt(0, 1, EAllowShrinking::No);
		}
		Record->ApplyStepDurationSamplesMs.Add(DurationMs);
		TArray<double> SortedSamples = Record->ApplyStepDurationSamplesMs;
		SortedSamples.Sort();
		Snapshot.ApplyStepP95DurationMs = CalculateNearestRankPercentileFromSorted(SortedSamples, 0.95);
		Snapshot.ApplyStepP99DurationMs = CalculateNearestRankPercentileFromSorted(SortedSamples, 0.99);
	}

	bool FMcpTaskManager::Submit(FMcpTaskRequest Request, FGuid& OutTaskId, FString& OutError)
	{
		OutTaskId.Invalidate();
		OutError.Reset();
		if (Request.ToolName.IsEmpty())
		{
			OutError = TEXT("任务工具名不能为空。");
			return false;
		}
		if (!Request.Work && !Request.Stepper.IsValid())
		{
			OutError = TEXT("任务工作函数和步骤执行器不能同时为空。");
			return false;
		}
		if (Request.Work && Request.Stepper.IsValid())
		{
			OutError = TEXT("任务只能使用工作函数或步骤执行器中的一种。");
			return false;
		}
		if (Request.Stepper.IsValid() && Request.ThreadPolicy != EMcpToolThreadPolicy::StagedGameThread)
		{
			OutError = TEXT("步骤执行器只能使用 StagedGameThread 调度策略。");
			return false;
		}

		const FDateTime Now = FDateTime::UtcNow();
		TSharedRef<FTaskRecord, ESPMode::ThreadSafe> Record = MakeShared<FTaskRecord, ESPMode::ThreadSafe>();
		do
		{
			Record->Snapshot.Id = FGuid::NewGuid();
		} while (!Record->Snapshot.Id.IsValid());
		Record->Snapshot.ToolName = MoveTemp(Request.ToolName);
		Record->Snapshot.Owner = MoveTemp(Request.Owner);
		Record->Snapshot.bCancelable = Request.bCancelable;
		Record->Snapshot.bResumable = Request.Stepper.IsValid();
		Record->Snapshot.CreatedAt = Now;
		Record->RequestId = Request.RequestId.IsValid() ? Request.RequestId : FGuid::NewGuid();
		Record->Snapshot.RequestId = Record->RequestId;
		Record->Snapshot.TraceId = Request.TraceId.IsValid() ? Request.TraceId : Record->RequestId;
		Record->Snapshot.ProtocolRequestId = MoveTemp(Request.ProtocolRequestId);
		Record->Snapshot.ClientId = MoveTemp(Request.ClientId);
		Record->Snapshot.SessionId = MoveTemp(Request.SessionId);
		Record->Snapshot.Source = MoveTemp(Request.Source);
		Record->Snapshot.Provider = MoveTemp(Request.Provider);
		Record->ProtocolRequestId = Record->Snapshot.ProtocolRequestId;
		Record->ClientId = Record->Snapshot.ClientId;
		Record->SessionId = Record->Snapshot.SessionId;
		Record->Source = Record->Snapshot.Source;
		Record->Work = MoveTemp(Request.Work);
		Record->Stepper = MoveTemp(Request.Stepper);
		Record->bHasWorkerPreparation = Record->Stepper.IsValid() && Record->Stepper->HasWorkerPreparation();
		Record->StepFrameBudget = FTimespan::FromMilliseconds(FMath::Clamp(Request.StepFrameBudget.GetTotalMilliseconds(), 0.25, 16.0));
		Record->Snapshot.ApplyStepBudgetMs = Record->StepFrameBudget.GetTotalMilliseconds();
		if (Request.Timeout > FTimespan::Zero())
		{
			Record->Snapshot.DeadlineAt = Now + Request.Timeout;
		}
		if (!AppendTransition(Record->Snapshot, EMcpTaskState::Received, Now, SharedState->SnapshotSink) ||
			!AppendTransition(Record->Snapshot, EMcpTaskState::Validating, Now, SharedState->SnapshotSink) ||
			!AppendTransition(Record->Snapshot, EMcpTaskState::Authorizing, Now, SharedState->SnapshotSink) ||
			!AppendTransition(Record->Snapshot, EMcpTaskState::Queued, Now, SharedState->SnapshotSink))
		{
			OutError = TEXT("Durable task evidence is unavailable; task submission was denied.");
			return false;
		}

		{
			FScopeLock Lock(&SharedState->Mutex);
			if (SharedState->bStopping)
			{
				OutError = TEXT("任务管理器正在停止，不能继续提交。");
				return false;
			}
			while (SharedState->Tasks.Contains(Record->Snapshot.Id))
			{
				Record->Snapshot.Id = FGuid::NewGuid();
			}
			SharedState->Tasks.Add(Record->Snapshot.Id, Record);
		}
		OutTaskId = Record->Snapshot.Id;

		const TSharedRef<FSharedState, ESPMode::ThreadSafe> State = SharedState;
		TFunction<void()> Run = [State, Record]()
		{
			FMcpTaskExecutionContext* Context = StartTask(State, Record);
			if (!Context || !Record->Work)
			{
				return;
			}

			TGuardValue<const FMcpTaskExecutionContext*> ContextGuard(GCurrentTaskExecutionContext, Context);
			const double StartSeconds = FPlatformTime::Seconds();
			FMcpTaskWorkResult WorkResult = Record->Work(*Context);
			const double DurationMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			RecordApplyStepMetrics(State, Record, DurationMs, IsInGameThread() ? 16.0 : 0.0);
			if (IsInGameThread() && DurationMs > 16.0)
			{
				UE_LOG(LogUnrealAgentMCPTaskManager, Warning, TEXT("Monolithic GameThread MCP task '%s' blocked one frame for %.2f ms."), *Record->Snapshot.ToolName, DurationMs);
			}
			CompleteTask(State, Record, MoveTemp(WorkResult));
			Record->Work = FMcpTaskWork();
			Record->ExecutionContext.Reset();
		};
		const auto MakeTrackedRun = [State](TFunction<void()> InRun)
		{
			const TSharedRef<FSharedState::FDispatchLease, ESPMode::ThreadSafe> Lease = MakeShared<FSharedState::FDispatchLease, ESPMode::ThreadSafe>(State);
			return TFunction<void()>(
				[Run = MoveTemp(InRun), Lease]() mutable
				{
					Run();
				});
		};
		const auto TrackTickerHandle = [State](const FTSTicker::FDelegateHandle Handle)
		{
			bool bShouldRemove = false;
			{
				FScopeLock Lock(&State->Mutex);
				bShouldRemove = State->bStopping;
				if (!bShouldRemove && Handle.IsValid())
				{
					State->TickerHandles.Add(Handle);
				}
			}
			if (bShouldRemove && Handle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(Handle);
			}
		};
		switch (Request.ThreadPolicy)
		{
		case EMcpToolThreadPolicy::Inline:
			MakeTrackedRun(MoveTemp(Run))();
			break;
		case EMcpToolThreadPolicy::GameThread:
			if (IsInGameThread())
			{
				MakeTrackedRun(MoveTemp(Run))();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MakeTrackedRun(MoveTemp(Run)));
			}
			break;
		case EMcpToolThreadPolicy::StagedGameThread:
		{
			if (!Record->Stepper.IsValid())
			{
				const FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
					[State, Run = MoveTemp(Run)](const float DeltaTime) mutable
					{
						(void)DeltaTime;
						const TSharedRef<FSharedState::FDispatchLease, ESPMode::ThreadSafe> Lease = MakeShared<FSharedState::FDispatchLease, ESPMode::ThreadSafe>(State);
						Run();
						return false;
					}));
				TrackTickerHandle(Handle);
				break;
			}

			if (Record->bHasWorkerPreparation)
			{
				Async(EAsyncExecution::ThreadPool,
					MakeTrackedRun(
						[State, Record]()
						{
							FMcpTaskPrepareResult PrepareResult;
							FMcpTaskExecutionContext* Context = StartTask(State, Record);
							const double PrepareStartSeconds = FPlatformTime::Seconds();
							if (!Context)
							{
								PrepareResult = FMcpTaskPrepareResult::Failed(TEXT("Task stopped before worker preparation."));
							}
							else
							{
								TGuardValue<const FMcpTaskExecutionContext*> ContextGuard(GCurrentTaskExecutionContext, Context);
								if (Context->ShouldStop())
								{
									PrepareResult = FMcpTaskPrepareResult::Failed(
										Context->IsDeadlineExceeded() ? TEXT("Task deadline exceeded before worker preparation.") : Context->GetCancellationReason());
								}
								else
								{
									PrepareResult = Record->Stepper->PrepareOnWorker(*Context);
									if (PrepareResult.bSucceeded && Context->ShouldStop())
									{
										PrepareResult = FMcpTaskPrepareResult::Failed(
											Context->IsDeadlineExceeded() ? TEXT("Task deadline exceeded during worker preparation.") : Context->GetCancellationReason());
									}
								}
							}

							const double PrepareDurationMs = (FPlatformTime::Seconds() - PrepareStartSeconds) * 1000.0;
							FScopeLock Lock(&State->Mutex);
							Record->Snapshot.PrepareDurationMs = PrepareDurationMs;
							Record->PreparationResult = MoveTemp(PrepareResult);
							Record->bPreparationFinished = true;
							if (Record->PreparationResult.bSucceeded && !Record->Snapshot.IsTerminal())
							{
								Record->Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::GameThreadApply;
							}
						}));
			}

			const TWeakPtr<FSharedState, ESPMode::ThreadSafe> WeakStepperState = State;
			const TWeakPtr<FTaskRecord, ESPMode::ThreadSafe> WeakStepperRecord = Record;
			const FTSTicker::FDelegateHandle StepperHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[WeakStepperState, WeakStepperRecord](const float DeltaTime) mutable
				{
					(void)DeltaTime;
					const TSharedPtr<FSharedState, ESPMode::ThreadSafe> State = WeakStepperState.Pin();
					const TSharedPtr<FTaskRecord, ESPMode::ThreadSafe> Record = WeakStepperRecord.Pin();
					if (!State.IsValid() || !Record.IsValid())
					{
						return false;
					}
					const TSharedRef<FSharedState, ESPMode::ThreadSafe> StateRef = State.ToSharedRef();
					const TSharedRef<FTaskRecord, ESPMode::ThreadSafe> RecordRef = Record.ToSharedRef();
					const TSharedRef<FSharedState::FDispatchLease, ESPMode::ThreadSafe> Lease = MakeShared<FSharedState::FDispatchLease, ESPMode::ThreadSafe>(StateRef);
					if (Record->bHasWorkerPreparation)
					{
						bool bPreparationFinished = false;
						bool bPreparationSucceeded = false;
						FString PreparationError;
						{
							FScopeLock Lock(&State->Mutex);
							bPreparationFinished = Record->bPreparationFinished;
							if (bPreparationFinished)
							{
								bPreparationSucceeded = Record->PreparationResult.bSucceeded;
								PreparationError = Record->PreparationResult.Error;
							}
						}
						if (!bPreparationFinished)
						{
							return true;
						}
						if (!bPreparationSucceeded)
						{
							FMcpTaskWorkResult WorkResult =
								FMcpTaskWorkResult::Failed(PreparationError.IsEmpty() ? TEXT("Worker preparation failed.") : MoveTemp(PreparationError));
							CompleteTask(StateRef, RecordRef, MoveTemp(WorkResult));
							Record->Stepper.Reset();
							Record->ExecutionContext.Reset();
							return false;
						}
					}

					FMcpTaskExecutionContext* Context = StartTask(StateRef, RecordRef);
					if (!Context)
					{
						Context = Record->ExecutionContext.Get();
						if (Context && Record->Stepper.IsValid())
						{
							Context->ShouldStop();
							FString Reason = Context->IsDeadlineExceeded() ? TEXT("任务执行超时。") : Context->GetCancellationReason();
							if (Reason.IsEmpty())
							{
								Reason = TEXT("任务已停止。");
							}
							TGuardValue<const FMcpTaskExecutionContext*> ContextGuard(GCurrentTaskExecutionContext, Context);
							const double AbortStartSeconds = FPlatformTime::Seconds();
							FMcpTaskStepResult AbortResult = Record->Stepper->Abort(*Context, MoveTemp(Reason));
							const double AbortDurationMs = (FPlatformTime::Seconds() - AbortStartSeconds) * 1000.0;
							RecordApplyStepMetrics(StateRef, RecordRef, AbortDurationMs, Record->StepFrameBudget.GetTotalMilliseconds());
							if (AbortResult.State == EMcpTaskStepState::Continue)
							{
								return true;
							}
							FMcpTaskWorkResult WorkResult;
							WorkResult.bSucceeded = AbortResult.State == EMcpTaskStepState::Succeeded;
							WorkResult.ValueJson = MoveTemp(AbortResult.ValueJson);
							WorkResult.Error = MoveTemp(AbortResult.Error);
							CompleteTask(StateRef, RecordRef, MoveTemp(WorkResult));
						}
						Record->Stepper.Reset();
						Record->ExecutionContext.Reset();
						return false;
					}

					TGuardValue<const FMcpTaskExecutionContext*> ContextGuard(GCurrentTaskExecutionContext, Context);
					{
						FScopeLock Lock(&State->Mutex);
						Record->Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::GameThreadApply;
						Record->Snapshot.SideEffectState = TEXT("running");
					}
					FMcpTaskStepResult StepResult;
					const double StepStartSeconds = FPlatformTime::Seconds();
					if (Context->ShouldStop())
					{
						FString Reason = Context->IsDeadlineExceeded() ? TEXT("任务执行超时。") : Context->GetCancellationReason();
						StepResult = Record->Stepper->Abort(*Context, MoveTemp(Reason));
					}
					else
					{
						StepResult = Record->Stepper->Step(*Context, Record->StepFrameBudget);
					}
					const double StepDurationMs = (FPlatformTime::Seconds() - StepStartSeconds) * 1000.0;
					const double StepBudgetMs = Record->StepFrameBudget.GetTotalMilliseconds();
					RecordApplyStepMetrics(StateRef, RecordRef, StepDurationMs, StepBudgetMs);
					if (StepDurationMs > StepBudgetMs)
					{
						UE_LOG(LogUnrealAgentMCPTaskManager, Warning, TEXT("Resumable MCP task '%s' exceeded its %.2f ms frame budget with a %.2f ms step."),
							*Record->Snapshot.ToolName, StepBudgetMs, StepDurationMs);
					}
					if (StepResult.State == EMcpTaskStepState::Continue)
					{
						return true;
					}

					FMcpTaskWorkResult WorkResult;
					{
						FScopeLock Lock(&State->Mutex);
						Record->Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::Finalizing;
					}
					WorkResult.bSucceeded = StepResult.State == EMcpTaskStepState::Succeeded;
					WorkResult.ValueJson = MoveTemp(StepResult.ValueJson);
					WorkResult.Error = MoveTemp(StepResult.Error);
					CompleteTask(StateRef, RecordRef, MoveTemp(WorkResult));
					Record->Stepper.Reset();
					Record->ExecutionContext.Reset();
					return false;
				}));
			TrackTickerHandle(StepperHandle);
			break;
		}
		case EMcpToolThreadPolicy::LongRunning:
			Async(EAsyncExecution::Thread, MakeTrackedRun(MoveTemp(Run)));
			break;
		case EMcpToolThreadPolicy::BackgroundThread:
		case EMcpToolThreadPolicy::NativeAsync:
		default:
			Async(EAsyncExecution::ThreadPool, MakeTrackedRun(MoveTemp(Run)));
			break;
		}
		return true;
	}

	EMcpTaskCancelResult FMcpTaskManager::Cancel(const FGuid& TaskId, FString Reason)
	{
		FScopeLock Lock(&SharedState->Mutex);
		const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* Found = SharedState->Tasks.Find(TaskId);
		if (!Found)
		{
			return EMcpTaskCancelResult::NotFound;
		}
		FMcpTaskSnapshot& Snapshot = (*Found)->Snapshot;
		if (Snapshot.IsTerminal())
		{
			return EMcpTaskCancelResult::AlreadyFinished;
		}
		if (!Snapshot.bCancelable)
		{
			return EMcpTaskCancelResult::NotCancelable;
		}
		(*Found)->Token->Request(MoveTemp(Reason));
		Snapshot.bCancellationRequested = true;
		if (Snapshot.State == EMcpTaskState::Received || Snapshot.State == EMcpTaskState::Validating || Snapshot.State == EMcpTaskState::Authorizing ||
			Snapshot.State == EMcpTaskState::Queued)
		{
			Snapshot.Error = (*Found)->Token->GetReason();
			Snapshot.SideEffectState = TEXT("not_started");
			Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::Completed;
			Snapshot.CompletedAt = FDateTime::UtcNow();
			AppendTransition(Snapshot, EMcpTaskState::Cancelled, Snapshot.CompletedAt, SharedState->SnapshotSink);
			return EMcpTaskCancelResult::CancelledBeforeStart;
		}
		Snapshot.SideEffectState = TEXT("cancellation_requested");
		if (SharedState->SnapshotSink)
		{
			FString PersistenceError;
			SharedState->SnapshotSink(Snapshot, PersistenceError);
		}
		return EMcpTaskCancelResult::CancellationRequested;
	}

	int32 FMcpTaskManager::CancelByClientId(const FString& ClientId, FString Reason)
	{
		if (ClientId.IsEmpty())
		{
			return 0;
		}
		if (Reason.IsEmpty())
		{
			Reason = TEXT("The owning MCP client disconnected.");
		}

		FScopeLock Lock(&SharedState->Mutex);
		const FDateTime Now = FDateTime::UtcNow();
		int32 CancelledCount = 0;
		for (TPair<FGuid, TSharedRef<FTaskRecord, ESPMode::ThreadSafe>>& Pair : SharedState->Tasks)
		{
			FTaskRecord& Record = Pair.Value.Get();
			if (Record.ClientId != ClientId || Record.Snapshot.IsTerminal())
			{
				continue;
			}
			Record.Token->Request(Reason);
			Record.Snapshot.bCancellationRequested = true;
			Record.Snapshot.bCancellationDeferred = Record.Snapshot.State == EMcpTaskState::Running || Record.Snapshot.State == EMcpTaskState::Waiting;
			Record.Snapshot.SideEffectState = Record.Snapshot.bCancellationDeferred ? TEXT("may_continue") : TEXT("not_started");
			Record.Snapshot.Error = Reason;
			if (!Record.Snapshot.bCancellationDeferred)
			{
				Record.Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::Completed;
				Record.Snapshot.CompletedAt = Now;
				AppendTransition(Record.Snapshot, EMcpTaskState::Cancelled, Now, SharedState->SnapshotSink);
			}
			++CancelledCount;
		}
		return CancelledCount;
	}

	bool FMcpTaskManager::TryRead(const FGuid& TaskId, FMcpTaskSnapshot& OutSnapshot, const bool bConsumeTerminal) const
	{
		FScopeLock Lock(&SharedState->Mutex);
		const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>* Found = SharedState->Tasks.Find(TaskId);
		if (!Found)
		{
			return false;
		}
		OutSnapshot = (*Found)->Snapshot;
		if (bConsumeTerminal && OutSnapshot.IsTerminal())
		{
			SharedState->Tasks.Remove(TaskId);
		}
		return true;
	}

	TArray<FMcpTaskSnapshot> FMcpTaskManager::List() const
	{
		TArray<FMcpTaskSnapshot> Snapshots;
		FScopeLock Lock(&SharedState->Mutex);
		Snapshots.Reserve(SharedState->Tasks.Num());
		for (const TPair<FGuid, TSharedRef<FTaskRecord, ESPMode::ThreadSafe>>& Pair : SharedState->Tasks)
		{
			Snapshots.Add(Pair.Value->Snapshot);
		}
		Snapshots.Sort(
			[](const FMcpTaskSnapshot& Left, const FMcpTaskSnapshot& Right)
			{
				if (Left.CreatedAt == Right.CreatedAt)
				{
					return Left.Id.ToString() < Right.Id.ToString();
				}
				return Left.CreatedAt < Right.CreatedAt;
			});
		return Snapshots;
	}

	int32 FMcpTaskManager::Tick(const FDateTime Now)
	{
		FScopeLock Lock(&SharedState->Mutex);
		int32 TimedOutCount = 0;
		for (TPair<FGuid, TSharedRef<FTaskRecord, ESPMode::ThreadSafe>>& Pair : SharedState->Tasks)
		{
			FTaskRecord& Record = Pair.Value.Get();
			FMcpTaskSnapshot& Snapshot = Record.Snapshot;
			if (Snapshot.IsTerminal() || Snapshot.DeadlineAt == FDateTime() || Now < Snapshot.DeadlineAt)
			{
				continue;
			}
			Record.Token->Request(TEXT("任务执行超时。"));
			Snapshot.bCancellationRequested = true;
			Snapshot.Error = TEXT("任务执行超时。");
			Snapshot.bCancellationDeferred = Snapshot.State == EMcpTaskState::Running || Snapshot.State == EMcpTaskState::Waiting;
			Snapshot.SideEffectState = Snapshot.bCancellationDeferred ? TEXT("may_continue") : TEXT("not_started");
			Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::Completed;
			Snapshot.CompletedAt = Now;
			AppendTransition(Snapshot, EMcpTaskState::TimedOut, Now, SharedState->SnapshotSink);
			++TimedOutCount;
		}
		return TimedOutCount;
	}

	int32 FMcpTaskManager::PurgeExpired(const FDateTime Now)
	{
		FScopeLock Lock(&SharedState->Mutex);
		int32 RemovedCount = 0;
		for (auto It = SharedState->Tasks.CreateIterator(); It; ++It)
		{
			const FMcpTaskSnapshot& Snapshot = It.Value()->Snapshot;
			if (Snapshot.IsTerminal() && Snapshot.CompletedAt != FDateTime() && Now - Snapshot.CompletedAt >= SharedState->Retention)
			{
				It.RemoveCurrent();
				++RemovedCount;
			}
		}
		return RemovedCount;
	}

	void FMcpTaskManager::Shutdown(FString Reason)
	{
		if (Reason.IsEmpty())
		{
			Reason = TEXT("任务管理器正在停止。");
		}
		TArray<FTSTicker::FDelegateHandle> TickerHandles;
		{
			FScopeLock Lock(&SharedState->Mutex);
			if (SharedState->bStopping)
			{
				return;
			}
			SharedState->bStopping = true;
			TickerHandles = MoveTemp(SharedState->TickerHandles);
			const FDateTime Now = FDateTime::UtcNow();
			for (TPair<FGuid, TSharedRef<FTaskRecord, ESPMode::ThreadSafe>>& Pair : SharedState->Tasks)
			{
				FTaskRecord& Record = Pair.Value.Get();
				if (Record.Snapshot.IsTerminal())
				{
					continue;
				}
				Record.Token->Request(Reason);
				Record.Snapshot.bCancellationRequested = true;
				Record.Snapshot.bCancellationDeferred = Record.Snapshot.State == EMcpTaskState::Running || Record.Snapshot.State == EMcpTaskState::Waiting;
				Record.Snapshot.SideEffectState = Record.Snapshot.bCancellationDeferred ? TEXT("may_continue") : TEXT("not_started");
				Record.Snapshot.ExecutionPhase = EMcpTaskExecutionPhase::Completed;
				Record.Snapshot.Error = Reason;
				Record.Snapshot.CompletedAt = Now;
				AppendTransition(Record.Snapshot, EMcpTaskState::Cancelled, Now, SharedState->SnapshotSink);
			}
		}
		for (const FTSTicker::FDelegateHandle Handle : TickerHandles)
		{
			if (Handle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(Handle);
			}
		}
	}

	bool FMcpTaskManager::ShutdownAndWait(FString Reason, const FTimespan Timeout)
	{
		Shutdown(MoveTemp(Reason));
		const double DeadlineSeconds = FPlatformTime::Seconds() + FMath::Max(0.0, Timeout.GetTotalSeconds());
		while (true)
		{
			if (IsInGameThread())
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			}

			int32 ActiveDispatchCount = 0;
			{
				FScopeLock Lock(&SharedState->Mutex);
				ActiveDispatchCount = SharedState->ActiveDispatchCount;
			}
			if (ActiveDispatchCount == 0)
			{
				return true;
			}
			const double RemainingSeconds = DeadlineSeconds - FPlatformTime::Seconds();
			if (RemainingSeconds <= 0.0)
			{
				return false;
			}
			const uint32 WaitMilliseconds = static_cast<uint32>(FMath::Clamp(RemainingSeconds * 1000.0, 1.0, 10.0));
			SharedState->DispatchesFinishedEvent->Wait(WaitMilliseconds);
		}
	}

	int32 FMcpTaskManager::Num() const
	{
		FScopeLock Lock(&SharedState->Mutex);
		return SharedState->Tasks.Num();
	}

	FString TaskStateToString(const EMcpTaskState State)
	{
		switch (State)
		{
		case EMcpTaskState::Received:
			return TEXT("Received");
		case EMcpTaskState::Validating:
			return TEXT("Validating");
		case EMcpTaskState::Authorizing:
			return TEXT("Authorizing");
		case EMcpTaskState::Queued:
			return TEXT("Queued");
		case EMcpTaskState::Running:
			return TEXT("Running");
		case EMcpTaskState::Waiting:
			return TEXT("Waiting");
		case EMcpTaskState::Completed:
			return TEXT("Completed");
		case EMcpTaskState::Failed:
			return TEXT("Failed");
		case EMcpTaskState::Cancelled:
			return TEXT("Cancelled");
		case EMcpTaskState::TimedOut:
			return TEXT("TimedOut");
		default:
			return TEXT("Unknown");
		}
	}

	FString TaskExecutionPhaseToString(const EMcpTaskExecutionPhase Phase)
	{
		switch (Phase)
		{
		case EMcpTaskExecutionPhase::Queued:
			return TEXT("Queued");
		case EMcpTaskExecutionPhase::WorkerPrepare:
			return TEXT("WorkerPrepare");
		case EMcpTaskExecutionPhase::GameThreadApply:
			return TEXT("GameThreadApply");
		case EMcpTaskExecutionPhase::DirectExecution:
			return TEXT("DirectExecution");
		case EMcpTaskExecutionPhase::Finalizing:
			return TEXT("Finalizing");
		case EMcpTaskExecutionPhase::Completed:
			return TEXT("Completed");
		default:
			return TEXT("Unknown");
		}
	}

	FString CancelResultToString(const EMcpTaskCancelResult Result)
	{
		switch (Result)
		{
		case EMcpTaskCancelResult::NotFound:
			return TEXT("NotFound");
		case EMcpTaskCancelResult::NotCancelable:
			return TEXT("NotCancelable");
		case EMcpTaskCancelResult::AlreadyFinished:
			return TEXT("AlreadyFinished");
		case EMcpTaskCancelResult::CancelledBeforeStart:
			return TEXT("CancelledBeforeStart");
		case EMcpTaskCancelResult::CancellationRequested:
			return TEXT("CancellationRequested");
		default:
			return TEXT("Unknown");
		}
	}
}
