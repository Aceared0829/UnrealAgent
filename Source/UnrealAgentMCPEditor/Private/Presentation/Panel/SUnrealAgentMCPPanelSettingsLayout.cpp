// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file SUnrealAgentMCPPanelSettingsLayout.cpp
 * @brief 设置、MCP 服务与 CLI 卡片的 Slate 布局。
 */

#include "Presentation/Panel/SUnrealAgentMCPPanel.h"
#include "Presentation/Panel/SUnrealAgentMCPPanelPrivate.h"
#include "Brushes/SlateRoundedBoxBrush.h"

#define LOCTEXT_NAMESPACE "UnrealAgentMCPEditor"

namespace
{
	const FSlateBrush* GetSettingsPanelBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 12.0f);
		return &Brush;
	}

	const FSlateBrush* GetSettingsCardBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 9.0f);
		return &Brush;
	}

	const FSlateBrush* GetSettingsBannerBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 7.0f);
		return &Brush;
	}

	const FSlateBrush* GetSettingsChipBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 5.0f);
		return &Brush;
	}
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildSettingsPanel()
{
		return SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				BuildSettingsContent()
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildSettingsContent()
{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 14.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SettingsTitle", "设置"))
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SettingsSubtitle", "调整当前 Unreal Agent 面板的本地偏好。"))
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					BuildToolbarButton(LOCTEXT("SettingsBackButton", "← 返回对话"), FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnSettingsBackClicked))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				BuildMcpServerPanel()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				BuildContextSettingsCard()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(12.0f)
				.BorderImage(GetSettingsPanelBrush())
				.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentSurfaceColor()); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SettingsColorLabel", "Color"))
							.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SettingsColorDescription", "默认白色，用于面板点缀色偏好。"))
							.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SNew(SBorder)
							.BorderImage(GetSettingsChipBrush())
							.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentFillColor(0.18f)); })
							[
								SNew(SBox)
								.HeightOverride(18.0f)
								.WidthOverride(280.0f)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								BuildColorPresetButton(FLinearColor::White, LOCTEXT("PresetWhiteTooltip", "白色"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								BuildColorPresetButton(FLinearColor(0.18f, 0.48f, 1.0f, 1.0f), LOCTEXT("PresetBlueTooltip", "蓝色"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								BuildColorPresetButton(FLinearColor(0.10f, 0.72f, 0.36f, 1.0f), LOCTEXT("PresetGreenTooltip", "绿色"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								BuildColorPresetButton(FLinearColor(1.0f, 0.74f, 0.14f, 1.0f), LOCTEXT("PresetYellowTooltip", "黄色"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								BuildColorPresetButton(FLinearColor(0.95f, 0.20f, 0.24f, 1.0f), LOCTEXT("PresetRedTooltip", "红色"))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								BuildColorPresetButton(FLinearColor(0.62f, 0.36f, 1.0f, 1.0f), LOCTEXT("PresetPurpleTooltip", "紫色"))
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(12.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SColorBlock)
						.Color_Lambda([this] { return SettingsColor; })
						.Size(FVector2D(42.0f, 22.0f))
						.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
						.OnMouseButtonDown(this, &SUnrealAgentMCPPanel::OnSettingsColorBlockClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return GetSettingsColorText(); })
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						BuildToolbarButton(LOCTEXT("PickSettingsColorButton", "选择颜色"), FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnPickSettingsColorClicked))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BuildToolbarButton(LOCTEXT("ResetSettingsColorButton", "重置"), FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnResetSettingsColorClicked))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				BuildCliSettingsPanel()
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildContextSettingsCard()
{
	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(GetSettingsPanelBrush())
		.BorderBackgroundColor_Lambda([this]
		{
			return FSlateColor(GetAccentSurfaceColor());
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ContextCapacityTitle", "上下文连续性"))
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity_Lambda([this]
						{
							return FSlateColor(GetReadableAccentTextColor());
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"ContextCapacityDescription",
							"接近容量时自动压缩，并重新记录项目架构、当前 Editor 状态、任务约束和压缩代次。"))
						.AutoWrapText(true)
						.ColorAndOpacity_Lambda([this]
						{
							return FSlateColor(GetPanelSubduedTextColor());
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Padding(FMargin(10.0f, 4.0f))
					.BorderImage(GetSettingsChipBrush())
					.BorderBackgroundColor_Lambda([this]
					{
						return FSlateColor(GetAccentFillColor(0.18f));
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return GetContextCapacityText(); })
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity_Lambda([this]
						{
							return FSlateColor(GetReadableAccentTextColor());
						})
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 16.0f, 2.0f, 0.0f)
			[
				SNew(SSlider)
				.Value_Lambda([this]
				{
					return UnrealAgentMCPConversationModel::ContextTokenCapacityToSliderValue(
							CurrentContextTokenCapacity);
				})
				.IndentHandle(false)
				.SliderBarColor_Lambda([this]
				{
					return GetAccentFillColor(0.45f);
				})
				.SliderHandleColor_Lambda([this]
				{
					return GetEffectiveAccentColor();
				})
				.OnValueChanged(this, &SUnrealAgentMCPPanel::HandleContextCapacityChanged)
				.OnMouseCaptureEnd(this, &SUnrealAgentMCPPanel::CommitContextCapacityChange)
				.OnControllerCaptureEnd(this, &SUnrealAgentMCPPanel::CommitContextCapacityChange)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 7.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ContextCapacityMinimum", "256K"))
					.ColorAndOpacity_Lambda([this]
					{
						return FSlateColor(GetPanelSubduedTextColor());
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ContextCapacityRecommended", "512K · 推荐"))
					.ColorAndOpacity_Lambda([this]
					{
						return FSlateColor(GetReadableAccentTextColor());
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ContextCapacityMaximum", "1M"))
					.ColorAndOpacity_Lambda([this]
					{
						return FSlateColor(GetPanelSubduedTextColor());
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT(
					"ContextCapacitySafetyHint",
					"为新回复预留安全空间，预计达到所选容量约 90% 时自动压缩；设置会应用到全部会话。"))
				.AutoWrapText(true)
				.ColorAndOpacity_Lambda([this]
				{
					return FSlateColor(GetPanelSubduedTextColor());
				})
			]
		];
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildMcpServerPanel()
{
		return SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(GetSettingsPanelBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentSurfaceColor()); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("McpServerTitle", "MCP 服务器"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("McpServerSubtitle", "Unreal Engine 连接状态。"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					BuildServerStatusCard()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					BuildServerClientsBanner()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					BuildServerPortCard()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					BuildRegisteredToolsCard()
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildServerStatusCard()
{
		return SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(GetSettingsCardBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentFillColor(0.10f)); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("McpStatusLabel", "状态"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SColorBlock)
						.Color_Lambda([this]
						{
							const FUnrealAgentMCPReadiness Readiness =
								ApplicationService->GetServerReadiness();
							return Readiness.IsReady()
								? FLinearColor(0.10f, 0.72f, 0.36f, 1.0f)
								: ApplicationService->IsServerRunning()
									? FLinearColor(0.92f, 0.62f, 0.12f, 1.0f)
									: FLinearColor(0.60f, 0.60f, 0.62f, 1.0f);
						})
						.Size(FVector2D(9.0f, 9.0f))
						.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return GetServerStatusText(); })
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.ButtonStyle(&ComposerButtonStyle)
						.ButtonColorAndOpacity_Lambda([this] { return FSlateColor(GetAccentControlColor()); })
						.ForegroundColor(FSlateColor(GetPanelTextColor()))
						.Text_Lambda([this] { return GetServerToggleText(); })
						.OnClicked(FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnToggleServerClicked))
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildServerClientsBanner()
{
		return SNew(SBorder)
			.Padding(FMargin(12.0f, 8.0f))
			.BorderImage(GetSettingsBannerBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentFillColor(0.14f)); })
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("McpClientsBanner", "支持 Claude Code、Cursor、Windsurf 及任意 MCP 客户端。"))
				.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildServerPortCard()
{
		return SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(GetSettingsCardBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentFillColor(0.10f)); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("McpPortLabel", "端口"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("McpPortDescription", "MCP 服务器端口号。"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SEditableTextBox)
						.Style(&LightTextBoxStyle)
						.IsEnabled_Lambda([this] { return !ApplicationService->IsServerRunning(); })
						.Text_Lambda([this] { return FText::FromString(ServerPortText); })
						.HintText(LOCTEXT("McpPortHint", "例如 7275"))
						.OnTextChanged_Lambda([this](const FText& Text) { ServerPortText = Text.ToString(); })
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { ServerPortText = Text.ToString(); })
						.SelectAllTextWhenFocused(true)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						BuildToolbarButton(
							LOCTEXT("McpPortApplyButton", "应用"),
							FOnClicked::CreateSP(this, &SUnrealAgentMCPPanel::OnApplyPortClicked),
							TAttribute<bool>::CreateLambda([this] { return !ApplicationService->IsServerRunning(); }))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 7.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("McpPortNeedsStopHint", "请先停止服务器再更改端口。"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildRegisteredToolsCard()
{
		const TArray<FString>& ToolNames = GetRegisteredToolNames();

		TSharedRef<SWrapBox> Chips = SNew(SWrapBox)
			.UseAllottedSize(true);
		for (const FString& ToolName : ToolNames)
		{
			Chips->AddSlot()
				.Padding(0.0f, 0.0f, 6.0f, 6.0f)
				[
					BuildToolChip(ToolName)
				];
		}

		return SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(GetSettingsCardBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentFillColor(0.10f)); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("McpToolsLabel", "已注册工具 ({0})"), FText::AsNumber(ToolNames.Num())))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("McpToolsSubtitle", "通过 MCP 服务器可用的 Unreal Engine 工具。"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					Chips
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildToolChip(const FString& ToolName)
{
		return SNew(SBorder)
			.Padding(FMargin(8.0f, 4.0f))
			.BorderImage(GetSettingsChipBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentFillColor(0.18f)); })
			[
				SNew(STextBlock)
				.Text(FText::FromString(ToolName))
				.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
			];
	}

FText SUnrealAgentMCPPanel::GetServerStatusText() const
{
	if (ApplicationService->IsServerRunning())
	{
		const FUnrealAgentMCPReadiness Readiness = ApplicationService->GetServerReadiness();
		if (Readiness.IsReady())
		{
			return FText::Format(LOCTEXT("McpStatusReady", "MCP Listening / ACP Ready — port {0} — {1} tools"), FText::AsNumber(ApplicationService->GetServerPort()),
				FText::AsNumber(Readiness.ToolCount));
		}
		return FText::Format(LOCTEXT("McpStatusListening", "MCP Listening / ACP 未就绪 — port {0}{1}"), FText::AsNumber(ApplicationService->GetServerPort()),
			FText::FromString(Readiness.Error.IsEmpty() ? FString() : FString::Printf(TEXT(" — %s"), *Readiness.Error)));
	}
	return LOCTEXT("McpStatusStopped", "已停止");
}

FText SUnrealAgentMCPPanel::GetServerToggleText() const
{
	return ApplicationService->IsServerRunning() ? LOCTEXT("McpToggleStop", "停止") : LOCTEXT("McpToggleStart", "启动");
}

int32 SUnrealAgentMCPPanel::ParseServerPort() const
{
	const int32 Port = FCString::Atoi(*ServerPortText);
	return (Port >= 1024 && Port <= 65535) ? Port : ApplicationService->LoadConfiguredPort();
}

const TArray<FString>& SUnrealAgentMCPPanel::GetRegisteredToolNames()
{
	if (CachedToolNames.Num() == 0)
	{
		const FString Json = ApplicationService->GetToolDefinitionsJson();
		TArray<TSharedPtr<FJsonValue>> Tools;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (FJsonSerializer::Deserialize(Reader, Tools))
		{
			for (const TSharedPtr<FJsonValue>& Value : Tools)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				FString Name;
				if (Object.IsValid() && Object->TryGetStringField(TEXT("name"), Name))
				{
					CachedToolNames.Add(Name);
				}
			}
		}
	}
	return CachedToolNames;
}

FReply SUnrealAgentMCPPanel::OnToggleServerClicked()
{
	if (ApplicationService->IsServerRunning())
	{
		ApplicationService->StopServer();
		SetLastAction(LOCTEXT("McpStoppedAction", "已停止 MCP 服务器。"));
		return FReply::Handled();
	}

	ApplicationService->StartServer(ParseServerPort());
	if (ApplicationService->IsServerRunning())
	{
		ApplicationService->RefreshConnectionFiles();
		ServerPortText = FString::FromInt(ApplicationService->GetServerPort());
		SetLastAction(LOCTEXT("McpVerifyingAction", "MCP Listener 已启动，正在验证 initialize 与 tools/list..."));
		VerifyServerReadinessForPanel(LOCTEXT("McpStartedAction", "MCP 已通过 initialize 与 tools/list 健康检查。"));
	}
	else
	{
		SetLastAction(LOCTEXT("McpStartFailedAction", "MCP 服务器启动失败。"));
	}
	return FReply::Handled();
}

FReply SUnrealAgentMCPPanel::OnApplyPortClicked()
{
	if (ApplicationService->IsServerRunning())
	{
		SetLastAction(LOCTEXT("McpPortNeedsStopAction", "请先停止服务器再更改端口。"));
		return FReply::Handled();
	}

	ApplicationService->StartServer(ParseServerPort());
	if (ApplicationService->IsServerRunning())
	{
		ApplicationService->RefreshConnectionFiles();
		ServerPortText = FString::FromInt(ApplicationService->GetServerPort());
		SetLastAction(FText::Format(LOCTEXT("McpPortVerifyingAction", "端口 {0} 已监听，正在验证 MCP 健康状态..."), FText::AsNumber(ApplicationService->GetServerPort())));
		VerifyServerReadinessForPanel(
			FText::Format(LOCTEXT("McpPortAppliedAction", "已在端口 {0} 启动。"), FText::FromString(FString::FromInt(ApplicationService->GetServerPort()))));
	}
	else
	{
		SetLastAction(LOCTEXT("McpPortApplyFailedAction", "应用端口失败，服务器未能启动。"));
	}
	return FReply::Handled();
}

void SUnrealAgentMCPPanel::VerifyServerReadinessForPanel(FText SuccessMessage, bool bConnectAcpAfterSuccess)
{
	const TWeakPtr<SUnrealAgentMCPPanel> WeakSelf = SharedThis(this);
	ApplicationService->VerifyServerReadinessAsync(
		[WeakSelf, SuccessMessage = MoveTemp(SuccessMessage), bConnectAcpAfterSuccess](const FUnrealAgentMCPReadiness& Readiness)
		{
			const TSharedPtr<SUnrealAgentMCPPanel> Self = WeakSelf.Pin();
			if (!Self.IsValid())
			{
				return;
			}

			if (!Readiness.IsReady())
			{
				Self->SetLastAction(
					FText::FromString(FString::Printf(TEXT("MCP 健康检查失败：%s"), Readiness.Error.IsEmpty() ? TEXT("initialize 或 tools/list 未就绪。") : *Readiness.Error)));
				Self->ShowProjectInfo();
				return;
			}

			Self->SetLastAction(SuccessMessage);
			Self->ShowProjectInfo();
			if (bConnectAcpAfterSuccess && UnrealAgentACPProviderModel::GetProvider(Self->ActiveProvider).bSupportsEmbeddedConversation && Self->AcpClient.IsValid() &&
				Self->IsProviderDependencyAvailable())
			{
				Self->AcpClient->Connect();
			}
		});
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildCliSettingsPanel()
{
		return SNew(SBorder)
			.Padding(12.0f)
			.BorderImage(GetSettingsPanelBrush())
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(GetAccentSurfaceColor()); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CliSettingsTitle", "CLI 支持"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 12.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT(
						"CliSettingsSubtitle",
						"Codex 账户通过官方 CLI 登录并复用系统凭据，不需要在 config.toml 中填写密钥。CLI 路径未填写时会从 PATH 自动发现。"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildCodexAccountCard()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 12.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildCliSettingsRow(ECliTool::Codex)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 12.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildCliSettingsRow(ECliTool::Cursor)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 12.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT(
							"ExternalCliConfigOptInDescription",
							"面板内对话直接注入当前 MCP 连接，不会自动修改第三方配置。仅当需要在外部 Codex/Cursor/Claude CLI 中使用时再显式同步。"))
						.AutoWrapText(true)
						.ColorAndOpacity_Lambda([this]
						{
							return FSlateColor(GetPanelSubduedTextColor());
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(12.0f, 0.0f, 0.0f, 0.0f)
					[
						BuildToolbarButton(
							LOCTEXT(
								"SyncExternalCliConfigButton",
								"同步到外部 CLI"),
							FOnClicked::CreateSP(
								this,
								&SUnrealAgentMCPPanel::OnSetupCliClicked))
					]
				]
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildCodexAccountCard()
{
	return SNew(SBorder)
		.Padding(10.0f)
		.BorderImage(GetSettingsCardBrush())
		.BorderBackgroundColor_Lambda([this]
		{
			return FSlateColor(GetAccentFillColor(0.10f));
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CodexAccountCardTitle", "Codex 账户"))
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.ColorAndOpacity_Lambda([this]
					{
						return FSlateColor(GetReadableAccentTextColor());
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]
					{
						return GetCodexAccountStatusText();
					})
					.ColorAndOpacity_Lambda([this]
					{
						return FSlateColor(GetPanelSubduedTextColor());
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([this]
				{
					return bCodexAuthenticated
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				[
					BuildToolbarButton(
						LOCTEXT(
							"CodexLoginButton",
							"登录 Codex"),
						FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnCodexLoginClicked),
						TAttribute<bool>::CreateLambda([this]
						{
							return IsCliAvailable(ECliTool::Codex)
								&& (!CodexAuthProcess.IsValid()
									|| !CodexAuthProcess->IsRunning());
						}))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([this]
				{
					return bCodexAuthenticated
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				[
					BuildToolbarButton(
						LOCTEXT(
							"CodexLogoutButton",
							"退出登录"),
						FOnClicked::CreateSP(
							this,
							&SUnrealAgentMCPPanel::OnCodexLogoutClicked),
						TAttribute<bool>::CreateLambda([this]
						{
							return !CodexAuthProcess.IsValid()
								|| !CodexAuthProcess->IsRunning();
						}))
				]
			]
		];
}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildCliSettingsRow(ECliTool Tool)
{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(GetCliTitle(Tool))
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetReadableAccentTextColor()); })
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(GetCliDescription(Tool))
						.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					BuildCliStatusBadge(Tool)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Style(&LightTextBoxStyle)
					.Text_Lambda([this, Tool] { return FText::FromString(GetCliConfiguredPath(Tool)); })
					.HintText_Lambda([this, Tool]
					{
						return FText::FromString(FString::Printf(TEXT("留空时自动使用 PATH 中的 %s"), *GetCliCommandName(Tool)));
					})
					.OnTextCommitted_Lambda([this, Tool](const FText& Text, ETextCommit::Type)
					{
						SetCliConfiguredPath(Tool, Text.ToString());
					})
					.SelectAllTextWhenFocused(true)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					BuildToolbarButton(LOCTEXT("DetectCliButton", "自动检测"), FOnClicked::CreateLambda([this, Tool] { return OnDetectCliClicked(Tool); }))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					BuildToolbarButton(LOCTEXT("ClearCliButton", "清空"), FOnClicked::CreateLambda([this, Tool] { return OnClearCliClicked(Tool); }))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([this, Tool] { return IsCliAvailable(Tool) ? EVisibility::Collapsed : EVisibility::Visible; })
					[
						BuildToolbarButton(LOCTEXT("DownloadCliButton", "下载"), FOnClicked::CreateLambda([this, Tool] { return OnDownloadCliClicked(Tool); }))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 7.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this, Tool] { return GetCliPathSummary(Tool); })
				.ColorAndOpacity_Lambda([this] { return FSlateColor(GetPanelSubduedTextColor()); })
			];
	}

TSharedRef<SWidget> SUnrealAgentMCPPanel::BuildCliStatusBadge(ECliTool Tool) const
{
		return SNew(SBorder)
			.Padding(FMargin(8.0f, 2.0f))
			.BorderImage(GetSettingsChipBrush())
			.BorderBackgroundColor_Lambda([this, Tool]
			{
				return FSlateColor(UnrealAgentMCP::Palette::Blend(
					GetPanelBackgroundColor(),
					IsCliAvailable(Tool) ? UnrealAgentMCP::Palette::Success() : UnrealAgentMCP::Palette::Warning(),
					0.14f));
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Tool]
				{
					return IsCliAvailable(Tool) ? LOCTEXT("CliFoundStatus", "已找到") : LOCTEXT("CliMissingStatus", "未找到");
				})
				.ColorAndOpacity_Lambda([this, Tool]
				{
					return FSlateColor(IsCliAvailable(Tool) ? UnrealAgentMCP::Palette::Success() : UnrealAgentMCP::Palette::Warning());
				})
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			];
	}

#undef LOCTEXT_NAMESPACE
