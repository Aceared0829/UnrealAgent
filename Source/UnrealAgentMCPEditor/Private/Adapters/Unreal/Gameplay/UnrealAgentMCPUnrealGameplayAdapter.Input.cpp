// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.Input.cpp
 * @brief Enhanced Input 资产发现、创建、读取和映射编辑实现。
 */

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.h"

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.Internal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputLibrary.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	using namespace GameplayPrivate;

	namespace
	{
		UClass* FindInputClass(const FString& Name, UClass* Base)
		{
			UClass* Class = LoadObject<UClass>(nullptr, *Name);
			if (!Class)
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (It->IsChildOf(Base) && (It->GetName().Equals(Name, ESearchCase::IgnoreCase) || It->GetPathName().Contains(Name)))
					{
						Class = *It;
						break;
					}
				}
			return Class && Class->IsChildOf(Base) && !Class->HasAnyClassFlags(CLASS_Abstract) ? Class : nullptr;
		}

		TSharedRef<FJsonObject> MappingJson(const FEnhancedActionKeyMapping& Mapping, const int32 Index)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetNumberField(TEXT("index"), Index);
			Item->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());
			Item->SetStringField(TEXT("inputAction"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT("None"));
			TArray<TSharedPtr<FJsonValue>> Triggers;
			for (const UInputTrigger* Trigger : Mapping.Triggers)
				if (Trigger)
					Triggers.Add(MakeShared<FJsonValueString>(Trigger->GetClass()->GetPathName()));
			TArray<TSharedPtr<FJsonValue>> Modifiers;
			for (const UInputModifier* Modifier : Mapping.Modifiers)
				if (Modifier)
					Modifiers.Add(MakeShared<FJsonValueString>(Modifier->GetClass()->GetPathName()));
			Item->SetArrayField(TEXT("triggers"), Triggers);
			Item->SetArrayField(TEXT("modifiers"), Modifiers);
			return Item;
		}

		int32 MappingIndex(const TSharedPtr<FJsonObject>& Args, const UInputMappingContext* Context)
		{
			const int32 Index = static_cast<int32>(NumberArg(Args, TEXT("mappingIndex"), 0));
			return Context && Context->GetMappings().IsValidIndex(Index) ? Index : INDEX_NONE;
		}
	}

	FString FUnrealAgentMCPUnrealGameplayAdapter::Input(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_input_action") || Action == TEXT("create_input_mapping"))
		{
			FString Path;
			UObject* Asset = CreateAsset(Action == TEXT("create_input_action") ? UInputAction::StaticClass() : UInputMappingContext::StaticClass(), Args,
				Action == TEXT("create_input_action") ? TEXT("IA_MCP") : TEXT("IMC_MCP"), Path);
			if (!Asset)
				return Error(TEXT("创建 Enhanced Input 资产失败。"));
			if (UInputAction* InputAction = Cast<UInputAction>(Asset))
			{
				const FString Type = StringArg(Args, { TEXT("valueType") }, TEXT("Boolean"));
				InputAction->ValueType = Type.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) ? EInputActionValueType::Axis3D
					: Type.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase)                    ? EInputActionValueType::Axis2D
					: Type.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase)                    ? EInputActionValueType::Axis1D
																							  : EInputActionValueType::Boolean;
				Save(InputAction);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Path);
			return Success(Result);
		}

		if (Action == TEXT("list_input_assets"))
		{
			FARFilter Filter;
			Filter.bRecursiveClasses = true;
			Filter.bRecursivePaths = BoolArg(Args, TEXT("recursive"), true);
			Filter.PackagePaths.Add(*StringArg(Args, { TEXT("directory") }, TEXT("/Game")));
			Filter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
			TArray<FAssetData> Assets;
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FAssetData& Asset : Assets)
				Values.Add(MakeShared<FJsonValueString>(Asset.GetObjectPathString()));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("assets"), Values);
			return Success(Result);
		}
		if (Action == TEXT("get_applied_imcs"))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("contexts"), {});
			Result->SetStringField(TEXT("scope"), TEXT("editor"));
			return Success(Result);
		}

		UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, *StringArg(Args, { TEXT("imcPath"), TEXT("assetPath") }));
		if (!Context)
			return Error(TEXT("找不到 InputMappingContext。"));
		if (Action == TEXT("read_imc") || Action == TEXT("list_input_mappings"))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Context->GetMappings().Num(); ++Index)
				Values.Add(MakeShared<FJsonValueObject>(MappingJson(Context->GetMappings()[Index], Index)));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("mappings"), Values);
			return Success(Result);
		}
		if (Action == TEXT("add_imc_mapping"))
		{
			UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *StringArg(Args, { TEXT("inputActionPath") }));
			if (!InputAction)
				return Error(TEXT("找不到 InputAction。"));
			Context->Modify();
			Context->MapKey(InputAction, FKey(*StringArg(Args, { TEXT("key") }, TEXT("SpaceBar"))));
		}
		else
		{
			const int32 Index = MappingIndex(Args, Context);
			if (Index == INDEX_NONE)
				return Error(TEXT("mappingIndex 越界。"));
			Context->Modify();
			FEnhancedActionKeyMapping& Mapping = Context->GetMapping(Index);
			if (Action == TEXT("remove_imc_mapping"))
				Context->UnmapKey(Mapping.Action, Mapping.Key);
			else if (Action == TEXT("set_imc_mapping_key"))
				Mapping.Key = FKey(*StringArg(Args, { TEXT("newKey") }, TEXT("SpaceBar")));
			else if (Action == TEXT("set_imc_mapping_action"))
			{
				UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *StringArg(Args, { TEXT("newInputActionPath") }));
				if (!InputAction)
					return Error(TEXT("找不到新的 InputAction。"));
				Mapping.Action = InputAction;
			}
			else if (Action == TEXT("set_mapping_modifiers"))
			{
				Mapping.Modifiers.Reset();
				Mapping.Triggers.Reset();
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (Args->TryGetArrayField(TEXT("modifiers"), Values))
					for (const TSharedPtr<FJsonValue>& Value : *Values)
					{
						const TSharedPtr<FJsonObject> Object = Value ? Value->AsObject() : nullptr;
						UClass* Class = FindInputClass(StringArg(Object, { TEXT("type"), TEXT("class") }), UInputModifier::StaticClass());
						if (Class)
							Mapping.Modifiers.Add(NewObject<UInputModifier>(Context, Class, NAME_None, RF_Transactional));
					}
				if (Args->TryGetArrayField(TEXT("triggers"), Values))
					for (const TSharedPtr<FJsonValue>& Value : *Values)
					{
						const TSharedPtr<FJsonObject> Object = Value ? Value->AsObject() : nullptr;
						UClass* Class = FindInputClass(StringArg(Object, { TEXT("type"), TEXT("class") }), UInputTrigger::StaticClass());
						if (Class)
							Mapping.Triggers.Add(NewObject<UInputTrigger>(Context, Class, NAME_None, RF_Transactional));
					}
			}
		}
		UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(Context, true);
		Save(Context);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), Context->GetPathName());
		return Success(Result);
	}
}
