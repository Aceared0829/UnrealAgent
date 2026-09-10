// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.Transition.cpp
 * @brief StateTree Transition 与条件操作。
 */

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "StateTree.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"

namespace UnrealAgentMCP
{
	using namespace StateTreePrivate;

	static EStateTreeTransitionTrigger ParseTrigger(const FString& Value)
	{
		if (Value.Equals(TEXT("OnStateSucceeded"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionTrigger::OnStateSucceeded;
		if (Value.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionTrigger::OnStateFailed;
		if (Value.Equals(TEXT("OnTick"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionTrigger::OnTick;
		if (Value.Equals(TEXT("OnEvent"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionTrigger::OnEvent;
		return EStateTreeTransitionTrigger::OnStateCompleted;
	}

	static EStateTreeTransitionType ParseTransitionType(const FString& Value)
	{
		if (Value.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::Succeeded;
		if (Value.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::Failed;
		if (Value.Equals(TEXT("Parent"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::Parent;
		if (Value.Equals(TEXT("NextState"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::NextState;
		if (Value.Equals(TEXT("NextSelectableState"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::NextSelectableState;
		if (Value.Equals(TEXT("NextParent"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::NextParent;
		if (Value.Equals(TEXT("NextSelectableParent"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::NextSelectableParent;
		if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			return EStateTreeTransitionType::None;
		return EStateTreeTransitionType::GotoState;
	}

	FString FUnrealAgentMCPUnrealStateTreeAdapter::ExecuteTransitionAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (const FString Error = RequireString(Args, TEXT("assetPath"), AssetPath); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		UStateTree* StateTree = LoadStateTree(AssetPath);
		UStateTreeEditorData* EditorData = GetEditorData(StateTree);
		UStateTreeState* State = ResolveState(EditorData, Args);
		if (!StateTree || !EditorData || !State)
		{
			return Failure(TEXT("找不到 StateTree 或目标状态。"));
		}
		MarkModified(StateTree, State);

		if (Action == TEXT("add_transition"))
		{
			const EStateTreeTransitionType Type = ParseTransitionType(OptionalString(Args, TEXT("transitionType"), TEXT("GotoState")));
			UStateTreeState* TargetState = nullptr;
			FString TargetId = OptionalString(Args, TEXT("targetStateId"));
			if (!TargetId.IsEmpty())
			{
				TargetState = EditorData->GetMutableStateByID(ParseGuid(TargetId));
			}
			FStateTreeTransition& Transition = State->AddTransition(ParseTrigger(OptionalString(Args, TEXT("trigger"))), Type, TargetState);
			Transition.bDelayTransition = OptionalBool(Args, TEXT("delayEnabled"), false);
			Transition.DelayDuration = static_cast<float>(OptionalInt(Args, TEXT("delay"), 0));
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("created"), true);
			Result->SetStringField(TEXT("transitionId"), GuidString(Transition.ID));
			Result->SetNumberField(TEXT("transitionIndex"), State->Transitions.Num() - 1);
			return Serialize(Result);
		}

		const int32 TransitionIndex = OptionalInt(Args, TEXT("transitionIndex"));
		if (!State->Transitions.IsValidIndex(TransitionIndex))
		{
			return Failure(FString::Printf(TEXT("无效的 Transition 索引：%d"), TransitionIndex));
		}
		if (Action == TEXT("remove_transition"))
		{
			State->Transitions.RemoveAt(TransitionIndex);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("removed"), true);
			return Serialize(Result);
		}

		FString StructType;
		if (const FString Error = RequireString(Args, TEXT("structType"), StructType); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		FStateTreeEditorNode* NewNode = nullptr;
		FString Error;
		if (!AddNode(State->Transitions[TransitionIndex].Conditions, State, StructType, FStateTreeConditionBase::StaticStruct(), NewNode, Error))
		{
			return Failure(Error);
		}
		NewNode->ExpressionOperand =
			OptionalString(Args, TEXT("operand")).Equals(TEXT("Or"), ESearchCase::IgnoreCase) ? EStateTreeExpressionOperand::Or : EStateTreeExpressionOperand::And;
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetBoolField(TEXT("created"), true);
		Result->SetStringField(TEXT("nodeId"), GuidString(NewNode->ID));
		return Serialize(Result);
	}
}
