// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNetworkingAdapter.Variables.cpp
 * @brief Blueprint 成员变量复制标记与 RepNotify 图写入实现。
 */

#include "Adapters/Unreal/Networking/UnrealAgentMCPUnrealNetworkingAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace UnrealAgentMCP
{
	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetPropertyReplicated(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, Error);
		if (!Blueprint)
			return ErrorJson(Error);

		FString VariableName;
		if (!Args->TryGetStringField(TEXT("variableName"), VariableName))
		{
			Args->TryGetStringField(TEXT("propertyName"), VariableName);
		}
		if (VariableName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 variableName。"));
		}
		const FName VariableFName(*VariableName);
		const int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableFName);
		if (!Blueprint->NewVariables.IsValidIndex(VariableIndex))
		{
			return ErrorJson(FString::Printf(TEXT("未找到 Blueprint 成员变量：%s"), *VariableName));
		}

		FString ReplicationType;
		Args->TryGetStringField(TEXT("replicationType"), ReplicationType);
		if (ReplicationType.IsEmpty())
		{
			bool bRepNotify = false;
			bool bReplicated = true;
			Args->TryGetBoolField(TEXT("repNotify"), bRepNotify);
			Args->TryGetBoolField(TEXT("replicated"), bReplicated);
			ReplicationType = bRepNotify ? TEXT("RepNotify") : bReplicated ? TEXT("Replicated") : TEXT("None");
		}

		FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];
		Blueprint->Modify();
		if (ReplicationType.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Variable.PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
			Variable.RepNotifyFunc = NAME_None;
			Variable.ReplicationCondition = COND_None;
			FBlueprintEditorUtils::SetBlueprintVariableRepNotifyFunc(Blueprint, VariableFName, NAME_None);
		}
		else if (ReplicationType.Equals(TEXT("Replicated"), ESearchCase::IgnoreCase))
		{
			Variable.PropertyFlags |= CPF_Net;
			Variable.PropertyFlags &= ~CPF_RepNotify;
			Variable.RepNotifyFunc = NAME_None;
			FBlueprintEditorUtils::SetBlueprintVariableRepNotifyFunc(Blueprint, VariableFName, NAME_None);
		}
		else if (ReplicationType.Equals(TEXT("RepNotify"), ESearchCase::IgnoreCase))
		{
			const FName FunctionName(*FString::Printf(TEXT("OnRep_%s"), *VariableName));
			UEdGraph* FunctionGraph = FindObject<UEdGraph>(Blueprint, *FunctionName.ToString());
			if (!FunctionGraph)
			{
				FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
				FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FunctionGraph, false, nullptr);
			}
			if (!FunctionGraph)
			{
				return ErrorJson(TEXT("RepNotify 函数图创建失败。"));
			}
			Variable.PropertyFlags |= CPF_Net | CPF_RepNotify;
			Variable.RepNotifyFunc = FunctionName;
			FBlueprintEditorUtils::SetBlueprintVariableRepNotifyFunc(Blueprint, VariableFName, FunctionName);
		}
		else
		{
			return ErrorJson(TEXT("replicationType 必须是 None、Replicated 或 RepNotify。"));
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);
		AActor* Defaults = ResolveActorDefaults(Blueprint, Error);
		return Defaults ? SuccessJson(MakeInfoJson(Blueprint, Defaults)) : ErrorJson(Error);
	}
}
