// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file WorldDataCodexAccountUsageService.h
 * @brief 通过 Codex 官方 app-server 读取真实账户额度与限额。
 */

#include "Application/Account/WorldDataCodexAccountUsage.h"

class FInteractiveProcess;

class FWorldDataCodexAccountUsageService final : public IWorldDataCodexAccountUsageService, public TSharedFromThis<FWorldDataCodexAccountUsageService>
{
public:
	virtual ~FWorldDataCodexAccountUsageService() override;

	virtual void Refresh(const FString& CodexCliPath) override;
	virtual void Cancel() override;

private:
	void ConsumeOutput(const FString& Output);
	void PublishFailure(const FString& Error);
	void PublishUsage(const TSharedPtr<FJsonObject>& Result);

	TSharedPtr<FInteractiveProcess> Process;
	FString StdoutBuffer;
	bool bResultPublished = false;
};
