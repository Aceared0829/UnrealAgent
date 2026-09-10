// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPanelSettingsStorage.h
 * @brief 面板设置持久化端口；隔离 Presentation 与具体文件系统实现。
 */

#include "CoreMinimal.h"

namespace UnrealAgentMCP::PanelSettingsStorage
{
	/** 将 JSON 原子写入设置路径，避免崩溃留下半截文件。 */
	bool WriteAtomically(const FString& SettingsPath, const FString& JsonText);
}
