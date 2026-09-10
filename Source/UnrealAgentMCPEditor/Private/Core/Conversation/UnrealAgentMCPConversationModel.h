// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPConversationModel.h
 * @brief MCP 面板会话状态，以及与 Slate 无关的 transcript 文本规则。
 */

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

enum class EWorldDataConversationMessageRole : uint8
{
	User,
	Assistant,
	Status,
	System,
	Tool,
	Error
};

enum class EWorldDataConversationToolState : uint8
{
	None,
	Running,
	Completed,
	Failed
};

struct FWorldDataConversationAttachment
{
	FString Path;
	FString DisplayName;
	bool bDirectory = false;
	/** true 表示附件原本位于消息正文光标处，而不是由“+”加入顶部。 */
	bool bInline = false;
};

/** 从富文本输入区解析出的行内文件或文件夹引用。 */
struct FWorldDataComposerInlineAttachment
{
	FString Id;
	FString Path;
	FString DisplayName;
	bool bDirectory = false;
};

struct FWorldDataConversationMessage
{
	FGuid Id = FGuid::NewGuid();
	EWorldDataConversationMessageRole Role = EWorldDataConversationMessageRole::Assistant;
	FString Text;
	TArray<FWorldDataConversationAttachment> Attachments;
	FString ToolCallId;
	FString ToolName;
	EWorldDataConversationToolState ToolState = EWorldDataConversationToolState::None;
	bool bStreaming = false;
	bool bCompleted = false;
	bool bFailed = false;
	FDateTime CreatedAt = FDateTime::Now();
	FDateTime CompletedAt;
};

/** 尚未发送给 ACP 的用户任务；配置按入队时快照，避免热切换后静默改变执行目标。 */
struct FWorldDataQueuedPrompt
{
	FGuid Id = FGuid::NewGuid();
	FString Text;
	TArray<FWorldDataConversationAttachment> Attachments;
	FString ProviderId = TEXT("codex");
	FString ModelId;
	FString ReasoningEffort;
	FString ServiceTier;
	uint8 AgentMode = 2;
	uint8 ApprovalPolicy = 0;
	uint8 SelfRepairPolicy = 0;
	FDateTime CreatedAt = FDateTime::Now();
};

struct FWorldDataContextCompactionRecord
{
	int32 Generation = 0;
	FDateTime CompactedAtUtc;
	int32 ContextTokenCapacity = 0;
	int32 EstimatedTokensBefore = 0;
	int32 EstimatedTokensAfter = 0;
	int32 SummaryThroughMessageIndex = 0;
};

struct FWorldDataConversation
{
	FGuid Id = FGuid::NewGuid();
	FText Title;
	FDateTime CreatedAt = FDateTime::Now();
	FDateTime UpdatedAt = FDateTime::Now();
	/** 该会话绑定的代理身份；空值仅用于识别旧版历史并执行迁移。 */
	FString ControllerId;
	FString ProviderId = TEXT("codex");
	uint8 AgentMode = 2;
	uint8 ApprovalPolicy = 0;
	uint8 SelfRepairPolicy = 0;
	bool bHasCustomTitle = false;
	bool bArchived = false;
	TArray<FWorldDataConversationMessage> Messages;
	TArray<FWorldDataQueuedPrompt> QueuedPrompts;
	FString Transcript;
	FString ContextSummary;
	/** 每次压缩时重新采集的项目架构与 Editor 状态；独立于滚动摘要，避免被历史截断。 */
	FString ContextContinuitySnapshot;
	int32 ContextSummaryThroughMessageIndex = 0;
	int32 ContextGeneration = 0;
	int32 EstimatedContextTokens = 0;
	int32 ContextTokenCapacity = 512 * 1024;
	TArray<FWorldDataContextCompactionRecord> ContextCompactionHistory;
	/** 运行态只在当前编辑器进程内有效；恢复历史时会被清除。 */
	bool bIsRunning = false;
	/** 后台任务已经结束但用户尚未查看该对话。 */
	bool bHasUnreadCompletion = false;
	int32 ActiveAssistantMessageIndex = INDEX_NONE;
	int32 ActiveTurnStatusMessageIndex = INDEX_NONE;
};

/** 会话输入区对一次按键应采取的动作。 */
enum class EWorldDataComposerKeyAction : uint8
{
	PassThrough,
	Consume,
	InsertNewLine,
	Submit
};

enum class EWorldDataConversationIndicator : uint8
{
	None,
	Running,
	UnreadCompletion
};

/** 对话消息在 Slate 中采用的视觉语义。 */
enum class EWorldDataConversationMessagePresentation : uint8
{
	UserBubble,
	PlainText,
	StatusLine,
	ToolCard,
	ErrorCard
};

enum class EWorldDataConversationRewriteResult : uint8
{
	Prepared,
	InvalidUserMessage
};

namespace UnrealAgentMCPConversationModel
{
	inline constexpr int32 MinimumContextTokenCapacity = 256 * 1024;
	inline constexpr int32 DefaultContextTokenCapacity = 512 * 1024;
	inline constexpr int32 MaximumContextTokenCapacity = 1024 * 1024;
	inline constexpr int32 ContextTokenCapacityStep = 64 * 1024;
	inline constexpr double DefaultContextCompactionThreshold = 0.90;

	int32 ClampContextTokenCapacity(int32 TokenCapacity);

	int32 SliderValueToContextTokenCapacity(float SliderValue);

	float ContextTokenCapacityToSliderValue(int32 TokenCapacity);

	FString FormatContextTokenCapacity(int32 TokenCapacity);

	double GetContextUsageRatio(const FWorldDataConversation& Conversation);

	FString FormatEstimatedTokenCount(int32 TokenCount);

	FString SanitizeSensitiveText(const FString& Text);

	FString SanitizeToolDisplayName(const FString& ToolName, int32 MaximumLength = 160);

	int32 EstimateTextTokens(const FString& Text);

	void RefreshConversationContextMetrics(FWorldDataConversation& Conversation);

	bool ShouldAutoCompactContext(const FWorldDataConversation& Conversation, double CompactThreshold = DefaultContextCompactionThreshold);

	/** 仅空白对话与对话详情显示输入区；设置和非对话详情必须释放布局空间。 */
	bool ShouldShowConversationComposer(bool bShowSettings, bool bShowDetail, bool bDetailIsConversation);

	/** 普通回复、意图和澄清直接显示；工具与错误保留消息卡片。 */
	EWorldDataConversationMessagePresentation ResolveMessagePresentation(EWorldDataConversationMessageRole Role);

	bool CompactConversationContext(FWorldDataConversation& Conversation, int32 RecentUserTurns = 8, int32 MaximumSummaryCharacters = 48000,
		const FString& ContinuitySnapshot = FString());

	FString BuildConversationContextStatus(const FWorldDataConversation& Conversation);

	/** 运行中优先显示圆环；仅后台完成且尚未查看时显示黄色未读点。 */
	EWorldDataConversationIndicator ResolveConversationIndicator(bool bRunning, bool bHasUnreadCompletion, bool bIsActiveConversation);

	/** 任一回合、ACP 运行态、审批或队列仍未清空时，对话都没有完全执行完。 */
	bool HasPendingConversationWork(bool bConversationMarkedRunning, bool bRuntimeHasActiveTurn, bool bHasPendingPermission, bool bHasQueuedPrompts);

	/** 仅在当前回合完全结束、队列非空且队首未处于编辑状态时自动调度。 */
	bool ShouldDispatchQueuedPrompt(bool bConversationMarkedRunning, bool bRuntimeHasActiveTurn, bool bQueueEmpty, bool bEditingQueueHead);

	bool ShouldMarkCompletionUnread(bool bIsActiveConversation, bool bHasQueuedPrompts);

	/** 澄清期识别“先分析/直接开始”等继续意图，并采用全部默认答案。 */
	bool ShouldUseClarificationDefaults(const FString& UserMessage);

	/**
	 * 精确判定 Enter 行为：IME 组合中仅确认候选，Shift+Enter 主动换行，
	 * Ctrl+Enter 交由控件原生换行，普通 Enter 才提交消息。
	 */
	EWorldDataComposerKeyAction ResolveComposerKeyAction(bool bIsEnter, bool bControlDown, bool bShiftDown, bool bInputMethodComposing);

	/**
	 * Windows IME 确认候选词时，可能在同一输入事务内连续触发文本变更与
	 * Enter 提交；用户再次主动按下 Enter 会发生在保护窗口之后。
	 */
	bool ShouldSubmitComposerEnter(double SecondsSinceLastTextChange, double ImeCommitGuardSeconds = 0.12);

	/** 去掉事件文本开头的 [标签]，正文为空时返回标签本身。 */
	FString TrimTaggedEventText(const FString& Text);

	/** 识别错误、工具和系统事件；普通助手文本返回 false。 */
	bool TryParseTaggedEvent(const FString& Text, EWorldDataConversationMessageRole& OutRole, FString& OutText);

	/** 从首行构造标题候选；空消息返回空字符串，由 UI 提供本地化占位标题。 */
	FString BuildConversationTitleCandidate(const FString& Message, int32 MaximumLength = 18);

	/** 构造会话中展示的用户消息，并附上不泄漏目录结构的附件名称。 */
	FString BuildAttachmentDisplayMessage(const FString& UserMessage, const TArray<FString>& AttachmentPaths);

	/**
	 * 构造交给 Agent 的附件感知提示词。附件保留绝对路径，使 Agent 能根据
	 * 路径类型调用合适的读取工具，而不是仅凭文件或文件夹名称猜测内容。
	 */
	FString BuildAttachmentAwarePrompt(const FString& UserMessage, const TArray<FString>& AttachmentPaths);

	/**
	 * 为输入区生成一个不会暴露原始路径的富文本附件标记。标记交给 Slate
	 * decorator 渲染为不可拆分的行内标签，发送前再解析回结构化引用。
	 */
	FString BuildComposerInlineAttachmentMarkup(const FString& AttachmentId, const FString& AttachmentPath, bool bDirectory);

	/**
	 * 将输入区富文本转换为用户可读消息，并按正文顺序提取行内引用。
	 * 普通富文本转义会恢复为原字符，附件则转换为 @file:/@folder: 语义标记。
	 */
	void ParseComposerRichText(const FString& ComposerRichText, FString& OutUserMessage, TArray<FWorldDataComposerInlineAttachment>& OutAttachments);

	/** 将历史消息及其行内附件重新构造成可编辑、可渲染的输入区富文本。 */
	FString BuildComposerRichTextForEditing(const FString& UserMessage, const TArray<FWorldDataConversationAttachment>& Attachments);

	/** 为工具卡片生成包含具体工具名的运行、完成或失败文案。 */
	FString BuildToolCallDisplayText(const FString& ToolName, EWorldDataConversationToolState State);

	/** 为失败工具卡片构造来自 MCP 响应的最小可诊断详情。 */
	FString BuildToolCallFailureDetails(const FString& CanonicalAction, const FString& Code, const TArray<FString>& SchemaErrorPaths, const FString& TraceId);

	/** 整轮回复结束后，工具调用分组默认自动收起。 */
	bool ShouldAutoCollapseToolGroup(const TArray<FWorldDataConversationMessage>& Messages, int32 ToolMessageIndex);

	/**
	 * 将结构化消息重建为可传给 Agent 的历史快照。状态消息属于界面进度，
	 * 不进入语义上下文；用户消息中的附件会保留绝对路径。
	 */
	FString BuildConversationHistorySnapshot(const TArray<FWorldDataConversationMessage>& Messages);

	/** 将分支点之前的历史与新请求组合成首次发送给新 ACP session 的提示词。 */
	FString BuildConversationContextReplayPrompt(const TArray<FWorldDataConversationMessage>& Messages, const FString& NewPrompt);

	FString BuildConversationContextReplayPrompt(const FWorldDataConversation& Conversation, const FString& NewPrompt);

	/**
	 * 删除指定用户消息及其后的全部消息，用于重新编辑历史提示词或删除一轮对话。
	 * 仅接受有效的用户消息下标；校验失败时保持原数组不变。
	 */
	bool TryRemoveConversationFromUserMessage(TArray<FWorldDataConversationMessage>& Messages, int32 UserMessageIndex);

	EWorldDataConversationRewriteResult PrepareConversationForUserMessageRewrite(FWorldDataConversation& Conversation, int32 UserMessageIndex);

	/**
	 * 从一条已完成的助手回复创建独立会话分支。新分支只复制到指定消息为止，
	 * 使用新 ID，并清除所有运行中下标。
	 */
	bool TryCreateConversationBranch(const FWorldDataConversation& Source, int32 AssistantMessageIndex, const FDateTime& CreatedAt, FWorldDataConversation& OutBranch);

	/** 将实际完成时间格式化为回复右下角标签；跨天时附带月日。 */
	FString FormatCompletionLabel(const FDateTime& CompletedAt, bool bFailed, const FDateTime& ReferenceTime);

	/** 返回第一个未归档会话；可排除当前正在归档的会话。 */
	int32 FindFirstUnarchivedConversation(const TArray<FWorldDataConversation>& Conversations, int32 ExcludedIndex = INDEX_NONE);
}
