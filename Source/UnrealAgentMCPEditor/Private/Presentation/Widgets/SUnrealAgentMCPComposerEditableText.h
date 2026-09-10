// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file SUnrealAgentMCPComposerEditableText.h
 * @brief 支持 Windows 中文 IME 组合态识别与上下文恢复的会话输入控件。
 */

#include "GenericPlatform/ITextInputMethodSystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/SMultiLineEditableText.h"

/**
 * 会话输入专用多行文本控件。
 *
 * UE 5.8 的基础控件在 Enter 路径中不会先检查 IME 组合态，因此这里向
 * 面板暴露精确状态，避免候选词确认触发文本提交与重载。
 */
class SUnrealAgentMCPComposerEditableText final : public SMultiLineEditableText
{
public:
	/**
	 * Slate 仍持有键盘焦点时，Windows TSF 偶尔会丢失文档管理器绑定。
	 * 此时英文 WM_CHAR 仍可进入控件，但中文候选窗完全消失。这里按低频率
	 * 重新绑定当前文本上下文；组合输入期间绝不重绑，避免打断候选词。
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SynchronizeResponsiveLayout(AllottedGeometry);

		SMultiLineEditableText::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		// 自动换行的可编辑布局仍可能为保持光标可见而水平平移，尤其是光标前存在
		// 内联控件时。输入区是响应式纵向文档，因此固定视口左边界，仅保留纵向滚动位置。
		const FVector2f CurrentScrollOffset = SlateEditableTextLayout.GetScrollOffset();
		if (CurrentScrollOffset.X > KINDA_SMALL_NUMBER)
		{
			SlateEditableTextLayout.SetScrollOffset(FVector2f(0.0f, CurrentScrollOffset.Y), AllottedGeometry);
			SlateEditableTextLayout.CacheDesiredSize(GetPrepassLayoutScaleMultiplier());
		}

		if (!HasKeyboardFocus() || InCurrentTime < NextInputMethodRecoverySeconds)
		{
			return;
		}
		NextInputMethodRecoverySeconds = InCurrentTime + 0.75;

		const TSharedRef<ITextInputMethodContext> Context = SlateEditableTextLayout.GetTextInputMethodContext();
		if (!Context->IsComposing())
		{
			SlateEditableTextLayout.EnableTextInputMethodContext();
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
		const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const override
	{
		// 交互式调整窗口大小时 Slate 可能暂停常规 Tick，但仍会继续绘制。
		// 在此同步可确保用户改变输入区任一尺寸时，软换行仍能及时响应。
		const_cast<SUnrealAgentMCPComposerEditableText*>(this)->SynchronizeResponsiveLayout(AllottedGeometry);
		return SMultiLineEditableText::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	/** 当前是否正在由系统输入法组合候选文本。 */
	bool IsInputMethodComposing() const
	{
		return SlateEditableTextLayout.GetTextInputMethodContext()->IsComposing();
	}

	/** 重新缓存宿主窗口并激活当前文本输入上下文，不改变键盘焦点。 */
	void EnsureInputMethodContext()
	{
		SlateEditableTextLayout.EnableTextInputMethodContext();
	}

	/** 禁用水平平移；该状态对外暴露以便执行布局回归检查。 */
	float GetHorizontalScrollOffset() const
	{
		const FVector2f CurrentScrollOffset = SlateEditableTextLayout.GetScrollOffset();
		return CurrentScrollOffset.X;
	}

private:
	void SynchronizeResponsiveLayout(const FGeometry& AllottedGeometry)
	{
		// AutoWrapText 仅在有限的绘制路径中刷新缓存宽度，因此富控件文本段可能继续
		// 使用过期且更宽的几何尺寸换行。这里改为按实际分配尺寸重新排版。
		constexpr float WrapSafetyInset = 2.0f;
		const FVector2f AllottedSize = AllottedGeometry.GetLocalSize();
		const bool bSizeChanged = !FMath::IsNearlyEqual(LastAllottedSize.X, AllottedSize.X, 0.5f) || !FMath::IsNearlyEqual(LastAllottedSize.Y, AllottedSize.Y, 0.5f);
		if (!bSizeChanged)
		{
			return;
		}

		LastAllottedSize = AllottedSize;
		SlateEditableTextLayout.SetCachedSize(AllottedGeometry);
		SetWrapTextAt(FMath::Max(1.0f, AllottedSize.X - WrapSafetyInset));
		SlateEditableTextLayout.CacheDesiredSize(GetPrepassLayoutScaleMultiplier());
	}

	/** 最近一次真实输入区尺寸，用于响应式换行和视口布局。 */
	FVector2f LastAllottedSize = FVector2f(-1.0f, -1.0f);

	/** 下一次允许执行 TSF 上下文健康恢复的 Slate 时间。 */
	double NextInputMethodRecoverySeconds = 0.0;
};
