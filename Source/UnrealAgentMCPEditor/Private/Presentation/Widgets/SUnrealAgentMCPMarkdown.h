// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file SUnrealAgentMCPMarkdown.h
 * @brief 将安全 Markdown 渲染为支持鼠标框选的只读 Slate 富文本。
 */

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"
#include "Widgets/SCompoundWidget.h"

class ITextLayoutMarshaller;
class FUnrealAgentMCPTextSelectionGroup;
class SMultiLineEditableText;

/** 为视觉上连续的 Agent 回合创建统一的鼠标选择范围。 */
TSharedRef<FUnrealAgentMCPTextSelectionGroup> CreateUnrealAgentMCPTextSelectionGroup();

class SUnrealAgentMCPMarkdown : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAgentMCPMarkdown) {}
		SLATE_ARGUMENT(FString, Markdown)
		SLATE_ARGUMENT(FLinearColor, TextColor)
		SLATE_ARGUMENT(FLinearColor, MutedColor)
		SLATE_ARGUMENT(FLinearColor, AccentColor)
		SLATE_ARGUMENT(FLinearColor, SurfaceColor)
		SLATE_ARGUMENT(FLinearColor, BorderColor)
		SLATE_ARGUMENT(
			TSharedPtr<FUnrealAgentMCPTextSelectionGroup>,
			SelectionGroup)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetMarkdown(const FString& Markdown);

private:
	void ConfigureStyles();

	FLinearColor TextColor;
	FLinearColor MutedColor;
	FLinearColor AccentColor;
	FLinearColor SurfaceColor;
	FLinearColor BorderColor;
	FTextBlockStyle BodyTextStyle;
	FTextBlockStyle HeadingTextStyles[6];
	TSharedPtr<FSlateStyleSet> MarkdownStyleSet;
	TSharedPtr<ITextLayoutMarshaller> TextMarshaller;
	TSharedPtr<SMultiLineEditableText> SelectableTextWidget;
};
