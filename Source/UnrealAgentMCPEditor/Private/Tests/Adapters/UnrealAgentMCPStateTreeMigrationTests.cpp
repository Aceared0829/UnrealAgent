// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPStateTreeMigrationTests.cpp
 * @brief StateTree 三十六项能力的真实资产往返黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"
#include "AssetToolsModule.h"
#include "Components/StateTreeComponentSchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IAssetTools.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeFactory.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPStateTreeMigrationIntegrationTest, "WorldData.UnrealAgent.StateTree.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPStateTreeMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPUnrealStateTreeAdapter::GetImplementedActions();
		TestEqual(TEXT("StateTree action 数量"), Actions.Num(), 36);

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		const FString AssetName = TEXT("ST_MCP_") + Suffix;
		const FString AssetPath = Directory + TEXT("/") + AssetName + TEXT(".") + AssetName;
		UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
		Factory->SetSchemaClass(UStateTreeComponentSchema::StaticClass());
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UStateTree* StateTree = Cast<UStateTree>(AssetTools.CreateAsset(AssetName, Directory, UStateTree::StaticClass(), Factory));
		if (!TestNotNull(TEXT("创建 StateTree 测试资产"), StateTree))
		{
			return false;
		}
		UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
		if (!TestNotNull(TEXT("StateTree 包含编辑数据"), EditorData) || !TestTrue(TEXT("StateTree 包含根状态"), EditorData->SubTrees.Num() > 0))
		{
			return false;
		}
		const FString RootId = EditorData->SubTrees[0]->ID.ToString(EGuidFormats::DigitsWithHyphensLower);

		FUnrealAgentMCPUnrealStateTreeAdapter Adapter;
		TSet<FString> Invoked;
		auto Execute = [&Adapter, &Invoked, this, &AssetPath](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Invoked.Add(Action);
			Args->SetStringField(TEXT("action"), Action);
			Args->SetStringField(TEXT("assetPath"), AssetPath);
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
		auto Empty = []()
		{
			return MakeShared<FJsonObject>();
		};
		auto ForState = [&RootId]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("stateId"), RootId);
			return Args;
		};
		auto ParseString = [](const FString& Json, const FString& Field)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			FString Value;
			if (FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid())
			{
				Object->TryGetStringField(Field, Value);
			}
			return Value;
		};

		Execute(TEXT("read"), Empty());
		Execute(TEXT("list_states"), Empty());
		FString ChildId;
		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetStringField(TEXT("name"), TEXT("MCP子状态"));
			ChildId = ParseString(Execute(TEXT("add_state"), Args), TEXT("stateId"));
		}
		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetStringField(TEXT("propertyName"), TEXT("description"));
			Args->SetStringField(TEXT("value"), TEXT("StateTree 自动化测试状态"));
			Execute(TEXT("set_state_property"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = Empty();
			Args->SetStringField(TEXT("displayName"), TEXT("MCP蓝色"));
			Args->SetStringField(TEXT("color"), TEXT("(R=0.1,G=0.4,B=0.9,A=1.0)"));
			Execute(TEXT("add_color"), Args);
			Execute(TEXT("list_colors"), Empty());
		}

		{
			TSharedRef<FJsonObject> Args = Empty();
			TArray<TSharedPtr<FJsonValue>> RootParameters;
			TSharedRef<FJsonObject> RootParam = MakeShared<FJsonObject>();
			RootParam->SetStringField(TEXT("name"), TEXT("MCPEnabled"));
			RootParam->SetStringField(TEXT("type"), TEXT("Bool"));
			RootParam->SetBoolField(TEXT("value"), true);
			RootParameters.Add(MakeShared<FJsonValueObject>(RootParam));
			Args->SetArrayField(TEXT("parameters"), RootParameters);
			Execute(TEXT("set_root_parameters"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetStringField(TEXT("paramName"), TEXT("MCPValue"));
			Args->SetStringField(TEXT("paramType"), TEXT("Int32"));
			Args->SetNumberField(TEXT("value"), 7);
			Execute(TEXT("add_state_parameter"), Args);
			Execute(TEXT("list_state_parameters"), ForState());
			Args = ForState();
			Args->SetStringField(TEXT("paramName"), TEXT("MCPValue"));
			Args->SetNumberField(TEXT("value"), 11);
			Execute(TEXT("set_state_parameter"), Args);
		}

		const FString TaskType = TEXT("StateTreeRunEnvQueryTask");
		const FString ConditionType = TEXT("StateTreeCompareIntCondition");
		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetStringField(TEXT("structType"), TaskType);
			Execute(TEXT("add_task"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetNumberField(TEXT("taskIndex"), 0);
			Args->SetStringField(TEXT("propertyName"), TEXT("bTaskEnabled"));
			Args->SetBoolField(TEXT("value"), true);
			Execute(TEXT("set_task_property"), Args);
			Args->SetStringField(TEXT("propertyName"), TEXT("QueryOwner"));
			Args->SetStringField(TEXT("value"), TEXT("None"));
			Execute(TEXT("set_task_instance_property"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetStringField(TEXT("structType"), ConditionType);
			Args->SetStringField(TEXT("operand"), TEXT("And"));
			Execute(TEXT("add_enter_condition"), Args);
			Args = ForState();
			Args->SetNumberField(TEXT("conditionIndex"), 0);
			Execute(TEXT("remove_enter_condition"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetStringField(TEXT("transitionType"), TEXT("Succeeded"));
			Args->SetStringField(TEXT("trigger"), TEXT("OnStateSucceeded"));
			Execute(TEXT("add_transition"), Args);
			Args = ForState();
			Args->SetNumberField(TEXT("transitionIndex"), 0);
			Args->SetStringField(TEXT("structType"), ConditionType);
			Execute(TEXT("add_transition_condition"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = Empty();
			Args->SetStringField(TEXT("structType"), TEXT("StateTreeEvaluatorBase"));
			Execute(TEXT("add_evaluator"), Args);
			Args = Empty();
			Args->SetNumberField(TEXT("evaluatorIndex"), 0);
			Args->SetStringField(TEXT("propertyName"), TEXT("Name"));
			Args->SetStringField(TEXT("value"), TEXT("MCPEvaluator"));
			Execute(TEXT("set_evaluator_property"), Args);
			Args->SetStringField(TEXT("propertyName"), TEXT("Missing"));
			Execute(TEXT("set_evaluator_instance_property"), Args, false);
		}
		{
			TSharedRef<FJsonObject> Args = Empty();
			Args->SetStringField(TEXT("structType"), TaskType);
			Execute(TEXT("add_global_task"), Args);
			Args = Empty();
			Args->SetNumberField(TEXT("globalTaskIndex"), 0);
			Args->SetStringField(TEXT("propertyName"), TEXT("bTaskEnabled"));
			Args->SetBoolField(TEXT("value"), true);
			Execute(TEXT("set_global_task_property"), Args);
			Args->SetStringField(TEXT("propertyName"), TEXT("QueryOwner"));
			Args->SetStringField(TEXT("value"), TEXT("None"));
			Execute(TEXT("set_global_task_instance_property"), Args);
		}

		Execute(TEXT("list_bindable_sources"), Empty());
		{
			TSharedRef<FJsonObject> Args = Empty();
			Args->SetStringField(TEXT("sourceStructId"), EditorData->GetRootParametersGuid().ToString());
			Args->SetStringField(TEXT("sourcePath"), TEXT("MCPEnabled"));
			Args->SetStringField(TEXT("targetStructId"), RootId);
			Args->SetStringField(TEXT("targetPath"), TEXT("MCPValue"));
			Execute(TEXT("add_binding"), Args);
			Execute(TEXT("list_bindings"), Empty());
			Args = Empty();
			Args->SetStringField(TEXT("targetStructId"), RootId);
			Args->SetStringField(TEXT("targetPath"), TEXT("MCPValue"));
			Execute(TEXT("remove_binding"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetNumberField(TEXT("transitionIndex"), 0);
			Execute(TEXT("remove_transition"), Args);
			Args = ForState();
			Args->SetNumberField(TEXT("taskIndex"), 0);
			Execute(TEXT("remove_task"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = Empty();
			Args->SetNumberField(TEXT("evaluatorIndex"), 0);
			Execute(TEXT("remove_evaluator"), Args);
			Args = Empty();
			Args->SetNumberField(TEXT("globalTaskIndex"), 0);
			Execute(TEXT("remove_global_task"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForState();
			Args->SetStringField(TEXT("paramName"), TEXT("MCPValue"));
			Execute(TEXT("remove_state_parameter"), Args);
			Execute(TEXT("clear_state_nodes"), ForState());
		}
		Execute(TEXT("validate"), Empty());
		Execute(TEXT("compile"), Empty());
		{
			TSharedRef<FJsonObject> Args = Empty();
			Args->SetStringField(TEXT("stateId"), ChildId);
			Execute(TEXT("remove_state"), Args);
		}

		TestEqual(TEXT("全部 StateTree action 均已执行"), Invoked.Num(), Actions.Num());
		for (const FString& Action : Actions)
		{
			TestTrue(*FString::Printf(TEXT("已覆盖 action：%s"), *Action), Invoked.Contains(Action));
		}
		TArray<UObject*> AssetsToDelete{ StateTree };
		ObjectTools::DeleteObjectsUnchecked(AssetsToDelete);
		return true;
	}
}

#endif
