// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.h
 * @brief 编辑器领域端口的 Unreal Editor 原生实现。
 */

#include "Application/Ports/UnrealAgentMCPEditorPort.h"

class FJsonObject;
class FJsonValue;
class FProperty;
class UObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealEditorAdapter final : public IUnrealAgentMCPEditorPort
	{
	public:
		FUnrealAgentMCPUnrealEditorAdapter();

		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) override;
		virtual TSharedPtr<Execution::IMcpTaskStepper> CreateTaskStepper(const TSharedPtr<FJsonObject>& Args) override;

		static FString GetString(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names);
		static UObject* ResolveObject(const TSharedPtr<FJsonObject>& Args, bool bPreferPlayWorld, FString& OutError);
		static FString InvokeReflectedFunction(UObject* Target, const FString& FunctionName, const TSharedPtr<FJsonObject>& Arguments);
		static void ShutdownDialogState();

	private:
		FString Session(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Objects(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Runtime(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Viewport(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Diagnostics(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Build(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Dialogs(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Sequencer(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
