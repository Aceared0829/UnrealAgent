// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPBlueprintMigrationTests.cpp
 * @brief Blueprint 六十项独立能力的真实资产黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"
#include "Application/Domains/Blueprint/UnrealAgentMCPBlueprintService.h"
#include "Application/Ports/UnrealAgentMCPBlueprintPort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPBlueprintMigrationIntegrationTest, "WorldData.UnrealAgent.Blueprint.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPBlueprintMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPBlueprintService::GetImplementedActions();
		TestEqual(TEXT("Blueprint action 数量"), Actions.Num(), 60);
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		const FString Name = TEXT("BP_MCP_") + Suffix;
		const FString InterfaceName = TEXT("BPI_MCP_") + Suffix;
		const FString CopyName = Name + TEXT("_Copy");
		const FString AssetPath = Directory + TEXT("/") + Name + TEXT(".") + Name;
		const FString InterfacePath = Directory + TEXT("/") + InterfaceName + TEXT(".") + InterfaceName;
		const FString CopyPath = Directory + TEXT("/") + CopyName + TEXT(".") + CopyName;

		TSharedRef<IUnrealAgentMCPBlueprintPort> Port = MakeShared<FUnrealAgentMCPUnrealBlueprintAdapter>();
		FUnrealAgentMCPBlueprintService Service(Port);
		TSet<FString> Invoked;
		auto Base = [&AssetPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), AssetPath);
			return Args;
		};
		auto Execute = [&Service, &Invoked, this](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Invoked.Add(Action);
			Args->SetStringField(TEXT("action"), Action);
			const FString Json = Service.Execute(Args);
			const bool bContract = Json.Contains(TEXT("\"success\":true")) || (Json.Contains(TEXT("\"success\":false")) && Json.Contains(TEXT("\"error\"")));
			TestTrue(*FString::Printf(TEXT("%s 返回标准结果：%s"), *Action, *Json.Left(800)), bContract);
			if (bExpectSuccess)
				TestTrue(*FString::Printf(TEXT("%s 执行成功"), *Action), Json.Contains(TEXT("\"success\":true")));
			return Json;
		};
		auto ParseString = [](const FString& Json, const FString& Field)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			FString Value;
			if (FJsonSerializer::Deserialize(Reader, Object) && Object)
				Object->TryGetStringField(Field, Value);
			return Value;
		};

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), AssetPath);
			Args->SetStringField(TEXT("parentClass"), TEXT("Actor"));
			Execute(TEXT("create"), Args);
			Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), InterfacePath);
			Execute(TEXT("create_interface"), Args);
		}
		Execute(TEXT("read"), Base());
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("name"), TEXT("MCPValue"));
			Args->SetStringField(TEXT("type"), TEXT("float"));
			Execute(TEXT("add_variable"), Args);
			Args->SetStringField(TEXT("category"), TEXT("MCP"));
			Args->SetStringField(TEXT("tooltip"), TEXT("自动化变量"));
			Execute(TEXT("set_variable_properties"), Args);
			Args->SetStringField(TEXT("value"), TEXT("3.5"));
			Execute(TEXT("set_variable_default"), Args);
		}
		Execute(TEXT("list_variables"), Base());
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("functionName"), TEXT("MCPFunction"));
			Execute(TEXT("create_function"), Args);
			Args->SetStringField(TEXT("name"), TEXT("InputValue"));
			Args->SetStringField(TEXT("type"), TEXT("float"));
			Execute(TEXT("add_function_parameter"), Args);
			Args->SetStringField(TEXT("name"), TEXT("LocalValue"));
			Args->SetStringField(TEXT("varType"), TEXT("float"));
			Execute(TEXT("add_local_variable"), Args);
			Execute(TEXT("list_local_variables"), Args);
		}
		Execute(TEXT("list_functions"), Base());
		Execute(TEXT("list_graphs"), Base());
		Execute(TEXT("resolve_graph"), Base());
		Execute(TEXT("read_graph"), Base());
		Execute(TEXT("read_graph_summary"), Base());
		Execute(TEXT("get_execution_flow"), Base());
		Execute(TEXT("list_node_types"), MakeShared<FJsonObject>());
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("search"), TEXT("IfThenElse"));
			Execute(TEXT("search_node_types"), Args);
		}
		FString NodeId;
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("nodeClass"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
			const FString Json = Execute(TEXT("add_node"), Args);
			TSharedPtr<FJsonObject> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed)
			{
				const TSharedPtr<FJsonObject>* Node = nullptr;
				if (Parsed->TryGetObjectField(TEXT("node"), Node) && Node)
					(*Node)->TryGetStringField(TEXT("nodeId"), NodeId);
			}
			Args->SetStringField(TEXT("nodeId"), NodeId);
			Args->SetStringField(TEXT("propertyName"), TEXT("NodeComment"));
			Args->SetStringField(TEXT("value"), TEXT("MCP 自动化节点"));
			Execute(TEXT("set_node_property"), Args);
			Execute(TEXT("read_node_property"), Args);
			Args->SetNumberField(TEXT("x"), 640);
			Args->SetNumberField(TEXT("y"), 320);
			Execute(TEXT("set_node_position"), Args);
			Execute(TEXT("export_nodes_t3d"), Args);
		}
		Execute(TEXT("auto_layout"), Base());
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetArrayField(TEXT("connections"), {});
			Execute(TEXT("connect_pins_batch"), Args);
		}
		Execute(TEXT("connect_pins"), Base(), false);
		Execute(TEXT("import_nodes_t3d"), Base());

		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("componentClass"), TEXT("/Script/Engine.StaticMeshComponent"));
			Args->SetStringField(TEXT("componentName"), TEXT("MCPMesh"));
			Execute(TEXT("add_component"), Args);
			Args->SetStringField(TEXT("propertyName"), TEXT("bAutoActivate"));
			Args->SetBoolField(TEXT("value"), true);
			Execute(TEXT("set_component_property"), Args);
			Execute(TEXT("get_component_property"), Args);
			Execute(TEXT("read_component_properties"), Args);
			Args->SetArrayField(TEXT("materials"), {});
			Execute(TEXT("set_component_override_materials"), Args);
		}
		Execute(TEXT("set_capsule_size"), Base(), false);
		Execute(TEXT("reparent_component"), Base(), false);
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("timelineName"), TEXT("MCPTimeline"));
			Args->SetStringField(TEXT("trackName"), TEXT("MCPFloat"));
			Execute(TEXT("add_timeline_track"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("name"), TEXT("MCPDispatcher"));
			Execute(TEXT("add_event_dispatcher"), Args);
		}
		Execute(TEXT("add_interface"), Base(), false);
		Execute(TEXT("override_function"), Base(), false);
		Execute(TEXT("list_overridable_functions"), Base());
		Execute(TEXT("reparent"), Base(), false);
		Execute(TEXT("flush_ich"), Base());
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("propertyName"), TEXT("InitialLifeSpan"));
			Args->SetNumberField(TEXT("value"), 0.0);
			Execute(TEXT("set_class_default"), Args);
			Execute(TEXT("set_cdo_property"), Args);
			Execute(TEXT("get_cdo_properties"), Args);
		}
		Execute(TEXT("set_actor_tick_settings"), Base());
		Execute(TEXT("run_construction_script"), Base());
		Execute(TEXT("compile"), Base());
		Execute(TEXT("validate"), Base());
		Execute(TEXT("get_dependencies"), Base());
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("targetPath"), CopyPath);
			Execute(TEXT("duplicate"), Args);
			Args->SetStringField(TEXT("targetPath"), AssetPath);
			Execute(TEXT("diff"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetArrayField(TEXT("assetPaths"), { MakeShared<FJsonValueString>(AssetPath) });
			Execute(TEXT("compile_all"), Args);
		}
		{
			TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
			Operation->SetStringField(TEXT("action"), TEXT("read"));
			TSharedRef<FJsonObject> Args = Base();
			Args->SetArrayField(TEXT("operations"), { MakeShared<FJsonValueObject>(Operation) });
			Execute(TEXT("author"), Args);
		}
		Execute(TEXT("cleanup_graph"), Base());
		{
			TSharedRef<FJsonObject> Args = Base();
			Args->SetStringField(TEXT("nodeId"), NodeId);
			Execute(TEXT("delete_node"), Args, false);
			Args = Base();
			Args->SetStringField(TEXT("componentName"), TEXT("MCPMesh"));
			Execute(TEXT("remove_component"), Args);
			Args = Base();
			Args->SetStringField(TEXT("name"), TEXT("MCPValue"));
			Execute(TEXT("delete_variable"), Args);
			Args = Base();
			Args->SetStringField(TEXT("oldName"), TEXT("MCPFunction"));
			Args->SetStringField(TEXT("newName"), TEXT("MCPFunctionRenamed"));
			Execute(TEXT("rename_function"), Args);
			Args = Base();
			Args->SetStringField(TEXT("functionName"), TEXT("MCPFunctionRenamed"));
			Execute(TEXT("delete_function"), Args);
		}

		TestEqual(TEXT("所有 Blueprint action 均已执行"), Invoked.Num(), Actions.Num());
		for (const FString& Action : Actions)
			TestTrue(*FString::Printf(TEXT("已覆盖 action：%s"), *Action), Invoked.Contains(Action));

		if (GEditor)
		{
			UEditorAssetSubsystem* Assets = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			TArray<UObject*> Created;
			for (const FString& Path : { AssetPath, InterfacePath, CopyPath })
				if (UObject* Asset = Assets ? Assets->LoadAsset(Path) : nullptr)
					Created.Add(Asset);
			if (!Created.IsEmpty())
				ObjectTools::DeleteObjectsUnchecked(Created);
		}
		return true;
	}
}

#endif
