// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class SUnrealAgentMCPContextUsageRing final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAgentMCPContextUsageRing)
		: _Usage(0.0f)
		, _ForegroundColor(FLinearColor::White)
		, _BackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.16f))
		, _Diameter(18.0f)
		, _StrokeWidth(2.0f)
	{
	}

		SLATE_ATTRIBUTE(float, Usage)
		SLATE_ATTRIBUTE(FSlateColor, ForegroundColor)
		SLATE_ATTRIBUTE(FSlateColor, BackgroundColor)
		SLATE_ARGUMENT(float, Diameter)
		SLATE_ARGUMENT(float, StrokeWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TAttribute<float> Usage;
	TAttribute<FSlateColor> ForegroundColor;
	TAttribute<FSlateColor> BackgroundColor;
	float Diameter = 18.0f;
	float StrokeWidth = 2.0f;
};
