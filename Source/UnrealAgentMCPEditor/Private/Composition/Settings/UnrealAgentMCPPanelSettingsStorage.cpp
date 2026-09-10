// Copyright ZhaoZining. All Rights Reserved.

#include "Application/Settings/UnrealAgentMCPPanelSettingsStorage.h"

#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

namespace UnrealAgentMCP::PanelSettingsStorage
{
	bool WriteAtomically(const FString& SettingsPath, const FString& JsonText)
	{
		// 复用 ServerEnvironment 已有的临时文件替换，避免再实现一套落盘。
		return ServerEnvironment::WriteStringAtomically(JsonText, SettingsPath, true);
	}
}
