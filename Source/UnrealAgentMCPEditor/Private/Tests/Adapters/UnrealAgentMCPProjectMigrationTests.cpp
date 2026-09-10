// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPProjectMigrationTests.cpp
 * @brief Project 应用服务与 Unreal Adapter 的真实工程集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Project/UnrealAgentMCPUnrealProjectAdapter.h"
#include "Application/Domains/Project/UnrealAgentMCPProjectService.h"
#include "Application/Ports/UnrealAgentMCPProjectPort.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPProjectMigrationIntegrationTest, "WorldData.UnrealAgent.Project.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPProjectMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		TSharedRef<IUnrealAgentMCPProjectPort> Port = MakeShared<FUnrealAgentMCPUnrealProjectAdapter>();
		FUnrealAgentMCPProjectService Service(Port);

		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1000)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		Execute(TEXT("get_status"), MakeShared<FJsonObject>());
		Execute(TEXT("get_info"), MakeShared<FJsonObject>());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("projectPath"), FPaths::GetProjectFilePath());
			Execute(TEXT("set_project"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("configName"), TEXT("Engine"));
			Execute(TEXT("read_config"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("query"), TEXT("Project"));
			Args->SetNumberField(TEXT("maxResults"), 20);
			Execute(TEXT("search_config"), Args);
		}

		Execute(TEXT("list_config_tags"), MakeShared<FJsonObject>());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("headerPath"), TEXT("Plugins/UnrealAgent/Source/UnrealAgentMCPEditor/Private/Application/Ports/UnrealAgentMCPProjectPort.h"));
			Execute(TEXT("read_cpp_header"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("moduleName"), TEXT("UnrealAgentMCPEditor"));
			Execute(TEXT("read_module"), Args);
		}

		Execute(TEXT("list_modules"), MakeShared<FJsonObject>());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("query"), TEXT("IUnrealAgentMCPProjectPort"));
			Args->SetStringField(TEXT("directory"), TEXT("Plugins/UnrealAgent"));
			Args->SetNumberField(TEXT("maxResults"), 20);
			Execute(TEXT("search_cpp"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("headerPath"), TEXT("Runtime/Core/Public/Misc/Paths.h"));
			Execute(TEXT("read_engine_header"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("symbol"), TEXT("EngineSourceDir"));
			Args->SetNumberField(TEXT("maxResults"), 2);
			Execute(TEXT("find_engine_symbol"), Args);
		}

		Execute(TEXT("list_engine_modules"), MakeShared<FJsonObject>());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("query"), TEXT("EngineSourceDir"));
			Args->SetStringField(TEXT("tree"), TEXT("Runtime"));
			Args->SetStringField(TEXT("subdirectory"), TEXT("Core"));
			Args->SetNumberField(TEXT("maxResults"), 2);
			Execute(TEXT("search_engine_cpp"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("query"), TEXT("engine header"));
			Args->SetNumberField(TEXT("limit"), 5);
			Execute(TEXT("search_tools"), Args);
		}

		Execute(TEXT("execute_python_report"), MakeShared<FJsonObject>());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("directory"), TEXT("Plugins/UnrealAgent"));
			Args->SetArrayField(TEXT("extensions"), { MakeShared<FJsonValueString>(TEXT("uplugin")) });
			Args->SetNumberField(TEXT("maxResults"), 20);
			Execute(TEXT("list_files"), Args);
		}

		Execute(TEXT("list_project_modules"), MakeShared<FJsonObject>());

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("filter"), TEXT("UnrealAgent"));
			Args->SetBoolField(TEXT("loadedOnly"), true);
			Execute(TEXT("list_loaded_modules"), Args);
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("moduleName"), TEXT("UnrealAgentMCPEditor"));
			const FString Result = Execute(TEXT("is_module_loaded"), Args);
			TestTrue(TEXT("UnrealAgentMCPEditor 模块处于加载状态"), Result.Contains(TEXT("\"loaded\":true")));
		}

		Execute(TEXT("live_coding_status"), MakeShared<FJsonObject>());

		const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultUnrealAgentMCPAutomation.ini"));
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("configName"), TEXT("UnrealAgentMCPAutomation"));
			Args->SetStringField(TEXT("section"), TEXT("Automation"));
			Args->SetStringField(TEXT("key"), TEXT("Migrated"));
			Args->SetStringField(TEXT("value"), TEXT("true"));
			Execute(TEXT("set_config"), Args);
			TestTrue(TEXT("测试配置文件已创建"), IFileManager::Get().FileExists(*ConfigPath));
		}

		const FString TestRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgentMCPAutomation")));
		const FString TestModuleDirectory = FPaths::Combine(TestRoot, TEXT("Source/UnrealAgentMCPAutomation"));
		const FString TestBuildFile = FPaths::Combine(TestModuleDirectory, TEXT("UnrealAgentMCPAutomation.Build.cs"));
		IFileManager::Get().MakeDirectory(*TestModuleDirectory, true);
		const FString BuildRules = TEXT("using UnrealBuildTool;\n") TEXT("public class UnrealAgentMCPAutomation : ModuleRules\n") TEXT("{\n")
			TEXT("\tpublic UnrealAgentMCPAutomation(ReadOnlyTargetRules Target) : base(Target)\n") TEXT("\t{\n") TEXT("\t\tPrivateDependencyModuleNames.AddRange(new string[]\n")
				TEXT("\t\t{\n") TEXT("\t\t\t\"Core\"\n") TEXT("\t\t});\n") TEXT("\t}\n") TEXT("}\n");
		TestTrue(TEXT("临时模块规则创建成功"), FFileHelper::SaveStringToFile(BuildRules, *TestBuildFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("className"), TEXT("MigratedObject"));
			Args->SetStringField(TEXT("parentClass"), TEXT("Object"));
			Args->SetStringField(TEXT("moduleName"), TEXT("UnrealAgentMCPAutomation"));
			Args->SetStringField(TEXT("classDomain"), TEXT("public"));
			Args->SetStringField(TEXT("subPath"), TEXT("Generated"));
			const FString Result = Execute(TEXT("create_cpp_class"), Args);
			TestTrue(TEXT("C++ 类头文件已生成"), Result.Contains(TEXT("MigratedObject.h")));
		}

		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("moduleName"), TEXT("UnrealAgentMCPAutomation"));
			Args->SetStringField(TEXT("dependency"), TEXT("Projects"));
			Args->SetStringField(TEXT("access"), TEXT("private"));
			Execute(TEXT("add_module_dependency"), Args);
			FString UpdatedRules;
			FFileHelper::LoadFileToString(UpdatedRules, *TestBuildFile);
			TestTrue(TEXT("模块依赖已写入"), UpdatedRules.Contains(TEXT("\"Projects\"")));
		}

		GConfig->UnloadFile(ConfigPath);
		IFileManager::Get().Delete(*ConfigPath, false, true, true);
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
		return true;
	}
}

#endif
