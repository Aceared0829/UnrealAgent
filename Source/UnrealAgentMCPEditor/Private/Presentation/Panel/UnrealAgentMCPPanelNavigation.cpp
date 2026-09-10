// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPanelNavigation.cpp
 * @brief 对话提交不得自动打开任务面板，设置页与任务面板互斥。
 */

#include "Presentation/Panel/UnrealAgentMCPPanelNavigation.h"

FUnrealAgentMCPPanelNavigationState UnrealAgentMCPPanelNavigation::Resolve(const FUnrealAgentMCPPanelNavigationState& CurrentState, const EUnrealAgentMCPPanelNavigationEvent Event)
{
	FUnrealAgentMCPPanelNavigationState Result = CurrentState;

	switch (Event)
	{
	case EUnrealAgentMCPPanelNavigationEvent::PromptDispatchStarted:
	case EUnrealAgentMCPPanelNavigationEvent::TaskPanelDismissed:
		Result.bShowTaskPanel = false;
		Result.bShowSettings = false;
		break;

	case EUnrealAgentMCPPanelNavigationEvent::TaskPanelRequested:
		Result.bShowTaskPanel = true;
		Result.bShowSettings = false;
		break;
	}

	return Result;
}
