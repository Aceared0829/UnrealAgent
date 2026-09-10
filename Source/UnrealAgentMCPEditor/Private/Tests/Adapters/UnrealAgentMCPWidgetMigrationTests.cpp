// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPWidgetMigrationTests.cpp
 * @brief Widget 二十七项目录及资产、树、属性、Utility 与运行态往返黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"
#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "WidgetBlueprint.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPWidgetMigrationIntegrationTest, "WorldData.UnrealAgent.Widget.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPWidgetMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPUnrealWidgetAdapter::GetImplementedActions();
		TestEqual(TEXT("Widget action 数量"), Actions.Num(), 27);
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		const FString WidgetName = TEXT("WBP_MCP_") + Suffix;
		const FString WidgetPath = Directory + TEXT("/") + WidgetName + TEXT(".") + WidgetName;
		const FString UtilityWidgetName = TEXT("EUW_MCP_") + Suffix;
		const FString UtilityWidgetPath = Directory + TEXT("/") + UtilityWidgetName + TEXT(".") + UtilityWidgetName;
		const FString UtilityName = TEXT("EUB_MCP_") + Suffix;
		const FString UtilityPath = Directory + TEXT("/") + UtilityName + TEXT(".") + UtilityName;

		FUnrealAgentMCPUnrealWidgetAdapter Adapter;
		TSet<FString> Invoked;
		auto Execute = [&Adapter, &Invoked, this](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Invoked.Add(Action);
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Adapter.Execute(Args);
			if (bExpectSuccess)
			{
				TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1000)), Result.Contains(TEXT("\"success\":true")));
			}
			else
			{
				TestTrue(*FString::Printf(TEXT("%s 返回可诊断错误"), *Action), Result.Contains(TEXT("\"success\":false")) && Result.Contains(TEXT("\"error\"")));
			}
			return Result;
		};
		auto ForAsset = [&WidgetPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), WidgetPath);
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), WidgetName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Args->SetStringField(TEXT("className"), TEXT("CanvasPanel"));
			Execute(TEXT("create"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("directory"), Directory);
			Args->SetStringField(TEXT("namePrefix"), WidgetName);
			Execute(TEXT("list"), Args);
		}
		Execute(TEXT("read_tree"), ForAsset());
		Execute(TEXT("get_details"), ForAsset());
		Execute(TEXT("get_properties"), ForAsset());
		Execute(TEXT("list_bindings"), ForAsset());
		Execute(TEXT("read_animations"), ForAsset());
		Execute(TEXT("list_classes"), MakeShared<FJsonObject>());

		auto AddWidget = [&Execute, &ForAsset](const FString& ClassName, const FString& Name, const FString& ParentName)
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("className"), ClassName);
			Args->SetStringField(TEXT("name"), Name);
			Args->SetStringField(TEXT("parentWidgetName"), ParentName);
			Execute(TEXT("add_widget"), Args);
		};
		AddWidget(TEXT("Border"), TEXT("MCP_Border"), TEXT("Root"));
		AddWidget(TEXT("Button"), TEXT("MCP_Button"), TEXT("Root"));
		AddWidget(TEXT("TextBlock"), TEXT("MCP_Text"), TEXT("MCP_Button"));

		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("widgetName"), TEXT("MCP_Button"));
			Args->SetStringField(TEXT("propertyName"), TEXT("RenderOpacity"));
			Args->SetNumberField(TEXT("value"), 0.75);
			Execute(TEXT("set_property"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("widgetName"), TEXT("MCP_Border"));
			TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			Properties->SetNumberField(TEXT("RenderOpacity"), 0.8);
			Args->SetObjectField(TEXT("properties"), Properties);
			Execute(TEXT("set_style"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			TArray<TSharedPtr<FJsonValue>> Entries;
			for (const TPair<FString, double>& Item : { TPair<FString, double>(TEXT("MCP_Button"), 0.65), TPair<FString, double>(TEXT("MCP_Border"), 0.7) })
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("widgetName"), Item.Key);
				Entry->SetStringField(TEXT("propertyName"), TEXT("RenderOpacity"));
				Entry->SetNumberField(TEXT("value"), Item.Value);
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Args->SetArrayField(TEXT("properties"), Entries);
			Execute(TEXT("bulk_set_properties"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("widgetName"), TEXT("MCP_Button"));
			Args->SetStringField(TEXT("parentWidgetName"), TEXT("Root"));
			Args->SetNumberField(TEXT("index"), 0);
			Execute(TEXT("reorder_child"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("widgetName"), TEXT("MCP_Text"));
			Args->SetStringField(TEXT("newParentWidgetName"), TEXT("MCP_Border"));
			Execute(TEXT("move_widget"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("widgetName"), TEXT("MCP_Button"));
			Execute(TEXT("set_root"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("widgetName"), TEXT("MCP_Button"));
			Args->SetStringField(TEXT("wrapperClass"), TEXT("CanvasPanel"));
			Args->SetStringField(TEXT("wrapperName"), TEXT("MCP_Wrapper"));
			Execute(TEXT("wrap_root"), Args);
		}
		AddWidget(TEXT("TextBlock"), TEXT("MCP_Temporary"), TEXT("MCP_Wrapper"));
		{
			TSharedRef<FJsonObject> Args = ForAsset();
			Args->SetStringField(TEXT("widgetName"), TEXT("MCP_Temporary"));
			Execute(TEXT("remove_widget"), Args);
		}
		Execute(TEXT("clear_binding"), ForAsset());
		Execute(TEXT("read_tree"), ForAsset());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), UtilityWidgetName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Execute(TEXT("create_utility_widget"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), UtilityName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Execute(TEXT("create_utility_blueprint"), Args);
		}
		for (const TPair<FString, FString>& Utility :
			{ TPair<FString, FString>(TEXT("run_utility_widget"), TEXT("/Game/Missing_EUW")), TPair<FString, FString>(TEXT("run_utility_blueprint"), TEXT("/Game/Missing_EUB")) })
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), Utility.Value);
			Execute(Utility.Key, Args, false);
		}

		Execute(TEXT("list_runtime"), MakeShared<FJsonObject>());
		UUserWidget* RuntimeWidget = nullptr;
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			UWidgetBlueprint* Blueprint = Assets ? Cast<UWidgetBlueprint>(Assets->LoadAsset(WidgetPath)) : nullptr;
			if (Blueprint && Blueprint->GeneratedClass)
			{
				RuntimeWidget = CreateWidget<UUserWidget>(GEditor->GetEditorWorldContext().World(), TSubclassOf<UUserWidget>(Blueprint->GeneratedClass),
					FName(*FString(TEXT("RuntimeMCP_") + Suffix)));
			}
		}
		if (RuntimeWidget)
		{
			for (const FString& Action : { TEXT("get_runtime"), TEXT("get_runtime_delegates") })
			{
				TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("widgetName"), RuntimeWidget->GetName());
				Execute(Action, Args);
			}
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("widgetName"), RuntimeWidget->GetName());
			Args->SetStringField(TEXT("functionName"), TEXT("RemoveFromParent"));
			Execute(TEXT("invoke_runtime_function"), Args);
		}
		else
		{
			for (const FString& Action : { TEXT("get_runtime"), TEXT("get_runtime_delegates"), TEXT("invoke_runtime_function") })
			{
				TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("widgetName"), TEXT("MissingRuntimeWidget"));
				Args->SetStringField(TEXT("functionName"), TEXT("RemoveFromParent"));
				Execute(Action, Args, false);
			}
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("widgetClass"), TEXT("/Game/MissingWidget_C"));
			Execute(TEXT("add_to_viewport"), Args, false);
		}

		for (const FString& Action : Actions)
		{
			TestTrue(*FString::Printf(TEXT("Widget action 已黑盒调用：%s"), *Action), Invoked.Contains(Action));
		}

		if (GEditor)
		{
			UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			TArray<UObject*> CreatedAssets;
			for (const FString& Path : { WidgetPath, UtilityWidgetPath, UtilityPath })
			{
				if (UObject* Asset = Assets ? Assets->LoadAsset(Path) : nullptr)
				{
					CreatedAssets.Add(Asset);
				}
			}
			if (!CreatedAssets.IsEmpty())
			{
				ObjectTools::DeleteObjectsUnchecked(CreatedAssets);
			}
		}
		return true;
	}
}

#endif
