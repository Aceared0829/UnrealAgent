// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file WorldDataCodexAccountUsage.h
 * @brief Codex 账户用量的应用层契约；不依赖 Slate 或具体进程实现。
 */

#include "CoreMinimal.h"

struct FWorldDataCodexAccountUsage
{
	bool bAvailable = false;
	FString PlanType;
	bool bHasCredits = false;
	bool bUnlimitedCredits = false;
	FString CreditBalance;
	TOptional<int32> UsedPercent;
	TOptional<int32> WindowDurationMinutes;
	TOptional<int64> ResetsAtUnixSeconds;
	FString Error;
};

DECLARE_DELEGATE_OneParam(FWorldDataCodexAccountUsageDelegate, const FWorldDataCodexAccountUsage&);

/** 为表现层提供真实 Codex 账户用量，不把 app-server 细节泄漏到 UI。 */
class IWorldDataCodexAccountUsageService
{
public:
	virtual ~IWorldDataCodexAccountUsageService() = default;

	virtual void Refresh(const FString& CodexCliPath) = 0;
	virtual void Cancel() = 0;

	FWorldDataCodexAccountUsageDelegate OnUsageChanged;
};
