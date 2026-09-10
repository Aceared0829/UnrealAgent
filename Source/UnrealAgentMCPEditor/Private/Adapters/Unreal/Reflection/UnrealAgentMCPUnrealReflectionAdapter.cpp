// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealReflectionAdapter.cpp
 * @brief Reflection 端口的类型、模块、标签与 SaveGame 查询实现。
 */

#include "Adapters/Unreal/Reflection/UnrealAgentMCPUnrealReflectionAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagsManager.h"
#include "Kismet/GameplayStatics.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/FieldIterator.h"
#include "UObject/PropertyIterator.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#include <type_traits>

namespace UnrealAgentMCP
{
	namespace
	{
		template <typename Type> Type* FindReflectedType(FString Name, const bool bAllowLoad)
		{
			if (Name.IsEmpty())
				return nullptr;
			if (Type* Found = FindObject<Type>(nullptr, *Name))
				return Found;
			if (Type* Found = FindFirstObject<Type>(*Name, EFindFirstObjectOptions::None, ELogVerbosity::NoLogging, TEXT("Unreal Agent Reflection 查询")))
			{
				return Found;
			}
			if (!bAllowLoad)
				return nullptr;
			if (Type* Loaded = LoadObject<Type>(nullptr, *Name))
				return Loaded;
			if constexpr (std::is_same_v<Type, UClass>)
			{
				if (!Name.EndsWith(TEXT("_C")))
				{
					Name += TEXT("_C");
					return LoadObject<Type>(nullptr, *Name);
				}
			}
			return nullptr;
		}

		TSharedRef<FJsonObject> DescribeProperty(const FProperty* Property)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Property->GetName());
			Row->SetStringField(TEXT("type"), Property->GetCPPType());
			Row->SetStringField(TEXT("owner"), Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetPathName() : FString());
			Row->SetNumberField(TEXT("arrayDim"), Property->ArrayDim);
			Row->SetNumberField(TEXT("offset"), Property->GetOffset_ForInternal());
			Row->SetStringField(TEXT("flags"), FString::Printf(TEXT("0x%016llx"), static_cast<uint64>(Property->GetPropertyFlags())));
			Row->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
			Row->SetBoolField(TEXT("blueprintVisible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
			Row->SetBoolField(TEXT("saveGame"), Property->HasAnyPropertyFlags(CPF_SaveGame));
			return Row;
		}

		TSharedRef<FJsonObject> DescribeFunction(const UFunction* Function)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Function->GetName());
			Row->SetStringField(TEXT("owner"), Function->GetOuterUClass() ? Function->GetOuterUClass()->GetPathName() : FString());
			Row->SetStringField(TEXT("flags"), FString::Printf(TEXT("0x%08x"), static_cast<uint32>(Function->FunctionFlags)));
			TArray<TSharedPtr<FJsonValue>> Parameters;
			for (TFieldIterator<FProperty> It(Function); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_Parm))
				{
					Parameters.Add(MakeShared<FJsonValueObject>(DescribeProperty(*It)));
				}
			}
			Row->SetArrayField(TEXT("parameters"), Parameters);
			return Row;
		}

		FString DescribeEnum(UEnum* Enum)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			const int32 ValueCount = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
			for (int32 Index = 0; Index < ValueCount; ++Index)
			{
				TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
				Value->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
				Value->SetNumberField(TEXT("value"), Enum->GetValueByIndex(Index));
				Value->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(Index).ToString());
				Value->SetStringField(TEXT("tooltip"), Enum->GetToolTipTextByIndex(Index).ToString());
				Values.Add(MakeShared<FJsonValueObject>(Value));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Enum->GetName());
			Result->SetStringField(TEXT("enumPath"), Enum->GetPathName());
			Result->SetBoolField(TEXT("userDefined"), Enum->IsA<UUserDefinedEnum>());
			Result->SetNumberField(TEXT("count"), Values.Num());
			Result->SetArrayField(TEXT("values"), Values);
			return SuccessJson(Result);
		}

		int32 GetReflectionLimit(const TSharedPtr<FJsonObject>& Args, const int32 DefaultValue = 200)
		{
			double Limit = DefaultValue;
			Args->TryGetNumberField(TEXT("limit"), Limit);
			return FMath::Clamp(static_cast<int32>(Limit), 1, 5000);
		}
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::ReflectClass(const TSharedPtr<FJsonObject>& Args)
	{
		FString ClassName;
		if (!Args->TryGetStringField(TEXT("className"), ClassName))
			return ErrorJson(TEXT("缺少必填 className。"));
		UClass* Class = FindReflectedType<UClass>(ClassName, true);
		if (!Class)
			return ErrorJson(FString::Printf(TEXT("未找到 UClass：%s"), *ClassName));
		bool bIncludeInherited = false;
		Args->TryGetBoolField(TEXT("includeInherited"), bIncludeInherited);
		const EFieldIterationFlags Flags = bIncludeInherited ? EFieldIterationFlags::IncludeSuper : EFieldIterationFlags::None;
		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Class, Flags); It; ++It)
			Properties.Add(MakeShared<FJsonValueObject>(DescribeProperty(*It)));
		TArray<TSharedPtr<FJsonValue>> Functions;
		for (TFieldIterator<UFunction> It(Class, Flags); It; ++It)
			Functions.Add(MakeShared<FJsonValueObject>(DescribeFunction(*It)));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Class->GetName());
		Result->SetStringField(TEXT("classPath"), Class->GetPathName());
		Result->SetStringField(TEXT("superClass"), Class->GetSuperClass() ? Class->GetSuperClass()->GetPathName() : FString());
		Result->SetStringField(TEXT("module"), FPackageName::GetShortName(Class->GetOutermost()->GetName()));
		Result->SetArrayField(TEXT("properties"), Properties);
		Result->SetArrayField(TEXT("functions"), Functions);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::ReflectStruct(const TSharedPtr<FJsonObject>& Args)
	{
		FString StructName;
		if (!Args->TryGetStringField(TEXT("structName"), StructName))
			return ErrorJson(TEXT("缺少必填 structName。"));
		UScriptStruct* Struct = FindReflectedType<UScriptStruct>(StructName, true);
		if (!Struct)
			return ErrorJson(FString::Printf(TEXT("未找到 UScriptStruct：%s"), *StructName));
		TArray<TSharedPtr<FJsonValue>> Fields;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			Fields.Add(MakeShared<FJsonValueObject>(DescribeProperty(*It)));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Struct->GetName());
		Result->SetStringField(TEXT("structPath"), Struct->GetPathName());
		Result->SetNumberField(TEXT("size"), Struct->GetStructureSize());
		Result->SetArrayField(TEXT("fields"), Fields);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::ReflectEnum(const TSharedPtr<FJsonObject>& Args)
	{
		FString EnumName;
		if (!Args->TryGetStringField(TEXT("enumName"), EnumName))
			return ErrorJson(TEXT("缺少必填 enumName。"));
		UEnum* Enum = FindReflectedType<UEnum>(EnumName, true);
		if (!Enum && !EnumName.StartsWith(TEXT("E")))
			Enum = FindReflectedType<UEnum>(TEXT("E") + EnumName, true);
		if (!Enum)
			return ErrorJson(FString::Printf(TEXT("未找到 UEnum：%s"), *EnumName));
		return DescribeEnum(Enum);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::ListClasses(const TSharedPtr<FJsonObject>& Args)
	{
		FString ParentFilter;
		Args->TryGetStringField(TEXT("parentFilter"), ParentFilter);
		UClass* Parent = ParentFilter.IsEmpty() ? nullptr : FindReflectedType<UClass>(ParentFilter, true);
		const int32 Limit = GetReflectionLimit(Args);
		TArray<UClass*> Classes;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class->HasAnyClassFlags(CLASS_Deprecated) || (Parent && !Class->IsChildOf(Parent)))
				continue;
			Classes.Add(Class);
		}
		Classes.Sort(
			[](const UClass& Left, const UClass& Right)
			{
				return Left.GetPathName() < Right.GetPathName();
			});
		TArray<TSharedPtr<FJsonValue>> Values;
		for (int32 Index = 0; Index < Classes.Num() && Index < Limit; ++Index)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("name"), Classes[Index]->GetName());
			Row->SetStringField(TEXT("classPath"), Classes[Index]->GetPathName());
			Values.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("total"), Classes.Num());
		Result->SetNumberField(TEXT("count"), Values.Num());
		Result->SetArrayField(TEXT("classes"), Values);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::ListTags(const TSharedPtr<FJsonObject>& Args)
	{
		FString Filter;
		Args->TryGetStringField(TEXT("filter"), Filter);
		FGameplayTagContainer Container;
		UGameplayTagsManager::Get().RequestAllGameplayTags(Container, true);
		TArray<FString> Names;
		for (const FGameplayTag& Tag : Container)
		{
			const FString Name = Tag.ToString();
			if (Filter.IsEmpty() || Name.Contains(Filter, ESearchCase::IgnoreCase))
			{
				Names.Add(Name);
			}
		}
		Names.Sort();
		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const FString& Name : Names)
			Tags.Add(MakeShared<FJsonValueString>(Name));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Tags.Num());
		Result->SetArrayField(TEXT("tags"), Tags);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::IsClassLoaded(const TSharedPtr<FJsonObject>& Args)
	{
		FString ClassName;
		if (!Args->TryGetStringField(TEXT("className"), ClassName))
			return ErrorJson(TEXT("缺少必填 className。"));
		UClass* Loaded = FindReflectedType<UClass>(ClassName, false);
		UClass* Existing = Loaded ? Loaded : FindReflectedType<UClass>(ClassName, true);
		FString ModuleName;
		if (Existing)
		{
			ModuleName = FPackageName::GetShortName(Existing->GetOutermost()->GetName());
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("className"), ClassName);
		Result->SetBoolField(TEXT("loaded"), Loaded != nullptr);
		Result->SetBoolField(TEXT("exists"), Existing != nullptr);
		Result->SetStringField(TEXT("moduleName"), ModuleName);
		Result->SetBoolField(TEXT("moduleLoaded"), !ModuleName.IsEmpty() && FModuleManager::Get().IsModuleLoaded(FName(*ModuleName)));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::IsModuleLoaded(const TSharedPtr<FJsonObject>& Args)
	{
		FString ModuleName;
		if (!Args->TryGetStringField(TEXT("moduleName"), ModuleName))
			return ErrorJson(TEXT("缺少必填 moduleName。"));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("moduleName"), ModuleName);
		Result->SetBoolField(TEXT("loaded"), FModuleManager::Get().IsModuleLoaded(FName(*ModuleName)));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::ListLoadedModules(const TSharedPtr<FJsonObject>& Args)
	{
		FString Filter;
		Args->TryGetStringField(TEXT("filter"), Filter);
		bool bLoadedOnly = false;
		Args->TryGetBoolField(TEXT("loadedOnly"), bLoadedOnly);
		TArray<FModuleStatus> Statuses;
		FModuleManager::Get().QueryModules(Statuses);
		Statuses.Sort(
			[](const FModuleStatus& Left, const FModuleStatus& Right)
			{
				return Left.Name < Right.Name;
			});
		int32 TotalLoaded = 0;
		TArray<TSharedPtr<FJsonValue>> Modules;
		for (const FModuleStatus& Status : Statuses)
		{
			if (Status.bIsLoaded)
				++TotalLoaded;
			if ((!Filter.IsEmpty() && !Status.Name.Contains(Filter, ESearchCase::IgnoreCase)) || (bLoadedOnly && !Status.bIsLoaded))
			{
				continue;
			}
			TSharedRef<FJsonObject> Module = MakeShared<FJsonObject>();
			Module->SetStringField(TEXT("name"), Status.Name);
			Module->SetBoolField(TEXT("loaded"), Status.bIsLoaded);
			Module->SetBoolField(TEXT("gameModule"), Status.bIsGameModule);
			Modules.Add(MakeShared<FJsonValueObject>(Module));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("totalModules"), Statuses.Num());
		Result->SetNumberField(TEXT("totalLoaded"), TotalLoaded);
		Result->SetNumberField(TEXT("count"), Modules.Num());
		Result->SetArrayField(TEXT("modules"), Modules);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealReflectionAdapter::InspectSaveGame(const TSharedPtr<FJsonObject>& Args)
	{
		FString SlotName;
		if (!Args->TryGetStringField(TEXT("slotName"), SlotName) || SlotName.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 slotName。"));
		}
		double UserIndexNumber = 0;
		Args->TryGetNumberField(TEXT("userIndex"), UserIndexNumber);
		const int32 UserIndex = static_cast<int32>(UserIndexNumber);
		if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
		{
			return ErrorJson(TEXT("指定 SaveGame 存档不存在。"));
		}
		USaveGame* SaveGame = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex);
		if (!SaveGame)
			return ErrorJson(TEXT("SaveGame 存档加载失败。"));

		TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Skipped;
		for (TFieldIterator<FProperty> It(SaveGame->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_SaveGame))
				continue;
			FString Text;
			if (Property->ExportText_InContainer(0, Text, SaveGame, nullptr, SaveGame, PPF_None))
			{
				Values->SetStringField(Property->GetName(), Text);
			}
			else
			{
				Skipped.Add(MakeShared<FJsonValueString>(Property->GetName()));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("slotName"), SlotName);
		Result->SetNumberField(TEXT("userIndex"), UserIndex);
		Result->SetStringField(TEXT("classPath"), SaveGame->GetClass()->GetPathName());
		Result->SetObjectField(TEXT("values"), Values);
		Result->SetArrayField(TEXT("skippedProperties"), Skipped);
		return SuccessJson(Result);
	}
}
