// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.Definitions.cpp
 * @brief 输入映射、用户枚举与用户结构体的独立原生实现。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraphSchema_K2.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/InputSettings.h"
#include "InputCoreTypes.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/PackageName.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString ResolveDefinitionAssetPath(const TSharedPtr<FJsonObject>& Args, FString& OutPackage, FString& OutName)
		{
			const FString ExplicitPath = FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(Args, { TEXT("assetPath"), TEXT("path") });
			if (!ExplicitPath.IsEmpty())
			{
				OutPackage = FUnrealAgentMCPUnrealAssetAdapter::ToPackageName(ExplicitPath);
				OutName = FPackageName::GetLongPackageAssetName(OutPackage);
			}
			else
			{
				OutName = FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(Args, { TEXT("name"), TEXT("assetName") });
				FString PackagePath = FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(Args, { TEXT("packagePath"), TEXT("directory") });
				if (PackagePath.IsEmpty())
				{
					PackagePath = TEXT("/Game");
				}
				while (PackagePath.EndsWith(TEXT("/")))
				{
					PackagePath.LeftChopInline(1);
				}
				OutPackage = PackagePath + TEXT("/") + OutName;
			}
			return OutPackage + TEXT(".") + OutName;
		}

		bool ParseEnumNames(const TSharedPtr<FJsonObject>& Args, TArray<FString>& OutNames, FString& OutError)
		{
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
			{
				OutError = TEXT("缺少 entries 数组。");
				return false;
			}
			TSet<FString> Unique;
			for (const TSharedPtr<FJsonValue>& Value : *Entries)
			{
				FString Name;
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					Name = Value->AsString();
				}
				else if (Value.IsValid() && Value->Type == EJson::Object)
				{
					Value->AsObject()->TryGetStringField(TEXT("name"), Name);
				}
				if (Name.IsEmpty() || Unique.Contains(Name) || !FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS))
				{
					OutError = FString::Printf(TEXT("枚举条目无效或重复：%s"), *Name);
					return false;
				}
				Unique.Add(Name);
				OutNames.Add(Name);
			}
			return true;
		}

		bool SetEnumNames(UUserDefinedEnum* Enum, const TArray<FString>& Names, FString& OutError)
		{
			TArray<TPair<FName, int64>> Values;
			for (int32 Index = 0; Index < Names.Num(); ++Index)
			{
				Values.Emplace(FName(*Enum->GenerateFullEnumName(*Names[Index])), Index);
			}
			if (!Enum->SetEnums(Values, UEnum::ECppForm::Namespaced, UEnum::EUnderlyingType::uint8, EEnumFlags::None, UEnum::EAddMaxKeyIfMissing::Yes))
			{
				OutError = TEXT("枚举条目写入失败。");
				return false;
			}
			for (int32 Index = 0; Index < Names.Num(); ++Index)
			{
				FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, FText::FromString(Names[Index]));
			}
			Enum->PostEditChange();
			Enum->MarkPackageDirty();
			return true;
		}

		FEdGraphPinType MakeStructPinType(const FString& TypeName)
		{
			FEdGraphPinType Type;
			if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			}
			else if (TypeName.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Int64;
			}
			else if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Real;
				Type.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			}
			else if (TypeName.Equals(TEXT("double"), ESearchCase::IgnoreCase))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Real;
				Type.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			}
			else if (TypeName.Equals(TEXT("name"), ESearchCase::IgnoreCase))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Name;
			}
			else if (TypeName.Equals(TEXT("text"), ESearchCase::IgnoreCase))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Text;
			}
			else if (TypeName.Equals(TEXT("string"), ESearchCase::IgnoreCase))
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_String;
			}
			else
			{
				Type.PinCategory = UEdGraphSchema_K2::PC_Int;
			}
			return Type;
		}

		bool ApplyStructFields(UUserDefinedStruct* Struct, const TSharedPtr<FJsonObject>& Args, const bool bReplace, FString& OutError)
		{
			const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
			if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("fields"), Fields) || !Fields)
			{
				OutError = TEXT("缺少 fields 数组。");
				return false;
			}
			if (bReplace)
			{
				TArray<FGuid> Guids;
				for (const FStructVariableDescription& Field : FStructureEditorUtils::GetVarDesc(Struct))
				{
					Guids.Add(Field.VarGuid);
				}
				for (const FGuid& Guid : Guids)
				{
					FStructureEditorUtils::RemoveVariable(Struct, Guid);
				}
			}
			for (const TSharedPtr<FJsonValue>& Value : *Fields)
			{
				const TSharedPtr<FJsonObject> Field = Value.IsValid() ? Value->AsObject() : nullptr;
				const FString Name = FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(Field, { TEXT("name"), TEXT("fieldName") });
				const FString TypeName = FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(Field, { TEXT("type"), TEXT("fieldType") });
				if (!Field.IsValid() || Name.IsEmpty() || !FStructureEditorUtils::AddVariable(Struct, MakeStructPinType(TypeName)))
				{
					OutError = FString::Printf(TEXT("结构体字段创建失败：%s"), *Name);
					return false;
				}
				const TArray<FStructVariableDescription>& Variables = FStructureEditorUtils::GetVarDesc(Struct);
				if (Variables.IsEmpty() || !FStructureEditorUtils::RenameVariable(Struct, Variables.Last().VarGuid, Name))
				{
					OutError = FString::Printf(TEXT("结构体字段命名失败：%s"), *Name);
					return false;
				}
			}
			FStructureEditorUtils::CompileStructure(Struct);
			Struct->MarkPackageDirty();
			return true;
		}

		TSharedRef<FJsonObject> StructFieldsToJson(const UUserDefinedStruct* Struct)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Struct->GetPathName());
			TArray<TSharedPtr<FJsonValue>> Fields;
			for (const FStructVariableDescription& Field : FStructureEditorUtils::GetVarDesc(Struct))
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Field.FriendlyName.IsEmpty() ? Field.VarName.ToString() : Field.FriendlyName);
				Item->SetStringField(TEXT("internalName"), Field.VarName.ToString());
				Item->SetStringField(TEXT("type"), Field.ToPinType().PinCategory.ToString());
				Item->SetStringField(TEXT("guid"), Field.VarGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
				Fields.Add(MakeShared<FJsonValueObject>(Item));
			}
			Result->SetNumberField(TEXT("count"), Fields.Num());
			Result->SetArrayField(TEXT("fields"), Fields);
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Definitions(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action.Contains(TEXT("input_mapping")))
		{
			UInputSettings* Settings = UInputSettings::GetInputSettings();
			if (!Settings)
			{
				return ErrorJson(TEXT("InputSettings 当前不可用。"));
			}
			if (Action == TEXT("list_input_mappings"))
			{
				TArray<TSharedPtr<FJsonValue>> Actions;
				for (const FInputActionKeyMapping& Mapping : Settings->GetActionMappings())
				{
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("type"), TEXT("action"));
					Item->SetStringField(TEXT("name"), Mapping.ActionName.ToString());
					Item->SetStringField(TEXT("key"), Mapping.Key.ToString());
					Item->SetBoolField(TEXT("shift"), Mapping.bShift);
					Item->SetBoolField(TEXT("ctrl"), Mapping.bCtrl);
					Item->SetBoolField(TEXT("alt"), Mapping.bAlt);
					Item->SetBoolField(TEXT("cmd"), Mapping.bCmd);
					Actions.Add(MakeShared<FJsonValueObject>(Item));
				}
				for (const FInputAxisKeyMapping& Mapping : Settings->GetAxisMappings())
				{
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("type"), TEXT("axis"));
					Item->SetStringField(TEXT("name"), Mapping.AxisName.ToString());
					Item->SetStringField(TEXT("key"), Mapping.Key.ToString());
					Item->SetNumberField(TEXT("scale"), Mapping.Scale);
					Actions.Add(MakeShared<FJsonValueObject>(Item));
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("count"), Actions.Num());
				Result->SetArrayField(TEXT("mappings"), Actions);
				return SuccessJson(Result);
			}
			const FString Type = GetStringArgument(Args, { TEXT("type"), TEXT("mappingType") });
			const FName Name(*GetStringArgument(Args, { TEXT("name"), TEXT("actionName"), TEXT("axisName") }));
			const FKey Key(FName(*GetStringArgument(Args, { TEXT("key"), TEXT("keyName") })));
			if (Name.IsNone() || !Key.IsValid())
			{
				return ErrorJson(TEXT("映射 name 或 key 无效。"));
			}
			const bool bAdding = Action == TEXT("add_input_mapping");
			if (Type.Equals(TEXT("axis"), ESearchCase::IgnoreCase))
			{
				double Scale = 1.0;
				Args->TryGetNumberField(TEXT("scale"), Scale);
				const FInputAxisKeyMapping Mapping(Name, Key, static_cast<float>(Scale));
				if (bAdding)
				{
					Settings->AddAxisMapping(Mapping, false);
				}
				else
				{
					Settings->RemoveAxisMapping(Mapping, false);
				}
			}
			else
			{
				bool bShift = false;
				bool bCtrl = false;
				bool bAlt = false;
				bool bCmd = false;
				Args->TryGetBoolField(TEXT("shift"), bShift);
				Args->TryGetBoolField(TEXT("ctrl"), bCtrl);
				Args->TryGetBoolField(TEXT("alt"), bAlt);
				Args->TryGetBoolField(TEXT("cmd"), bCmd);
				const FInputActionKeyMapping Mapping(Name, Key, bShift, bCtrl, bAlt, bCmd);
				if (bAdding)
				{
					Settings->AddActionMapping(Mapping, false);
				}
				else
				{
					Settings->RemoveActionMapping(Mapping, false);
				}
			}
			Settings->SaveKeyMappings();
			Settings->ForceRebuildKeymaps();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Name.ToString());
			Result->SetStringField(TEXT("key"), Key.ToString());
			Result->SetBoolField(TEXT("added"), bAdding);
			return SuccessJson(Result);
		}

		if (Action.Contains(TEXT("enum")))
		{
			FString PackageName;
			FString AssetName;
			const FString AssetPath = ResolveDefinitionAssetPath(Args, PackageName, AssetName);
			UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *AssetPath);
			if (Action == TEXT("create_user_defined_enum"))
			{
				if (!FPackageName::IsValidLongPackageName(PackageName) || AssetName.IsEmpty())
				{
					return ErrorJson(TEXT("枚举资产路径无效。"));
				}
				if (!Enum)
				{
					UPackage* Package = CreatePackage(*PackageName);
					Enum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional));
					if (Enum)
					{
						FAssetRegistryModule::AssetCreated(Enum);
					}
				}
			}
			if (!Enum)
			{
				return ErrorJson(FString::Printf(TEXT("未找到或无法创建 UserDefinedEnum：%s"), *AssetPath));
			}
			if (Action == TEXT("list_enum_values"))
			{
				TArray<TSharedPtr<FJsonValue>> Values;
				for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
				{
					if (Enum->HasMetaData(TEXT("Hidden"), Index))
					{
						continue;
					}
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
					Item->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(Index).ToString());
					Item->SetNumberField(TEXT("value"), Enum->GetValueByIndex(Index));
					Values.Add(MakeShared<FJsonValueObject>(Item));
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("assetPath"), AssetPath);
				Result->SetNumberField(TEXT("count"), Values.Num());
				Result->SetArrayField(TEXT("values"), Values);
				return SuccessJson(Result);
			}
			TArray<FString> Names;
			FString Error;
			if (!ParseEnumNames(Args, Names, Error) || !SetEnumNames(Enum, Names, Error))
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Enum->GetPathName());
			Result->SetNumberField(TEXT("entryCount"), Names.Num());
			return SuccessJson(Result);
		}

		FString PackageName;
		FString AssetName;
		const FString AssetPath = ResolveDefinitionAssetPath(Args, PackageName, AssetName);
		UUserDefinedStruct* Struct = LoadObject<UUserDefinedStruct>(nullptr, *AssetPath);
		if (Action == TEXT("create_user_defined_struct"))
		{
			if (!Struct && FPackageName::IsValidLongPackageName(PackageName) && !AssetName.IsEmpty())
			{
				UPackage* Package = CreatePackage(*PackageName);
				Struct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
				if (Struct)
				{
					FAssetRegistryModule::AssetCreated(Struct);
				}
			}
			if (!Struct)
			{
				return ErrorJson(TEXT("UserDefinedStruct 创建失败。"));
			}
			FString Error;
			if (Args->HasField(TEXT("fields")) && !ApplyStructFields(Struct, Args, false, Error))
			{
				return ErrorJson(Error);
			}
			return SuccessJson(StructFieldsToJson(Struct));
		}
		if (!Struct)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 UserDefinedStruct：%s"), *AssetPath));
		}
		if (Action == TEXT("list_struct_fields"))
		{
			return SuccessJson(StructFieldsToJson(Struct));
		}
		if (Action == TEXT("edit_user_defined_struct"))
		{
			FString Error;
			if (!ApplyStructFields(Struct, Args, true, Error))
			{
				return ErrorJson(Error);
			}
			return SuccessJson(StructFieldsToJson(Struct));
		}
		if (Action == TEXT("rename_struct_field"))
		{
			const FString OldName = GetStringArgument(Args, { TEXT("oldName"), TEXT("fieldName"), TEXT("name") });
			const FString NewName = GetStringArgument(Args, { TEXT("newName"), TEXT("newFieldName") });
			if (!FStructureEditorUtils::RenameVariable(Struct, OldName, NewName))
			{
				return ErrorJson(TEXT("结构体字段重命名失败。"));
			}
			FStructureEditorUtils::CompileStructure(Struct);
			Struct->MarkPackageDirty();
			return SuccessJson(StructFieldsToJson(Struct));
		}
		return ErrorJson(FString::Printf(TEXT("定义资产动作未进入有效分支：%s"), *Action));
	}
}
