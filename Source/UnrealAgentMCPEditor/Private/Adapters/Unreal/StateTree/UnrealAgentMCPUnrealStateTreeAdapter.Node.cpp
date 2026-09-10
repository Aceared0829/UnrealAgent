// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.Node.cpp
 * @brief StateTree Task、Condition、Evaluator 与 Global Task 操作。
 */

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditorData.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"

namespace UnrealAgentMCP
{
	using namespace StateTreePrivate;

	static bool ApplyInitialProperties(FStateTreeEditorNode& Node, const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!Args.IsValid() || !Args->TryGetObjectField(TEXT("instanceProperties"), Properties))
		{
			return true;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
		{
			if (!SetNodeProperty(Node, true, Pair.Key, Pair.Value, OutError))
			{
				return false;
			}
		}
		return true;
	}

	FString FUnrealAgentMCPUnrealStateTreeAdapter::ExecuteNodeAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (const FString Error = RequireString(Args, TEXT("assetPath"), AssetPath); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		UStateTree* StateTree = LoadStateTree(AssetPath);
		UStateTreeEditorData* EditorData = GetEditorData(StateTree);
		if (!StateTree || !EditorData)
		{
			return Failure(FString::Printf(TEXT("找不到 StateTree：%s"), *AssetPath));
		}

		const bool bEvaluator = Action.Contains(TEXT("evaluator"));
		const bool bGlobalTask = Action.Contains(TEXT("global_task"));
		const bool bEnterCondition = Action.Contains(TEXT("enter_condition"));
		UStateTreeState* State = nullptr;
		if (!bEvaluator && !bGlobalTask)
		{
			State = ResolveState(EditorData, Args);
			if (!State)
			{
				return Failure(TEXT("找不到目标状态。"));
			}
		}

		TArray<FStateTreeEditorNode>* Nodes = nullptr;
		UObject* Owner = EditorData;
		const UScriptStruct* BaseStruct = nullptr;
		FString IndexName;
		if (bEvaluator)
		{
			Nodes = &EditorData->Evaluators;
			BaseStruct = FStateTreeEvaluatorBase::StaticStruct();
			IndexName = TEXT("evaluatorIndex");
		}
		else if (bGlobalTask)
		{
			Nodes = &EditorData->GlobalTasks;
			BaseStruct = FStateTreeTaskBase::StaticStruct();
			IndexName = TEXT("globalTaskIndex");
		}
		else if (bEnterCondition)
		{
			Nodes = &State->EnterConditions;
			Owner = State;
			BaseStruct = FStateTreeConditionBase::StaticStruct();
			IndexName = TEXT("conditionIndex");
		}
		else
		{
			Nodes = &State->Tasks;
			Owner = State;
			BaseStruct = FStateTreeTaskBase::StaticStruct();
			IndexName = TEXT("taskIndex");
		}

		if (Action.StartsWith(TEXT("add_")))
		{
			FString StructType;
			if (const FString Error = RequireString(Args, TEXT("structType"), StructType); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			MarkModified(StateTree, Owner);
			FStateTreeEditorNode* NewNode = nullptr;
			FString Error;
			if (!AddNode(*Nodes, Owner, StructType, BaseStruct, NewNode, Error) || !ApplyInitialProperties(*NewNode, Args, Error))
			{
				if (NewNode)
					Nodes->Pop();
				return Failure(Error);
			}
			if (bEnterCondition)
			{
				NewNode->ExpressionOperand =
					OptionalString(Args, TEXT("operand")).Equals(TEXT("Or"), ESearchCase::IgnoreCase) ? EStateTreeExpressionOperand::Or : EStateTreeExpressionOperand::And;
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("created"), true);
			Result->SetStringField(TEXT("nodeId"), GuidString(NewNode->ID));
			Result->SetNumberField(IndexName, Nodes->Num() - 1);
			return Serialize(Result);
		}

		const int32 Index = OptionalInt(Args, IndexName);
		if (!Nodes->IsValidIndex(Index))
		{
			return Failure(FString::Printf(TEXT("无效的节点索引：%d"), Index));
		}
		MarkModified(StateTree, Owner);
		if (Action.StartsWith(TEXT("remove_")))
		{
			Nodes->RemoveAt(Index);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("removed"), true);
			return Serialize(Result);
		}

		FString PropertyName;
		if (const FString Error = RequireString(Args, TEXT("propertyName"), PropertyName); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		const TSharedPtr<FJsonValue> Value = Args->TryGetField(TEXT("value"));
		if (!Value.IsValid())
		{
			return Failure(TEXT("缺少必填参数 'value'。"));
		}
		const bool bInstance = Action.Contains(TEXT("instance_property"));
		FString Error;
		if (!SetNodeProperty((*Nodes)[Index], bInstance, PropertyName, Value, Error))
		{
			return Failure(Error);
		}
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetBoolField(TEXT("updated"), true);
		return Serialize(Result);
	}
}
