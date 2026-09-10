// Copyright ZhaoZining. All Rights Reserved.

#include "Presentation/Widgets/UnrealAgentMCPToolTip.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FSlateBrush* GetUnrealAgentToolTipBackgroundBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FStyleColors::Dropdown, 6.0f, FStyleColors::DropdownOutline, 1.0f);
		return &Brush;
	}
}

namespace UnrealAgentMCPToolTip
{
	TSharedRef<IToolTip> Make(const FText& Text)
	{
		return Make(TAttribute<FText>(Text));
	}

	TSharedRef<IToolTip> Make(const TAttribute<FText>& Text)
	{
		TSharedRef<SToolTip> ToolTip =
			SNew(SToolTip)
			.BorderImage(GetUnrealAgentToolTipBackgroundBrush())
			.TextMargin(FMargin(10.0f, 8.0f))
			[
				SNew(SBox)
				.MaxDesiredWidth(420.0f)
				[
					SNew(STextBlock)
					.Text(Text)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseForeground())
					.AutoWrapText(true)
				]
			];

		return StaticCastSharedRef<IToolTip>(ToolTip);
	}
}
