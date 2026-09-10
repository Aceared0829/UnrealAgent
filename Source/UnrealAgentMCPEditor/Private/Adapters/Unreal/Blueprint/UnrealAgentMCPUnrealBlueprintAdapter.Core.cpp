// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.Core.cpp
 * @brief Blueprint 资产、变量、函数、接口、编译与校验实现。
 */

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.Internal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/MemberReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "BlueprintEditorLibrary.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/Interface.h"

namespace UnrealAgentMCP
{
	using namespace BlueprintPrivate;

	namespace
	{
		FEdGraphPinType PinTypeFromText(const FString& Text)
		{
			FEdGraphPinType Type;
			const FString Lower = Text.ToLower();
			if (Lower == TEXT("bool"))
				Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			else if (Lower == TEXT("int") || Lower == TEXT("int32"))
				Type.PinCategory = UEdGraphSchema_K2::PC_Int;
			else if (Lower == TEXT("int64"))
				Type.PinCategory = UEdGraphSchema_K2::PC_Int64;
			else if (Lower == TEXT("string"))
				Type.PinCategory = UEdGraphSchema_K2::PC_String;
			else if (Lower == TEXT("name"))
				Type.PinCategory = UEdGraphSchema_K2::PC_Name;
			else if (Lower == TEXT("text"))
				Type.PinCategory = UEdGraphSchema_K2::PC_Text;
			else if (Lower == TEXT("byte"))
				Type.PinCategory = UEdGraphSchema_K2::PC_Byte;
			else if (Lower.StartsWith(TEXT("object:")))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Object;
				Type.PinSubCategoryObject = ResolveClass(Text.Mid(7));
			}
			else
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Real;
				Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			}
			return Type;
		}

		TSharedRef<FJsonObject> GraphInfo(UEdGraph* Graph)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			if (Graph)
			{
				Item->SetStringField(TEXT("name"), Graph->GetName());
				Item->SetStringField(TEXT("class"), Graph->GetClass()->GetPathName());
				Item->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
			}
			return Item;
		}
	}

	FString FUnrealAgentMCPUnrealBlueprintAdapter::ExecuteCore(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create") || Action == TEXT("create_interface"))
		{
			FString AssetPath = StringArg(Args, { TEXT("assetPath"), TEXT("path") });
			if (AssetPath.IsEmpty())
				return Failure(TEXT("缺少 assetPath。"));
			AssetPath.RemoveFromEnd(TEXT("."));
			const FString PackageName = AssetPath.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(AssetPath) : AssetPath;
			if (!FPackageName::IsValidLongPackageName(PackageName))
			{
				return Failure(FString::Printf(TEXT("无效 Blueprint 包路径：%s"), *PackageName));
			}
			const FString Name = FPackageName::GetShortName(PackageName);
			const FString ObjectPath = PackageName + TEXT(".") + Name;
			if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *ObjectPath))
			{
				TSharedRef<FJsonObject> Result = SuccessObject();
				Result->SetStringField(TEXT("assetPath"), Existing->GetPathName());
				Result->SetBoolField(TEXT("created"), false);
				return Serialize(Result);
			}
			const bool bInterface = Action == TEXT("create_interface");
			UClass* Parent = bInterface ? UInterface::StaticClass() : ResolveClass(StringArg(Args, { TEXT("parentClass") }, TEXT("Actor")));
			if (!Parent)
				return Failure(TEXT("找不到 Blueprint 父类。"));
			UPackage* Package = CreatePackage(*PackageName);
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, *Name, bInterface ? BPTYPE_Interface : BPTYPE_Normal, UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(), TEXT("UnrealAgent"));
			if (!Blueprint)
				return Failure(TEXT("创建 Blueprint 失败。"));
			FAssetRegistryModule::AssetCreated(Blueprint);
			SaveBlueprint(Blueprint, true);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
			Result->SetBoolField(TEXT("created"), true);
			return Serialize(Result);
		}

		FString Error;
		UBlueprint* Blueprint = LoadBlueprint(Args, Error);
		if (!Blueprint)
			return Failure(Error);

		if (Action == TEXT("read"))
		{
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
			Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : TEXT("None"));
			Result->SetStringField(TEXT("status"), UEnum::GetValueAsString(Blueprint->Status));
			Result->SetNumberField(TEXT("variableCount"), Blueprint->NewVariables.Num());
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);
			Result->SetNumberField(TEXT("graphCount"), Graphs.Num());
			TArray<TSharedPtr<FJsonValue>> Components;
			if (Blueprint->SimpleConstructionScript)
			{
				for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
				{
					if (!Node || !Node->ComponentTemplate)
						continue;
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
					Item->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetPathName());
					Components.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			Result->SetArrayField(TEXT("components"), Components);
			return Serialize(Result);
		}

		if (Action == TEXT("list_variables"))
		{
			TArray<TSharedPtr<FJsonValue>> Variables;
			for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Variable.VarName.ToString());
				Item->SetStringField(TEXT("type"), Variable.VarType.PinCategory.ToString());
				Item->SetStringField(TEXT("defaultValue"), Variable.DefaultValue);
				Variables.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("variables"), Variables);
			return Serialize(Result);
		}

		if (Action == TEXT("list_functions") || Action == TEXT("list_graphs"))
		{
			TArray<UEdGraph*> Graphs;
			Blueprint->GetAllGraphs(Graphs);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (UEdGraph* Graph : Graphs)
				Values.Add(MakeShared<FJsonValueObject>(GraphInfo(Graph)));
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(Action == TEXT("list_functions") ? TEXT("functions") : TEXT("graphs"), Values);
			return Serialize(Result);
		}

		if (Action == TEXT("resolve_graph"))
		{
			UEdGraph* Graph = ResolveGraph(Blueprint, StringArg(Args, { TEXT("graphName"), TEXT("functionName") }));
			if (!Graph)
				return Failure(TEXT("找不到目标 Blueprint 图。"));
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetObjectField(TEXT("graph"), GraphInfo(Graph));
			return Serialize(Result);
		}

		if (Action == TEXT("add_variable"))
		{
			const FString Name = StringArg(Args, { TEXT("name") });
			if (Name.IsEmpty())
				return Failure(TEXT("缺少变量名。"));
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, *Name, PinTypeFromText(StringArg(Args, { TEXT("varType"), TEXT("type") }, TEXT("float")))))
			{
				return Failure(TEXT("添加变量失败，名称可能已经存在。"));
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("delete_variable"))
		{
			const FString Name = StringArg(Args, { TEXT("name") });
			if (Name.IsEmpty())
				return Failure(TEXT("缺少变量名。"));
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, *Name);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("set_variable_default"))
		{
			const FString Name = StringArg(Args, { TEXT("name") });
			const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, *Name);
			if (!Blueprint->NewVariables.IsValidIndex(Index))
				return Failure(TEXT("找不到变量。"));
			Blueprint->NewVariables[Index].DefaultValue = StringArg(Args, { TEXT("value") });
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("set_variable_properties"))
		{
			const FString Name = StringArg(Args, { TEXT("name") });
			const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, *Name);
			if (!Blueprint->NewVariables.IsValidIndex(Index))
				return Failure(TEXT("找不到变量。"));
			FBPVariableDescription& Variable = Blueprint->NewVariables[Index];
			const FString Category = StringArg(Args, { TEXT("category") });
			const FString Tooltip = StringArg(Args, { TEXT("tooltip") });
			if (!Category.IsEmpty())
				Variable.Category = FText::FromString(Category);
			if (!Tooltip.IsEmpty())
				Variable.SetMetaData(TEXT("tooltip"), Tooltip);
			if (Args->HasField(TEXT("instanceEditable")))
			{
				Variable.PropertyFlags = BoolArg(Args, TEXT("instanceEditable"), false) ? Variable.PropertyFlags | CPF_Edit : Variable.PropertyFlags & ~CPF_Edit;
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("create_function"))
		{
			const FString Name = StringArg(Args, { TEXT("functionName") });
			if (Name.IsEmpty() || ResolveGraph(Blueprint, Name))
				return Failure(TEXT("函数名为空或已存在。"));
			UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, *Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("delete_function"))
		{
			UEdGraph* Graph = ResolveGraph(Blueprint, StringArg(Args, { TEXT("functionName") }));
			if (!Graph)
				return Failure(TEXT("找不到函数图。"));
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("rename_function"))
		{
			UEdGraph* Graph = ResolveGraph(Blueprint, StringArg(Args, { TEXT("oldName") }));
			const FString NewName = StringArg(Args, { TEXT("newName") });
			if (!Graph || NewName.IsEmpty())
				return Failure(TEXT("函数图或新名称无效。"));
			FBlueprintEditorUtils::RenameGraph(Graph, NewName);
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("add_local_variable") || Action == TEXT("list_local_variables"))
		{
			UEdGraph* Graph = ResolveGraph(Blueprint, StringArg(Args, { TEXT("functionName") }));
			if (!Graph)
				return Failure(TEXT("找不到函数图。"));
			if (Action == TEXT("add_local_variable"))
			{
				const FString Name = StringArg(Args, { TEXT("name") });
				if (Name.IsEmpty() || !FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, *Name, PinTypeFromText(StringArg(Args, { TEXT("varType") }, TEXT("float")))))
				{
					return Failure(TEXT("添加局部变量失败。"));
				}
				SaveBlueprint(Blueprint, true);
			}
			TArray<TSharedPtr<FJsonValue>> Variables;
			TArray<UK2Node_FunctionEntry*> Entries;
			Graph->GetNodesOfClass(Entries);
			for (const UK2Node_FunctionEntry* Entry : Entries)
			{
				if (!Entry)
					continue;
				for (const FBPVariableDescription& Variable : Entry->LocalVariables)
				{
					Variables.Add(MakeShared<FJsonValueString>(Variable.VarName.ToString()));
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("variables"), Variables);
			return Serialize(Result);
		}
		else if (Action == TEXT("add_interface"))
		{
			const FString Path = StringArg(Args, { TEXT("interfacePath") });
			UClass* InterfaceClass = ResolveClass(Path);
			if (!InterfaceClass || !FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName()))
			{
				return Failure(TEXT("添加 Blueprint Interface 失败。"));
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("add_function_parameter"))
		{
			UEdGraph* Graph = ResolveGraph(Blueprint, StringArg(Args, { TEXT("functionName") }));
			TArray<UK2Node_FunctionEntry*> Entries;
			if (Graph)
				Graph->GetNodesOfClass(Entries);
			const FString Name = StringArg(Args, { TEXT("name"), TEXT("parameterName") });
			if (Entries.IsEmpty() || Name.IsEmpty())
				return Failure(TEXT("函数或参数名无效。"));
			const bool bOutput = BoolArg(Args, TEXT("isOutput"), false);
			if (!Entries[0]->CreateUserDefinedPin(*Name, PinTypeFromText(StringArg(Args, { TEXT("type"), TEXT("varType") }, TEXT("float"))), bOutput ? EGPD_Input : EGPD_Output))
			{
				return Failure(TEXT("添加函数参数失败。"));
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("override_function"))
		{
			const FName Name(*StringArg(Args, { TEXT("functionName"), TEXT("name") }));
			if (Name.IsNone() || !UBlueprintEditorLibrary::AddFunctionOverride(Blueprint, Name))
			{
				return Failure(TEXT("目标函数不可覆盖或已经以事件形式覆盖。"));
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("add_event_dispatcher"))
		{
			const FName Name(*StringArg(Args, { TEXT("name"), TEXT("dispatcherName") }));
			if (Name.IsNone() || !UBlueprintEditorLibrary::AddEventDispatcher(Blueprint, Name))
			{
				return Failure(TEXT("添加事件分发器失败。"));
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("reparent"))
		{
			UClass* Parent = ResolveClass(StringArg(Args, { TEXT("parentClass") }));
			if (!Parent)
				return Failure(TEXT("找不到新的父类。"));
			Blueprint->ParentClass = Parent;
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("set_class_default"))
		{
			UObject* Cdo = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
			const FString Property = StringArg(Args, { TEXT("propertyName") });
			if (!SetProperty(Cdo, Property, Args->TryGetField(TEXT("value")), Error))
				return Failure(Error);
			SaveBlueprint(Blueprint, false);
		}
		else if (Action == TEXT("compile") || Action == TEXT("validate"))
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			if (Action == TEXT("compile"))
				SaveBlueprint(Blueprint, false);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("status"), UEnum::GetValueAsString(Blueprint->Status));
			Result->SetBoolField(TEXT("valid"), Blueprint->Status != BS_Error);
			return Serialize(Result);
		}
		else if (Action == TEXT("flush_ich"))
		{
			if (UBlueprintGeneratedClass* Class = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
			{
				Class->InheritableComponentHandler = nullptr;
			}
			SaveBlueprint(Blueprint, true);
		}
		else if (Action == TEXT("list_overridable_functions"))
		{
			TArray<TSharedPtr<FJsonValue>> Functions;
			for (TFieldIterator<UFunction> It(Blueprint->ParentClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (It->HasAnyFunctionFlags(FUNC_BlueprintEvent))
					Functions.Add(MakeShared<FJsonValueString>(It->GetName()));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("functions"), Functions);
			return Serialize(Result);
		}
		else
		{
			return Failure(FString::Printf(TEXT("操作 %s 的参数组合尚不满足创建条件。"), *Action));
		}

		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
		return Serialize(Result);
	}
}
