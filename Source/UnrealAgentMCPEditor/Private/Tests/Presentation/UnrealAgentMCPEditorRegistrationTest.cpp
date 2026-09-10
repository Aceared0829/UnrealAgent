// Copyright ZhaoZining. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenu.h"
#include "ToolMenus.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPEditorRegistrationTest, "WorldData.UnrealAgent.Editor.Registration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentMCPEditorRegistrationTest::RunTest(const FString& Parameters)
{
	const FName ToolbarMenuName(TEXT("LevelEditor.LevelEditorToolBar.User"));
	const FName ToolbarSectionName(TEXT("UnrealAgent"));
	const FName ToolbarEntryName(TEXT("UnrealAgent_OpenPanel"));
	const FName WindowMenuName(TEXT("LevelEditor.MainMenu.Window"));
	const FName WindowEntryName(TEXT("UnrealAgentMCP_OpenPanel"));

	const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(FName(TEXT("UnrealAgentStyle")));
	if (!TestNotNull(TEXT("UnrealAgent style is registered"), Style))
	{
		return false;
	}
	TestNotNull(TEXT("UnrealAgent icon brush is registered"), Style->GetOptionalBrush(FName(TEXT("UnrealAgent.Icon"))));

	UToolMenu* ToolbarMenu = UToolMenus::Get()->FindMenu(ToolbarMenuName);
	if (!TestNotNull(TEXT("Level Editor user toolbar is registered"), ToolbarMenu))
	{
		return false;
	}

	int32 ToolbarEntryCount = 0;
	for (const FToolMenuSection& Section : ToolbarMenu->Sections)
	{
		if (Section.Name != ToolbarSectionName)
		{
			continue;
		}

		for (const FToolMenuEntry& Entry : Section.Blocks)
		{
			ToolbarEntryCount += Entry.Name == ToolbarEntryName ? 1 : 0;
		}
	}
	TestEqual(TEXT("UnrealAgent toolbar entry is registered once"), ToolbarEntryCount, 1);

	UToolMenu* WindowMenu = UToolMenus::Get()->FindMenu(WindowMenuName);
	if (!TestNotNull(TEXT("Level Editor Window menu is registered"), WindowMenu))
	{
		return false;
	}

	int32 WindowEntryCount = 0;
	for (const FToolMenuSection& Section : WindowMenu->Sections)
	{
		for (const FToolMenuEntry& Entry : Section.Blocks)
		{
			WindowEntryCount += Entry.Name == WindowEntryName ? 1 : 0;
		}
	}
	TestEqual(TEXT("Existing UnrealAgent window entry remains registered once"), WindowEntryCount, 1);

	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
