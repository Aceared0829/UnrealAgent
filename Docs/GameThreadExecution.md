# Unreal Agent GameThread 执行流水线

UnrealAgent 的 UE 运行时主体继续使用 C++。需要访问 `UObject`、`UWorld`、Slate、
`GEditor` 或编辑器事务的 Apply 必须在 GameThread；纯解析、过滤、规划和普通 C++ 数据
构建应进入 Worker Prepare。Python、Node.js 和 PowerShell 不参与 Editor 热路径。

## 执行相位

```text
Queued
  -> WorkerPrepare      纯数据；线程池；不得触碰 UObject/Slate/GEditor
  -> GameThreadApply    每 Tick 一个有界 Step；默认 4 ms，允许 0.25–16 ms
  -> Finalizing         提交事务或执行逆序补偿
  -> Completed          持久化终态指标
```

没有 Worker Prepare 的现有 Stepper 保持兼容，直接进入 `GameThreadApply`。同步短工具仍可
使用 `DirectExecution`，但任何可能遍历大量对象、等待编译/GPU、写大量资产或执行批处理
的 GameThread Tool 都必须提供真实 Stepper；不得把单体 Handler 伪装成 Task。

## C++ 合同

Stepper 通过 `HasWorkerPreparation()` 声明是否需要准备相位，并在
`PrepareOnWorker()` 中只生成普通 C++ 数据。TaskManager 在 Prepare 成功且再次检查取消/
Deadline 后，才允许调用 `Step()`。

```cpp
class FExampleTaskStepper final : public IMcpTaskStepper
{
public:
	virtual bool HasWorkerPreparation() const override { return true; }

	virtual FMcpTaskPrepareResult PrepareOnWorker(
		FMcpTaskExecutionContext& Context) override
	{
		// 只解析 JSON、规范化字符串、构建值类型计划。
		return Context.ShouldStop()
			? FMcpTaskPrepareResult::Failed(TEXT("cancelled"))
			: FMcpTaskPrepareResult::Succeeded();
	}

	virtual FMcpTaskStepResult Step(
		FMcpTaskExecutionContext& Context,
		FTimespan FrameBudget) override
	{
		// 在 GameThread 上应用一个能在 FrameBudget 内完成的最小批次。
		return FMcpTaskStepResult::Continue();
	}
};
```

`UnrealAgentMCPUnrealLevelAdapter.Deletion.cpp` 是当前参考实现：删除参数解析、范围限制、
folder/filter 校验移到 Worker；世界、Actor、Class、事务和分块删除只在 GameThread。

## 取消与补偿

- Prepare 前后、每个 Apply Step 和领域循环都必须调用 `Context.ShouldStop()`；
- `cancellationCheckpointCount` 记录实际检查次数，而不是仅记录取消请求次数；
- Apply 尚未开始时取消，`sideEffectState` 保持 `not_started`；
- Compensating Tool 按注册逆序执行补偿，并记录注册、执行、失败与总耗时；
- `Abort()` 可以返回 `Continue`，在正常取消/超时清理中跨 Tick 推进领域补偿；
- 补偿 Handler 本身仍应短小。需要多帧回滚的领域必须在 Stepper 的 `Abort()` 中实现
  显式补偿状态机，不能注册一个长时间阻塞 GameThread 的单体 Handler。

## 指标与门禁

Task 回执、`get_task`、`list_tasks` 与持久执行账本提供：

- `executionPhase`、`prepareDurationMs`；
- `applyStepBudgetMs`、`stepCount`、`totalApplyDurationMs`；
- `lastStepDurationMs`、`maxStepDurationMs`；
- `applyStepP95DurationMs`、`applyStepP99DurationMs`；
- `budgetOverrunCount`、`cancellationCheckpointCount`；
- `compensationRegisteredCount`、`compensationExecutedCount`、
  `compensationFailureCount`、`compensationDurationMs`。

每个 Task 保留最近 512 个 Apply Step 样本并计算 nearest-rank p95/p99。当前默认预算是
4 ms；建议发布门禁为：典型编辑器负载下 p95 不超过预算、p99 不超过预算的 2 倍、
`budgetOverrunCount / stepCount` 小于 1%。门禁必须按 ToolId 分桶，不能用轻量工具平均值
掩盖重工具。平台记录超预算但无法抢占已经进入的 C++ Step，因此领域实现仍必须主动限制
单批工作量。

自动化 `WorldData.UnrealAgent.Core.Execution.WorkerPrepareAndBoundedApply` 验证线程边界、
预算、p95/p99 和取消检查点；`PreparationShutdownDrain` 验证准备中关闭不会形成引用环；
`CompensatingTransaction` 验证补偿指标。
