// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPEditorModule.cpp
 * @brief Unreal Agent 编辑器模块：注册 MCP 面板 Tab 与菜单入口。
 */
#include "Application/UnrealAgentMCPApplicationService.h"
#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"
#include "Core/Common/UnrealAgentMCPBrand.h"
#include "Infrastructure/Conversation/UnrealAgentMCPConversationStore.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"
#include "Presentation/Panel/SUnrealAgentMCPPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Textures/SlateIcon.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "UnrealAgentMCPEditor"

namespace UnrealAgentStyle
{
	static const FName StyleSetName(TEXT("UnrealAgentStyle"));
	static const FName IconName(TEXT("UnrealAgent.Icon"));
	static const FVector2D IconSize(20.0f, 20.0f);
}

class FUnrealAgentMCPEditorModule : public IModuleInterface
{
public:
	/** 启动 MCP 服务并注册 Nomad Tab 与 Window 菜单。 */

	// =============================================================================
	// 模块启动 / 关闭
	// =============================================================================

	virtual void StartupModule() override
	{
		RegisterStyle();
		FUnrealAgentMCPServer::BindToolProviderHost();
		ApplicationService = CreateUnrealAgentMCPApplicationService();
		ApplicationService->StartServer(ApplicationService->LoadConfiguredPort());

		FGlobalTabmanager::Get()
			->RegisterNomadTabSpawner(UnrealAgentMCP::PanelTabName, FOnSpawnTab::CreateRaw(this, &FUnrealAgentMCPEditorModule::SpawnPanelTab))
			.SetDisplayName(FText::FromString(UnrealAgentMCP::Brand::ProductName))
			.SetTooltipText(FText::Format(LOCTEXT("PanelTabTooltip", "打开 {0}。"), FText::FromString(UnrealAgentMCP::Brand::ProductName)))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
			.SetIcon(GetUnrealAgentIcon());

		UToolMenus::Get()->RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealAgentMCPEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		if (const TSharedPtr<SUnrealAgentMCPPanel> Panel = PanelWidget.Pin())
		{
			Panel->ShutdownPanelSession();
		}
		PanelWidget.Reset();
		UnrealAgentMCP::FUnrealAgentMCPUnrealEditorAdapter::ShutdownDialogState();
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAgentMCP::PanelTabName);
		}

		if (ApplicationService.IsValid())
		{
			ApplicationService->Shutdown();
			ApplicationService.Reset();
		}
		FUnrealAgentMCPServer::UnbindToolProviderHost();

		UnregisterStyle();
	}

private:
	void RegisterStyle()
	{
		StyleSet = MakeShared<FSlateStyleSet>(UnrealAgentStyle::StyleSetName);
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAgent")))
		{
			StyleSet->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
		}

		StyleSet->Set(UnrealAgentStyle::IconName,
			new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("UnrealAgent"), TEXT(".svg")), UnrealAgentStyle::IconSize, FLinearColor(0.93f, 0.95f, 0.98f, 1.0f)));
		FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
	}

	void UnregisterStyle()
	{
		if (StyleSet.IsValid())
		{
			FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
			StyleSet.Reset();
		}
	}

	FSlateIcon GetUnrealAgentIcon() const
	{
		return FSlateIcon(UnrealAgentStyle::StyleSetName, UnrealAgentStyle::IconName);
	}

	// =============================================================================
	// Tab 与菜单
	// =============================================================================

	TSharedRef<SDockTab> SpawnPanelTab(const FSpawnTabArgs& Args)
	{
		TSharedRef<SUnrealAgentMCPPanel> Panel =
			SNew(SUnrealAgentMCPPanel)
			.ApplicationService(ApplicationService)
			.ConversationRepository(MakeShared<FUnrealAgentMCPConversationStore>());
		PanelWidget = Panel;
		TSharedRef<SDockTab> Tab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				Panel
			];
		Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(
			this,
			&FUnrealAgentMCPEditorModule::HandlePanelTabClosed));
		return Tab;
	}

	void HandlePanelTabClosed(TSharedRef<SDockTab> ClosedTab)
	{
		(void)ClosedTab;
		if (const TSharedPtr<SUnrealAgentMCPPanel> Panel = PanelWidget.Pin())
		{
			Panel->ShutdownPanelSession();
		}
		PanelWidget.Reset();
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntry(FName(TEXT("UnrealAgentMCP_OpenPanel")), FText::FromString(UnrealAgentMCP::Brand::ProductName),
				FText::Format(LOCTEXT("OpenPanelMenuTooltip", "打开 {0}。"), FText::FromString(UnrealAgentMCP::Brand::ProductName)), GetUnrealAgentIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FUnrealAgentMCPEditorModule::OpenPanel)));
		}

		if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User"))
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection(TEXT("UnrealAgent"));
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(FName(TEXT("UnrealAgent_OpenPanel")),
				FUIAction(FExecuteAction::CreateRaw(this, &FUnrealAgentMCPEditorModule::OpenPanel)), FText::FromString(UnrealAgentMCP::Brand::ProductName),
				FText::Format(LOCTEXT("OpenPanelToolbarTooltip", "打开 {0} 窗口。"), FText::FromString(UnrealAgentMCP::Brand::ProductName)), GetUnrealAgentIcon()));
		}
	}

	void OpenPanel()
	{
		// 打开 Unreal Agent 的 Nomad Tab。
		FGlobalTabmanager::Get()->TryInvokeTab(UnrealAgentMCP::PanelTabName);
	}

	TSharedPtr<IUnrealAgentMCPApplicationService> ApplicationService;
	TSharedPtr<FSlateStyleSet> StyleSet;
	TWeakPtr<SUnrealAgentMCPPanel> PanelWidget;
};

IMPLEMENT_MODULE(FUnrealAgentMCPEditorModule, UnrealAgentMCPEditor)

#undef LOCTEXT_NAMESPACE
