// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.Tables.cpp
 * @brief DataTable、CurveTable 与 StringTable 的原生读写实现。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Curves/RealCurve.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/CurveTable.h"
#include "Engine/DataTable.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool SplitTableAssetPath(const FString& Input, FString& OutPackage, FString& OutName)
		{
			OutPackage = FUnrealAgentMCPUnrealAssetAdapter::ToPackageName(Input);
			OutName = FPackageName::GetLongPackageAssetName(OutPackage);
			return FPackageName::IsValidLongPackageName(OutPackage) && !OutName.IsEmpty();
		}

		template <typename TObjectType> TObjectType* CreateTableAsset(const FString& Input, FString& OutError)
		{
			FString PackageName;
			FString AssetName;
			if (!SplitTableAssetPath(Input, PackageName, AssetName))
			{
				OutError = FString::Printf(TEXT("无效的资产路径：%s"), *Input);
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				OutError = TEXT("无法创建资产包。");
				return nullptr;
			}
			TObjectType* Asset = NewObject<TObjectType>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			if (!Asset)
			{
				OutError = TEXT("无法创建表格资产对象。");
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Asset);
			Asset->MarkPackageDirty();
			return Asset;
		}

		TSharedPtr<FJsonObject> GetObjectArgument(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names)
		{
			if (!Args.IsValid())
			{
				return nullptr;
			}
			for (const TCHAR* Name : Names)
			{
				const TSharedPtr<FJsonObject>* Value = nullptr;
				if (Args->TryGetObjectField(Name, Value) && Value)
				{
					return *Value;
				}
			}
			return nullptr;
		}

		TSharedRef<FJsonObject> DataTableRowToJson(const UDataTable* Table, const FName RowName, const uint8* RowData)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("rowName"), RowName.ToString());
			TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
			if (Table && Table->GetRowStruct() && RowData)
			{
				FJsonObjectConverter::UStructToJsonObject(Table->GetRowStruct(), RowData, Data);
			}
			Item->SetObjectField(TEXT("data"), Data);
			return Item;
		}

		bool WriteDataTableRow(UDataTable* Table, const FName RowName, const TSharedRef<FJsonObject>& Data, FString& OutError)
		{
			if (!Table || !Table->GetRowStruct())
			{
				OutError = TEXT("DataTable 或 RowStruct 无效。");
				return false;
			}
			UScriptStruct* RowStruct = Table->RowStruct;
			uint8* Memory = static_cast<uint8*>(FMemory::Malloc(RowStruct->GetStructureSize()));
			RowStruct->InitializeStruct(Memory);
			FText Failure;
			const bool bConverted = FJsonObjectConverter::JsonObjectToUStruct(Data, RowStruct, Memory, 0, 0, false, &Failure);
			if (bConverted)
			{
				Table->AddRow(RowName, Memory, RowStruct);
				Table->MarkPackageDirty();
			}
			else
			{
				OutError = Failure.ToString();
			}
			RowStruct->DestroyStruct(Memory);
			FMemory::Free(Memory);
			return bConverted;
		}

		TSharedRef<FJsonObject> CurveToJson(const FName RowName, const FRealCurve* Curve)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("rowName"), RowName.ToString());
			TArray<TSharedPtr<FJsonValue>> Keys;
			if (Curve)
			{
				for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
				{
					const TPair<float, float> Pair = Curve->GetKeyTimeValuePair(*It);
					TSharedRef<FJsonObject> Key = MakeShared<FJsonObject>();
					Key->SetNumberField(TEXT("time"), Pair.Key);
					Key->SetNumberField(TEXT("value"), Pair.Value);
					Keys.Add(MakeShared<FJsonValueObject>(Key));
				}
			}
			Item->SetNumberField(TEXT("keyCount"), Keys.Num());
			Item->SetArrayField(TEXT("keys"), Keys);
			return Item;
		}

		bool ApplyCurveKeys(FRealCurve& Curve, const TArray<TSharedPtr<FJsonValue>>& Keys, const bool bReplace)
		{
			if (bReplace)
			{
				Curve.Reset();
			}
			bool bAny = false;
			for (const TSharedPtr<FJsonValue>& Value : Keys)
			{
				const TSharedPtr<FJsonObject> Key = Value.IsValid() ? Value->AsObject() : nullptr;
				double Time = 0.0;
				double Number = 0.0;
				if (Key.IsValid() && Key->TryGetNumberField(TEXT("time"), Time) && Key->TryGetNumberField(TEXT("value"), Number))
				{
					Curve.UpdateOrAddKey(static_cast<float>(Time), static_cast<float>(Number));
					bAny = true;
				}
			}
			return bAny || (bReplace && Keys.IsEmpty());
		}

		TSharedRef<FJsonObject> ReadStringTableJson(UStringTable* Table)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Table->GetPathName());
			TArray<TSharedPtr<FJsonValue>> Entries;
			Table->GetStringTable()->EnumerateKeysAndSourceStrings(
				[&Entries](const FTextKey& Key, const FString& Source)
				{
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("key"), Key.ToString());
					Entry->SetStringField(TEXT("value"), Source);
					Entries.Add(MakeShared<FJsonValueObject>(Entry));
					return true;
				});
			Entries.Sort(
				[](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
				{
					return Left->AsObject()->GetStringField(TEXT("key")) < Right->AsObject()->GetStringField(TEXT("key"));
				});
			Result->SetNumberField(TEXT("count"), Entries.Num());
			Result->SetArrayField(TEXT("entries"), Entries);
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Tables(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		UEditorAssetSubsystem* Subsystem = GetAssetSubsystem();
		const FString AssetPath = GetStringArgument(Args, { TEXT("assetPath"), TEXT("path") });
		if (!Subsystem)
		{
			return ErrorJson(TEXT("EditorAssetSubsystem 当前不可用。"));
		}
		FString Error;

		if (Action == TEXT("create_datatable"))
		{
			UDataTable* Table = CreateTableAsset<UDataTable>(AssetPath, Error);
			if (!Table)
			{
				return ErrorJson(Error);
			}
			const FString StructPath = GetStringArgument(Args, { TEXT("rowStruct"), TEXT("rowStructPath"), TEXT("structType") });
			UScriptStruct* RowStruct = StructPath.IsEmpty() ? FTableRowBase::StaticStruct() : LoadObject<UScriptStruct>(nullptr, *StructPath);
			if (!RowStruct)
			{
				Subsystem->DeleteAsset(Table->GetPathName());
				return ErrorJson(FString::Printf(TEXT("无法加载 RowStruct：%s"), *StructPath));
			}
			Table->RowStruct = RowStruct;
			Table->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Table->GetPathName());
			Result->SetStringField(TEXT("rowStruct"), RowStruct->GetPathName());
			return SuccessJson(Result);
		}

		if (Action.Contains(TEXT("datatable")))
		{
			UDataTable* Table = Cast<UDataTable>(Subsystem->LoadAsset(AssetPath));
			if (!Table)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 DataTable：%s"), *AssetPath));
			}
			if (Action == TEXT("read_datatable"))
			{
				TArray<FName> Names;
				Table->GetRowMap().GetKeys(Names);
				Names.Sort(FNameLexicalLess());
				TArray<TSharedPtr<FJsonValue>> Rows;
				for (const FName Name : Names)
				{
					Rows.Add(MakeShared<FJsonValueObject>(DataTableRowToJson(Table, Name, Table->GetRowMap()[Name])));
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("assetPath"), Table->GetPathName());
				Result->SetStringField(TEXT("rowStruct"), Table->GetRowStructPathName().ToString());
				Result->SetNumberField(TEXT("rowCount"), Rows.Num());
				Result->SetArrayField(TEXT("rows"), Rows);
				return SuccessJson(Result);
			}
			const FString RowNameText = GetStringArgument(Args, { TEXT("rowName"), TEXT("name"), TEXT("key") });
			const FName RowName(*RowNameText);
			if (Action == TEXT("get_datatable_row"))
			{
				const uint8* const* Row = Table->GetRowMap().Find(RowName);
				return Row ? SuccessJson(DataTableRowToJson(Table, RowName, *Row)) : ErrorJson(TEXT("未找到指定 DataTable 行。"));
			}
			if (Action == TEXT("remove_datatable_row"))
			{
				const bool bExisted = Table->GetRowMap().Contains(RowName);
				Table->RemoveRow(RowName);
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("rowName"), RowNameText);
				Result->SetBoolField(TEXT("removed"), bExisted);
				return SuccessJson(Result);
			}
			if (Action == TEXT("rename_datatable_row"))
			{
				const FString NewNameText = GetStringArgument(Args, { TEXT("newRowName"), TEXT("newName") });
				const uint8* const* Row = Table->GetRowMap().Find(RowName);
				if (!Row || NewNameText.IsEmpty())
				{
					return ErrorJson(TEXT("原行不存在或缺少 newRowName。"));
				}
				Table->AddRow(FName(*NewNameText), *Row, Table->GetRowStruct());
				Table->RemoveRow(RowName);
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("rowName"), RowNameText);
				Result->SetStringField(TEXT("newRowName"), NewNameText);
				return SuccessJson(Result);
			}
			if (Action == TEXT("set_datatable_cell"))
			{
				const FString PropertyName = GetStringArgument(Args, { TEXT("columnName"), TEXT("propertyName"), TEXT("column") });
				uint8* const* Row = Table->GetRowMap().Find(RowName);
				FProperty* Property = Table->GetRowStruct() ? FindFProperty<FProperty>(Table->GetRowStruct(), *PropertyName) : nullptr;
				const TSharedPtr<FJsonValue> Value = Args.IsValid() ? Args->TryGetField(TEXT("value")) : nullptr;
				FText Failure;
				if (!Row || !Property || !Value.IsValid() ||
					!FJsonObjectConverter::JsonValueToUProperty(Value, Property, Property->ContainerPtrToValuePtr<void>(*Row), 0, 0, false, &Failure))
				{
					return ErrorJson(Failure.IsEmpty() ? TEXT("DataTable 单元格参数无效。") : Failure.ToString());
				}
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("rowName"), RowNameText);
				Result->SetStringField(TEXT("columnName"), PropertyName);
				return SuccessJson(Result);
			}
			if (Action == TEXT("fill_datatable_from_json") || Action == TEXT("reimport_datatable"))
			{
				const TSharedPtr<FJsonObject> Rows = GetObjectArgument(Args, { TEXT("rows"), TEXT("data") });
				if (!Rows.IsValid())
				{
					return ErrorJson(TEXT("缺少 rows JSON 对象。"));
				}
				bool bReplace = true;
				Args->TryGetBoolField(TEXT("replace"), bReplace);
				if (bReplace)
				{
					Table->EmptyTable();
				}
				int32 Written = 0;
				for (const auto& Pair : Rows->Values)
				{
					const TSharedPtr<FJsonObject> Data = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
					if (Data.IsValid() && WriteDataTableRow(Table, FName(*Pair.Key), Data.ToSharedRef(), Error))
					{
						++Written;
					}
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("writtenRows"), Written);
				Result->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
				return SuccessJson(Result);
			}
			if (Action == TEXT("set_datatable_row") || Action == TEXT("add_datatable_row") || Action == TEXT("update_datatable_row"))
			{
				const bool bExists = Table->GetRowMap().Contains(RowName);
				if (Action == TEXT("add_datatable_row") && bExists)
				{
					return ErrorJson(TEXT("DataTable 行已经存在。"));
				}
				if (Action == TEXT("update_datatable_row") && !bExists)
				{
					return ErrorJson(TEXT("DataTable 行不存在。"));
				}
				const TSharedPtr<FJsonObject> Data = GetObjectArgument(Args, { TEXT("row"), TEXT("data"), TEXT("value") });
				if (RowName.IsNone() || !Data.IsValid() || !WriteDataTableRow(Table, RowName, Data.ToSharedRef(), Error))
				{
					return ErrorJson(Error.IsEmpty() ? TEXT("缺少 rowName 或 row 数据。") : Error);
				}
				return SuccessJson(DataTableRowToJson(Table, RowName, Table->GetRowMap()[RowName]));
			}
		}

		if (Action == TEXT("create_curvetable"))
		{
			UCurveTable* Table = CreateTableAsset<UCurveTable>(AssetPath, Error);
			if (!Table)
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Table->GetPathName());
			return SuccessJson(Result);
		}

		if (Action.Contains(TEXT("curvetable")))
		{
			UCurveTable* Table = Cast<UCurveTable>(Subsystem->LoadAsset(AssetPath));
			if (!Table)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 CurveTable：%s"), *AssetPath));
			}
			const FString RowNameText = GetStringArgument(Args, { TEXT("rowName"), TEXT("name") });
			const FName RowName(*RowNameText);
			if (Action == TEXT("read_curvetable") || Action == TEXT("list_curvetable_rows"))
			{
				TArray<FName> Names;
				Table->GetRowMap().GetKeys(Names);
				Names.Sort(FNameLexicalLess());
				TArray<TSharedPtr<FJsonValue>> Rows;
				for (const FName Name : Names)
				{
					Rows.Add(MakeShared<FJsonValueObject>(CurveToJson(Name, Table->GetRowMap()[Name])));
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("rowCount"), Rows.Num());
				Result->SetArrayField(TEXT("rows"), Rows);
				return SuccessJson(Result);
			}
			if (Action == TEXT("remove_curvetable_row"))
			{
				const bool bExisted = Table->GetRowMap().Contains(RowName);
				Table->RemoveRow(RowName);
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("removed"), bExisted);
				Result->SetStringField(TEXT("rowName"), RowNameText);
				return SuccessJson(Result);
			}
			if (Action == TEXT("rename_curvetable_row"))
			{
				const FString NewName = GetStringArgument(Args, { TEXT("newRowName"), TEXT("newName") });
				if (!Table->GetRowMap().Contains(RowName) || NewName.IsEmpty())
				{
					return ErrorJson(TEXT("原曲线行不存在或缺少 newRowName。"));
				}
				Table->RenameRow(RowName, FName(*NewName));
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("rowName"), RowNameText);
				Result->SetStringField(TEXT("newRowName"), NewName);
				return SuccessJson(Result);
			}
			if (Action == TEXT("get_curvetable_keys"))
			{
				FRealCurve* const* Curve = Table->GetRowMap().Find(RowName);
				return Curve ? SuccessJson(CurveToJson(RowName, *Curve)) : ErrorJson(TEXT("未找到指定曲线行。"));
			}
			if (Action == TEXT("add_curvetable_row") || Action == TEXT("set_curvetable_keys") || Action == TEXT("add_curvetable_key"))
			{
				FRealCurve* Curve = Table->GetRowMap().FindRef(RowName);
				if (!Curve)
				{
					Curve = &Table->AddRichCurve(RowName);
				}
				TArray<TSharedPtr<FJsonValue>> Keys;
				if (Action == TEXT("add_curvetable_key"))
				{
					TSharedRef<FJsonObject> Key = MakeShared<FJsonObject>();
					double Time = 0.0;
					double Number = 0.0;
					Args->TryGetNumberField(TEXT("time"), Time);
					Args->TryGetNumberField(TEXT("value"), Number);
					Key->SetNumberField(TEXT("time"), Time);
					Key->SetNumberField(TEXT("value"), Number);
					Keys.Add(MakeShared<FJsonValueObject>(Key));
				}
				else
				{
					const TArray<TSharedPtr<FJsonValue>>* Input = nullptr;
					if (Args->TryGetArrayField(TEXT("keys"), Input) && Input)
					{
						Keys = *Input;
					}
				}
				const bool bReplace = Action == TEXT("set_curvetable_keys");
				if (!ApplyCurveKeys(*Curve, Keys, bReplace))
				{
					return ErrorJson(TEXT("曲线 keys 参数无效。"));
				}
				Table->MarkPackageDirty();
				return SuccessJson(CurveToJson(RowName, Curve));
			}
			if (Action == TEXT("import_curvetable"))
			{
				const TSharedPtr<FJsonObject> Rows = GetObjectArgument(Args, { TEXT("rows"), TEXT("data") });
				if (!Rows.IsValid())
				{
					return ErrorJson(TEXT("缺少 rows JSON 对象。"));
				}
				Transactions::FUnrealAgentMCPCompensatingEditorTransaction Compensation(FText::FromString(TEXT("Unreal Agent 导入 CurveTable")));
				if (!Compensation.IsReady(Error))
				{
					return ErrorJson(Error);
				}
				Table->Modify();
				int32 Written = 0;
				for (const auto& Pair : Rows->Values)
				{
					const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
					if (Pair.Value.IsValid() && Pair.Value->TryGetArray(Keys) && Keys)
					{
						FRealCurve& Curve = Table->AddRichCurve(FName(*Pair.Key));
						if (ApplyCurveKeys(Curve, *Keys, true))
						{
							++Written;
						}
					}
				}
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetNumberField(TEXT("writtenRows"), Written);
				if (!Compensation.Register(Error))
				{
					return ErrorJson(Error);
				}
				return SuccessJson(Result);
			}
		}

		if (Action == TEXT("create_stringtable"))
		{
			UStringTable* Table = CreateTableAsset<UStringTable>(AssetPath, Error);
			return Table ? SuccessJson(ReadStringTableJson(Table)) : ErrorJson(Error);
		}

		if (Action.Contains(TEXT("stringtable")))
		{
			UStringTable* Table = Cast<UStringTable>(Subsystem->LoadAsset(AssetPath));
			if (!Table)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 StringTable：%s"), *AssetPath));
			}
			if (Action == TEXT("read_stringtable") || Action == TEXT("list_stringtable_keys"))
			{
				return SuccessJson(ReadStringTableJson(Table));
			}
			const FString Key = GetStringArgument(Args, { TEXT("key"), TEXT("entryKey") });
			if (Action == TEXT("get_stringtable_entry"))
			{
				FString Source;
				if (!Table->GetStringTable()->GetSourceString(FTextKey(Key), Source))
				{
					return ErrorJson(TEXT("StringTable Key 不存在。"));
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("key"), Key);
				Result->SetStringField(TEXT("value"), Source);
				return SuccessJson(Result);
			}
			if (Action == TEXT("set_stringtable_entry"))
			{
				const FString Value = GetStringArgument(Args, { TEXT("value"), TEXT("sourceString"), TEXT("text") });
				Table->Modify();
				Table->GetMutableStringTable()->SetSourceString(FTextKey(Key), Value, FString());
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("key"), Key);
				Result->SetStringField(TEXT("value"), Value);
				return SuccessJson(Result);
			}
			if (Action == TEXT("remove_stringtable_entry"))
			{
				Table->Modify();
				Table->GetMutableStringTable()->RemoveSourceString(FTextKey(Key));
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("key"), Key);
				Result->SetBoolField(TEXT("removed"), true);
				return SuccessJson(Result);
			}
			if (Action == TEXT("import_stringtable"))
			{
				const TSharedPtr<FJsonObject> Entries = GetObjectArgument(Args, { TEXT("entries"), TEXT("data") });
				if (!Entries.IsValid())
				{
					return ErrorJson(TEXT("缺少 entries JSON 对象。"));
				}
				Transactions::FUnrealAgentMCPCompensatingEditorTransaction Compensation(FText::FromString(TEXT("Unreal Agent 导入 StringTable")));
				if (!Compensation.IsReady(Error))
				{
					return ErrorJson(Error);
				}
				Table->Modify();
				int32 Written = 0;
				for (const auto& Pair : Entries->Values)
				{
					FString Source;
					if (Pair.Value.IsValid() && Pair.Value->TryGetString(Source))
					{
						Table->GetMutableStringTable()->SetSourceString(FTextKey(Pair.Key), Source, FString());
						++Written;
					}
				}
				Table->MarkPackageDirty();
				TSharedRef<FJsonObject> Result = ReadStringTableJson(Table);
				Result->SetNumberField(TEXT("writtenEntries"), Written);
				if (!Compensation.Register(Error))
				{
					return ErrorJson(Error);
				}
				return SuccessJson(Result);
			}
		}

		return ErrorJson(FString::Printf(TEXT("表格资产动作未进入有效分支：%s"), *Action));
	}
}
