// Copyright ZhaoZining. All Rights Reserved.

#include "Presentation/Widgets/SUnrealAgentMCPContextUsageRing.h"

#include "Rendering/DrawElements.h"

void SUnrealAgentMCPContextUsageRing::Construct(const FArguments& InArgs)
{
	Usage = InArgs._Usage;
	ForegroundColor = InArgs._ForegroundColor;
	BackgroundColor = InArgs._BackgroundColor;
	Diameter = FMath::Max(8.0f, InArgs._Diameter);
	StrokeWidth = FMath::Clamp(InArgs._StrokeWidth, 1.0f, Diameter * 0.25f);
}

FVector2D SUnrealAgentMCPContextUsageRing::ComputeDesiredSize(const float LayoutScaleMultiplier) const
{
	return FVector2D(Diameter, Diameter);
}

int32 SUnrealAgentMCPContextUsageRing::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	constexpr int32 SegmentCount = 48;
	constexpr float StartAngle = -0.5f * UE_PI;
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2D Center = LocalSize * 0.5f;
	const float Radius = FMath::Max(1.0f, 0.5f * FMath::Min(LocalSize.X, LocalSize.Y) - StrokeWidth * 0.5f);
	const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor StyleTint = InWidgetStyle.GetColorAndOpacityTint();

	auto BuildArcPoints = [Center, Radius](const float ArcRatio)
	{
		TArray<FVector2D> Points;
		const float ClampedRatio = FMath::Clamp(ArcRatio, 0.0f, 1.0f);
		const int32 DrawnSegments = FMath::Max(1, FMath::CeilToInt(ClampedRatio * SegmentCount));
		Points.Reserve(DrawnSegments + 1);
		for (int32 SegmentIndex = 0; SegmentIndex <= DrawnSegments; ++SegmentIndex)
		{
			const float SegmentRatio = FMath::Min(static_cast<float>(SegmentIndex) / SegmentCount, ClampedRatio);
			const float Angle = StartAngle + SegmentRatio * 2.0f * UE_PI;
			Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	};

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), BuildArcPoints(1.0f), DrawEffects,
		BackgroundColor.Get().GetColor(InWidgetStyle) * StyleTint, true, StrokeWidth);

	const float UsageRatio = FMath::Clamp(Usage.Get(0.0f), 0.0f, 1.0f);
	if (UsageRatio > KINDA_SMALL_NUMBER)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), BuildArcPoints(UsageRatio), DrawEffects,
			ForegroundColor.Get().GetColor(InWidgetStyle) * StyleTint, true, StrokeWidth);
	}

	return LayerId + 1;
}
