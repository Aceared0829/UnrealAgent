// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPToolDescriptor.h
 * @brief Unreal Agent 所有工具来源共用的契约描述与 Provider 扩展点。
 */

#include "CoreMinimal.h"
#include "Templates/Function.h"

class FJsonObject;

namespace UnrealAgentMCP::Execution
{
	class IMcpTaskStepper;
}

namespace UnrealAgentMCP
{
	/** 当前二进制支持的 Provider SDK 主版本。 */
	inline constexpr uint32 McpToolProviderApiVersion = 1;

	/** 工具风险等级；权限引擎以该值为强制决策输入。 */
	enum class EMcpToolRisk : uint8
	{
		ReadOnly,
		EditorState,
		ContentMutation,
		FileMutation,
		Destructive,
		CodeExecution,
		ExternalProcess
	};

	/** 工具执行所需的线程与生命周期策略。 */
	enum class EMcpToolThreadPolicy : uint8
	{
		/** 在提交线程立即执行。 */
		Inline,
		/** 在游戏线程执行；已位于游戏线程时立即执行。 */
		GameThread,
		/** 在线程池执行短时、非阻塞编辑器状态的工作。 */
		BackgroundThread,
		/** 在下一次 Core Ticker 执行，避免嵌套 TaskGraph 调度。 */
		StagedGameThread,
		/** 在专用线程执行长时阻塞工作。 */
		LongRunning,
		/** 启动原生异步工作的适配策略。 */
		NativeAsync
	};

	/** 工具对调用方呈现的执行模型。 */
	enum class EMcpToolExecutionMode : uint8
	{
		Synchronous,
		Asynchronous,
		Task
	};

	/** 工具修改状态时采用的事务策略。 */
	enum class EMcpToolTransactionPolicy : uint8
	{
		None,
		ReadOnly,
		ScopedTransaction,
		Atomic,
		Compensating
	};

	using FMcpDynamicToolHandler = TFunction<FString(const TSharedPtr<FJsonObject>& Arguments)>;

	/** 分类 action 在本次调用中解析出的权威执行契约。 */
	struct FMcpResolvedToolContract
	{
		EMcpToolRisk Risk = EMcpToolRisk::ContentMutation;
		EMcpToolExecutionMode ExecutionMode = EMcpToolExecutionMode::Synchronous;
		EMcpToolTransactionPolicy TransactionPolicy = EMcpToolTransactionPolicy::None;
		bool bCancelable = false;

		/** API-v1 兼容扩展必须追加在既有字段之后，不能改变旧字段偏移。 */
		FString CanonicalToolId;
		TSharedPtr<FJsonObject> InputSchema;
		TSharedPtr<FJsonObject> OutputSchema;
	};

	/**
	 * 工具契约的唯一事实来源。
	 * 目录、Schema、权限、调度、审计与执行均从该描述符读取。
	 */
	struct UNREALAGENTMCPCORE_API FMcpToolDescriptor
	{
		FName Provider;
		uint32 ProviderApiVersion = McpToolProviderApiVersion;
		/** Registry 分配的注册代次；不属于对外 Tool 契约。 */
		uint64 RegistrationGeneration = 0;
		FString Toolset;
		FString Name;
		FString QualifiedName;
		FString Description;
		FString ContractVersion = TEXT("1.0");

		TSharedPtr<FJsonObject> InputSchema;
		TSharedPtr<FJsonObject> OutputSchema;

		EMcpToolRisk Risk = EMcpToolRisk::ContentMutation;
		EMcpToolThreadPolicy ThreadPolicy = EMcpToolThreadPolicy::GameThread;
		EMcpToolExecutionMode ExecutionMode = EMcpToolExecutionMode::Synchronous;
		EMcpToolTransactionPolicy TransactionPolicy = EMcpToolTransactionPolicy::None;

		bool bReadOnly = false;
		bool bIdempotent = false;
		bool bCancelable = false;
		/** 中央策略引擎必须在调用此工具前确认显式授权。 */
		bool bRequiresConfirmation = false;
		FString ConfirmationArgument = TEXT("confirmation");
		FString ConfirmationValue;
		bool bInputSchemaExplicit = true;
		bool bOutputSchemaExplicit = true;
		bool bInputSchemaEnforced = true;

		/** 需要按项目根目录沙箱校验的字符串参数名。 */
		TArray<FString> FilePathArguments;

		/** 需要按允许内容根校验的 Unreal 资产路径参数名。 */
		TArray<FString> AssetPathArguments;

		/** 分类入口可按 action 选择本次调用需要文件沙箱校验的参数。 */
		TFunction<TArray<FString>(const TSharedPtr<FJsonObject>& Arguments)> FilePathArgumentsResolver;

		/** 分类入口可按 action 选择本次调用需要内容根校验的参数。 */
		TFunction<TArray<FString>(const TSharedPtr<FJsonObject>& Arguments)> AssetPathArgumentsResolver;

		/** 分类入口可按 action 收窄实际风险等级。 */
		TFunction<EMcpToolRisk(const TSharedPtr<FJsonObject>& Arguments)> RiskResolver;

		/** 分类入口可按 action 选择实际线程策略。 */
		TFunction<EMcpToolThreadPolicy(const TSharedPtr<FJsonObject>& Arguments)> ThreadPolicyResolver;

		/** 分类入口可按 action 切换同步或任务回执。 */
		TFunction<EMcpToolExecutionMode(const TSharedPtr<FJsonObject>& Arguments)> ExecutionModeResolver;

		/** 分类入口可按 action 选择真实事务策略。 */
		TFunction<EMcpToolTransactionPolicy(const TSharedPtr<FJsonObject>& Arguments)> TransactionPolicyResolver;

		/** 分类入口可按 action 决定任务能否取消。 */
		TFunction<bool(const TSharedPtr<FJsonObject>& Arguments)> CancelableResolver;

		/**
		 * 分类入口从统一 action Manifest 解析风险、事务、执行模式与取消策略；
		 * 成功解析时覆盖兼容期的分散 Resolver。
		 */
		TFunction<bool(const TSharedPtr<FJsonObject>& Arguments, FMcpResolvedToolContract& OutContract)> ActionContractResolver;

		FMcpDynamicToolHandler Invoker;

		/** API-v1 兼容扩展：为可分片 GameThread 工具创建持久步骤执行器。 */
		TFunction<TSharedPtr<Execution::IMcpTaskStepper>(const TSharedPtr<FJsonObject>& Arguments)> StagedTaskFactory;

		/** API-v1 兼容扩展：要求本次 Task 调用必须获得真实可恢复执行器。 */
		bool bRequiresResumableTask = false;
		TFunction<bool(const TSharedPtr<FJsonObject>& Arguments)> RequiresResumableTaskResolver;

		/** 校验描述符自身能否安全进入运行时 Registry。 */
		bool Validate(FString& OutError) const;

		/** 返回策略、审计与任务应使用的语义身份。 */
		const FString& GetEffectiveToolId() const
		{
			return ActionContractResolver && !QualifiedName.IsEmpty() ? QualifiedName : Name;
		}

		/** 生成标准 tools/list 定义及 Unreal Agent 扩展元数据。 */
		TSharedRef<FJsonObject> ToJsonObject() const;
	};

	/** 手写、反射、领域服务和扩展插件统一实现的工具来源接口。 */
	class UNREALAGENTMCPCORE_API IMcpToolProvider
	{
	public:
		virtual ~IMcpToolProvider() = default;

		virtual FName GetProviderName() const = 0;
		/** 主版本不匹配时整批拒绝注册，避免静默 ABI/API 漂移。 */
		virtual uint32 GetProviderApiVersion() const
		{
			return McpToolProviderApiVersion;
		}
		virtual void EnumerateTools(TArray<FMcpToolDescriptor>& OutTools, TArray<FString>& OutErrors) = 0;
	};

	namespace ToolDescriptor
	{
		UNREALAGENTMCPCORE_API FString RiskToString(EMcpToolRisk Value);
		UNREALAGENTMCPCORE_API FString ThreadPolicyToString(EMcpToolThreadPolicy Value);
		UNREALAGENTMCPCORE_API FString ExecutionModeToString(EMcpToolExecutionMode Value);
		UNREALAGENTMCPCORE_API FString TransactionPolicyToString(EMcpToolTransactionPolicy Value);

		/**
		 * 将旧 tools/list JSON 定义升级成统一描述符。
		 * 缺失的输出 Schema 会被标记为兼容占位，便于渐进迁移。
		 */
		UNREALAGENTMCPCORE_API bool FromJsonDefinition(const FString& Owner, const TSharedRef<FJsonObject>& Definition, FMcpDynamicToolHandler Handler,
			FMcpToolDescriptor& OutDescriptor, FString& OutError);
	}
}
