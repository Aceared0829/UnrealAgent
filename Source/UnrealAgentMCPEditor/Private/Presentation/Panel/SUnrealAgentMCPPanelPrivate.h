// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file SUnrealAgentMCPPanelPrivate.h
 * @brief 面板实现文件共享的 Slate、JSON 与平台依赖；不对模块外暴露。
 */

#include "Presentation/Panel/UnrealAgentMCPStyle.h"
#include "Presentation/Widgets/SUnrealAgentMCPComposerEditableText.h"

#include "Animation/CurveSequence.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDesktopPlatform.h"
#include "InputCoreTypes.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace UnrealAgentMCPPanelWidgets
{
	/** 短促的弹出入场效果，不改变布局，也不排队叠加过渡。 */
	class SAnimatedMenu final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAnimatedMenu) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			EntranceAnimation = FCurveSequence(
				0.0f,
				0.105f,
				ECurveEaseFunction::CubicOut);
			ChildSlot
			[
				SAssignNew(ContentRoot, SBox)
				[
					InArgs._Content.Widget
				]
			];
			ContentRoot->SetRenderOpacity(0.0f);
			ContentRoot->SetRenderTransform(
				TOptional<FSlateRenderTransform>(FSlateRenderTransform(
					FVector2D(0.0f, 6.0f))));
			SetCanTick(true);
			EntranceAnimation.Play(AsShared());
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			const float Alpha = EntranceAnimation.GetLerp();
			ContentRoot->SetRenderOpacity(Alpha);
			ContentRoot->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0.0f, FMath::Lerp(6.0f, 0.0f, Alpha)))));
			if (EntranceAnimation.IsAtEnd())
			{
				SetCanTick(false);
			}
		}

	private:
		FCurveSequence EntranceAnimation;
		TSharedPtr<SBox> ContentRoot;
	};

	/** 为工具栏和组合控件提供一致的悬停及按压反馈。 */
	class SAnimatedControl final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAnimatedControl) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ChildSlot[InArgs._Content.Widget];
			SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			SetCanTick(true);
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			const bool bPressed = IsHovered() && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
			const float TargetScale = bPressed ? 0.97f : IsHovered() ? 1.01f : 1.0f;
			const float TargetOpacity = bPressed ? 0.86f : 1.0f;
			CurrentScale = FMath::FInterpTo(CurrentScale, TargetScale, InDeltaTime, 30.0f);
			CurrentOpacity = FMath::FInterpTo(CurrentOpacity, TargetOpacity, InDeltaTime, 32.0f);
			SetRenderOpacity(CurrentOpacity);
			SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FScale2D(CurrentScale))));
		}

	private:
		float CurrentScale = 1.0f;
		float CurrentOpacity = 1.0f;
	};

	/** 可直接替换 SButton，并保持相同的轻量交互反馈。 */
	class SAnimatedButton final : public SButton
	{
	public:
		using FArguments = SButton::FArguments;

		void Construct(const FArguments& InArgs)
		{
			SButton::Construct(InArgs);
			SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			SetCanTick(true);
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SButton::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			const bool bPressed = IsHovered() && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
			const float TargetScale = bPressed ? 0.97f : IsHovered() ? 1.01f : 1.0f;
			CurrentScale = FMath::FInterpTo(CurrentScale, TargetScale, InDeltaTime, 30.0f);
			CurrentOpacity = FMath::FInterpTo(CurrentOpacity, bPressed ? 0.86f : 1.0f, InDeltaTime, 32.0f);
			SetRenderOpacity(CurrentOpacity);
			SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FScale2D(CurrentScale))));
		}

	private:
		float CurrentScale = 1.0f;
		float CurrentOpacity = 1.0f;
	};

	inline TSharedRef<SWidget> AnimateMenu(const TSharedRef<SWidget>& Content)
	{
		return SNew(SAnimatedMenu)
		[
			Content
		];
	}

	inline TSharedRef<SWidget> AnimateControl(const TSharedRef<SWidget>& Content)
	{
		return SNew(SAnimatedControl)
		[
			Content
		];
	}
}
