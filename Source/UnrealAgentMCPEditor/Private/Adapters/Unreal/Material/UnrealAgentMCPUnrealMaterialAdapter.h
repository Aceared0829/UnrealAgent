// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.h
 * @brief Material action execution.
 */

#include "CoreMinimal.h"

#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"

class FJsonObject;

class UMaterial;
class UMaterialExpression;
class UMaterialFunction;
class UMaterialInterface;
class UMaterialInstanceConstant;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealMaterialAdapter final
	{
	public:
		FString Execute(const TSharedPtr<FJsonObject>& Args);
		static TArray<FString> GetImplementedActions();

		FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);

	private:
		FString ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteInstanceAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteGraphAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteFunctionAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString ExecuteInspectionAction(const FString& Action, const TSharedPtr<FJsonObject>& Args);

		TUniquePtr<Transactions::FUnrealAgentMCPScopedEditorTransaction> ActiveTransaction;
	};
}
