// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPanelSettings.h
 * @brief MCP 面板设置持久化与 CLI 路径解析，不依赖 Slate 控件。
 */

#include "CoreMinimal.h"
#include "Application/ACP/UnrealAgentACPExecutionPolicy.h"
#include "Core/Conversation/UnrealAgentMCPConversationModel.h"

enum class EUnrealAgentMCPPanelCliTool : uint8
{
	Codex,
	Cursor
};

/**
 * 决定由谁拥有任务规划。
 * NativeUnrealAgent 仅用于读取旧 settings / 会话；运行时必须迁到外部 ACP。
 */
enum class EUnrealAgentControllerMode : uint8
{
	NativeUnrealAgent,
	ExternalCodexAgent,
	ExternalCursorAgent
};

struct FUnrealAgentMCPPanelSettings
{
	int32 SchemaVersion = 5;
	EUnrealAgentControllerMode ControllerMode = EUnrealAgentControllerMode::ExternalCodexAgent;
	FLinearColor AccentColor = FLinearColor::White;
	FString CodexCliPath;
	FString CursorCliPath;
	FString ActiveProviderId = TEXT("codex");
	FString CodexModelId;
	FString CursorModelId;
	bool bSidebarCollapsed = false;
	EWorldDataAgentMode DefaultAgentMode = EWorldDataAgentMode::Agent;
	EWorldDataApprovalPolicy ApprovalPolicy = EWorldDataApprovalPolicy::AlwaysAsk;
	EWorldDataSelfRepairPolicy SelfRepairPolicy = EWorldDataSelfRepairPolicy::Off;
	int32 ContextTokenCapacity = UnrealAgentMCPConversationModel::DefaultContextTokenCapacity;

	/**
	 * Native UnrealAgent 使用的 ProviderId（providers.json）。
	 * External Codex/Cursor Agent 的选择由 ControllerMode 表达。
	 */
	FString DirectProviderId;

	/** light / standard / fine */
	FString BudgetTierId = TEXT("standard");

	/** 历史字段；schema v5 起不再由 Native Kernel 分派。 */
	bool bMultiAgentEnabled = false;

	bool bMemoryEnabled = true;

	/** 表示已迁移旧中继设置的瞬态标记，不写回磁盘。 */
	bool bMigratedLegacySubscriptionRelay = false;
};

namespace UnrealAgentMCPPanelSettings
{
	inline constexpr int32 CurrentSchemaVersion = 5;
	inline constexpr int32 ControllerContractSchemaVersion = 2;

	FString LexToString(EUnrealAgentControllerMode Mode);
	bool TryParseControllerMode(const FString& Text, EUnrealAgentControllerMode& OutMode);
	bool UsesNativeUnrealAgentController(EUnrealAgentControllerMode Mode);
	/**
	 * 把已退役的 Native 控制者落到 Codex 或 Cursor。
	 * 仅当旧档案明确指向 Cursor（含 cursor_subscription_relay）时才选 Cursor，其余一律 Codex。
	 */
	EUnrealAgentControllerMode ResolveRetiredNativeControllerMode(const FString& FallbackProviderId);
	/**
	 * 将控制者身份解析为唯一的外部 ACP Provider。
	 * Codex/Cursor 控制者固定返回各自 Provider；Native 回退到合法 ACP 名。
	 */
	FString ResolveControllerAcpProviderId(EUnrealAgentControllerMode ControllerMode, const FString& FallbackProviderId);
	/**
	 * 恢复会话代理身份；旧版当前会话沿用面板选择，旧版后台会话按原 Provider 推断。
	 */
	EUnrealAgentControllerMode ResolveConversationControllerMode(const FString& SerializedControllerId, const FString& LegacyProviderId,
		EUnrealAgentControllerMode CurrentControllerMode, bool bIsActiveLegacyConversation);
	/** schema v5 起始终返回 false；Native 不再作为可运行控制者。 */
	bool SupportsKernelMultiAgent(EUnrealAgentControllerMode ControllerMode);

	FString GetCliCommandName(EUnrealAgentMCPPanelCliTool Tool);

	/** 返回 PATH 中检测到的可执行文件路径。 */
	FString DetectCliPath(EUnrealAgentMCPPanelCliTool Tool);

	/** 返回项目本地 codex-acp 安装所携带的 Codex CLI 候选路径。 */
	FString GetManagedCodexCliCandidatePath(const FString& ProjectSavedDirectory);

	/** 返回项目本地用于启动 WSL Cursor Agent 的 Windows 启动器路径。 */
	FString GetManagedCursorCliCandidatePath(const FString& ProjectSavedDirectory);

	/** 返回 PATH 中可启动的 npm 路径。 */
	FString DetectNpmPath();

	/** 从按优先级排序的候选列表中返回首个可启动的 npm 路径。 */
	FString ResolveNpmPathFromCandidates(const TArray<FString>& CandidatePaths);

	/** 解析安装器输出的机器可读进度行。 */
	bool IsInstallerProgressLine(const FString& Line, int32& OutPercent, FString& OutStage, FString& OutComponent);

	/** 构建不含末尾分隔符的安装器绝对目录。 */
	FString NormalizeInstallerDirectoryArgument(const FString& Directory);

	/** 去除引号、解析命令名，并转换为当前平台路径格式。 */
	FString NormalizeConfiguredCliPath(const FString& NewPath);

	/** 显式配置优先；显式路径失效时不回退到自动检测结果。 */
	FString ResolveEffectiveCliPath(const FString& ConfiguredPath, const FString& DetectedPath);

	FLinearColor ClampOpaqueAccentColor(const FLinearColor& Color);

	/** 解析设置 JSON；失败时 OutSettings 保持默认值。 */
	bool TryParseSettingsJson(const FString& JsonText, FUnrealAgentMCPPanelSettings& OutSettings);

	/** 生成与既有 settings.json 兼容的 UTF-8 JSON 文本。 */
	bool TrySerializeSettingsJson(const FUnrealAgentMCPPanelSettings& Settings, FString& OutJsonText);

	bool LoadSettingsFile(const FString& SettingsPath, FUnrealAgentMCPPanelSettings& OutSettings);

	bool SaveSettingsFile(const FString& SettingsPath, const FUnrealAgentMCPPanelSettings& Settings);
}
