// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPanelModelTests.cpp
 * @brief 验证面板纯规则、对话模型、设置路由与展示组件契约。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Application/ACP/UnrealAgentACPProviderModel.h"
#include "Application/ACP/UnrealAgentCodexACPClientRules.h"
#include "Application/CLI/WorldDataCliProcessRules.h"
#include "Core/Conversation/UnrealAgentMCPConversationModel.h"
#include "Core/Conversation/UnrealAgentMCPMarkdown.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/SlateWidgetRun.h"
#include "Infrastructure/Conversation/UnrealAgentMCPConversationStore.h"
#include "Internationalization/BreakIterator.h"
#include "Misc/Base64.h"
#include "Presentation/Settings/UnrealAgentMCPPanelSettings.h"
#include "Presentation/Panel/UnrealAgentMCPPanelNavigation.h"
#include "Presentation/Widgets/SUnrealAgentMCPComposerEditableText.h"
#include "Presentation/Widgets/SUnrealAgentMCPMarkdown.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPanelModelTest, "WorldData.UnrealAgent.Editor.PanelModel.PureRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPPanelModelTest::RunTest(const FString& Parameters)
{
	using namespace UnrealAgentMCPConversationModel;
	using namespace UnrealAgentMCPPanelNavigation;
	using namespace UnrealAgentMCPPanelSettings;
	TestFalse(
		TEXT("退役 Native 控制者不再由 Kernel 分派"),
		SupportsKernelMultiAgent(
			EUnrealAgentControllerMode::NativeUnrealAgent));
	TestFalse(
		TEXT("Codex 代理不由 Unreal Agent Kernel 分派"),
		SupportsKernelMultiAgent(
			EUnrealAgentControllerMode::ExternalCodexAgent));
	TestFalse(
		TEXT("Cursor 代理不由 Unreal Agent Kernel 分派"),
		SupportsKernelMultiAgent(
			EUnrealAgentControllerMode::ExternalCursorAgent));
	TestTrue(
		TEXT("旧 Native 且回退 Provider 为 Cursor 时落到 Cursor 代理"),
		ResolveRetiredNativeControllerMode(TEXT("cursor"))
			== EUnrealAgentControllerMode::ExternalCursorAgent);
	TestTrue(
		TEXT("旧 Native 默认落到 Codex 代理"),
		ResolveRetiredNativeControllerMode(TEXT("ollama"))
			== EUnrealAgentControllerMode::ExternalCodexAgent);
	TestEqual(
		TEXT("Cursor 代理始终解析到 Cursor ACP Provider"),
		ResolveControllerAcpProviderId(
			EUnrealAgentControllerMode::ExternalCursorAgent,
			TEXT("codex")),
		FString(TEXT("cursor")));
	TestEqual(
		TEXT("Codex 代理始终解析到 Codex ACP Provider"),
		ResolveControllerAcpProviderId(
			EUnrealAgentControllerMode::ExternalCodexAgent,
			TEXT("cursor")),
		FString(TEXT("codex")));
	TestEqual(
		TEXT("Native Unreal Agent 仅保留合法的兼容 Provider"),
		ResolveControllerAcpProviderId(
			EUnrealAgentControllerMode::NativeUnrealAgent,
			TEXT("cursor")),
		FString(TEXT("cursor")));
	TestTrue(
		TEXT("旧版当前会话沿用左上角 Cursor 代理而不恢复 Codex Provider"),
		ResolveConversationControllerMode(
			FString(),
			TEXT("codex"),
			EUnrealAgentControllerMode::ExternalCursorAgent,
			true)
			== EUnrealAgentControllerMode::ExternalCursorAgent);
	TestTrue(
		TEXT("旧版后台会话仍按自身 Provider 推断代理"),
		ResolveConversationControllerMode(
			FString(),
			TEXT("codex"),
			EUnrealAgentControllerMode::ExternalCursorAgent,
			false)
			== EUnrealAgentControllerMode::ExternalCodexAgent);
	TestTrue(
		TEXT("旧 Native 会话按兼容 Provider 迁到外部 ACP"),
		ResolveConversationControllerMode(
			TEXT("native_unreal_agent"),
			TEXT("cursor"),
			EUnrealAgentControllerMode::ExternalCodexAgent,
			true)
			== EUnrealAgentControllerMode::ExternalCursorAgent);

	const FUnrealAgentMCPPanelNavigationState TaskStartedNavigation = Resolve(
		{true, true},
		EUnrealAgentMCPPanelNavigationEvent::PromptDispatchStarted);
	TestFalse(
		TEXT("对话提交开始后不自动打开任务面板"),
		TaskStartedNavigation.bShowTaskPanel);
	TestFalse(
		TEXT("对话提交开始后关闭设置并回到对话"),
		TaskStartedNavigation.bShowSettings);

	const FUnrealAgentMCPPanelNavigationState TaskPanelRequestedNavigation =
		Resolve(
			{false, true},
			EUnrealAgentMCPPanelNavigationEvent::TaskPanelRequested);
	TestTrue(
		TEXT("只有用户主动请求才打开任务面板"),
		TaskPanelRequestedNavigation.bShowTaskPanel);
	TestFalse(
		TEXT("任务面板与设置页互斥"),
		TaskPanelRequestedNavigation.bShowSettings);

	const FUnrealAgentMCPPanelNavigationState TaskPanelDismissedNavigation =
		Resolve(
			{true, false},
			EUnrealAgentMCPPanelNavigationEvent::TaskPanelDismissed);
	TestFalse(
		TEXT("从任务面板返回时恢复对话界面"),
		TaskPanelDismissedNavigation.bShowTaskPanel);

	TestTrue(
		TEXT("IME 组合态 Enter 只确认候选"),
		ResolveComposerKeyAction(true, false, false, true)
			== EWorldDataComposerKeyAction::Consume);
	TestTrue(
		TEXT("普通 Enter 提交消息"),
		ResolveComposerKeyAction(true, false, false, false)
			== EWorldDataComposerKeyAction::Submit);
	TestTrue(
		TEXT("Ctrl+Enter 交给多行控件换行"),
		ResolveComposerKeyAction(true, true, false, false)
			== EWorldDataComposerKeyAction::PassThrough);
	TestTrue(
		TEXT("Shift+Enter 在光标处插入换行"),
		ResolveComposerKeyAction(true, false, true, false)
			== EWorldDataComposerKeyAction::InsertNewLine);
	TestTrue(
		TEXT("非 Enter 按键保持原处理路径"),
		ResolveComposerKeyAction(false, false, false, false)
			== EWorldDataComposerKeyAction::PassThrough);

	TestFalse(
		TEXT("IME 候选确认后紧随的 Enter 不发送消息"),
		ShouldSubmitComposerEnter(0.01));
	TestFalse(
		TEXT("IME 组合态保护窗口内不发送消息"),
		ShouldSubmitComposerEnter(0.119));
	TestTrue(
		TEXT("保护窗口后的独立 Enter 可以发送消息"),
		ShouldSubmitComposerEnter(0.12));
	TestTrue(
		TEXT("尚无文本变更记录时 Enter 保持原发送语义"),
		ShouldSubmitComposerEnter(-1.0));

	TestTrue(
		TEXT("空白对话页显示输入区"),
		ShouldShowConversationComposer(false, false, false));
	TestTrue(
		TEXT("对话详情页显示输入区"),
		ShouldShowConversationComposer(false, true, true));
	TestFalse(
		TEXT("设置页折叠整个输入区"),
		ShouldShowConversationComposer(true, true, true));
	TestFalse(
		TEXT("非对话详情页折叠整个输入区"),
		ShouldShowConversationComposer(false, true, false));

	TestTrue(
		TEXT("普通助手回复直接显示为文本"),
		ResolveMessagePresentation(EWorldDataConversationMessageRole::Assistant)
			== EWorldDataConversationMessagePresentation::PlainText);
	TestTrue(
		TEXT("意图和澄清系统消息直接显示为文本"),
		ResolveMessagePresentation(EWorldDataConversationMessageRole::System)
			== EWorldDataConversationMessagePresentation::PlainText);
	TestTrue(
		TEXT("错误消息保留醒目的错误卡片"),
		ResolveMessagePresentation(EWorldDataConversationMessageRole::Error)
			== EWorldDataConversationMessagePresentation::ErrorCard);
	TestTrue(
		TEXT("只有工具消息使用工具卡片"),
		ResolveMessagePresentation(EWorldDataConversationMessageRole::Tool)
			== EWorldDataConversationMessagePresentation::ToolCard);
	TestTrue(
		TEXT("状态消息保持紧凑状态行"),
		ResolveMessagePresentation(EWorldDataConversationMessageRole::Status)
			== EWorldDataConversationMessagePresentation::StatusLine);
	TestTrue(
		TEXT("用户消息保持右侧气泡"),
		ResolveMessagePresentation(EWorldDataConversationMessageRole::User)
			== EWorldDataConversationMessagePresentation::UserBubble);

	TestTrue(
		TEXT("运行中对话优先显示旋转状态"),
		ResolveConversationIndicator(true, true, false)
			== EWorldDataConversationIndicator::Running);
	TestTrue(
		TEXT("ACP 回合仍在执行时保持旋转状态"),
		HasPendingConversationWork(false, true, false, false));
	TestTrue(
		TEXT("等待权限批准时保持旋转状态"),
		HasPendingConversationWork(false, false, true, false));
	TestTrue(
		TEXT("队列仍有任务时保持旋转状态"),
		HasPendingConversationWork(false, false, false, true));
	TestFalse(
		TEXT("所有执行来源都结束后才清除旋转状态"),
		HasPendingConversationWork(false, false, false, false));
	TestTrue(
		TEXT("后台完成显示未读黄点"),
		ResolveConversationIndicator(false, true, false)
			== EWorldDataConversationIndicator::UnreadCompletion);
	TestTrue(
		TEXT("当前打开对话不显示完成未读点"),
		ResolveConversationIndicator(false, true, true)
			== EWorldDataConversationIndicator::None);
	TestTrue(
		TEXT("空闲对话自动调度未编辑的队首任务"),
		ShouldDispatchQueuedPrompt(false, false, false, false));
	TestFalse(
		TEXT("同一对话运行时保持队列串行"),
		ShouldDispatchQueuedPrompt(true, false, false, false));
	TestFalse(
		TEXT("正在编辑队首时不抢先发送"),
		ShouldDispatchQueuedPrompt(false, false, false, true));
	TestTrue(
		TEXT("仅后台且队列已清空时标记完成未读"),
		ShouldMarkCompletionUnread(false, false));
	TestFalse(
		TEXT("仍有后续队列时继续显示运行流程而非未读"),
		ShouldMarkCompletionUnread(false, true));
	TestTrue(
		TEXT("澄清期的先分析意图采用全部默认答案继续"),
		ShouldUseClarificationDefaults(
			TEXT("你可以先分析分析该场景么，你先了解这个场景的全貌")));
	TestTrue(
		TEXT("澄清期直接开始采用全部默认答案继续"),
		ShouldUseClarificationDefaults(TEXT("直接开始！")));
	TestFalse(
		TEXT("具体澄清答案不会被误判为采用默认"),
		ShouldUseClarificationDefaults(
			TEXT("优先保持现有美术风格，并限制为只读扫描")));

	TestEqual(
		TEXT("会话标题只使用首行"),
		BuildConversationTitleCandidate(TEXT("第一行\n第二行")),
		FString(TEXT("第一行")));
	TestEqual(
		TEXT("空白消息不生成标题"),
		BuildConversationTitleCandidate(TEXT(" \r\n ")),
		FString());
	TestEqual(
		TEXT("过长标题按既有 18 字规则截断"),
		BuildConversationTitleCandidate(TEXT("12345678901234567890")),
		FString(TEXT("123456789012345678…")));

	const TArray<FString> AttachmentPaths = {
		TEXT("D:/References/design brief.pdf"),
		TEXT("D:/References/preview.png"),
		TEXT("D:/References/source-folder")};
	const FString AttachmentDisplayMessage =
		BuildAttachmentDisplayMessage(TEXT("按附件实现"), AttachmentPaths);
	TestTrue(
		TEXT("会话消息展示附件文件名"),
		AttachmentDisplayMessage.Contains(TEXT("design brief.pdf"))
			&& AttachmentDisplayMessage.Contains(TEXT("preview.png"))
			&& AttachmentDisplayMessage.Contains(TEXT("source-folder")));
	TestFalse(
		TEXT("会话消息不暴露附件目录"),
		AttachmentDisplayMessage.Contains(TEXT("D:/References")));
	const FString AttachmentPrompt =
		BuildAttachmentAwarePrompt(TEXT("按附件实现"), AttachmentPaths);
	TestTrue(
		TEXT("Agent 提示词包含每个附件的绝对路径"),
		AttachmentPrompt.Contains(AttachmentPaths[0])
			&& AttachmentPrompt.Contains(AttachmentPaths[1])
			&& AttachmentPrompt.Contains(AttachmentPaths[2]));
	TestTrue(
		TEXT("Agent 提示词明确支持文件夹"),
		AttachmentPrompt.Contains(TEXT("文件或文件夹"))
			&& AttachmentPrompt.Contains(TEXT("文件夹应按用户需求检查")));
	TestTrue(
		TEXT("仅附件消息也能生成明确任务"),
		BuildAttachmentAwarePrompt(FString(), AttachmentPaths)
			.StartsWith(TEXT("请查看并处理我添加的附件。")));
	TestEqual(
		TEXT("无附件时提示词保持原消息"),
		BuildAttachmentAwarePrompt(TEXT("  普通消息  "), TArray<FString>()),
		FString(TEXT("普通消息")));

	const FString InlineFileMarkup = BuildComposerInlineAttachmentMarkup(
		TEXT("file1"),
		AttachmentPaths[0],
		false);
	const FString InlineFolderMarkup = BuildComposerInlineAttachmentMarkup(
		TEXT("folder1"),
		AttachmentPaths[2],
		true);
	TestFalse(
		TEXT("输入区行内标记不暴露本地绝对路径"),
		InlineFileMarkup.Contains(AttachmentPaths[0]));
	FString ParsedComposerMessage;
	TArray<FWorldDataComposerInlineAttachment> ParsedInlineAttachments;
	ParseComposerRichText(
		TEXT("请检查 ") + InlineFileMarkup
			+ TEXT(" 和 &lt;配置&gt; ") + InlineFolderMarkup,
		ParsedComposerMessage,
		ParsedInlineAttachments);
	TestEqual(
		TEXT("行内文件和文件夹按光标位置转换为语义标记"),
		ParsedComposerMessage,
		FString(TEXT(
			"请检查 @file:design brief.pdf 和 <配置> @folder:source-folder")));
	TestEqual(
		TEXT("行内引用解析数量正确"),
		ParsedInlineAttachments.Num(),
		2);
	TestEqual(
		TEXT("行内引用恢复绝对路径"),
		ParsedInlineAttachments[0].Path,
		AttachmentPaths[0]);
	TestTrue(
		TEXT("行内文件夹保留目录类型"),
		ParsedInlineAttachments[1].bDirectory);

	TArray<FWorldDataConversationAttachment> EditableAttachments;
	FWorldDataConversationAttachment EditableFile;
	EditableFile.Path = AttachmentPaths[0];
	EditableFile.DisplayName = TEXT("design brief.pdf");
	EditableFile.bInline = true;
	EditableAttachments.Add(EditableFile);
	const FString EditableRichText = BuildComposerRichTextForEditing(
		TEXT("调整 @file:design brief.pdf 后继续"),
		EditableAttachments);
	ParseComposerRichText(
		EditableRichText,
		ParsedComposerMessage,
		ParsedInlineAttachments);
	TestEqual(
		TEXT("历史行内引用重新编辑后位置和文本保持一致"),
		ParsedComposerMessage,
		FString(TEXT("调整 @file:design brief.pdf 后继续")));
	TestEqual(
		TEXT("历史行内引用重新编辑后仍是结构化附件"),
		ParsedInlineAttachments.Num(),
		1);

	TSharedPtr<FSlateTextLayout> CapturedComposerLayout;
	const FCreateSlateTextLayout CreateComposerLayout =
		FCreateSlateTextLayout::CreateLambda(
			[&CapturedComposerLayout](
				SWidget* OwningWidget,
				const FTextBlockStyle& DefaultTextStyle)
			{
				const TSharedRef<FSlateTextLayout> Layout =
					FSlateTextLayout::Create(
						OwningWidget,
						DefaultTextStyle);
				if (!CapturedComposerLayout.IsValid())
				{
					CapturedComposerLayout = Layout;
				}
				return Layout;
			});
	const TSharedRef<FRichTextLayoutMarshaller> ComposerMarshaller =
		FRichTextLayoutMarshaller::Create(
			TArray<TSharedRef<ITextDecorator>>(),
			&FAppStyle::Get());
	const TSharedRef<SMultiLineEditableText> ComposerEditor =
		SNew(SMultiLineEditableText)
		.Marshaller(ComposerMarshaller)
		.AutoWrapText(true)
		.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
		.CreateSlateTextLayout(CreateComposerLayout);
	ComposerEditor->InsertTextAtCursor(TEXT("前 "));
	FRunInfo InlineRunInfo(TEXT("attachment"));
	InlineRunInfo.MetaData.Add(TEXT("id"), TEXT("slate1"));
	InlineRunInfo.MetaData.Add(
		TEXT("path64"),
		FBase64::Encode(AttachmentPaths[0], EBase64Mode::UrlSafe));
	InlineRunInfo.MetaData.Add(TEXT("directory"), TEXT("0"));
	ComposerEditor->InsertRunAtCursor(FSlateWidgetRun::Create(
		CapturedComposerLayout.ToSharedRef(),
		InlineRunInfo,
		MakeShared<FString>(TEXT("\u200B")),
		FSlateWidgetRun::FWidgetRunInfo(
			SNew(STextBlock).Text(FText::FromString(TEXT("文件标签"))),
			0)));
	ComposerEditor->InsertTextAtCursor(TEXT(" 后"));
	ParseComposerRichText(
		ComposerEditor->GetText().ToString(),
		ParsedComposerMessage,
		ParsedInlineAttachments);
	TestEqual(
		TEXT("Slate 光标插入 widget run 后前后文字位置正确"),
		ParsedComposerMessage,
		FString(TEXT("前 @file:design brief.pdf 后")));
	TestEqual(
		TEXT("Slate widget run 经 marshaller 后仍保留结构化路径"),
		ParsedInlineAttachments.Num(),
		1);

	ComposerEditor->SetText(FText::FromString(TEXT(
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")));
	CapturedComposerLayout->SetWrappingPolicy(
		ETextWrappingPolicy::AllowPerCharacterWrapping);
	CapturedComposerLayout->SetWrappingWidth(72.0f);
	CapturedComposerLayout->UpdateIfNeeded();
	TestTrue(
		TEXT("无空格连续字符超过输入宽度时逐字符换行"),
		CapturedComposerLayout->GetLineViews().Num() > 1);
	for (const FTextLayout::FLineView& LineView :
		CapturedComposerLayout->GetLineViews())
	{
		TestTrue(
			TEXT("连续字符换行后每一行均受输入宽度约束"),
			LineView.Size.X <= 72.5f);
	}

	ComposerEditor->SetText(FText::GetEmpty());
	for (int32 ChipIndex = 0; ChipIndex < 4; ++ChipIndex)
	{
		FRunInfo ChipRunInfo(TEXT("attachment"));
		ChipRunInfo.MetaData.Add(
			TEXT("id"),
			FString::FromInt(ChipIndex));
		ComposerEditor->InsertRunAtCursor(FSlateWidgetRun::Create(
			CapturedComposerLayout.ToSharedRef(),
			ChipRunInfo,
			MakeShared<FString>(TEXT("\u200B")),
			FSlateWidgetRun::FWidgetRunInfo(
				SNew(SBox)
				.WidthOverride(48.0f)
				.HeightOverride(18.0f),
				0)));
	}
	CapturedComposerLayout->SetWrappingWidth(100.0f);
	CapturedComposerLayout->UpdateIfNeeded();
	TestTrue(
		TEXT("连续文件标签放不下时整块移动到下一行"),
		CapturedComposerLayout->GetLineViews().Num() >= 2);
	for (const FTextLayout::FLineView& LineView :
		CapturedComposerLayout->GetLineViews())
	{
		TestTrue(
			TEXT("文件标签换行后不越过输入宽度"),
			LineView.Size.X <= 100.5f);
	}

	TSharedPtr<FSlateTextLayout> ResponsiveComposerLayout;
	const FCreateSlateTextLayout CreateResponsiveComposerLayout =
		FCreateSlateTextLayout::CreateLambda(
			[&ResponsiveComposerLayout](
				SWidget* OwningWidget,
				const FTextBlockStyle& DefaultTextStyle)
			{
				const TSharedRef<FSlateTextLayout> Layout =
					FSlateTextLayout::Create(
						OwningWidget,
						DefaultTextStyle);
				Layout->SetLineBreakIterator(
					FBreakIterator::CreateCharacterBoundaryIterator());
				ResponsiveComposerLayout = Layout;
				return Layout;
			});
	const TSharedRef<SUnrealAgentMCPComposerEditableText> ResponsiveComposer =
		SNew(SUnrealAgentMCPComposerEditableText)
		.Marshaller(FRichTextLayoutMarshaller::Create(
			TArray<TSharedRef<ITextDecorator>>(),
			&FAppStyle::Get()))
		.WrapTextAt(1.0f)
		.AutoWrapText(false)
		.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
		.CreateSlateTextLayout(CreateResponsiveComposerLayout);
	ResponsiveComposer->SetText(FText::FromString(TEXT(
		"这是没有空格的连续中文用于验证输入控件按照真实宽度立即换行")));
	ResponsiveComposer->Tick(
		FGeometry::MakeRoot(
			FVector2f(92.0f, 64.0f),
			FSlateLayoutTransform()),
		0.0,
		0.0f);
	TestTrue(
		TEXT("会话输入控件按真实分配宽度更新折行宽度"),
		FMath::IsNearlyEqual(
			ResponsiveComposerLayout->GetWrappingWidth(),
			90.0f,
			0.5f));
	TestTrue(
		TEXT("真实输入控件中的连续中文不会保持在单行"),
		ResponsiveComposerLayout->GetLineViews().Num() > 1);
	for (const FTextLayout::FLineView& LineView :
		ResponsiveComposerLayout->GetLineViews())
	{
		TestTrue(
			TEXT("真实输入控件每一行均不越过当前宽度"),
			LineView.Size.X <= 90.5f);
	}

	ResponsiveComposer->SetText(FText::GetEmpty());
	for (int32 ChipIndex = 0; ChipIndex < 4; ++ChipIndex)
	{
		FRunInfo ResponsiveChipRunInfo(TEXT("attachment"));
		ResponsiveChipRunInfo.MetaData.Add(
			TEXT("id"),
			FString::FromInt(ChipIndex));
		ResponsiveComposer->InsertRunAtCursor(FSlateWidgetRun::Create(
			ResponsiveComposerLayout.ToSharedRef(),
			ResponsiveChipRunInfo,
			MakeShared<FString>(TEXT("\u200B")),
			FSlateWidgetRun::FWidgetRunInfo(
				SNew(SBox)
				.WidthOverride(88.0f)
				.HeightOverride(18.0f),
				0)));
	}
	ResponsiveComposer->InsertTextAtCursor(TEXT(
		"标签后面的连续文字必须使用剩余空间并在边界处继续软换行"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"));
	ResponsiveComposer->Tick(
		FGeometry::MakeRoot(
			FVector2f(320.0f, 64.0f),
			FSlateLayoutTransform()),
		1.0,
		0.0f);
	TestTrue(
		TEXT("标签文字混排后视口保持在最左侧"),
		FMath::IsNearlyZero(
			ResponsiveComposer->GetHorizontalScrollOffset()));
	TestTrue(
		TEXT("最后一个标签后的剩余行宽会继续放置普通文字"),
		ResponsiveComposerLayout->GetLineViews().Num() > 1
			&& ResponsiveComposerLayout->GetLineViews()[1]
				.Range.EndIndex > 4);
	for (const FTextLayout::FLineView& LineView :
		ResponsiveComposerLayout->GetLineViews())
	{
		TestTrue(
			TEXT("标签文字混排的每一行均受框体宽度约束"),
			LineView.Size.X <= 318.5f);
	}

	ResponsiveComposer->Tick(
		FGeometry::MakeRoot(
			FVector2f(220.0f, 96.0f),
			FSlateLayoutTransform()),
		2.0,
		0.0f);
	TestTrue(
		TEXT("输入框宽度改变后立即使用新的软换行宽度"),
		FMath::IsNearlyEqual(
			ResponsiveComposerLayout->GetWrappingWidth(),
			218.0f,
			0.5f));
	TestTrue(
		TEXT("输入框长宽改变后仍不产生横向滚动"),
		FMath::IsNearlyZero(
			ResponsiveComposer->GetHorizontalScrollOffset()));
	for (const FTextLayout::FLineView& LineView :
		ResponsiveComposerLayout->GetLineViews())
	{
		TestTrue(
			TEXT("尺寸改变后混合内容仍不越过新宽度"),
			LineView.Size.X <= 218.5f);
	}

	TestEqual(
		TEXT("运行中的工具卡片显示具体工具名"),
		BuildToolCallDisplayText(
			TEXT("List MCP Resources"),
			EWorldDataConversationToolState::Running),
		FString(TEXT("正在调用：List MCP Resources")));
	TestEqual(
		TEXT("完成的工具卡片仍保留具体工具名"),
		BuildToolCallDisplayText(
			TEXT("Fetch MCP Resource"),
			EWorldDataConversationToolState::Completed),
		FString(TEXT("调用完成：Fetch MCP Resource")));
	TestEqual(
		TEXT("失败的工具卡片显示工具名和失败状态"),
		BuildToolCallDisplayText(
			TEXT("Read File"),
			EWorldDataConversationToolState::Failed),
		FString(TEXT("调用失败：Read File")));

	TArray<FWorldDataConversationMessage> ActiveToolTurn;
	FWorldDataConversationMessage ToolTurnUser;
	ToolTurnUser.Role = EWorldDataConversationMessageRole::User;
	ActiveToolTurn.Add(ToolTurnUser);
	FWorldDataConversationMessage ToolTurnStatus;
	ToolTurnStatus.Role = EWorldDataConversationMessageRole::Status;
	ActiveToolTurn.Add(ToolTurnStatus);
	FWorldDataConversationMessage ToolTurnCall;
	ToolTurnCall.Role = EWorldDataConversationMessageRole::Tool;
	ToolTurnCall.ToolName = TEXT("Read File");
	ToolTurnCall.ToolState =
		EWorldDataConversationToolState::Completed;
	ActiveToolTurn.Add(ToolTurnCall);
	TestFalse(
		TEXT("Agent 仍在生成时工具分组保持展开"),
		ShouldAutoCollapseToolGroup(ActiveToolTurn, 2));
	ActiveToolTurn[1].bCompleted = true;
	TestTrue(
		TEXT("整轮回复完成后工具分组自动收起"),
		ShouldAutoCollapseToolGroup(ActiveToolTurn, 2));

	const FString MarkdownFixture =
		TEXT("## 标题\n\n- 列表项\n\n")
		TEXT("| 名称 | 说明 |\n|---|---|\n| A | B |\n\n")
		TEXT("```cpp\nint Value = 1;\n```");
	const TArray<FWorldDataMarkdownBlock> MarkdownBlocks =
		UnrealAgentMCPMarkdown::Parse(MarkdownFixture);
	TestEqual(TEXT("Markdown 解析得到四类内容块"), MarkdownBlocks.Num(), 4);
	TestTrue(
		TEXT("Markdown 标题被识别"),
		MarkdownBlocks[0].Type == EWorldDataMarkdownBlockType::Heading
			&& MarkdownBlocks[0].Level == 2
			&& MarkdownBlocks[0].Text == TEXT("标题"));
	TestTrue(
		TEXT("Markdown 列表标记不会进入正文"),
		MarkdownBlocks[1].Type
			== EWorldDataMarkdownBlockType::UnorderedListItem
			&& MarkdownBlocks[1].Text == TEXT("列表项"));
	TestTrue(
		TEXT("Markdown 表格分隔行被消费"),
		MarkdownBlocks[2].Type == EWorldDataMarkdownBlockType::Table
			&& MarkdownBlocks[2].Rows.Num() == 2
			&& MarkdownBlocks[2].Rows[0].Num() == 2);
	TestTrue(
		TEXT("Markdown 代码围栏被移除并保留语言"),
		MarkdownBlocks[3].Type == EWorldDataMarkdownBlockType::CodeBlock
			&& MarkdownBlocks[3].Info == TEXT("cpp")
			&& MarkdownBlocks[3].Text == TEXT("int Value = 1;"));

	const FString RichInline = UnrealAgentMCPMarkdown::InlineToRichText(
		TEXT("**粗体**、`代码`、[链接](https://example.com) <script>"));
	TestTrue(
		TEXT("行内 Markdown 转为富文本样式"),
		RichInline.Contains(TEXT("<WorldDataMarkdown.Bold>粗体</>"))
			&& RichInline.Contains(TEXT("<WorldDataMarkdown.Code>代码</>")));
	TestTrue(
		TEXT("Markdown 链接生成受控浏览器装饰器"),
		RichInline.Contains(TEXT("id=\"browser\""))
			&& RichInline.Contains(TEXT("https://example.com")));
	TestTrue(
		TEXT("原始 HTML 被转义而不会执行"),
		RichInline.Contains(TEXT("&lt;script&gt;")));

	const FString SelectableRichText =
		UnrealAgentMCPMarkdown::ToSelectableRichText(MarkdownFixture);
	TestTrue(
		TEXT("完整 Markdown 被合并为可选择的单一富文本布局"),
		SelectableRichText.Contains(
			TEXT("<WorldDataMarkdown.Heading2>标题</>"))
			&& SelectableRichText.Contains(
				TEXT("<WorldDataMarkdown.ListMarker>•</> 列表项")));
	TestTrue(
		TEXT("表格和代码块在可选择布局中保留视觉样式标记"),
		SelectableRichText.Contains(
			TEXT("<WorldDataMarkdown.TableTop>"))
			&& SelectableRichText.Contains(
				TEXT("<WorldDataMarkdown.TableBottom>"))
			&& SelectableRichText.Contains(
				TEXT("<WorldDataMarkdown.CodeBlockMiddle>")));
	TestFalse(
		TEXT("代码块正文不会保留 Markdown 围栏"),
		SelectableRichText.Contains(TEXT("```")));
	TestTrue(
		TEXT("Markdown 块背景绘制在 Slate 原生文字选区下方"),
		UnrealAgentMCPMarkdown::BackgroundHighlightZOrder < -10);

	const FDateTime CompletionTime(2026, 8, 5, 16, 7, 0);
	TestEqual(
		TEXT("当天完成时间只显示时分"),
		FormatCompletionLabel(
			CompletionTime,
			false,
			FDateTime(2026, 8, 5, 18, 0, 0)),
		FString(TEXT("16:07 完成")));
	TestEqual(
		TEXT("跨天完成时间附带月日"),
		FormatCompletionLabel(
			CompletionTime,
			true,
			FDateTime(2026, 8, 6, 9, 0, 0)),
		FString(TEXT("08-05 16:07 失败")));
	TestTrue(
		TEXT("无完成时间不显示标签"),
		FormatCompletionLabel(
			FDateTime(),
			false,
			FDateTime::Now()).IsEmpty());

	FWorldDataConversation BranchSource;
	BranchSource.ControllerId = TEXT("external_cursor_agent");
	BranchSource.Title = FText::FromString(TEXT("原始任务"));
	BranchSource.ProviderId = TEXT("cursor");
	BranchSource.AgentMode = 1;
	BranchSource.ApprovalPolicy = 2;
	BranchSource.SelfRepairPolicy = 2;
	FWorldDataConversationMessage BranchUserMessage;
	BranchUserMessage.Role = EWorldDataConversationMessageRole::User;
	BranchUserMessage.Text = TEXT("检查这个文件");
	FWorldDataConversationAttachment BranchAttachment;
	BranchAttachment.Path = TEXT("D:/Project/Source/Task.cpp");
	BranchAttachment.DisplayName = TEXT("Task.cpp");
	BranchUserMessage.Attachments.Add(BranchAttachment);
	BranchSource.Messages.Add(BranchUserMessage);
	FWorldDataConversationMessage BranchStatusMessage;
	BranchStatusMessage.Role = EWorldDataConversationMessageRole::Status;
	BranchStatusMessage.Text = TEXT("正在调用工具");
	BranchSource.Messages.Add(BranchStatusMessage);
	FWorldDataConversationMessage BranchAssistantMessage;
	BranchAssistantMessage.Role =
		EWorldDataConversationMessageRole::Assistant;
	BranchAssistantMessage.Text = TEXT("## 结论\n\n已完成检查。");
	BranchAssistantMessage.bCompleted = true;
	BranchAssistantMessage.CompletedAt = CompletionTime;
	BranchSource.Messages.Add(BranchAssistantMessage);
	FWorldDataConversationMessage LaterUserMessage;
	LaterUserMessage.Role = EWorldDataConversationMessageRole::User;
	LaterUserMessage.Text = TEXT("这条不应进入分支");
	BranchSource.Messages.Add(LaterUserMessage);

	FWorldDataConversation ConversationBranch;
	TestTrue(
		TEXT("可以从已完成助手回复创建会话分支"),
		TryCreateConversationBranch(
			BranchSource,
			2,
			FDateTime(2026, 8, 5, 17, 0, 0),
			ConversationBranch));
	TestTrue(
		TEXT("会话分支获得独立 ID"),
		ConversationBranch.Id != BranchSource.Id);
	TestEqual(
		TEXT("会话分支只复制到选定回复"),
		ConversationBranch.Messages.Num(),
		3);
	TestFalse(
		TEXT("分支上下文不包含分支点之后的消息"),
		ConversationBranch.Transcript.Contains(
			LaterUserMessage.Text));
	TestTrue(
		TEXT("分支继承 Provider 并带分支标题"),
		ConversationBranch.ControllerId == BranchSource.ControllerId
			&& ConversationBranch.ProviderId == BranchSource.ProviderId
			&& ConversationBranch.Title.ToString()
				== TEXT("原始任务（分支）"));
	TestTrue(
		TEXT("分支继承三组执行策略"),
		ConversationBranch.AgentMode == BranchSource.AgentMode
			&& ConversationBranch.ApprovalPolicy
				== BranchSource.ApprovalPolicy
			&& ConversationBranch.SelfRepairPolicy
				== BranchSource.SelfRepairPolicy);
	TestTrue(
		TEXT("分支上下文保留附件绝对路径和 Markdown 回复"),
		ConversationBranch.Transcript.Contains(BranchAttachment.Path)
			&& ConversationBranch.Transcript.Contains(
				BranchAssistantMessage.Text));
	TestFalse(
		TEXT("界面状态消息不会污染 Agent 历史上下文"),
		ConversationBranch.Transcript.Contains(
			BranchStatusMessage.Text));
	const FString ReplayedBranchPrompt =
		BuildConversationContextReplayPrompt(
			ConversationBranch.Messages,
			TEXT("继续完成新的修改"));
	TestTrue(
		TEXT("首次分支请求同时包含历史和当前新任务"),
		ReplayedBranchPrompt.Contains(BranchAssistantMessage.Text)
			&& ReplayedBranchPrompt.Contains(TEXT("继续完成新的修改"))
			&& ReplayedBranchPrompt.Contains(
				TEXT("<conversation_history>")));
	TestFalse(
		TEXT("不能从用户消息创建会话分支"),
		TryCreateConversationBranch(
			BranchSource,
			0,
			FDateTime::Now(),
			ConversationBranch));

	TArray<FWorldDataConversationMessage> EditableMessages =
		BranchSource.Messages;
	TestTrue(
		TEXT("重新编辑历史提示词会移除该用户消息及其后的分支"),
		TryRemoveConversationFromUserMessage(EditableMessages, 3));
	TestEqual(
		TEXT("截断后只保留所选用户消息之前的历史"),
		EditableMessages.Num(),
		3);
	TestEqual(
		TEXT("截断不会改动此前已完成的助手回复"),
		EditableMessages.Last().Text,
		BranchAssistantMessage.Text);
	const int32 MessageCountBeforeInvalidRemoval =
		EditableMessages.Num();
	TestFalse(
		TEXT("不能从助手消息执行用户轮次删除"),
		TryRemoveConversationFromUserMessage(EditableMessages, 2));
	TestEqual(
		TEXT("无效删除不会改变会话历史"),
		EditableMessages.Num(),
		MessageCountBeforeInvalidRemoval);

	FWorldDataConversation RewriteConversation = BranchSource;
	RewriteConversation.bIsRunning = true;
	RewriteConversation.bHasUnreadCompletion = true;
	RewriteConversation.ActiveAssistantMessageIndex = 4;
	RewriteConversation.ActiveTurnStatusMessageIndex = 1;
	RewriteConversation.QueuedPrompts.Add(FWorldDataQueuedPrompt());
	TestTrue(
		TEXT("运行中重新编辑会即时准备干净的历史分支"),
		PrepareConversationForUserMessageRewrite(
			RewriteConversation,
			3) == EWorldDataConversationRewriteResult::Prepared);
	TestEqual(
		TEXT("历史重写只保留目标用户消息之前的内容"),
		RewriteConversation.Messages.Num(),
		3);
	TestTrue(
		TEXT("历史重写清空排队消息和运行状态"),
		RewriteConversation.QueuedPrompts.IsEmpty()
			&& !RewriteConversation.bIsRunning
			&& !RewriteConversation.bHasUnreadCompletion);
	TestTrue(
		TEXT("历史重写清空所有流式消息下标"),
		RewriteConversation.ActiveAssistantMessageIndex == INDEX_NONE
			&& RewriteConversation.ActiveTurnStatusMessageIndex
				== INDEX_NONE);
	TestFalse(
		TEXT("历史重写后的语义快照不包含被删除提示词"),
		RewriteConversation.Transcript.Contains(LaterUserMessage.Text));

	EWorldDataConversationMessageRole ParsedRole =
		EWorldDataConversationMessageRole::Assistant;
	FString ParsedText;
	TestTrue(
		TEXT("工具事件能够被识别"),
		TryParseTaggedEvent(TEXT("[工具调用] 读取关卡"), ParsedRole, ParsedText));
	TestTrue(
		TEXT("工具事件角色正确"),
		ParsedRole == EWorldDataConversationMessageRole::Tool);
	TestEqual(TEXT("事件标签被移除"), ParsedText, FString(TEXT("读取关卡")));
	TestFalse(
		TEXT("普通助手文本不会被误判为事件"),
		TryParseTaggedEvent(TEXT("普通回复"), ParsedRole, ParsedText));

	const FString JsonText = TEXT("{\"color\":{\"r\":1.4,\"g\":-0.2,\"b\":0.5},\"cli\":{\"codexPath\":\"\\\"C:/Tools/codex.exe\\\"\",\"cursorPath\":\"cursor-agent\"},\"ui\":{\"activeProvider\":\"cursor\",\"sidebarCollapsed\":true,\"agentMode\":1,\"approvalPolicy\":2,\"selfRepairPolicy\":2},\"context\":{\"tokenCapacity\":524288}}");
	FUnrealAgentMCPPanelSettings ParsedSettings;
	TestTrue(TEXT("有效设置 JSON 能解析"), TryParseSettingsJson(JsonText, ParsedSettings));
	TestTrue(TEXT("主题红色分量被限制到 1"), FMath::IsNearlyEqual(ParsedSettings.AccentColor.R, 1.0f));
	TestTrue(TEXT("主题绿色分量被限制到 0"), FMath::IsNearlyEqual(ParsedSettings.AccentColor.G, 0.0f));
	TestTrue(TEXT("主题色始终不透明"), FMath::IsNearlyEqual(ParsedSettings.AccentColor.A, 1.0f));
	TestEqual(
		TEXT("CLI 路径外层引号被移除"),
		ParsedSettings.CodexCliPath,
		FString(TEXT("C:/Tools/codex.exe")));
	TestEqual(
		TEXT("ACP Provider 偏好被读取"),
		ParsedSettings.ActiveProviderId,
		FString(TEXT("cursor")));
	TestTrue(
		TEXT("v1 显式 Cursor 偏好保守迁移为 Cursor 原生 Agent"),
		ParsedSettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCursorAgent);
	TestTrue(
		TEXT("侧栏收起偏好被读取"),
		ParsedSettings.bSidebarCollapsed);
	TestEqual(TEXT("Agent 模式被读取"), ParsedSettings.DefaultAgentMode, EWorldDataAgentMode::Plan);
	TestEqual(TEXT("审批策略被读取"), ParsedSettings.ApprovalPolicy, EWorldDataApprovalPolicy::FullProject);
	TestEqual(TEXT("自修复策略被读取"), ParsedSettings.SelfRepairPolicy, EWorldDataSelfRepairPolicy::Automatic);
	TestEqual(
		TEXT("上下文容量偏好被读取"),
		ParsedSettings.ContextTokenCapacity,
		512 * 1024);

	FString SerializedSettings;
	TestTrue(
		TEXT("设置能够序列化"),
		TrySerializeSettingsJson(ParsedSettings, SerializedSettings));
	FUnrealAgentMCPPanelSettings RoundTripSettings;
	TestTrue(
		TEXT("序列化结果能够重新解析"),
		TryParseSettingsJson(SerializedSettings, RoundTripSettings));
	TestEqual(TEXT("审批策略往返保持一致"), RoundTripSettings.ApprovalPolicy, ParsedSettings.ApprovalPolicy);
	TestEqual(TEXT("自修复策略往返保持一致"), RoundTripSettings.SelfRepairPolicy, ParsedSettings.SelfRepairPolicy);
	TestEqual(
		TEXT("Codex 路径往返保持一致"),
		RoundTripSettings.CodexCliPath,
		ParsedSettings.CodexCliPath);
	TestTrue(
		TEXT("侧栏偏好往返保持一致"),
		RoundTripSettings.bSidebarCollapsed);
	TestEqual(
		TEXT("上下文容量往返保持一致"),
		RoundTripSettings.ContextTokenCapacity,
		ParsedSettings.ContextTokenCapacity);
	TestTrue(
		TEXT("schema v4 往返保持 Controller 身份"),
		RoundTripSettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCursorAgent);
	TestTrue(
		TEXT("schema v5 写入版本和三代理合同"),
		SerializedSettings.Contains(TEXT("\"schemaVersion\": 5"))
			&& SerializedSettings.Contains(TEXT("\"agent\""))
			&& SerializedSettings.Contains(
				TEXT("\"external_cursor_agent\""))
			&& !SerializedSettings.Contains(TEXT("brainTransport")));

	FUnrealAgentMCPPanelSettings ConflictingControllerSettings;
	TestTrue(
		TEXT("schema v4 冲突设置仍可解析"),
		TryParseSettingsJson(
			TEXT("{\"schemaVersion\":4,\"agent\":{"
				"\"controller\":\"external_cursor_agent\"},"
				"\"ui\":{\"activeProvider\":\"codex\"}}"),
			ConflictingControllerSettings));
	TestEqual(
		TEXT("Cursor 控制者覆盖冲突的 Codex 兼容字段"),
		ConflictingControllerSettings.ActiveProviderId,
		FString(TEXT("cursor")));

	FUnrealAgentMCPPanelSettings InconsistentSettings;
	InconsistentSettings.ControllerMode =
		EUnrealAgentControllerMode::ExternalCursorAgent;
	InconsistentSettings.ActiveProviderId = TEXT("codex");
	FString InconsistentSettingsJson;
	TestTrue(
		TEXT("不一致的内存设置仍可安全序列化"),
		TrySerializeSettingsJson(
			InconsistentSettings,
			InconsistentSettingsJson));
	FUnrealAgentMCPPanelSettings ReparsedInconsistentSettings;
	TestTrue(
		TEXT("安全序列化结果可重新解析"),
		TryParseSettingsJson(
			InconsistentSettingsJson,
			ReparsedInconsistentSettings));
	TestEqual(
		TEXT("序列化不会再次写出 Cursor 与 Codex 分叉"),
		ReparsedInconsistentSettings.ActiveProviderId,
		FString(TEXT("cursor")));

	const FUnrealAgentMCPPanelSettings FreshSettings;
	TestTrue(
		TEXT("新安装默认由 Codex 代理控制任务"),
		FreshSettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCodexAgent);
	TestFalse(
		TEXT("新安装不再把消息路由到 Native Kernel"),
		UsesNativeUnrealAgentController(FreshSettings.ControllerMode));
	TestTrue(
		TEXT("只有历史 Native 枚举值仍被识别为退役控制者"),
		UsesNativeUnrealAgentController(
			EUnrealAgentControllerMode::NativeUnrealAgent)
			&& !UsesNativeUnrealAgentController(
				EUnrealAgentControllerMode::ExternalCodexAgent)
			&& !UsesNativeUnrealAgentController(
				EUnrealAgentControllerMode::ExternalCursorAgent));

	FUnrealAgentMCPPanelSettings NativeV2Settings;
	TestTrue(
		TEXT("schema v2 Native 设置能够解析"),
		TryParseSettingsJson(
			TEXT("{\"schemaVersion\":2,\"agent\":{"
				"\"controller\":\"native_unreal_agent\","
				"\"brainTransport\":\"direct_model_api\"},"
				"\"ui\":{\"activeProvider\":\"codex\","
				"\"directProvider\":\"ollama\"}}"),
			NativeV2Settings));
	TestTrue(
		TEXT("schema v2 Native Controller 读取后迁到 Codex 代理"),
		NativeV2Settings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCodexAgent);
	TestEqual(
		TEXT("schema v2 仍保留历史模型 Provider 字段"),
		NativeV2Settings.DirectProviderId,
		FString(TEXT("ollama")));

	FUnrealAgentMCPPanelSettings NativeCursorSettings;
	TestTrue(
		TEXT("旧 Native 且 activeProvider 为 cursor 能够解析"),
		TryParseSettingsJson(
			TEXT("{\"schemaVersion\":4,\"agent\":{"
				"\"controller\":\"native_unreal_agent\"},"
				"\"ui\":{\"activeProvider\":\"cursor\"}}"),
			NativeCursorSettings));
	TestTrue(
		TEXT("旧 Native + cursor 回退迁到 Cursor 代理"),
		NativeCursorSettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCursorAgent);

	FUnrealAgentMCPPanelSettings LegacyDirectSettings;
	TestTrue(
		TEXT("v1 直连选择能够迁移"),
		TryParseSettingsJson(
			TEXT("{\"ui\":{\"activeProvider\":\"codex\","
				"\"useDirectKernelChannel\":true,"
				"\"directProvider\":\"local\"}}"),
			LegacyDirectSettings));
	TestTrue(
		TEXT("v1 直连选择迁移为 Codex 代理"),
		LegacyDirectSettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCodexAgent);

	FUnrealAgentMCPPanelSettings UnsupportedRelaySettings;
	TestTrue(
		TEXT("旧 ACP 推理中继设置仍可安全读取"),
		TryParseSettingsJson(
			TEXT("{\"schemaVersion\":2,\"agent\":{"
				"\"controller\":\"native_unreal_agent\","
				"\"brainTransport\":\"acp_inference_relay\"}}"),
			UnsupportedRelaySettings));
	TestTrue(
		TEXT("旧 ACP 推理中继迁移到 Codex 代理"),
		UnsupportedRelaySettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCodexAgent
			&& UnsupportedRelaySettings.bMigratedLegacySubscriptionRelay);

	FUnrealAgentMCPPanelSettings CodexRelaySettings;
	TestTrue(
		TEXT("schema v3 Codex relay remains readable for migration"),
		TryParseSettingsJson(
			TEXT("{\"schemaVersion\":3,\"agent\":{"
				"\"controller\":\"native_unreal_agent\","
				"\"brainTransport\":\"codex_subscription_relay\"}}"),
			CodexRelaySettings));
	TestTrue(
		TEXT("Codex relay migrates to Codex agent"),
		CodexRelaySettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCodexAgent
			&& CodexRelaySettings.bMigratedLegacySubscriptionRelay);

	FUnrealAgentMCPPanelSettings CursorRelaySettings;
	TestTrue(
		TEXT("schema v3 Cursor 中继仍可读取以便迁移"),
		TryParseSettingsJson(
			TEXT("{\"schemaVersion\":3,\"agent\":{"
				"\"controller\":\"native_unreal_agent\","
				"\"brainTransport\":\"cursor_subscription_relay\"}}"),
			CursorRelaySettings));
	TestTrue(
		TEXT("Cursor 中继必须迁到 Cursor 代理"),
		CursorRelaySettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCursorAgent
			&& CursorRelaySettings.ActiveProviderId == TEXT("cursor")
			&& CursorRelaySettings.bMigratedLegacySubscriptionRelay);

	FUnrealAgentMCPPanelSettings CursorRelayWithCodexProvider;
	TestTrue(
		TEXT("Cursor 中继即使 activeProvider 仍为 Codex 也能解析"),
		TryParseSettingsJson(
			TEXT("{\"schemaVersion\":3,\"agent\":{"
				"\"controller\":\"native_unreal_agent\","
				"\"brainTransport\":\"cursor_subscription_relay\"},"
				"\"ui\":{\"activeProvider\":\"codex\"}}"),
			CursorRelayWithCodexProvider));
	TestTrue(
		TEXT("transport 中的 Cursor 身份覆盖冲突的 Codex Provider"),
		CursorRelayWithCodexProvider.ControllerMode
			== EUnrealAgentControllerMode::ExternalCursorAgent
			&& CursorRelayWithCodexProvider.ActiveProviderId
				== TEXT("cursor"));
	FString CursorRelayJson;
	TestTrue(
		TEXT("migrated settings serialize"),
		TrySerializeSettingsJson(CursorRelaySettings, CursorRelayJson));
	TestTrue(
		TEXT("migrated settings remove relay fields and write schema v5"),
		CursorRelayJson.Contains(TEXT("\"schemaVersion\": 5"))
			&& !CursorRelayJson.Contains(TEXT("brainTransport"))
			&& !CursorRelayJson.Contains(TEXT("subscription_relay"))
			&& !CursorRelayJson.Contains(TEXT("native_unreal_agent")));

	const FString SettingsTestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("UnrealAgent"),
		TEXT("Automation"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString SettingsTestPath = FPaths::Combine(
		SettingsTestDirectory, TEXT("settings-v2.json"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(
			*SettingsTestDirectory, false, true);
	};
	TestTrue(
		TEXT("schema v5 设置通过原子写入口落盘"),
		SaveSettingsFile(SettingsTestPath, NativeV2Settings));
	FUnrealAgentMCPPanelSettings LoadedSettings;
	TestTrue(
		TEXT("原子落盘后的设置能够重新加载"),
		LoadSettingsFile(SettingsTestPath, LoadedSettings));
	TestTrue(
		TEXT("落盘往返不再写出 Native Controller"),
		LoadedSettings.ControllerMode
			== EUnrealAgentControllerMode::ExternalCodexAgent);
	TestEqual(
		TEXT("滑条左端对应 256K"),
		SliderValueToContextTokenCapacity(0.0f),
		256 * 1024);
	TestEqual(
		TEXT("滑条中间对应推荐 512K 档位"),
		SliderValueToContextTokenCapacity(0.5f),
		512 * 1024);
	TestEqual(
		TEXT("滑条右端对应 1M"),
		SliderValueToContextTokenCapacity(1.0f),
		1024 * 1024);

	FString ExpectedDetectedCliPath = TEXT("C:/Detected/codex.exe");
	FPaths::MakePlatformFilename(ExpectedDetectedCliPath);
	TestEqual(
		TEXT("未显式配置时使用自动检测路径"),
		ResolveEffectiveCliPath(FString(), TEXT("C:/Detected/codex.exe")),
		ExpectedDetectedCliPath);

	FString ExpectedManagedCodexPath = FPaths::Combine(
		TEXT("C:/Project/Saved"),
		TEXT("UnrealAgent"),
		TEXT("ACPAdapters"),
		TEXT("Codex"),
		TEXT("node_modules"),
#if PLATFORM_WINDOWS
		TEXT("@openai"),
		TEXT("codex-win32-x64"),
		TEXT("vendor"),
		TEXT("x86_64-pc-windows-msvc"),
		TEXT("bin"),
		TEXT("codex.exe"));
#else
		TEXT(".bin"),
		TEXT("codex"));
#endif
	TestEqual(
		TEXT("项目本地 Codex CLI 候选路径稳定"),
		GetManagedCodexCliCandidatePath(TEXT("C:/Project/Saved")),
		ExpectedManagedCodexPath);

	FString ExpectedManagedCursorPath = FPaths::Combine(
		TEXT("C:/Project/Saved"),
		TEXT("UnrealAgent"),
		TEXT("ACPAdapters"),
		TEXT("Cursor"),
#if PLATFORM_WINDOWS
		TEXT("cursor-agent.cmd"));
#else
		TEXT("cursor-agent"));
#endif
	TestEqual(
		TEXT("项目本地 Cursor Agent 启动路径稳定"),
		GetManagedCursorCliCandidatePath(TEXT("C:/Project/Saved")),
		ExpectedManagedCursorPath);

	int32 InstallerPercent = 0;
	FString InstallerStage;
	FString InstallerComponent;
	TestTrue(
		TEXT("安装器进度协议可解析"),
		IsInstallerProgressLine(
			TEXT("UEBRIDGE_PROGRESS|42|downloading|Node.js LTS"),
			InstallerPercent,
			InstallerStage,
			InstallerComponent));
	TestEqual(TEXT("安装进度百分比"), InstallerPercent, 42);
	TestEqual(TEXT("安装阶段"), InstallerStage, FString(TEXT("downloading")));
	TestEqual(TEXT("安装组件"), InstallerComponent, FString(TEXT("Node.js LTS")));
	TestFalse(
		TEXT("普通日志不会被误认为安装进度"),
		IsInstallerProgressLine(
			TEXT("npm install completed"),
			InstallerPercent,
			InstallerStage,
			InstallerComponent));

	FString ExpectedInstallerDirectory = TEXT("C:/Project/Saved");
	FPaths::MakePlatformFilename(ExpectedInstallerDirectory);
	TestEqual(
		TEXT("安装目录参数移除尾部分隔符以保护命令行引号"),
		NormalizeInstallerDirectoryArgument(TEXT("C:/Project/Saved/")),
		ExpectedInstallerDirectory);

	FString ExistingNpmCandidate = FPaths::GetProjectFilePath();
	FPaths::MakePlatformFilename(ExistingNpmCandidate);
	TestEqual(
		TEXT("npm 候选解析会跳过缺失路径并返回首个现有文件"),
		ResolveNpmPathFromCandidates(
			{
				TEXT("Z:/Missing/npm.cmd"),
				FPaths::GetProjectFilePath()
			}),
		ExistingNpmCandidate);
	TestTrue(
		TEXT("npm 候选全部缺失时返回空路径"),
		ResolveNpmPathFromCandidates(
			{
				TEXT("Z:/Missing/npm.cmd"),
				TEXT("Z:/AlsoMissing/npm.cmd")
			}).IsEmpty());

	TestEqual(
		TEXT("邮箱账户头像使用 @ 前的首字符"),
		UnrealAgentACPProviderModel::MakeAccountInitials(
			TEXT("developer@example.com")),
		FString(TEXT("D")));
	TestEqual(
		TEXT("双词账户头像使用两个首字母"),
		UnrealAgentACPProviderModel::MakeAccountInitials(TEXT("World Data")),
		FString(TEXT("WD")));
	TestEqual(
		TEXT("空账户提供稳定回退字符"),
		UnrealAgentACPProviderModel::MakeAccountInitials(FString()),
		FString(TEXT("U")));
	const FUnrealAgentACPProviderDescriptor& CursorProvider =
		UnrealAgentACPProviderModel::GetProvider(
			EUnrealAgentACPProvider::Cursor);
	TestTrue(
		TEXT("Cursor 官方 ACP Agent 支持内嵌会话"),
		CursorProvider.bSupportsEmbeddedConversation);
	TestEqual(
		TEXT("Cursor 使用官方 agent acp 命令入口"),
		CursorProvider.CliCommand,
		FString(TEXT("agent")));
	const FUnrealAgentACPAccountState CursorAccount =
		UnrealAgentACPProviderModel::ParseCursorAccountStateJson(
			TEXT("{\"status\":\"authenticated\",\"isAuthenticated\":true,\"userInfo\":{\"email\":\"jim@example.com\",\"firstName\":\"Jim\",\"lastName\":\"Davis\"}}"));
	TestTrue(
		TEXT("Cursor 官方 status JSON 能识别已登录"),
		CursorAccount.bAuthenticated);
	TestEqual(
		TEXT("Cursor 左下角显示真实账户姓名"),
		CursorAccount.DisplayLabel,
		FString(TEXT("Jim Davis")));
	TestEqual(
		TEXT("Cursor 账户菜单保留邮箱副标题"),
		CursorAccount.SecondaryLabel,
		FString(TEXT("jim@example.com")));

	const FString UserProfile =
		FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		FString ExpectedAuthPath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(
				UserProfile,
				TEXT(".codex"),
				TEXT("auth.json")));
		FPaths::NormalizeFilename(ExpectedAuthPath);
		TestTrue(
			TEXT("Windows 用户目录下的 Codex 凭据路径会被检测"),
			UnrealAgentACPProviderModel::GetCodexAuthCandidatePaths().Contains(
				ExpectedAuthPath));
	}

	TArray<FWorldDataConversation> ArchiveCandidates;
	ArchiveCandidates.SetNum(3);
	ArchiveCandidates[0].bArchived = true;
	TestEqual(
		TEXT("归档后选择首个仍可见的会话"),
		FindFirstUnarchivedConversation(ArchiveCandidates, 0),
		1);
	ArchiveCandidates[1].bArchived = true;
	ArchiveCandidates[2].bArchived = true;
	TestEqual(
		TEXT("所有旧会话归档后要求新建会话"),
		FindFirstUnarchivedConversation(ArchiveCandidates, 0),
		INDEX_NONE);

	TArray<FWorldDataConversation> StoredConversations;
	FWorldDataConversation StoredConversation;
	StoredConversation.ControllerId = TEXT("external_cursor_agent");
	StoredConversation.Title = FText::FromString(TEXT("持久化对话"));
	StoredConversation.ProviderId = TEXT("cursor");
	StoredConversation.AgentMode = 1;
	StoredConversation.ApprovalPolicy = 2;
	StoredConversation.SelfRepairPolicy = 2;
	StoredConversation.bHasCustomTitle = true;
	FWorldDataConversationMessage StoredMessage;
	StoredMessage.Role = EWorldDataConversationMessageRole::User;
	StoredMessage.Text = TEXT("关闭编辑器后仍需保留");
	StoredMessage.bCompleted = true;
	StoredMessage.CompletedAt = CompletionTime;
	FWorldDataConversationAttachment StoredFileAttachment;
	StoredFileAttachment.Path = TEXT("D:/References/design brief.pdf");
	StoredFileAttachment.DisplayName = TEXT("design brief.pdf");
	StoredMessage.Attachments.Add(StoredFileAttachment);
	FWorldDataConversationAttachment StoredFolderAttachment;
	StoredFolderAttachment.Path = TEXT("D:/References/source-folder");
	StoredFolderAttachment.DisplayName = TEXT("source-folder");
	StoredFolderAttachment.bDirectory = true;
	StoredFolderAttachment.bInline = true;
	StoredMessage.Attachments.Add(StoredFolderAttachment);
	StoredConversation.Messages.Add(StoredMessage);
	FWorldDataConversationMessage StoredToolMessage;
	StoredToolMessage.Role = EWorldDataConversationMessageRole::Tool;
	StoredToolMessage.Text = TEXT("调用完成：Read File");
	StoredToolMessage.ToolCallId = TEXT("tool-call-17");
	StoredToolMessage.ToolName = TEXT("Read File");
	StoredToolMessage.ToolState =
		EWorldDataConversationToolState::Completed;
	StoredToolMessage.bCompleted = true;
	StoredConversation.Messages.Add(StoredToolMessage);
	StoredConversation.Transcript = StoredMessage.Text;
	StoredConversation.bHasUnreadCompletion = true;
	StoredConversation.bIsRunning = true;
	StoredConversation.ContextContinuitySnapshot =
		TEXT("architecture=Core->ProviderSDK->Editor; level=MapOfTestMCP");
	StoredConversation.ContextTokenCapacity = 768 * 1024;
	FWorldDataContextCompactionRecord StoredCompaction;
	StoredCompaction.Generation = 2;
	StoredCompaction.CompactedAtUtc = CompletionTime;
	StoredCompaction.ContextTokenCapacity = StoredConversation.ContextTokenCapacity;
	StoredCompaction.EstimatedTokensBefore = 700000;
	StoredCompaction.EstimatedTokensAfter = 32000;
	StoredCompaction.SummaryThroughMessageIndex = 14;
	StoredConversation.ContextCompactionHistory.Add(StoredCompaction);
	FWorldDataQueuedPrompt StoredQueuedPrompt;
	StoredQueuedPrompt.Text = TEXT("完成后继续检查附件");
	StoredQueuedPrompt.ProviderId = TEXT("codex");
	StoredQueuedPrompt.ModelId = TEXT("gpt-5.6-sol");
	StoredQueuedPrompt.ReasoningEffort = TEXT("high");
	StoredQueuedPrompt.ServiceTier = TEXT("fast");
	StoredQueuedPrompt.AgentMode = 2;
	StoredQueuedPrompt.ApprovalPolicy = 2;
	StoredQueuedPrompt.SelfRepairPolicy = 2;
	StoredQueuedPrompt.Attachments.Add(StoredFolderAttachment);
	StoredConversation.QueuedPrompts.Add(StoredQueuedPrompt);
	StoredConversations.Add(StoredConversation);

	FString StoredJson;
	TestTrue(
		TEXT("会话历史能够序列化"),
		UnrealAgentMCPConversationStore::TrySerializeHistory(
			StoredConversations,
			0,
			StoredJson));
	TestTrue(
		TEXT("会话历史写入代理控制者合同"),
		StoredJson.Contains(TEXT("\"version\": 8"))
			&& StoredJson.Contains(
				TEXT("\"controller\": \"external_cursor_agent\"")));
	TestFalse(
		TEXT("持久化不再写入重复 transcript"),
		StoredJson.Contains(TEXT("\"transcript\"")));
	TArray<FWorldDataConversation> RestoredConversations;
	int32 RestoredActiveIndex = INDEX_NONE;
	TestTrue(
		TEXT("会话历史能够恢复"),
		UnrealAgentMCPConversationStore::TryParseHistory(
			StoredJson,
			RestoredConversations,
			RestoredActiveIndex));
	TestEqual(
		TEXT("恢复后仍选中同一会话"),
		RestoredActiveIndex,
		0);
	TestEqual(
		TEXT("恢复后消息正文保持一致"),
		RestoredConversations[0].Messages[0].Text,
		StoredMessage.Text);
	TestEqual(
		TEXT("恢复后消息稳定 ID 保持一致"),
		RestoredConversations[0].Messages[0].Id,
		StoredMessage.Id);
	TestEqual(
		TEXT("恢复后附件数量保持一致"),
		RestoredConversations[0].Messages[0].Attachments.Num(),
		2);
	TestEqual(
		TEXT("恢复后附件显示名保持一致"),
		RestoredConversations[0].Messages[0].Attachments[0].DisplayName,
		StoredFileAttachment.DisplayName);
	TestEqual(
		TEXT("恢复后附件路径保持一致"),
		RestoredConversations[0].Messages[0].Attachments[1].Path,
		StoredFolderAttachment.Path);
	TestTrue(
		TEXT("恢复后文件夹类型保持一致"),
		RestoredConversations[0].Messages[0].Attachments[1].bDirectory);
	TestTrue(
		TEXT("恢复后行内引用来源保持一致"),
		RestoredConversations[0].Messages[0].Attachments[1].bInline);
	TestTrue(
		TEXT("恢复后消息完成状态保持一致"),
		RestoredConversations[0].Messages[0].bCompleted);
	TestEqual(
		TEXT("恢复后消息完成时间保持一致"),
		RestoredConversations[0].Messages[0].CompletedAt,
		CompletionTime);
	TestEqual(
		TEXT("恢复后 Provider 保持一致"),
		RestoredConversations[0].ProviderId,
		FString(TEXT("cursor")));
	TestEqual(
		TEXT("恢复后代理控制者保持一致"),
		RestoredConversations[0].ControllerId,
		FString(TEXT("external_cursor_agent")));
	TestTrue(
		TEXT("会话自身的三组执行策略能够持久化"),
		RestoredConversations[0].AgentMode == 1
			&& RestoredConversations[0].ApprovalPolicy == 2
			&& RestoredConversations[0].SelfRepairPolicy == 2);
	TestTrue(
		TEXT("上下文容量、连续性快照和压缩记录能够持久化"),
		RestoredConversations[0].ContextTokenCapacity == 768 * 1024
			&& RestoredConversations[0].ContextContinuitySnapshot.Contains(
				TEXT("MapOfTestMCP"))
			&& RestoredConversations[0].ContextCompactionHistory.Num() == 1
			&& RestoredConversations[0].ContextCompactionHistory[0].Generation
				== 2
			&& RestoredConversations[0].ContextCompactionHistory[0].EstimatedTokensBefore
				== 700000);
	TestTrue(
		TEXT("工具调用 ID、名称和状态能够持久化"),
		RestoredConversations[0].Messages.Num() == 2
			&& RestoredConversations[0].Messages[1].ToolCallId
				== StoredToolMessage.ToolCallId
			&& RestoredConversations[0].Messages[1].ToolName
				== StoredToolMessage.ToolName
			&& RestoredConversations[0].Messages[1].ToolState
				== EWorldDataConversationToolState::Completed);
	TestTrue(
		TEXT("待执行队列及代理模型快照能够持久化"),
		RestoredConversations[0].QueuedPrompts.Num() == 1
			&& RestoredConversations[0].QueuedPrompts[0].Text
				== StoredQueuedPrompt.Text
			&& RestoredConversations[0].QueuedPrompts[0].ProviderId
				== StoredQueuedPrompt.ProviderId
			&& RestoredConversations[0].QueuedPrompts[0].ModelId
				== StoredQueuedPrompt.ModelId
			&& RestoredConversations[0].QueuedPrompts[0].ReasoningEffort
				== StoredQueuedPrompt.ReasoningEffort
			&& RestoredConversations[0].QueuedPrompts[0].ServiceTier
				== StoredQueuedPrompt.ServiceTier
			&& RestoredConversations[0].QueuedPrompts[0].AgentMode == 2
			&& RestoredConversations[0].QueuedPrompts[0].ApprovalPolicy == 2
			&& RestoredConversations[0].QueuedPrompts[0].SelfRepairPolicy == 2
			&& RestoredConversations[0].QueuedPrompts[0].Attachments.Num() == 1);
	TestTrue(
		TEXT("后台完成未读状态能够持久化"),
		RestoredConversations[0].bHasUnreadCompletion);
	TestFalse(
		TEXT("编辑器重启后不会伪装成仍在运行"),
		RestoredConversations[0].bIsRunning);

	const FString SensitiveSample = FString::Printf(
		TEXT("Authorization: Bearer %s"),
		*FString::ChrN(64, TCHAR('a')));
	const FString RedactedSample = SanitizeSensitiveText(SensitiveSample);
	TestFalse(
		TEXT("会话文本脱敏不会保留原始令牌"),
		RedactedSample.Contains(FString::ChrN(64, TCHAR('a'))));
	TestTrue(
		TEXT("会话文本脱敏留下明确占位符"),
		RedactedSample.Contains(TEXT("REDACTED")));
	TestTrue(
		TEXT("工具标题会折叠换行并限制长度"),
		!SanitizeToolDisplayName(
			FString::ChrN(220, TCHAR('x')) + TEXT("\ncommand"))
			.Contains(TEXT("\n"))
			&& SanitizeToolDisplayName(FString::ChrN(220, TCHAR('x'))).Len()
				<= 160);

	FWorldDataConversation ContextConversation;
	ContextConversation.ContextTokenCapacity = 64;
	for (int32 TurnIndex = 0; TurnIndex < 10; ++TurnIndex)
	{
		FWorldDataConversationMessage ContextUser;
		ContextUser.Role = EWorldDataConversationMessageRole::User;
		ContextUser.Text = FString::Printf(
			TEXT("用户任务 %d：%s"),
			TurnIndex,
			*FString::ChrN(48, TCHAR(0x4EFB)));
		ContextConversation.Messages.Add(MoveTemp(ContextUser));
		FWorldDataConversationMessage ContextAssistant;
		ContextAssistant.Role = EWorldDataConversationMessageRole::Assistant;
		ContextAssistant.Text = FString::Printf(
			TEXT("完成任务 %d：%s"),
			TurnIndex,
			*FString::ChrN(48, TCHAR(0x7B54)));
		ContextConversation.Messages.Add(MoveTemp(ContextAssistant));
	}
	RefreshConversationContextMetrics(ContextConversation);
	FWorldDataConversation UsageConversation;
	UsageConversation.ContextTokenCapacity = 1000;
	UsageConversation.EstimatedContextTokens = 282;
	TestTrue(
		TEXT("上下文占用比例由估算 token 与当前容量计算"),
		FMath::IsNearlyEqual(
			GetContextUsageRatio(UsageConversation),
			0.282));
	UsageConversation.EstimatedContextTokens = 1200;
	TestTrue(
		TEXT("上下文占用比例上限为 100%"),
		FMath::IsNearlyEqual(GetContextUsageRatio(UsageConversation), 1.0));
	TestEqual(
		TEXT("上下文悬停信息使用紧凑 token 格式"),
		FormatEstimatedTokenCount(74650),
		FString(TEXT("72.9K")));
	TestTrue(
		TEXT("上下文达到预算阈值后要求自动压缩"),
		ShouldAutoCompactContext(ContextConversation));
	TestTrue(
		TEXT("滚动摘要会保留最近四轮并折叠更早消息"),
		CompactConversationContext(
			ContextConversation,
			4,
			48000,
			TEXT("architecture=Core->ProviderSDK->Editor; state=MapOfTestMCP"))
			&& ContextConversation.ContextGeneration == 1
			&& ContextConversation.ContextSummaryThroughMessageIndex > 0
			&& !ContextConversation.ContextSummary.IsEmpty()
			&& ContextConversation.ContextCompactionHistory.Num() == 1);
	const FString CompactedReplay = BuildConversationContextReplayPrompt(
		ContextConversation,
		TEXT("继续"));
	TestTrue(
		TEXT("压缩后重放同时包含摘要代次与当前请求"),
		CompactedReplay.Contains(TEXT("generation=\"1\""))
			&& CompactedReplay.Contains(TEXT("project_continuity_snapshot"))
			&& CompactedReplay.Contains(TEXT("MapOfTestMCP"))
			&& CompactedReplay.Contains(TEXT("继续")));

	const FString LegacyHistoryJson = TEXT(R"JSON(
{
  "version": 1,
  "conversations": [
    {
      "id": "b5c0b1cd-306a-4dc7-b3ac-dff5f2480f54",
      "title": "legacy",
      "messages": [
        { "role": "user", "text": "legacy message" }
      ]
    }
  ]
}
)JSON");
	TArray<FWorldDataConversation> LegacyConversations;
	int32 LegacyActiveIndex = INDEX_NONE;
	TestTrue(
		TEXT("旧版无附件字段的历史仍能恢复"),
		UnrealAgentMCPConversationStore::TryParseHistory(
			LegacyHistoryJson,
			LegacyConversations,
			LegacyActiveIndex));
	TestTrue(
		TEXT("旧版消息恢复为无附件"),
		LegacyConversations.Num() == 1
			&& LegacyConversations[0].Messages.Num() == 1
			&& LegacyConversations[0].Messages[0].Attachments.IsEmpty());
	TestTrue(
		TEXT("旧版历史保留空控制者标记供面板按当前代理迁移"),
		LegacyConversations[0].ControllerId.IsEmpty());

	TestEqual(
		TEXT("实时目录模型名按 Codex 选择器格式展示"),
		WorldDataCodexAcpRules::FormatModelDisplayName(
			TEXT("GPT-5.6-Sol")),
		FString(TEXT("5.6 Sol")));

#if PLATFORM_WINDOWS
	const FString DetectedCodexPath =
		DetectCliPath(EUnrealAgentMCPPanelCliTool::Codex);
	if (!DetectedCodexPath.IsEmpty())
	{
		TestFalse(
			TEXT("Windows 自动检测不会返回不可直接启动的无扩展 npm shim"),
			FPaths::GetExtension(DetectedCodexPath).IsEmpty());
	}

	FWorldDataCliProcessLaunchSpec LoginLaunchSpec;
	TestTrue(
		TEXT("Codex cmd shim 可生成浏览器登录进程规格"),
		WorldDataCliProcessRules::BuildLaunchSpec(
			TEXT("C:/Tools/codex.cmd"),
			TEXT("login"),
			TEXT("C:/Windows/System32/cmd.exe"),
			FString(),
			LoginLaunchSpec));
	TestTrue(
		TEXT("Codex 登录通过 cmd 宿主并保留 login 子命令"),
		LoginLaunchSpec.Arguments.Contains(TEXT("codex.cmd"))
			&& LoginLaunchSpec.Arguments.Contains(TEXT("login")));
#endif

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPMarkdownSelectionTest, "WorldData.UnrealAgent.Editor.PanelModel.MarkdownSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPMarkdownSelectionTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SUnrealAgentMCPMarkdown> MarkdownWidget =
		SNew(SUnrealAgentMCPMarkdown)
		.Markdown(TEXT("## 标题\n\n- 列表项\n\n```cpp\nint Value = 1;\n```"))
		.TextColor(FLinearColor::White)
		.MutedColor(FLinearColor::Gray)
		.AccentColor(FLinearColor(0.2f, 0.55f, 1.0f))
		.SurfaceColor(FLinearColor(0.08f, 0.09f, 0.11f))
		.BorderColor(FLinearColor(0.2f, 0.22f, 0.26f));

	TestEqual(
		TEXT("Markdown 回复由单一文本布局承载"),
		MarkdownWidget->GetChildren()->Num(),
		1);
	const TSharedRef<SWidget> Child =
		MarkdownWidget->GetChildren()->GetChildAt(0);
	TestEqual(
		TEXT("Markdown 回复使用支持跨消息选区的多行文本控件"),
		Child->GetTypeAsString(),
		FString(TEXT("SUnrealAgentMCPSelectableText")));

	const TSharedRef<SMultiLineEditableText> SelectableText =
		StaticCastSharedRef<SMultiLineEditableText>(Child);
	TestTrue(
		TEXT("可选择文本保持只读"),
		SelectableText->IsTextReadOnly());
	TestTrue(
		TEXT("可选择文本可获得键盘焦点以接收 Ctrl+C"),
		Child->SupportsKeyboardFocus());
	SelectableText->SelectAllText();
	const FString SelectedText = SelectableText->GetSelectedText().ToString();
	TestTrue(
		TEXT("跨 Markdown 块选择得到可读正文"),
		SelectedText.Contains(TEXT("标题"))
			&& SelectedText.Contains(TEXT("列表项"))
			&& SelectedText.Contains(TEXT("int Value = 1;")));
	TestFalse(
		TEXT("选择结果不包含 Slate 富文本标签"),
		SelectedText.Contains(TEXT("WorldDataMarkdown.")));

	MarkdownWidget->SetMarkdown(TEXT("**Streaming update**\n\nSecond chunk"));
	SelectableText->SelectAllText();
	const FString UpdatedText =
		SelectableText->GetSelectedText().ToString();
	TestTrue(
		TEXT("Markdown widget updates content without rebuilding its child"),
		UpdatedText.Contains(TEXT("Streaming update"))
			&& UpdatedText.Contains(TEXT("Second chunk")));
	TestTrue(
		TEXT("Incremental Markdown update preserves the text widget"),
		&MarkdownWidget->GetChildren()->GetChildAt(0).Get() == &Child.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPConversationStoreRecoveryTest, "WorldData.UnrealAgent.Editor.PanelModel.ConversationStoreRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPConversationStoreRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("UnrealAgentConversationStore"));
	const FString HistoryPath = FPaths::Combine(TestDirectory, FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".json"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*HistoryPath);
		IFileManager::Get().Delete(*(HistoryPath + TEXT(".bak")));
		IFileManager::Get().Delete(*(HistoryPath + TEXT(".tmp")));
	};

	FWorldDataConversation Conversation;
	Conversation.Title = FText::FromString(TEXT("backup-version"));
	TArray<FWorldDataConversation> Conversations{ Conversation };
	TestTrue(TEXT("首次会话快照保存成功"), UnrealAgentMCPConversationStore::SaveHistoryFile(HistoryPath, Conversations, 0));
	Conversations[0].Title = FText::FromString(TEXT("current-version"));
	TestTrue(TEXT("第二次保存生成有效备份"), UnrealAgentMCPConversationStore::SaveHistoryFile(HistoryPath, Conversations, 0));
	TestTrue(TEXT("备份文件存在"), FPaths::FileExists(HistoryPath + TEXT(".bak")));
	TestTrue(TEXT("模拟主文件损坏"), FFileHelper::SaveStringToFile(TEXT("{invalid"), *HistoryPath));

	TArray<FWorldDataConversation> RestoredConversations;
	int32 RestoredActiveIndex = INDEX_NONE;
	TestTrue(TEXT("主文件损坏后回退到上一个有效快照"), UnrealAgentMCPConversationStore::LoadHistoryFile(HistoryPath, RestoredConversations, RestoredActiveIndex));
	TestEqual(TEXT("恢复一个会话"), RestoredConversations.Num(), 1);
	if (RestoredConversations.Num() == 1)
	{
		TestEqual(TEXT("恢复的是上一个有效版本"), RestoredConversations[0].Title.ToString(), FString(TEXT("backup-version")));
	}
	return true;
}

#endif
