// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.cpp
 * @brief 将 Niagara 操作分派到资产、组件、发射器和模块栈实现。
 */

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonValue.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	TArray<FString> FUnrealAgentMCPUnrealNiagaraAdapter::GetImplementedActions()
	{
		return { TEXT("list"), TEXT("get_info"), TEXT("validate"), TEXT("spawn"), TEXT("spawn_actor"), TEXT("reactivate"), TEXT("set_parameter"), TEXT("create"),
			TEXT("create_emitter"), TEXT("add_emitter"), TEXT("remove_emitter"), TEXT("list_emitters"), TEXT("set_emitter_property"), TEXT("list_modules"),
			TEXT("get_emitter_info"), TEXT("list_renderers"), TEXT("add_renderer"), TEXT("remove_renderer"), TEXT("set_renderer_property"), TEXT("inspect_data_interfaces"),
			TEXT("create_system_from_spec"), TEXT("get_compiled_hlsl"), TEXT("list_system_parameters"), TEXT("list_module_inputs"), TEXT("set_module_input"), TEXT("add_module"),
			TEXT("list_static_switches"), TEXT("set_static_switch"), TEXT("create_module_from_hlsl"), TEXT("create_scratch_module"), TEXT("batch") };
	}

	FString FUnrealAgentMCPUnrealNiagaraAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TArray<FString> Actions = GetImplementedActions();
		if (!Actions.Contains(Action))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Name : Actions)
			{
				Values.Add(MakeShared<FJsonValueString>(Name));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("niagara"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Niagara action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		return ExecuteAction(Action, SafeArgs);
	}

	FString FUnrealAgentMCPUnrealNiagaraAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("batch"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
			if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("ops"), Operations))
			{
				return NiagaraPrivate::Failure(TEXT("ops 必须是操作数组。"));
			}
			TArray<TSharedPtr<FJsonValue>> Results;
			int32 StoppedAt = INDEX_NONE;
			for (int32 Index = 0; Index < Operations->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject>* Operation = nullptr;
				FString SubAction;
				if (!(*Operations)[Index].IsValid() || !(*Operations)[Index]->TryGetObject(Operation) || !(*Operation)->TryGetStringField(TEXT("action"), SubAction) ||
					SubAction == TEXT("batch"))
				{
					StoppedAt = Index;
					break;
				}
				TSharedRef<FJsonObject> SubArgs = MakeShared<FJsonObject>();
				const TSharedPtr<FJsonObject>* Parameters = nullptr;
				if ((*Operation)->TryGetObjectField(TEXT("params"), Parameters))
				{
					SubArgs = MakeShared<FJsonObject>(**Parameters);
				}
				SubArgs->SetStringField(TEXT("action"), SubAction);
				const FString SubResult = ExecuteAction(SubAction, SubArgs);
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("action"), SubAction);
				Entry->SetStringField(TEXT("result"), SubResult);
				Results.Add(MakeShared<FJsonValueObject>(Entry));
				if (SubResult.Contains(TEXT("\"success\":false")))
				{
					StoppedAt = Index;
					break;
				}
			}
			TSharedRef<FJsonObject> Result = NiagaraPrivate::SuccessObject();
			Result->SetArrayField(TEXT("results"), Results);
			if (StoppedAt == INDEX_NONE)
			{
				Result->SetField(TEXT("stoppedAt"), MakeShared<FJsonValueNull>());
			}
			else
			{
				Result->SetNumberField(TEXT("stoppedAt"), StoppedAt);
			}
			return NiagaraPrivate::Serialize(Result);
		}

		static const TSet<FString> ComponentActions{ TEXT("spawn"), TEXT("spawn_actor"), TEXT("reactivate"), TEXT("set_parameter") };
		if (ComponentActions.Contains(Action))
		{
			return ExecuteComponentAction(Action, Args);
		}
		static const TSet<FString> EmitterActions{ TEXT("add_emitter"), TEXT("remove_emitter"), TEXT("list_emitters"), TEXT("set_emitter_property"), TEXT("get_emitter_info"),
			TEXT("list_renderers"), TEXT("add_renderer"), TEXT("remove_renderer"), TEXT("set_renderer_property") };
		if (EmitterActions.Contains(Action))
		{
			return ExecuteEmitterAction(Action, Args);
		}
		static const TSet<FString> StackActions{ TEXT("list_module_inputs"), TEXT("set_module_input"), TEXT("add_module"), TEXT("list_static_switches"), TEXT("set_static_switch"),
			TEXT("get_compiled_hlsl"), TEXT("create_module_from_hlsl"), TEXT("create_scratch_module") };
		if (StackActions.Contains(Action))
		{
			return ExecuteStackAction(Action, Args);
		}
		return ExecuteAssetAction(Action, Args);
	}
}
