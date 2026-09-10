#pragma once

/**
 * @file UnrealAgentMCPProjectPort.h
 * @brief Project 应用服务访问工程文件、配置和模块状态的稳定端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPProjectPort
	{
	public:
		virtual ~IUnrealAgentMCPProjectPort() = default;

		virtual FString GetStatus(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetProject(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadConfig(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SearchConfig(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListConfigTags(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadCppHeader(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadModule(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListModules(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SearchCpp(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadEngineHeader(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString FindEngineSymbol(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListEngineModules(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SearchEngineCpp(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SearchTools(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ExecutePythonReport(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListFiles(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetConfig(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Build(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString GenerateProjectFiles(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString CreateCppClass(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListProjectModules(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListLoadedModules(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString IsModuleLoaded(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString LiveCodingCompile(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString LiveCodingStatus(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString WriteCppFile(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadCppSource(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString WriteSourceFile(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ReadSourceFile(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddModuleDependency(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
