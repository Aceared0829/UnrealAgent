// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPanelNavigation.h
 * @brief 面板对话、设置与任务视图的显式导航归约。
 */

/** 面板内会影响设置页与任务面板可见性的显式导航事件。 */
enum class EUnrealAgentMCPPanelNavigationEvent
{
	/** 对话提交已开始发送；不得抢占当前对话界面。 */
	PromptDispatchStarted,

	/** 用户主动点击任务按钮。 */
	TaskPanelRequested,

	/** 用户从任务面板返回对话。 */
	TaskPanelDismissed
};

/** 任务面板与设置页的互斥导航状态。 */
struct FUnrealAgentMCPPanelNavigationState
{
	bool bShowTaskPanel = false;
	bool bShowSettings = false;
};

namespace UnrealAgentMCPPanelNavigation
{
	/**
	 * 归约一次面板导航事件。
	 *
	 * 发送消息属于对话行为。提交开始只更新后台发送状态，不能抢占
	 * 当前对话；任务面板只能由用户主动请求打开。
	 */
	FUnrealAgentMCPPanelNavigationState Resolve(const FUnrealAgentMCPPanelNavigationState& CurrentState, EUnrealAgentMCPPanelNavigationEvent Event);
}
