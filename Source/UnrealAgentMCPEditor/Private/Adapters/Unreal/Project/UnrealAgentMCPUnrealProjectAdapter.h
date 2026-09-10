// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealProjectAdapter.h
 * @brief 使用 Unreal 与受限项目文件系统实现 Project 应用端口。
 */

#include "Application/Ports/UnrealAgentMCPProjectPort.h"

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealProjectAdapter final : public IUnrealAgentMCPProjectPort
	{
	public:
		virtual FString GetStatus(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetProject(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GetInfo(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadConfig(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SearchConfig(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListConfigTags(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadCppHeader(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadModule(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListModules(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SearchCpp(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadEngineHeader(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString FindEngineSymbol(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListEngineModules(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SearchEngineCpp(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SearchTools(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ExecutePythonReport(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListFiles(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetConfig(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Build(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString GenerateProjectFiles(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString CreateCppClass(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListProjectModules(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListLoadedModules(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString IsModuleLoaded(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString LiveCodingCompile(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString LiveCodingStatus(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString WriteCppFile(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadCppSource(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString WriteSourceFile(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ReadSourceFile(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString AddModuleDependency(const TSharedPtr<FJsonObject>& Args) override;
	};
}
