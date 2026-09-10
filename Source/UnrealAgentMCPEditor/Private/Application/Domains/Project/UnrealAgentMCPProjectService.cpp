// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPProjectService.cpp
 * @brief Project 应用服务实现；只依赖 Project Port 和 JSON 契约。
 */

#include "Application/Domains/Project/UnrealAgentMCPProjectService.h"

#include "Application/Ports/UnrealAgentMCPProjectPort.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> NormalizeProjectArguments(const TSharedPtr<FJsonObject>& Args)
		{
			TSharedRef<FJsonObject> Normalized = MakeShared<FJsonObject>();
			if (Args.IsValid())
			{
				Normalized->Values = Args->Values;
			}
			auto CopyAlias = [&Normalized](const TCHAR* Target, std::initializer_list<const TCHAR*> Aliases)
			{
				if (Normalized->HasField(Target))
					return;
				for (const TCHAR* Alias : Aliases)
				{
					const TSharedPtr<FJsonValue> Value = Normalized->TryGetField(Alias);
					if (Value.IsValid())
					{
						Normalized->SetField(Target, Value);
						return;
					}
				}
			};
			CopyAlias(TEXT("file_path"), { TEXT("path"), TEXT("headerPath"), TEXT("sourcePath"), TEXT("fileName") });
			CopyAlias(TEXT("moduleName"), { TEXT("module"), TEXT("name") });
			return Normalized;
		}

		FString UnsupportedProjectAction(const FString& Action)
		{
			TArray<TSharedPtr<FJsonValue>> Actions;
			for (const FString& Name : FUnrealAgentMCPProjectService::GetImplementedActions())
			{
				Actions.Add(MakeShared<FJsonValueString>(Name));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("project"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Project action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Actions);
			return JsonObjectToString(Result);
		}
	}

	FUnrealAgentMCPProjectService::FUnrealAgentMCPProjectService(TSharedRef<IUnrealAgentMCPProjectPort> InProjectPort) : ProjectPort(MoveTemp(InProjectPort))
	{
	}

	TArray<FString> FUnrealAgentMCPProjectService::GetImplementedActions()
	{
		return { TEXT("get_status"), TEXT("set_project"), TEXT("get_info"), TEXT("read_config"), TEXT("search_config"), TEXT("list_config_tags"), TEXT("read_cpp_header"),
			TEXT("read_module"), TEXT("list_modules"), TEXT("search_cpp"), TEXT("read_engine_header"), TEXT("find_engine_symbol"), TEXT("list_engine_modules"),
			TEXT("search_engine_cpp"), TEXT("search_tools"), TEXT("execute_python_report"), TEXT("list_files"), TEXT("set_config"), TEXT("build"), TEXT("generate_project_files"),
			TEXT("create_cpp_class"), TEXT("list_project_modules"), TEXT("list_loaded_modules"), TEXT("is_module_loaded"), TEXT("live_coding_compile"), TEXT("live_coding_status"),
			TEXT("write_cpp_file"), TEXT("read_cpp_source"), TEXT("write_source_file"), TEXT("read_source_file"), TEXT("add_module_dependency") };
	}

	FString FUnrealAgentMCPProjectService::Execute(const TSharedPtr<FJsonObject>& Args) const
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TSharedRef<FJsonObject> Normalized = NormalizeProjectArguments(Args);
		if (Action == TEXT("get_status"))
			return ProjectPort->GetStatus(Normalized);
		if (Action == TEXT("set_project"))
			return ProjectPort->SetProject(Normalized);
		if (Action == TEXT("get_info"))
			return ProjectPort->GetInfo(Normalized);
		if (Action == TEXT("read_config"))
			return ProjectPort->ReadConfig(Normalized);
		if (Action == TEXT("search_config"))
			return ProjectPort->SearchConfig(Normalized);
		if (Action == TEXT("list_config_tags"))
			return ProjectPort->ListConfigTags(Normalized);
		if (Action == TEXT("read_cpp_header"))
			return ProjectPort->ReadCppHeader(Normalized);
		if (Action == TEXT("read_module"))
			return ProjectPort->ReadModule(Normalized);
		if (Action == TEXT("list_modules"))
			return ProjectPort->ListModules(Normalized);
		if (Action == TEXT("search_cpp"))
			return ProjectPort->SearchCpp(Normalized);
		if (Action == TEXT("read_engine_header"))
			return ProjectPort->ReadEngineHeader(Normalized);
		if (Action == TEXT("find_engine_symbol"))
			return ProjectPort->FindEngineSymbol(Normalized);
		if (Action == TEXT("list_engine_modules"))
			return ProjectPort->ListEngineModules(Normalized);
		if (Action == TEXT("search_engine_cpp"))
			return ProjectPort->SearchEngineCpp(Normalized);
		if (Action == TEXT("search_tools"))
			return ProjectPort->SearchTools(Normalized);
		if (Action == TEXT("execute_python_report"))
			return ProjectPort->ExecutePythonReport(Normalized);
		if (Action == TEXT("list_files"))
			return ProjectPort->ListFiles(Normalized);
		if (Action == TEXT("set_config"))
			return ProjectPort->SetConfig(Normalized);
		if (Action == TEXT("build"))
			return ProjectPort->Build(Normalized);
		if (Action == TEXT("generate_project_files"))
			return ProjectPort->GenerateProjectFiles(Normalized);
		if (Action == TEXT("create_cpp_class"))
			return ProjectPort->CreateCppClass(Normalized);
		if (Action == TEXT("list_project_modules"))
			return ProjectPort->ListProjectModules(Normalized);
		if (Action == TEXT("list_loaded_modules"))
			return ProjectPort->ListLoadedModules(Normalized);
		if (Action == TEXT("is_module_loaded"))
			return ProjectPort->IsModuleLoaded(Normalized);
		if (Action == TEXT("live_coding_compile"))
			return ProjectPort->LiveCodingCompile(Normalized);
		if (Action == TEXT("live_coding_status"))
			return ProjectPort->LiveCodingStatus(Normalized);
		if (Action == TEXT("write_cpp_file"))
			return ProjectPort->WriteCppFile(Normalized);
		if (Action == TEXT("read_cpp_source"))
			return ProjectPort->ReadCppSource(Normalized);
		if (Action == TEXT("write_source_file"))
			return ProjectPort->WriteSourceFile(Normalized);
		if (Action == TEXT("read_source_file"))
			return ProjectPort->ReadSourceFile(Normalized);
		if (Action == TEXT("add_module_dependency"))
			return ProjectPort->AddModuleDependency(Normalized);
		return UnsupportedProjectAction(Action);
	}
}
