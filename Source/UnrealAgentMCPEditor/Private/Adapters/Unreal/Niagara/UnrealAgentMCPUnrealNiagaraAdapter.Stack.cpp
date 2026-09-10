// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNiagaraAdapter.Stack.cpp
 * @brief Niagara 模块栈、输入、静态开关与 HLSL 模块实现。
 */

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"

#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraEmitter.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		ENiagaraScriptUsage UsageForContext(const FString& Context)
		{
			if (Context.Equals(TEXT("ParticleUpdate"), ESearchCase::IgnoreCase))
			{
				return ENiagaraScriptUsage::ParticleUpdateScript;
			}
			if (Context.Equals(TEXT("EmitterSpawn"), ESearchCase::IgnoreCase))
			{
				return ENiagaraScriptUsage::EmitterSpawnScript;
			}
			if (Context.Equals(TEXT("EmitterUpdate"), ESearchCase::IgnoreCase))
			{
				return ENiagaraScriptUsage::EmitterUpdateScript;
			}
			return ENiagaraScriptUsage::ParticleSpawnScript;
		}

		FString BuildScratchHlsl(const TSharedPtr<FJsonObject>& Args)
		{
			FString Hlsl;
			const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
			if (Args.IsValid() && Args->TryGetArrayField(TEXT("inputs"), Inputs))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Inputs)
				{
					const TSharedPtr<FJsonObject>* Item = nullptr;
					FString Name;
					FString Type = TEXT("float");
					if (Value.IsValid() && Value->TryGetObject(Item) && (*Item)->TryGetStringField(TEXT("name"), Name))
					{
						(*Item)->TryGetStringField(TEXT("type"), Type);
						Hlsl += FString::Printf(TEXT("%s %s;\n"), *Type, *Name);
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
			if (Args.IsValid() && Args->TryGetArrayField(TEXT("outputs"), Outputs))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Outputs)
				{
					const TSharedPtr<FJsonObject>* Item = nullptr;
					FString Name;
					FString Type = TEXT("float");
					if (Value.IsValid() && Value->TryGetObject(Item) && (*Item)->TryGetStringField(TEXT("name"), Name))
					{
						(*Item)->TryGetStringField(TEXT("type"), Type);
						Hlsl += FString::Printf(TEXT("out %s %s;\n"), *Type, *Name);
					}
				}
			}
			return Hlsl;
		}
	}

	FString FUnrealAgentMCPUnrealNiagaraAdapter::ExecuteStackAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		using namespace NiagaraPrivate;
		if (Action == TEXT("create_module_from_hlsl") || Action == TEXT("create_scratch_module"))
		{
			FString Name;
			if (const FString Error = RequireString(Args, TEXT("name"), Name); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			FString Hlsl = OptionalString(Args, TEXT("hlsl"));
			if (Action == TEXT("create_module_from_hlsl") && Hlsl.IsEmpty())
			{
				return Failure(TEXT("缺少必填参数 'hlsl'。"));
			}
			if (Hlsl.IsEmpty())
			{
				Hlsl = BuildScratchHlsl(Args);
			}
			bool bCreated = false;
			FString CreateError;
			UNiagaraScript* Script = Cast<UNiagaraScript>(CreateAsset(Name, OptionalString(Args, TEXT("packagePath"), TEXT("/Game/VFX/Modules")), UNiagaraScript::StaticClass(),
				TEXT("/Script/NiagaraEditor.NiagaraModuleScriptFactory"), OptionalString(Args, TEXT("onConflict"), TEXT("skip")), bCreated, CreateError));
			if (!Script)
			{
				return Failure(CreateError);
			}
			UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
			UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
			if (bCreated && Graph && !Hlsl.IsEmpty())
			{
				FGraphNodeCreator<UNiagaraNodeCustomHlsl> Creator(*Graph);
				UNiagaraNodeCustomHlsl* CustomNode = Creator.CreateNode();
				Creator.Finalize();
				if (FStrProperty* Property = CastField<FStrProperty>(CustomNode->GetClass()->FindPropertyByName(TEXT("CustomHlsl"))))
				{
					CustomNode->Modify();
					Property->SetPropertyValue(Property->ContainerPtrToValuePtr<void>(CustomNode), Hlsl);
				}
				CustomNode->ReconstructNode();
				CustomNode->PostEditChange();
				Graph->NotifyGraphChanged();
			}
			SaveAsset(Script);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("path"), Script->GetPathName());
			Result->SetBoolField(TEXT("created"), bCreated);
			Result->SetNumberField(TEXT("hlslLength"), Hlsl.Len());
			return Serialize(Result);
		}

		FString SystemPath;
		if (const FString Error = RequireString(Args, TEXT("systemPath"), SystemPath); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		UNiagaraSystem* System = Cast<UNiagaraSystem>(LoadAsset(SystemPath, UNiagaraSystem::StaticClass()));
		if (!System)
		{
			return Failure(FString::Printf(TEXT("找不到 NiagaraSystem：%s"), *SystemPath));
		}
		const FString EmitterName = OptionalString(Args, TEXT("emitterName"));
		FResolvedEmitter Resolved = ResolveEmitter(System, EmitterName, OptionalInt(Args, TEXT("emitterIndex"), 0));
		if (!Resolved.Data || !Resolved.Emitter)
		{
			return Failure(TEXT("无法解析目标发射器。"));
		}

		if (Action == TEXT("get_compiled_hlsl"))
		{
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("systemPath"), System->GetPathName());
			Result->SetStringField(TEXT("emitter"), Resolved.Emitter->GetName());
			Result->SetStringField(TEXT("simTarget"), Resolved.Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"));
			if (UNiagaraScript* Script = Resolved.Data->GetGPUComputeScript())
			{
				Result->SetStringField(TEXT("scriptName"), Script->GetName());
				Result->SetBoolField(TEXT("isCompilable"), Script->IsCompilable());
			}
			else
			{
				Result->SetStringField(TEXT("note"), TEXT("CPU 模拟不包含 GPU HLSL 脚本。"));
			}
			return Serialize(Result);
		}

		const FString StackContext = OptionalString(Args, TEXT("stackContext"), TEXT("all"));
		if (Action == TEXT("add_module"))
		{
			FString ModulePath = OptionalString(Args, TEXT("modulePath"));
			if (ModulePath.IsEmpty())
			{
				ModulePath = OptionalString(Args, TEXT("assetPath"));
			}
			if (ModulePath.IsEmpty())
			{
				return Failure(TEXT("缺少必填参数 'modulePath'。"));
			}
			const FString Context = StackContext.Equals(TEXT("all"), ESearchCase::IgnoreCase) ? TEXT("ParticleSpawn") : StackContext;
			TArray<FScriptSlot> Scripts;
			CollectEmitterScripts(Resolved.Data, Context, Scripts);
			if (Scripts.IsEmpty() || !Scripts[0].Script)
			{
				return Failure(TEXT("目标模块栈脚本不可用。"));
			}
			UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Scripts[0].Script->GetLatestSource());
			bool bFoundModuleAsset = false;
			const bool bAdded = Source && Source->AddModuleIfMissing(ModulePath, UsageForContext(Context), bFoundModuleAsset);
			if (!bFoundModuleAsset)
			{
				return Failure(FString::Printf(TEXT("找不到 Niagara 模块：%s"), *ModulePath));
			}
			Resolved.Emitter->PostEditChange();
			SaveAsset(System);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("added"), bAdded);
			Result->SetStringField(TEXT("modulePath"), ModulePath);
			Result->SetStringField(TEXT("stackContext"), Context);
			return Serialize(Result);
		}

		TArray<FScriptSlot> Scripts;
		CollectEmitterScripts(Resolved.Data, StackContext, Scripts);
		const FString ModuleFilter = OptionalString(Args, TEXT("moduleName"));
		if (Action == TEXT("list_module_inputs") || Action == TEXT("list_static_switches"))
		{
			TArray<TSharedPtr<FJsonValue>> Modules;
			for (const FScriptSlot& Slot : Scripts)
			{
				UNiagaraGraph* Graph = GraphOfScript(Slot.Script);
				if (!Graph)
				{
					continue;
				}
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
					if (!Call || (!ModuleFilter.IsEmpty() && !Call->GetFunctionName().Equals(ModuleFilter, ESearchCase::IgnoreCase)))
					{
						continue;
					}
					TArray<TSharedPtr<FJsonValue>> Values;
					if (Action == TEXT("list_module_inputs"))
					{
						for (UEdGraphPin* Pin : Call->Pins)
						{
							if (Pin && Pin->Direction == EGPD_Input)
							{
								Values.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
							}
						}
					}
					else if (UNiagaraGraph* CalledGraph = Call->GetCalledGraph())
					{
						for (const FNiagaraVariable& Variable : CalledGraph->FindStaticSwitchInputs(false))
						{
							TSharedRef<FJsonObject> Switch = MakeShared<FJsonObject>();
							Switch->SetStringField(TEXT("name"), Variable.GetName().ToString());
							Switch->SetStringField(TEXT("type"), Variable.GetType().GetName());
							for (UEdGraphPin* Pin : Call->Pins)
							{
								if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == Variable.GetName())
								{
									Switch->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
									Switch->SetBoolField(TEXT("boundToPin"), true);
									break;
								}
							}
							Values.Add(MakeShared<FJsonValueObject>(Switch));
						}
					}
					if (Action == TEXT("list_static_switches") && Values.IsEmpty())
					{
						continue;
					}
					TSharedRef<FJsonObject> Module = MakeShared<FJsonObject>();
					Module->SetStringField(TEXT("stackContext"), Slot.Context);
					Module->SetStringField(TEXT("moduleName"), Call->GetFunctionName());
					Module->SetArrayField(Action == TEXT("list_module_inputs") ? TEXT("inputs") : TEXT("staticSwitches"), Values);
					Modules.Add(MakeShared<FJsonValueObject>(Module));
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("modules"), Modules);
			Result->SetNumberField(TEXT("moduleCount"), Modules.Num());
			return Serialize(Result);
		}

		FString TargetName;
		const FString NameField = Action == TEXT("set_module_input") ? TEXT("inputName") : TEXT("switchName");
		if (ModuleFilter.IsEmpty() || (TargetName = OptionalString(Args, NameField)).IsEmpty())
		{
			return Failure(FString::Printf(TEXT("%s 需要 moduleName 与 %s。"), *Action, *NameField));
		}
		const TSharedPtr<FJsonValue>* Value = Args->Values.Find(TEXT("value"));
		if (!Value)
		{
			return Failure(TEXT("缺少必填参数 'value'。"));
		}
		FString TextValue;
		if (!(*Value)->TryGetString(TextValue))
		{
			double Number = 0.0;
			bool bBoolean = false;
			if ((*Value)->TryGetNumber(Number))
			{
				TextValue = FString::SanitizeFloat(Number);
			}
			else if ((*Value)->TryGetBool(bBoolean))
			{
				TextValue = bBoolean ? TEXT("true") : TEXT("false");
			}
			else
			{
				return Failure(TEXT("value 需要字符串、数值或布尔值。"));
			}
		}
		int32 Updated = 0;
		for (const FScriptSlot& Slot : Scripts)
		{
			UNiagaraGraph* Graph = GraphOfScript(Slot.Script);
			if (!Graph)
			{
				continue;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
				if (!Call || !Call->GetFunctionName().Equals(ModuleFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}
				for (UEdGraphPin* Pin : Call->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input && Pin->PinName.ToString().Equals(TargetName, ESearchCase::IgnoreCase))
					{
						Pin->Modify();
						Pin->DefaultValue = TextValue;
						++Updated;
					}
				}
				if (Updated > 0)
				{
					Call->MarkNodeRequiresSynchronization(TEXT("UnrealAgentMCPNiagaraUpdate"), true);
				}
			}
			if (Updated > 0)
			{
				Graph->NotifyGraphChanged();
			}
		}
		if (Updated == 0)
		{
			return Failure(FString::Printf(TEXT("找不到模块 %s 的输入 %s。"), *ModuleFilter, *TargetName));
		}
		Resolved.Emitter->PostEditChange();
		SaveAsset(System);
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("moduleName"), ModuleFilter);
		Result->SetStringField(NameField, TargetName);
		Result->SetStringField(TEXT("value"), TextValue);
		Result->SetNumberField(TEXT("pinsUpdated"), Updated);
		return Serialize(Result);
	}
}
