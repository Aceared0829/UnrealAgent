// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentACPProviderModel.h
 * @brief ACP Provider 元数据与本机账户识别；不依赖 Slate。
 */

#include "CoreMinimal.h"

enum class EUnrealAgentACPProvider : uint8
{
	Codex,
	Cursor
};

struct FUnrealAgentACPProviderDescriptor
{
	EUnrealAgentACPProvider Provider = EUnrealAgentACPProvider::Codex;
	FName Id;
	FText DisplayName;
	FText Description;
	FString CliCommand;
	bool bSupportsEmbeddedConversation = false;
};

struct FUnrealAgentACPAccountState
{
	FString DisplayLabel;
	FString SecondaryLabel;
	bool bAuthenticated = false;
};

namespace UnrealAgentACPProviderModel
{
	/** 返回按 UI 展示顺序排列的 Provider。 */
	const TArray<FUnrealAgentACPProviderDescriptor>& GetProviders();

	const FUnrealAgentACPProviderDescriptor& GetProvider(EUnrealAgentACPProvider Provider);

	/**
	 * 返回 Codex 本地凭据的候选路径。
	 * Windows 上不能只依赖 FPlatformProcess::UserDir()，因为它可能指向“文档”目录。
	 */
	TArray<FString> GetCodexAuthCandidatePaths();

	/**
	 * 尝试从 Provider 的本机凭据元数据中提取可显示账户名。
	 * 不返回、记录或持久化 access token / refresh token。
	 */
	FUnrealAgentACPAccountState DetectAccountState(EUnrealAgentACPProvider Provider, const FString& ProviderCliPath = FString());

	/** 解析官方 `agent status --format json` 响应。 */
	FUnrealAgentACPAccountState ParseCursorAccountStateJson(const FString& JsonText);

	/** 兼容仅需要显示名的调用方。 */
	FString DetectAccountLabel(EUnrealAgentACPProvider Provider);

	/** 为账户头像生成一至两个可读字符。 */
	FString MakeAccountInitials(const FString& AccountLabel);
}
