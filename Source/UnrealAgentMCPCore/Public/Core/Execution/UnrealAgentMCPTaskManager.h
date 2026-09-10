// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPTaskManager.h
 * @brief Unreal Agent 自有任务状态机、进度、取消与超时基础设施。
 */

#include "CoreMinimal.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"
#include "HAL/ThreadSafeBool.h"
#include "Templates/Function.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class FTransactionalTaskStepper;

	/** 工具任务从接收到结束的统一状态。 */
	enum class EMcpTaskState : uint8
	{
		Received,
		Validating,
		Authorizing,
		Queued,
		Running,
		Waiting,
		Completed,
		Failed,
		Cancelled,
		TimedOut
	};

	/** 标识工具调用当前处于数据准备阶段还是编辑器应用阶段。 */
	enum class EMcpTaskExecutionPhase : uint8
	{
		Queued,
		WorkerPrepare,
		GameThreadApply,
		DirectExecution,
		Finalizing,
		Completed
	};

	/** 取消请求的可观察结果。 */
	enum class EMcpTaskCancelResult : uint8
	{
		NotFound,
		NotCancelable,
		AlreadyFinished,
		CancelledBeforeStart,
		CancellationRequested
	};

	/** 单次状态变更记录，用于审计与协议映射。 */
	struct UNREALAGENTMCPCORE_API FMcpTaskTransition
	{
		EMcpTaskState State = EMcpTaskState::Received;
		FDateTime Timestamp;
	};

	/** 任务进度快照。 */
	struct UNREALAGENTMCPCORE_API FMcpTaskProgress
	{
		double Fraction = 0.0;
		FString Message;
		int64 Sequence = 0;
		FDateTime UpdatedAt;
	};

	/** 可并发读取的任务只读快照。 */
	struct UNREALAGENTMCPCORE_API FMcpTaskSnapshot
	{
		FGuid Id;
		FGuid TraceId;
		FGuid RequestId;
		FString ProtocolRequestId;
		FString ClientId;
		FString SessionId;
		FString Source;
		FString Provider;
		FString ToolName;
		FString Owner;
		EMcpTaskState State = EMcpTaskState::Received;
		EMcpTaskExecutionPhase ExecutionPhase = EMcpTaskExecutionPhase::Queued;
		FString ValueJson;
		FString Error;
		bool bCancelable = false;
		bool bResumable = false;
		bool bCancellationRequested = false;
		bool bCancellationDeferred = false;
		FString SideEffectState = TEXT("not_started");
		FDateTime CreatedAt;
		FDateTime StartedAt;
		FDateTime CompletedAt;
		FDateTime DeadlineAt;
		FMcpTaskProgress Progress;
		int64 StepCount = 0;
		double ApplyStepBudgetMs = 0.0;
		double PrepareDurationMs = 0.0;
		double TotalApplyDurationMs = 0.0;
		double LastStepDurationMs = 0.0;
		double MaxStepDurationMs = 0.0;
		double ApplyStepP95DurationMs = 0.0;
		double ApplyStepP99DurationMs = 0.0;
		int64 BudgetOverrunCount = 0;
		int64 CancellationCheckpointCount = 0;
		int64 CompensationRegisteredCount = 0;
		int64 CompensationExecutedCount = 0;
		int64 CompensationFailureCount = 0;
		double CompensationDurationMs = 0.0;
		TArray<FMcpTaskTransition> History;

		bool IsTerminal() const;
		TSharedRef<FJsonObject> ToJsonObject() const;
	};

	/** 在线程之间共享的协作取消令牌。 */
	class UNREALAGENTMCPCORE_API FMcpCancellationToken
	{
	public:
		bool Request(FString Reason = FString());
		bool IsRequested() const;
		bool ObserveRequest();
		bool WasObserved() const;
		FString GetReason() const;

	private:
		FThreadSafeBool bRequested = false;
		FThreadSafeBool bObserved = false;
		mutable FCriticalSection ReasonMutex;
		FString CancellationReason;
	};

	/** 工具失败后执行的单个补偿动作；成功返回 true。 */
	using FMcpCompensationHandler = TFunction<bool(FString& OutError)>;

	/** 将补偿动作登记到本次调用的补偿栈。 */
	using FMcpCompensationRegistrar = TFunction<bool(FString Description, FMcpCompensationHandler Handler, FString& OutError)>;

	/** 报告补偿动作的登记、执行、失败数量和耗时增量。 */
	using FMcpCompensationMetricsReporter = TFunction<void(int64 RegisteredDelta, int64 ExecutedDelta, int64 FailureDelta, double DurationMs)>;

	/**
	 * 任务工作函数可用的执行上下文。
	 * 长任务应定期检查取消，并在阶段边界报告进度。
	 */
	class UNREALAGENTMCPCORE_API FMcpTaskExecutionContext
	{
	public:
		/**
		 * 返回当前工具调用栈绑定的上下文；不在工具调用中时返回 nullptr。
		 * 返回值不能缓存，也不能传递到其他线程或调用结束后继续使用。
		 */
		static const FMcpTaskExecutionContext* GetCurrent();

		const FGuid& GetTaskId() const;
		const FGuid& GetTraceId() const;
		const FGuid& GetRequestId() const;
		const FString& GetProtocolRequestId() const;
		const FString& GetClientId() const;
		const FString& GetSessionId() const;
		const FString& GetSource() const;
		bool HasDeadline() const;
		FDateTime GetDeadline() const;
		TOptional<FTimespan> GetRemainingTime() const;
		bool IsDeadlineExceeded() const;
		bool ShouldStop() const;
		bool IsCancellationRequested() const;
		FString GetCancellationReason() const;
		void ReportProgress(double Fraction, FString Message = FString()) const;
		void EnterWaiting() const;
		void ResumeRunning() const;
		bool IsCompensationEnabled() const;

		/**
		 * 为 Compensating 工具登记失败补偿；调用失败时按登记逆序执行。
		 * 非 Compensating 调用会拒绝登记，避免产生虚假的回滚保证。
		 */
		bool RegisterCompensation(FString Description, FMcpCompensationHandler Handler, FString& OutError) const;

	private:
		friend class FMcpTaskManager;
		friend class FMcpToolExecutionService;
		friend class FTransactionalTaskStepper;

		FMcpTaskExecutionContext(FGuid InTaskId, FGuid InTraceId, FGuid InRequestId, FString InProtocolRequestId, FString InClientId, FString InSessionId, FString InSource,
			FDateTime InDeadline, TSharedRef<FMcpCancellationToken, ESPMode::ThreadSafe> InToken, TFunction<void(double, FString)> InProgressReporter,
			TFunction<void(bool)> InWaitingReporter, TFunction<void()> InCancellationCheckpointReporter, FMcpCompensationMetricsReporter InCompensationMetricsReporter);

		void SetCompensationRegistrar(FMcpCompensationRegistrar InRegistrar);
		FMcpCompensationMetricsReporter GetCompensationMetricsReporter() const;

		FGuid TaskId;
		FGuid TraceId;
		FGuid RequestId;
		FString ProtocolRequestId;
		FString ClientId;
		FString SessionId;
		FString Source;
		FDateTime Deadline;
		TSharedRef<FMcpCancellationToken, ESPMode::ThreadSafe> Token;
		TFunction<void(double, FString)> ProgressReporter;
		TFunction<void(bool)> WaitingReporter;
		TFunction<void()> CancellationCheckpointReporter;
		FMcpCompensationRegistrar CompensationRegistrar;
		FMcpCompensationMetricsReporter CompensationMetricsReporter;
	};

	/** 新工具优先采用的显式上下文处理器签名。 */
	using FMcpContextualToolHandler = TFunction<FString(const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext& Context)>;

	/**
	 * 将显式上下文处理器适配到 Provider API v1 的旧处理器槽位。
	 * 适配不会改变 FMcpToolDescriptor 布局，旧 Provider 无需迁移。
	 */
	UNREALAGENTMCPCORE_API FMcpDynamicToolHandler BindInvocationContext(FMcpContextualToolHandler Handler);

	/** 工作函数的结构化完成结果。 */
	struct UNREALAGENTMCPCORE_API FMcpTaskWorkResult
	{
		bool bSucceeded = false;
		FString ValueJson;
		FString Error;

		static FMcpTaskWorkResult Succeeded(FString ValueJson);
		static FMcpTaskWorkResult Failed(FString Error);
	};

	using FMcpTaskWork = TFunction<FMcpTaskWorkResult(FMcpTaskExecutionContext& Context)>;

	/** 描述工作线程准备阶段的结果；该阶段只允许产生普通 C++ 数据。 */
	struct UNREALAGENTMCPCORE_API FMcpTaskPrepareResult
	{
		bool bSucceeded = false;
		FString Error;

		static FMcpTaskPrepareResult Succeeded();
		static FMcpTaskPrepareResult Failed(FString Error);
	};

	/** 可分片任务单步执行后的状态。 */
	enum class EMcpTaskStepState : uint8
	{
		Continue,
		Succeeded,
		Failed
	};

	/** 可分片任务单步结果；Continue 会在后续 GameThread tick 恢复。 */
	struct UNREALAGENTMCPCORE_API FMcpTaskStepResult
	{
		EMcpTaskStepState State = EMcpTaskStepState::Continue;
		FString ValueJson;
		FString Error;

		static FMcpTaskStepResult Continue();
		static FMcpTaskStepResult Succeeded(FString ValueJson);
		static FMcpTaskStepResult Failed(FString Error);
	};

	/** 跨 GameThread tick 保留状态的任务步骤执行器。 */
	class UNREALAGENTMCPCORE_API IMcpTaskStepper
	{
	public:
		virtual ~IMcpTaskStepper() = default;

		/** 返回 true 时，TaskManager 必须在线程池中执行 PrepareOnWorker。 */
		virtual bool HasWorkerPreparation() const;

		/**
		 * 在线程池中准备普通 C++ 数据。实现不得读取或修改 UObject、Slate、
		 * GEditor、UWorld 以及其他只能由 GameThread 访问的状态。
		 */
		virtual FMcpTaskPrepareResult PrepareOnWorker(FMcpTaskExecutionContext& Context);

		/** 在给定帧预算内推进一次；实现必须主动控制单步耗时。 */
		virtual FMcpTaskStepResult Step(FMcpTaskExecutionContext& Context, FTimespan FrameBudget) = 0;

		/** 任务取消、超时或停止时释放未完成状态。 */
		virtual FMcpTaskStepResult Abort(FMcpTaskExecutionContext& Context, FString Reason);
	};

	/** 提交任务时的不可变调度参数。 */
	struct UNREALAGENTMCPCORE_API FMcpTaskRequest
	{
		FString ToolName;
		FString Owner;
		EMcpToolThreadPolicy ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
		bool bCancelable = false;
		FTimespan Timeout = FTimespan::Zero();
		FMcpTaskWork Work;
		TSharedPtr<IMcpTaskStepper> Stepper;
		FTimespan StepFrameBudget = FTimespan::FromMilliseconds(4);

		/** Preview API 的调用身份字段追加在既有调度字段之后。 */
		FGuid RequestId;
		FGuid TraceId;
		FString ProtocolRequestId;
		FString ClientId = TEXT("internal");
		FString SessionId;
		FString Source = TEXT("Internal");
		FString Provider = TEXT("internal");
	};

	/**
	 * 线程安全的任务管理器。
	 *
	 * 管理器析构后，后台工作仍只持有共享状态，不会回调已销毁对象。
	 */
	using FMcpTaskSnapshotSink = TFunction<bool(const FMcpTaskSnapshot& Snapshot, FString& OutError)>;

	class UNREALAGENTMCPCORE_API FMcpTaskManager
	{
	public:
		explicit FMcpTaskManager(FTimespan InRetention = FTimespan::FromMinutes(10), FMcpTaskSnapshotSink InSnapshotSink = FMcpTaskSnapshotSink());
		~FMcpTaskManager();

		bool Submit(FMcpTaskRequest Request, FGuid& OutTaskId, FString& OutError);
		EMcpTaskCancelResult Cancel(const FGuid& TaskId, FString Reason = FString());
		/** 取消指定已认证客户端拥有的全部非终态任务。 */
		int32 CancelByClientId(const FString& ClientId, FString Reason = FString());
		bool TryRead(const FGuid& TaskId, FMcpTaskSnapshot& OutSnapshot, bool bConsumeTerminal = false) const;
		TArray<FMcpTaskSnapshot> List() const;

		/** 检查截止时间，并把超时请求传递给正在运行的工作函数。 */
		int32 Tick(FDateTime Now = FDateTime::UtcNow());

		/** 清理超过保留期的终态任务。 */
		int32 PurgeExpired(FDateTime Now = FDateTime::UtcNow());

		/** 取消所有未结束任务，并阻止继续提交。 */
		void Shutdown(FString Reason = FString());
		/** 停止调度并等待已进入工作函数的调用退出。 */
		bool ShutdownAndWait(FString Reason = FString(), FTimespan Timeout = FTimespan::FromSeconds(30));
		int32 Num() const;

	private:
		struct FTaskRecord;
		struct FSharedState;

		static FMcpTaskExecutionContext* StartTask(const TSharedRef<FSharedState, ESPMode::ThreadSafe>& State, const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>& Record);
		static void CompleteTask(const TSharedRef<FSharedState, ESPMode::ThreadSafe>& State, const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>& Record,
			FMcpTaskWorkResult WorkResult);
		static void RecordApplyStepMetrics(const TSharedRef<FSharedState, ESPMode::ThreadSafe>& State, const TSharedRef<FTaskRecord, ESPMode::ThreadSafe>& Record,
			double DurationMs, double BudgetMs);

		TSharedRef<FSharedState, ESPMode::ThreadSafe> SharedState;
	};

	UNREALAGENTMCPCORE_API FString TaskStateToString(EMcpTaskState State);
	UNREALAGENTMCPCORE_API FString TaskExecutionPhaseToString(EMcpTaskExecutionPhase Phase);
	UNREALAGENTMCPCORE_API FString CancelResultToString(EMcpTaskCancelResult Result);
}
