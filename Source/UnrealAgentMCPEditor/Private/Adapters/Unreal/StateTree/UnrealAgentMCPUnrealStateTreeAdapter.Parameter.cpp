// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.Parameter.cpp
 * @brief StateTree 状态参数与根参数操作。
 */

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StructUtils/PropertyBag.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"

namespace UnrealAgentMCP
{
	using namespace StateTreePrivate;

	FString FUnrealAgentMCPUnrealStateTreeAdapter::ExecuteParameterAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
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
			return Failure(TEXT("找不到 StateTree 或编辑数据。"));
		}

		if (Action == TEXT("set_root_parameters"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Parameters = nullptr;
			if (!Args->TryGetArrayField(TEXT("parameters"), Parameters))
			{
				return Failure(TEXT("parameters 必须是参数数组。"));
			}
			FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(EditorData->GetRootParametersPropertyBag());
			MarkModified(StateTree, EditorData);
			Bag.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *Parameters)
			{
				const TSharedPtr<FJsonObject>* Parameter = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(Parameter))
				{
					continue;
				}
				FString Name;
				FString Type;
				(*Parameter)->TryGetStringField(TEXT("name"), Name);
				(*Parameter)->TryGetStringField(TEXT("type"), Type);
				FString Error;
				if (!AddBagProperty(Bag, Name, Type, Error))
				{
					return Failure(Error);
				}
				if (const TSharedPtr<FJsonValue> Initial = (*Parameter)->TryGetField(TEXT("value")); Initial.IsValid() && !SetBagValue(Bag, Name, Initial, Error))
				{
					return Failure(Error);
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("updated"), true);
			Result->SetNumberField(TEXT("parameterCount"), Parameters->Num());
			return Serialize(Result);
		}

		UStateTreeState* State = ResolveState(EditorData, Args);
		if (!State)
		{
			return Failure(TEXT("找不到目标状态。"));
		}
		FInstancedPropertyBag& Bag = State->Parameters.Parameters;
		if (Action == TEXT("list_state_parameters"))
		{
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("parameters"), SerializePropertyBag(Bag));
			Result->SetBoolField(TEXT("fixedLayout"), State->Parameters.bFixedLayout);
			return Serialize(Result);
		}

		FString ParameterName;
		if (const FString Error = RequireString(Args, TEXT("paramName"), ParameterName); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		MarkModified(StateTree, State);
		if (Action == TEXT("add_state_parameter"))
		{
			if (State->Parameters.bFixedLayout)
			{
				return Failure(TEXT("固定布局状态不能添加参数。"));
			}
			FString ParameterType;
			if (const FString Error = RequireString(Args, TEXT("paramType"), ParameterType); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			FString Error;
			if (!AddBagProperty(Bag, ParameterName, ParameterType, Error))
			{
				return Failure(Error);
			}
			if (const TSharedPtr<FJsonValue> Initial = Args->TryGetField(TEXT("value")); Initial.IsValid() && !SetBagValue(Bag, ParameterName, Initial, Error))
			{
				return Failure(Error);
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("created"), true);
			return Serialize(Result);
		}
		if (Action == TEXT("remove_state_parameter"))
		{
			if (State->Parameters.bFixedLayout)
			{
				return Failure(TEXT("固定布局状态不能移除参数。"));
			}
			Bag.RemovePropertyByName(*ParameterName);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("removed"), true);
			return Serialize(Result);
		}
		const TSharedPtr<FJsonValue> Value = Args->TryGetField(TEXT("value"));
		FString Error;
		if (!SetBagValue(Bag, ParameterName, Value, Error))
		{
			return Failure(Error);
		}
		if (State->Parameters.bFixedLayout)
		{
			if (const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(*ParameterName))
			{
				State->SetParametersPropertyOverridden(Desc->ID, true);
			}
		}
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetBoolField(TEXT("updated"), true);
		return Serialize(Result);
	}
}
