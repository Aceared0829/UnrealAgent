// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPMaterialMigrationTests.cpp
 * @brief Material 四十一项目录及核心材质、实例、函数、图谱往返黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ObjectTools.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPMaterialMigrationIntegrationTest, "WorldData.UnrealAgent.Material.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPMaterialMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Actions = FUnrealAgentMCPUnrealMaterialAdapter::GetImplementedActions();
		TestEqual(TEXT("Material action 数量"), Actions.Num(), 41);
		for (const FString& Required :
			{ TEXT("create_simple"), TEXT("set_parameter"), TEXT("add_expression"), TEXT("create_function"), TEXT("export_graph"), TEXT("render_preview") })
		{
			TestTrue(*FString::Printf(TEXT("Material action 已注册：%s"), *Required), Actions.Contains(Required));
		}

		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Directory = TEXT("/Game/UnrealAgentAutomation");
		const FString MaterialName = TEXT("M_MCP_") + Suffix;
		const FString MaterialPath = Directory + TEXT("/") + MaterialName + TEXT(".") + MaterialName;
		const FString PlainMaterialName = TEXT("M_MCP_Plain_") + Suffix;
		const FString PlainMaterialPath = Directory + TEXT("/") + PlainMaterialName + TEXT(".") + PlainMaterialName;
		const FString InstanceName = TEXT("MI_MCP_") + Suffix;
		const FString InstancePath = Directory + TEXT("/") + InstanceName + TEXT(".") + InstanceName;
		const FString FunctionName = TEXT("MF_MCP_") + Suffix;
		const FString FunctionPath = Directory + TEXT("/") + FunctionName + TEXT(".") + FunctionName;
		const FString DuplicateName = TEXT("M_MCP_Copy_") + Suffix;
		const FString DuplicatePath = Directory + TEXT("/") + DuplicateName + TEXT(".") + DuplicateName;

		FUnrealAgentMCPUnrealMaterialAdapter Adapter;
		auto Execute = [&Adapter, this](const FString& Action, const TSharedRef<FJsonObject>& Args, const bool bExpectSuccess = true)
		{
			Args->SetStringField(TEXT("action"), Action);
			const FString Result = Adapter.Execute(Args);
			if (bExpectSuccess)
			{
				TestTrue(*FString::Printf(TEXT("%s 返回成功：%s"), *Action, *Result.Left(1200)), Result.Contains(TEXT("\"success\":true")));
			}
			return Result;
		};
		auto ForMaterial = [&MaterialPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), MaterialPath);
			return Args;
		};
		auto ForGraph = [&MaterialPath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("materialPath"), MaterialPath);
			return Args;
		};

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("label"), TEXT("材质自动化事务"));
			Execute(TEXT("begin_transaction"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), PlainMaterialName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Execute(TEXT("create"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), MaterialName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Args->SetArrayField(TEXT("baseColor"), { MakeShared<FJsonValueNumber>(0.1), MakeShared<FJsonValueNumber>(0.25), MakeShared<FJsonValueNumber>(0.8) });
			Args->SetNumberField(TEXT("metallic"), 0.7);
			Args->SetNumberField(TEXT("roughness"), 0.22);
			Execute(TEXT("create_simple"), Args);
		}
		Execute(TEXT("read"), ForMaterial());
		Execute(TEXT("list_parameters"), ForMaterial());
		Execute(TEXT("list_expressions"), ForGraph());
		Execute(TEXT("list_expression_types"), MakeShared<FJsonObject>());
		Execute(TEXT("get_shader_stats"), ForMaterial());
		Execute(TEXT("validate"), ForMaterial());
		Execute(TEXT("export_graph"), ForMaterial());

		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("expressionType"), TEXT("ScalarParameter"));
			Args->SetStringField(TEXT("name"), TEXT("MCP_Roughness"));
			Args->SetStringField(TEXT("parameterName"), TEXT("MCP_Roughness"));
			Args->SetNumberField(TEXT("defaultValue"), 0.33);
			Execute(TEXT("add_expression"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("expressionName"), TEXT("MCP_Roughness"));
			Args->SetStringField(TEXT("property"), TEXT("Roughness"));
			Execute(TEXT("connect_to_property"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("expressionType"), TEXT("Custom"));
			Args->SetStringField(TEXT("name"), TEXT("MCP_Custom"));
			Execute(TEXT("add_expression"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("expressionName"), TEXT("MCP_Custom"));
			Args->SetStringField(TEXT("code"), TEXT("return InputValue;"));
			Args->SetArrayField(TEXT("inputs"), { MakeShared<FJsonValueString>(TEXT("InputValue")) });
			Args->SetStringField(TEXT("outputType"), TEXT("float1"));
			Execute(TEXT("set_custom_expression"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("sourceExpression"), TEXT("MCP_Roughness"));
			Args->SetStringField(TEXT("targetExpression"), TEXT("MCP_Custom"));
			Args->SetStringField(TEXT("targetInput"), TEXT("InputValue"));
			Execute(TEXT("connect_expressions"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("expressionName"), TEXT("MCP_Roughness"));
			Args->SetNumberField(TEXT("value"), 0.42);
			Execute(TEXT("set_expression_value"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForMaterial();
			Args->SetArrayField(TEXT("color"), { MakeShared<FJsonValueNumber>(0.8), MakeShared<FJsonValueNumber>(0.15), MakeShared<FJsonValueNumber>(0.05) });
			Execute(TEXT("set_base_color"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("texturePath"), TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
			Args->SetStringField(TEXT("property"), TEXT("BaseColor"));
			Execute(TEXT("connect_texture"), Args);
		}
		for (const TPair<FString, FString>& Setting : { TPair<FString, FString>(TEXT("set_shading_model"), TEXT("DefaultLit")),
				 TPair<FString, FString>(TEXT("set_blend_mode"), TEXT("Opaque")), TPair<FString, FString>(TEXT("set_domain"), TEXT("Surface")) })
		{
			TSharedRef<FJsonObject> Args = ForMaterial();
			Args->SetStringField(Setting.Key == TEXT("set_shading_model") ? TEXT("shadingModel")
					: Setting.Key == TEXT("set_blend_mode")               ? TEXT("blendMode")
																		  : TEXT("materialDomain"),
				Setting.Value);
			Execute(Setting.Key, Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForMaterial();
			Args->SetStringField(TEXT("usage"), TEXT("Nanite"));
			Args->SetBoolField(TEXT("enabled"), true);
			Execute(TEXT("set_usage"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("parentPath"), MaterialPath);
			Args->SetStringField(TEXT("name"), InstanceName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Execute(TEXT("create_instance"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), InstancePath);
			Args->SetStringField(TEXT("parameterName"), TEXT("MCP_Roughness"));
			Args->SetStringField(TEXT("parameterType"), TEXT("scalar"));
			Args->SetNumberField(TEXT("value"), 0.66);
			Execute(TEXT("set_parameter"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), InstancePath);
			Args->SetStringField(TEXT("parameterName"), TEXT("MCP_TestSwitch"));
			Args->SetBoolField(TEXT("value"), true);
			Execute(TEXT("set_static_switch"), Args);
		}
		for (const FString& Action : { TEXT("read_instance"), TEXT("list_static_switches") })
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), InstancePath);
			Execute(Action, Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("assetPath"), InstancePath);
			TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
			Parameter->SetStringField(TEXT("name"), TEXT("MCP_Roughness"));
			Parameter->SetStringField(TEXT("type"), TEXT("scalar"));
			Parameter->SetNumberField(TEXT("value"), 0.77);
			Item->SetArrayField(TEXT("parameters"), { MakeShared<FJsonValueObject>(Parameter) });
			Args->SetArrayField(TEXT("instances"), { MakeShared<FJsonValueObject>(Item) });
			Execute(TEXT("batch_set_instances"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), InstancePath);
			Args->SetStringField(TEXT("newParentPath"), MaterialPath);
			Execute(TEXT("set_instance_parent"), Args);
			Execute(TEXT("clear_instance_parameters"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("name"), FunctionName);
			Args->SetStringField(TEXT("packagePath"), Directory);
			Args->SetStringField(TEXT("description"), TEXT("材质函数自动化测试"));
			Execute(TEXT("create_function"), Args);
		}
		for (const TPair<FString, FString>& Node :
			{ TPair<FString, FString>(TEXT("FunctionInput"), TEXT("InValue")), TPair<FString, FString>(TEXT("FunctionOutput"), TEXT("OutValue")) })
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("functionPath"), FunctionPath);
			Args->SetStringField(TEXT("expressionType"), Node.Key);
			if (Node.Key == TEXT("FunctionInput"))
			{
				Args->SetStringField(TEXT("inputName"), Node.Value);
				Args->SetStringField(TEXT("inputType"), TEXT("Scalar"));
			}
			else
			{
				Args->SetStringField(TEXT("outputName"), Node.Value);
			}
			Execute(TEXT("add_function_expression"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("functionPath"), FunctionPath);
			Args->SetStringField(TEXT("sourceExpression"), TEXT("InValue"));
			Args->SetStringField(TEXT("targetExpression"), TEXT("OutValue"));
			Execute(TEXT("connect_function_expressions"), Args);
			Execute(TEXT("list_function_expressions"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("sourcePath"), MaterialPath);
			Args->SetStringField(TEXT("destinationPath"), Directory + TEXT("/") + DuplicateName);
			Execute(TEXT("duplicate"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), MaterialPath);
			Args->SetArrayField(TEXT("nodes"),
				{ MakeShared<FJsonValueObject>(
					[]()
					{
						TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
						Node->SetStringField(TEXT("id"), TEXT("base"));
						Node->SetStringField(TEXT("expressionType"), TEXT("Constant3Vector"));
						Node->SetArrayField(TEXT("value"), { MakeShared<FJsonValueNumber>(0.2), MakeShared<FJsonValueNumber>(0.3), MakeShared<FJsonValueNumber>(0.4) });
						return Node;
					}()) });
			TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetStringField(TEXT("source"), TEXT("base"));
			Output->SetStringField(TEXT("property"), TEXT("BaseColor"));
			Args->SetArrayField(TEXT("propertyConnections"), { MakeShared<FJsonValueObject>(Output) });
			Execute(TEXT("import_graph"), Args);
			Args->SetArrayField(TEXT("nodes"), {});
			Args->SetArrayField(TEXT("propertyConnections"), {});
			Execute(TEXT("build_graph"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForMaterial();
			Execute(TEXT("recompile"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForMaterial();
			Args->SetStringField(TEXT("property"), TEXT("BaseColor"));
			Execute(TEXT("disconnect_property"), Args);
		}
		{
			TSharedRef<FJsonObject> Args = ForGraph();
			Args->SetStringField(TEXT("expressionName"), TEXT("MCP_Custom"));
			// import_graph 已重建图，删除旧名称应被明确拒绝。
			const FString Result = Execute(TEXT("delete_expression"), Args, false);
			TestTrue(*FString::Printf(TEXT("删除缺失表达式返回可诊断错误：%s"), *Result), Result.Contains(TEXT("\"success\":false")));
		}
		{
			TSharedRef<FJsonObject> Args = ForMaterial();
			Args->SetStringField(TEXT("outputPath"), TEXT("D:/UnrealAgent-Outside.png"));
			const FString Result = Execute(TEXT("render_preview"), Args, false);
			TestTrue(TEXT("预览路径沙箱拒绝 Saved 外路径"), Result.Contains(TEXT("\"success\":false")));
		}
		Execute(TEXT("end_transaction"), MakeShared<FJsonObject>());

		UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		TArray<UObject*> ObjectsToDelete;
		for (const FString& Path : { InstancePath, FunctionPath, DuplicatePath, PlainMaterialPath, MaterialPath })
		{
			if (AssetSubsystem)
			{
				if (UObject* Asset = AssetSubsystem->LoadAsset(Path))
				{
					ObjectsToDelete.Add(Asset);
				}
			}
		}
		if (!ObjectsToDelete.IsEmpty())
		{
			ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		}
		return true;
	}
}

#endif
