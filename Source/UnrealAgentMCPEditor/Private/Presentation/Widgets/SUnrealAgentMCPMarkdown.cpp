// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file SUnrealAgentMCPMarkdown.cpp
 * @brief 对话 Markdown 的可选择只读富文本布局。
 */

#include "Presentation/Widgets/SUnrealAgentMCPMarkdown.h"

#include "Core/Conversation/UnrealAgentMCPMarkdown.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Text/ISlateLineHighlighter.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "Framework/Text/TextDecorators.h"
#include "Framework/Text/TextLineHighlight.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Widgets/Text/SMultiLineEditableText.h"

class SUnrealAgentMCPSelectableText;

class FUnrealAgentMCPTextSelectionGroup : public TSharedFromThis<FUnrealAgentMCPTextSelectionGroup>
{
public:
	void Register(const TSharedRef<SUnrealAgentMCPSelectableText>& Widget);
	void BeginSelection(SUnrealAgentMCPSelectableText& Widget, const FTextLocation& Location);
	void UpdateSelection(const FVector2D& ScreenPosition);
	void EndSelection();
	bool CopySelectionToClipboard();
	void ClearSelection();

private:
	TArray<TSharedRef<SUnrealAgentMCPSelectableText>> GetWidgetsInVisualOrder();
	int32 FindWidgetIndex(const TArray<TSharedRef<SUnrealAgentMCPSelectableText>>& Widgets, const SUnrealAgentMCPSelectableText* Widget) const;

	TArray<TWeakPtr<SUnrealAgentMCPSelectableText>> Widgets;
	TWeakPtr<SUnrealAgentMCPSelectableText> AnchorWidget;
	FTextLocation AnchorLocation;
	bool bSelecting = false;
};

namespace
{
	TWeakPtr<FUnrealAgentMCPTextSelectionGroup> ActiveSelectionGroup;
}

class SUnrealAgentMCPSelectableText final : public SMultiLineEditableText
{
public:
	void SetSelectionGroup(const TSharedPtr<FUnrealAgentMCPTextSelectionGroup>& InSelectionGroup)
	{
		SelectionGroup = InSelectionGroup;
		if (SelectionGroup.IsValid())
		{
			SelectionGroup->Register(SharedThis(this));
		}
	}

	FTextLocation ResolveTextLocation(const FVector2D& ScreenPosition)
	{
		if (HasMouseCapture())
		{
			return GetCursorLocation();
		}

		const FGeometry& Geometry = GetCachedGeometry();
		TSet<FKey> PressedButtons;
		PressedButtons.Add(EKeys::LeftMouseButton);
		const FPointerEvent PointerEvent(0, ScreenPosition, ScreenPosition, PressedButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		SlateEditableTextLayout.HandleMouseButtonDown(Geometry, PointerEvent);
		return GetCursorLocation();
	}

	FTextLocation GetDocumentStart() const
	{
		return FTextLocation(0, 0);
	}

	FTextLocation GetDocumentEnd() const
	{
		const int32 LineCount = GetTextLineCount();
		if (LineCount <= 0)
		{
			return FTextLocation(0, 0);
		}
		FString LastLine;
		GetTextLine(LineCount - 1, LastLine);
		return FTextLocation(LineCount - 1, LastLine.Len());
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		FReply Reply = SMultiLineEditableText::OnMouseButtonDown(MyGeometry, MouseEvent);
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && SelectionGroup.IsValid())
		{
			SelectionGroup->BeginSelection(*this, GetCursorLocation());
		}
		return Reply;
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		FReply Reply = SMultiLineEditableText::OnMouseMove(MyGeometry, MouseEvent);
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && SelectionGroup.IsValid())
		{
			SelectionGroup->UpdateSelection(MouseEvent.GetScreenSpacePosition());
		}
		return Reply;
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		FReply Reply = SMultiLineEditableText::OnMouseButtonUp(MyGeometry, MouseEvent);
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && SelectionGroup.IsValid())
		{
			SelectionGroup->UpdateSelection(MouseEvent.GetScreenSpacePosition());
			SelectionGroup->EndSelection();
		}
		return Reply;
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override
	{
		if (KeyEvent.GetKey() == EKeys::C && KeyEvent.IsControlDown() && SelectionGroup.IsValid() && SelectionGroup->CopySelectionToClipboard())
		{
			return FReply::Handled();
		}
		return SMultiLineEditableText::OnKeyDown(MyGeometry, KeyEvent);
	}

private:
	TSharedPtr<FUnrealAgentMCPTextSelectionGroup> SelectionGroup;
};

TSharedRef<FUnrealAgentMCPTextSelectionGroup> CreateUnrealAgentMCPTextSelectionGroup()
{
	return MakeShared<FUnrealAgentMCPTextSelectionGroup>();
}

void FUnrealAgentMCPTextSelectionGroup::Register(const TSharedRef<SUnrealAgentMCPSelectableText>& Widget)
{
	Widgets.Add(Widget);
}

TArray<TSharedRef<SUnrealAgentMCPSelectableText>> FUnrealAgentMCPTextSelectionGroup::GetWidgetsInVisualOrder()
{
	TArray<TSharedRef<SUnrealAgentMCPSelectableText>> Result;
	for (int32 Index = Widgets.Num() - 1; Index >= 0; --Index)
	{
		if (const TSharedPtr<SUnrealAgentMCPSelectableText> Widget = Widgets[Index].Pin())
		{
			Result.Add(Widget.ToSharedRef());
		}
		else
		{
			Widgets.RemoveAtSwap(Index);
		}
	}
	Result.Sort(
		[](const TSharedRef<SUnrealAgentMCPSelectableText>& Left, const TSharedRef<SUnrealAgentMCPSelectableText>& Right)
		{
			const FVector2D LeftPosition = Left->GetCachedGeometry().GetAbsolutePosition();
			const FVector2D RightPosition = Right->GetCachedGeometry().GetAbsolutePosition();
			if (!FMath::IsNearlyEqual(LeftPosition.Y, RightPosition.Y, 2.0f))
			{
				return LeftPosition.Y < RightPosition.Y;
			}
			return LeftPosition.X < RightPosition.X;
		});
	return Result;
}

int32 FUnrealAgentMCPTextSelectionGroup::FindWidgetIndex(const TArray<TSharedRef<SUnrealAgentMCPSelectableText>>& InWidgets, const SUnrealAgentMCPSelectableText* Widget) const
{
	for (int32 Index = 0; Index < InWidgets.Num(); ++Index)
	{
		if (&InWidgets[Index].Get() == Widget)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void FUnrealAgentMCPTextSelectionGroup::ClearSelection()
{
	for (const TSharedRef<SUnrealAgentMCPSelectableText>& Widget : GetWidgetsInVisualOrder())
	{
		Widget->ClearSelection();
	}
	AnchorWidget.Reset();
	bSelecting = false;
}

void FUnrealAgentMCPTextSelectionGroup::BeginSelection(SUnrealAgentMCPSelectableText& Widget, const FTextLocation& Location)
{
	if (const TSharedPtr<FUnrealAgentMCPTextSelectionGroup> Previous = ActiveSelectionGroup.Pin(); Previous.IsValid() && Previous.Get() != this)
	{
		Previous->ClearSelection();
	}
	ClearSelection();
	AnchorWidget = StaticCastSharedRef<SUnrealAgentMCPSelectableText>(Widget.AsShared());
	AnchorLocation = Location;
	bSelecting = true;
	ActiveSelectionGroup = AsShared();
}

void FUnrealAgentMCPTextSelectionGroup::UpdateSelection(const FVector2D& ScreenPosition)
{
	if (!bSelecting)
	{
		return;
	}

	const TSharedPtr<SUnrealAgentMCPSelectableText> Anchor = AnchorWidget.Pin();
	TArray<TSharedRef<SUnrealAgentMCPSelectableText>> OrderedWidgets = GetWidgetsInVisualOrder();
	const int32 AnchorIndex = FindWidgetIndex(OrderedWidgets, Anchor.Get());
	if (!Anchor.IsValid() || AnchorIndex == INDEX_NONE || OrderedWidgets.IsEmpty())
	{
		ClearSelection();
		return;
	}

	int32 TargetIndex = INDEX_NONE;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < OrderedWidgets.Num(); ++Index)
	{
		const FGeometry& Geometry = OrderedWidgets[Index]->GetCachedGeometry();
		const FVector2D TopLeft = Geometry.GetAbsolutePosition();
		const FVector2D BottomRight = TopLeft + Geometry.GetLocalSize() * Geometry.Scale;
		const FVector2D ClosestPoint(FMath::Clamp(ScreenPosition.X, TopLeft.X, BottomRight.X), FMath::Clamp(ScreenPosition.Y, TopLeft.Y, BottomRight.Y));
		const float DistanceSquared = FVector2D::DistSquared(ScreenPosition, ClosestPoint);
		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			TargetIndex = Index;
		}
	}
	if (!OrderedWidgets.IsValidIndex(TargetIndex))
	{
		return;
	}

	const TSharedRef<SUnrealAgentMCPSelectableText> Target = OrderedWidgets[TargetIndex];
	const FTextLocation TargetLocation = Target->ResolveTextLocation(ScreenPosition);
	for (const TSharedRef<SUnrealAgentMCPSelectableText>& Widget : OrderedWidgets)
	{
		Widget->ClearSelection();
	}

	if (AnchorIndex == TargetIndex)
	{
		Anchor->SelectText(AnchorLocation, TargetLocation);
		return;
	}

	const bool bForward = AnchorIndex < TargetIndex;
	const int32 FirstIndex = FMath::Min(AnchorIndex, TargetIndex);
	const int32 LastIndex = FMath::Max(AnchorIndex, TargetIndex);
	for (int32 Index = FirstIndex; Index <= LastIndex; ++Index)
	{
		const TSharedRef<SUnrealAgentMCPSelectableText> Current = OrderedWidgets[Index];
		if (Index == AnchorIndex)
		{
			Current->SelectText(AnchorLocation, bForward ? Current->GetDocumentEnd() : Current->GetDocumentStart());
		}
		else if (Index == TargetIndex)
		{
			Current->SelectText(bForward ? Current->GetDocumentStart() : Current->GetDocumentEnd(), TargetLocation);
		}
		else
		{
			Current->SelectAllText();
		}
	}
}

void FUnrealAgentMCPTextSelectionGroup::EndSelection()
{
	bSelecting = false;
}

bool FUnrealAgentMCPTextSelectionGroup::CopySelectionToClipboard()
{
	FString CombinedText;
	TSharedPtr<SUnrealAgentMCPSelectableText> PreviousWidget;
	for (const TSharedRef<SUnrealAgentMCPSelectableText>& Widget : GetWidgetsInVisualOrder())
	{
		const FString SelectedText = Widget->GetSelectedText().ToString();
		if (SelectedText.IsEmpty())
		{
			continue;
		}
		if (!CombinedText.IsEmpty())
		{
			const FGeometry& PreviousGeometry = PreviousWidget->GetCachedGeometry();
			const FGeometry& CurrentGeometry = Widget->GetCachedGeometry();
			const float PreviousTop = PreviousGeometry.GetAbsolutePosition().Y;
			const float CurrentTop = CurrentGeometry.GetAbsolutePosition().Y;
			const bool bSameVisualLine = FMath::IsNearlyEqual(PreviousTop, CurrentTop, 2.0f);
			CombinedText += bSameVisualLine ? TEXT(" ") : TEXT("\n");
		}
		CombinedText += SelectedText;
		PreviousWidget = Widget;
	}
	if (CombinedText.IsEmpty())
	{
		return false;
	}
	FPlatformApplicationMisc::ClipboardCopy(*CombinedText);
	return true;
}

namespace
{
	enum class EWorldDataMarkdownBackground : uint8
	{
		CodeTop,
		CodeMiddle,
		CodeBottom,
		TableSingle,
		TableTop,
		TableMiddle,
		TableBottom
	};

	void OpenMarkdownLink(const FSlateHyperlinkRun::FMetadata& Metadata)
	{
		const FString* Url = Metadata.Find(TEXT("href"));
		if (!Url || (!Url->StartsWith(TEXT("https://"), ESearchCase::IgnoreCase) && !Url->StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)))
		{
			return;
		}
		FPlatformProcess::LaunchURL(**Url, nullptr, nullptr);
	}

	class FWorldDataMarkdownLineHighlighter final : public ISlateLineHighlighter
	{
	public:
		FWorldDataMarkdownLineHighlighter(const FLinearColor& FillColor, const FVector4& CornerRadii) : Brush(FillColor, CornerRadii)
		{
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FTextLayout::FLineView& Line, const FVector2D Offset, const float Width, const FTextBlockStyle& DefaultStyle,
			const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			const float InverseScale = Inverse(AllottedGeometry.Scale);
			const FVector2D Location(0.0f, Line.Offset.Y + Offset.Y);
			const FVector2D Size(AllottedGeometry.GetLocalSize().X * AllottedGeometry.Scale, FMath::Max(Line.Size.Y, Line.TextHeight));
			FSlateDrawElement::MakeBox(OutDrawElements, ++LayerId,
				AllottedGeometry.ToPaintGeometry(TransformVector(InverseScale, Size), FSlateLayoutTransform(TransformPoint(InverseScale, Location))), &Brush,
				bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect, Brush.GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}

	private:
		FSlateRoundedBoxBrush Brush;
	};

	class FWorldDataMarkdownMarshaller final : public FRichTextLayoutMarshaller
	{
	public:
		static TSharedRef<FWorldDataMarkdownMarshaller> Create(TArray<TSharedRef<ITextDecorator>> InDecorators, const ISlateStyle* InStyleSet, const FLinearColor& SurfaceColor,
			const FLinearColor& TableHeaderColor)
		{
			return MakeShareable(new FWorldDataMarkdownMarshaller(MoveTemp(InDecorators), InStyleSet, SurfaceColor, TableHeaderColor));
		}

		virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override
		{
			FRichTextLayoutMarshaller::SetText(SourceString, TargetTextLayout);

			TArray<FString> SourceLines;
			SourceString.ParseIntoArray(SourceLines, TEXT("\n"), false);
			const TArray<FTextLayout::FLineModel>& LineModels = TargetTextLayout.GetLineModels();
			TArray<FTextLineHighlight> Highlights;
			const int32 LineCount = FMath::Min(SourceLines.Num(), LineModels.Num());
			for (int32 LineIndex = 0; LineIndex < LineCount; ++LineIndex)
			{
				const TSharedRef<ISlateLineHighlighter> Highlighter = GetHighlighter(SourceLines[LineIndex]);
				if (Highlighter != EmptyHighlighter)
				{
					Highlights.Emplace(LineIndex, FTextRange(0, LineModels[LineIndex].Text->Len()), UnrealAgentMCPMarkdown::BackgroundHighlightZOrder, Highlighter);
				}
			}
			TargetTextLayout.SetLineHighlights(Highlights);
		}

	private:
		FWorldDataMarkdownMarshaller(TArray<TSharedRef<ITextDecorator>> InDecorators, const ISlateStyle* InStyleSet, const FLinearColor& SurfaceColor,
			const FLinearColor& TableHeaderColor)
			: FRichTextLayoutMarshaller(MoveTemp(InDecorators), InStyleSet),
			  EmptyHighlighter(MakeShared<FWorldDataMarkdownLineHighlighter>(FLinearColor::Transparent, FVector4(0.0f))),
			  CodeTop(MakeShared<FWorldDataMarkdownLineHighlighter>(SurfaceColor, FVector4(7.0f, 7.0f, 0.0f, 0.0f))),
			  CodeMiddle(MakeShared<FWorldDataMarkdownLineHighlighter>(SurfaceColor, FVector4(0.0f))),
			  CodeBottom(MakeShared<FWorldDataMarkdownLineHighlighter>(SurfaceColor, FVector4(0.0f, 0.0f, 7.0f, 7.0f))),
			  TableSingle(MakeShared<FWorldDataMarkdownLineHighlighter>(TableHeaderColor, FVector4(7.0f))),
			  TableTop(MakeShared<FWorldDataMarkdownLineHighlighter>(TableHeaderColor, FVector4(7.0f, 7.0f, 0.0f, 0.0f))),
			  TableMiddle(MakeShared<FWorldDataMarkdownLineHighlighter>(SurfaceColor, FVector4(0.0f))),
			  TableBottom(MakeShared<FWorldDataMarkdownLineHighlighter>(SurfaceColor, FVector4(0.0f, 0.0f, 7.0f, 7.0f)))
		{
		}

		TSharedRef<ISlateLineHighlighter> GetHighlighter(const FString& SourceLine) const
		{
			if (SourceLine.Contains(TEXT("<WorldDataMarkdown.CodeBlockTop>")))
			{
				return CodeTop;
			}
			if (SourceLine.Contains(TEXT("<WorldDataMarkdown.CodeBlockMiddle>")))
			{
				return CodeMiddle;
			}
			if (SourceLine.Contains(TEXT("<WorldDataMarkdown.CodeBlockBottom>")))
			{
				return CodeBottom;
			}
			if (SourceLine.Contains(TEXT("<WorldDataMarkdown.TableSingle>")))
			{
				return TableSingle;
			}
			if (SourceLine.Contains(TEXT("<WorldDataMarkdown.TableTop>")))
			{
				return TableTop;
			}
			if (SourceLine.Contains(TEXT("<WorldDataMarkdown.TableMiddle>")))
			{
				return TableMiddle;
			}
			if (SourceLine.Contains(TEXT("<WorldDataMarkdown.TableBottom>")))
			{
				return TableBottom;
			}
			return EmptyHighlighter;
		}

		TSharedRef<ISlateLineHighlighter> EmptyHighlighter;
		TSharedRef<ISlateLineHighlighter> CodeTop;
		TSharedRef<ISlateLineHighlighter> CodeMiddle;
		TSharedRef<ISlateLineHighlighter> CodeBottom;
		TSharedRef<ISlateLineHighlighter> TableSingle;
		TSharedRef<ISlateLineHighlighter> TableTop;
		TSharedRef<ISlateLineHighlighter> TableMiddle;
		TSharedRef<ISlateLineHighlighter> TableBottom;
	};
}

void SUnrealAgentMCPMarkdown::Construct(const FArguments& InArgs)
{
	TextColor = InArgs._TextColor;
	MutedColor = InArgs._MutedColor;
	AccentColor = InArgs._AccentColor;
	SurfaceColor = InArgs._SurfaceColor;
	BorderColor = InArgs._BorderColor;
	ConfigureStyles();

	TArray<TSharedRef<ITextDecorator>> Decorators;
	Decorators.Add(FHyperlinkDecorator::Create(
		TEXT("browser"),
		FSlateHyperlinkRun::FOnClick::CreateStatic(&OpenMarkdownLink)));
	TextMarshaller = FWorldDataMarkdownMarshaller::Create(
		MoveTemp(Decorators),
		MarkdownStyleSet.Get(),
		SurfaceColor,
		BorderColor);

	TSharedRef<SUnrealAgentMCPSelectableText> SelectableText =
		SNew(SUnrealAgentMCPSelectableText)
		.Text(FText::FromString(
			UnrealAgentMCPMarkdown::ToSelectableRichText(InArgs._Markdown)))
		.Marshaller(TextMarshaller)
		.TextStyle(&BodyTextStyle)
		.IsReadOnly(true)
		.AllowMultiLine(true)
		.AutoWrapText(true)
		.AllowContextMenu(true)
		.SelectWordOnMouseDoubleClick(true)
		.ClearTextSelectionOnFocusLoss(false);
	SelectableText->SetSelectionGroup(InArgs._SelectionGroup);
	SelectableTextWidget = SelectableText;

	ChildSlot
	[
		SelectableText
	];
}

void SUnrealAgentMCPMarkdown::SetMarkdown(const FString& Markdown)
{
	if (SelectableTextWidget.IsValid())
	{
		SelectableTextWidget->SetText(FText::FromString(UnrealAgentMCPMarkdown::ToSelectableRichText(Markdown)));
	}
}

void SUnrealAgentMCPMarkdown::ConfigureStyles()
{
	BodyTextStyle = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	BodyTextStyle.SetColorAndOpacity(FSlateColor(TextColor)).SetSelectedBackgroundColor(FSlateColor(AccentColor.CopyWithNewOpacity(0.68f)));

	static const int32 HeadingSizes[] = { 20, 18, 16, 14, 12, 11 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(HeadingTextStyles); ++Index)
	{
		FSlateFontInfo HeadingFont = FAppStyle::GetFontStyle("NormalFontBold");
		HeadingFont.Size = HeadingSizes[Index];
		HeadingTextStyles[Index] = FTextBlockStyle(BodyTextStyle).SetFont(HeadingFont).SetColorAndOpacity(FSlateColor(TextColor));
	}

	MarkdownStyleSet = MakeShared<FSlateStyleSet>(TEXT("WorldDataConversationMarkdown"));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.Bold"), FTextBlockStyle(BodyTextStyle).SetFont(FAppStyle::GetFontStyle("NormalFontBold")));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.Italic"),
		FTextBlockStyle(FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("RichTextBlock.Italic"))
			.SetColorAndOpacity(FSlateColor(TextColor))
			.SetSelectedBackgroundColor(FSlateColor(AccentColor.CopyWithNewOpacity(0.68f))));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.Code"),
		FTextBlockStyle(BodyTextStyle).SetFont(FAppStyle::GetFontStyle("MonospacedText")).SetColorAndOpacity(FSlateColor(AccentColor)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.Muted"), FTextBlockStyle(BodyTextStyle).SetColorAndOpacity(FSlateColor(MutedColor)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.ListMarker"),
		FTextBlockStyle(BodyTextStyle).SetFont(FAppStyle::GetFontStyle("NormalFontBold")).SetColorAndOpacity(FSlateColor(AccentColor)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.QuoteMark"),
		FTextBlockStyle(BodyTextStyle).SetFont(FAppStyle::GetFontStyle("NormalFontBold")).SetColorAndOpacity(FSlateColor(AccentColor)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.CodeLabel"),
		FTextBlockStyle(BodyTextStyle).SetFont(FAppStyle::GetFontStyle("SmallFont")).SetColorAndOpacity(FSlateColor(MutedColor)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.HorizontalRule"), FTextBlockStyle(BodyTextStyle).SetColorAndOpacity(FSlateColor(BorderColor)));

	FTextBlockStyle CodeBlockStyle(BodyTextStyle);
	CodeBlockStyle.SetFont(FAppStyle::GetFontStyle("MonospacedText"));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.CodeBlockTop"), FTextBlockStyle(CodeBlockStyle).SetFontSize(4).SetColorAndOpacity(FSlateColor(FLinearColor::Transparent)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.CodeBlockMiddle"), FTextBlockStyle(CodeBlockStyle).SetFontSize(1).SetColorAndOpacity(FSlateColor(FLinearColor::Transparent)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.CodeBlockBottom"), FTextBlockStyle(CodeBlockStyle).SetFontSize(4).SetColorAndOpacity(FSlateColor(FLinearColor::Transparent)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.CodeBlockText"), CodeBlockStyle);

	const FTextBlockStyle BackgroundMarkerStyle = FTextBlockStyle(BodyTextStyle).SetFontSize(1).SetColorAndOpacity(FSlateColor(FLinearColor::Transparent));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.TableSingle"), BackgroundMarkerStyle);
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.TableTop"), BackgroundMarkerStyle);
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.TableMiddle"), BackgroundMarkerStyle);
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.TableBottom"), BackgroundMarkerStyle);
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.Spacer"), FTextBlockStyle(BodyTextStyle).SetFontSize(4).SetColorAndOpacity(FSlateColor(FLinearColor::Transparent)));

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(HeadingTextStyles); ++Index)
	{
		MarkdownStyleSet->Set(*FString::Printf(TEXT("WorldDataMarkdown.Heading%d"), Index + 1), HeadingTextStyles[Index]);
	}

	FHyperlinkStyle LinkStyle = FAppStyle::Get().GetWidgetStyle<FHyperlinkStyle>("Documentation.Hyperlink");
	LinkStyle.SetTextStyle(FTextBlockStyle(BodyTextStyle).SetColorAndOpacity(FSlateColor(AccentColor)));
	MarkdownStyleSet->Set(TEXT("WorldDataMarkdown.Link"), LinkStyle);
}
