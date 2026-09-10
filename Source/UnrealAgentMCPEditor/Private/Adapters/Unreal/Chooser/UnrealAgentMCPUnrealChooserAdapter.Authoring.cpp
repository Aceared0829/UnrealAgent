// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealChooserAdapter.Authoring.cpp
 * @brief Chooser 列、行、单元格和结果对象的反射式编写实现。
 */

#include "Adapters/Unreal/Chooser/UnrealAgentMCPUnrealChooserAdapter.h"

#include "BoolColumn.h"
#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IChooserColumn.h"
#include "IChooserParameterBase.h"
#include "Misc/PackageName.h"
#include "ObjectChooser_Asset.h"
#include "Serialization/JsonSerializer.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString RequiredString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field)
		{
			FString Value;
			if (Args.IsValid())
			{
				Args->TryGetStringField(Field, Value);
			}
			Value.TrimStartAndEndInline();
			return Value;
		}

		UScriptStruct* FindDerivedStruct(const FString& RequestedName, const UScriptStruct* BaseStruct)
		{
			FString Wanted = RequestedName;
			Wanted.TrimStartAndEndInline();
			Wanted.RemoveFromStart(TEXT("F"));
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				UScriptStruct* Candidate = *It;
				FString CandidateName = Candidate->GetName();
				CandidateName.RemoveFromStart(TEXT("F"));
				if (Candidate->IsChildOf(BaseStruct) &&
					(CandidateName.Equals(Wanted, ESearchCase::IgnoreCase) || Candidate->GetPathName().Equals(RequestedName, ESearchCase::IgnoreCase)))
				{
					return Candidate;
				}
			}
			return nullptr;
		}

		FString CellText(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid() || Value->IsNull())
			{
				return FString();
			}
			switch (Value->Type)
			{
			case EJson::String:
				return Value->AsString();
			case EJson::Boolean:
				return Value->AsBool() ? TEXT("true") : TEXT("false");
			case EJson::Number:
				return FString::SanitizeFloat(Value->AsNumber());
			default:
			{
				FString Json;
				const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
				FJsonSerializer::Serialize(Value, TEXT(""), Writer);
				return Json;
			}
			}
		}

		bool ImportCellValue(FChooserColumnBase& Column, const UScriptStruct* ColumnStruct, const int32 RowIndex, const TSharedPtr<FJsonValue>& Value, FString& OutError)
		{
			if (!Value.IsValid() || Value->IsNull())
			{
				return true;
			}
			const FName RowPropertyName = Column.RowValuesPropertyName();
			FArrayProperty* RowProperty = FindFProperty<FArrayProperty>(ColumnStruct, RowPropertyName);
			if (!RowProperty)
			{
				OutError = FString::Printf(TEXT("列 %s 不提供可写单元格数组。"), *ColumnStruct->GetName());
				return false;
			}
			FScriptArrayHelper Rows(RowProperty, RowProperty->ContainerPtrToValuePtr<void>(&Column));
			if (!Rows.IsValidIndex(RowIndex))
			{
				OutError = TEXT("列单元格数量与行数量不一致。");
				return false;
			}
			FString Text = CellText(Value);
			if (Value->Type == EJson::Boolean)
			{
				if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(RowProperty->Inner))
				{
					const UEnum* Enum = EnumProperty->GetEnum();
					if (Enum && Enum->GetName().Contains(TEXT("BoolColumnCellValue")))
					{
						Text = Value->AsBool() ? TEXT("MatchTrue") : TEXT("MatchFalse");
					}
				}
				else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(RowProperty->Inner);
					ByteProperty && ByteProperty->Enum && ByteProperty->Enum->GetName().Contains(TEXT("BoolColumnCellValue")))
				{
					Text = Value->AsBool() ? TEXT("MatchTrue") : TEXT("MatchFalse");
				}
			}
			void* Destination = Rows.GetRawPtr(RowIndex);
			if (!RowProperty->Inner->ImportText_Direct(*Text, Destination, nullptr, PPF_None))
			{
				OutError = FString::Printf(TEXT("无法把 '%s' 写入列 %s 的第 %d 行。"), *Text, *ColumnStruct->GetName(), RowIndex);
				return false;
			}
			return true;
		}

		FString ExportCellValue(FChooserColumnBase& Column, const UScriptStruct* ColumnStruct, const int32 RowIndex)
		{
			const FName RowPropertyName = Column.RowValuesPropertyName();
			FArrayProperty* RowProperty = FindFProperty<FArrayProperty>(ColumnStruct, RowPropertyName);
			if (!RowProperty)
			{
				return FString();
			}
			FScriptArrayHelper Rows(RowProperty, RowProperty->ContainerPtrToValuePtr<void>(&Column));
			if (!Rows.IsValidIndex(RowIndex))
			{
				return FString();
			}
			FString Text;
			RowProperty->Inner->ExportTextItem_Direct(Text, Rows.GetRawPtr(RowIndex), nullptr, nullptr, PPF_None);
			return Text;
		}

		int32 ResolveColumnIndex(UChooserTable* Table, const FString& Key)
		{
			if (Key.IsNumeric())
			{
				return FCString::Atoi(*Key);
			}
			for (int32 Index = 0; Index < Table->ColumnsStructs.Num(); ++Index)
			{
				const FInstancedStruct& Data = Table->ColumnsStructs[Index];
				if (!Data.IsValid())
				{
					continue;
				}
				const UScriptStruct* Struct = Data.GetScriptStruct();
				if (Struct->GetName().Equals(Key, ESearchCase::IgnoreCase) || Struct->GetDisplayNameText().ToString().Equals(Key, ESearchCase::IgnoreCase))
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		UObject* LoadOutputObject(const FString& Path)
		{
			if (Path.IsEmpty())
			{
				return nullptr;
			}
			UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
			if (!Object && Path.StartsWith(TEXT("/Game/")))
			{
				const FString Name = FPackageName::GetLongPackageAssetName(Path);
				Object = StaticLoadObject(UObject::StaticClass(), nullptr, *(Path + TEXT(".") + Name));
			}
			return Object;
		}
	}

	bool FUnrealAgentMCPUnrealChooserAdapter::SetResult(FInstancedStruct& ResultData, const FString& OutputType, const FString& OutputPath, FString& OutError)
	{
		FString Type = OutputType;
		if (Type.IsEmpty())
		{
			Type = TEXT("asset");
		}
		if (Type.Equals(TEXT("soft_asset"), ESearchCase::IgnoreCase))
		{
			ResultData.InitializeAs(FSoftAssetChooser::StaticStruct());
			ResultData.GetMutable<FSoftAssetChooser>().Asset = TSoftObjectPtr<UObject>(FSoftObjectPath(OutputPath));
			return true;
		}
		UObject* Object = LoadOutputObject(OutputPath);
		if (!OutputPath.IsEmpty() && !Object)
		{
			OutError = FString::Printf(TEXT("无法加载行输出对象：%s"), *OutputPath);
			return false;
		}
		if (Type.Equals(TEXT("evaluate"), ESearchCase::IgnoreCase))
		{
			UChooserTable* Nested = Cast<UChooserTable>(Object);
			if (!Nested && Object)
			{
				OutError = TEXT("outputType=evaluate 要求输出为 ChooserTable。");
				return false;
			}
			ResultData.InitializeAs(FEvaluateChooser::StaticStruct());
			ResultData.GetMutable<FEvaluateChooser>().Chooser = Nested;
			return true;
		}
		if (!Type.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("未知 outputType：%s"), *Type);
			return false;
		}
		ResultData.InitializeAs(FAssetChooser::StaticStruct());
		ResultData.GetMutable<FAssetChooser>().Asset = Object;
		return true;
	}

	bool FUnrealAgentMCPUnrealChooserAdapter::SetRowCells(UChooserTable* Table, const int32 RowIndex, const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Cells = nullptr;
		if (Args.IsValid() && Args->TryGetArrayField(TEXT("cells"), Cells) && Cells)
		{
			for (int32 Index = 0; Index < Cells->Num() && Index < Table->ColumnsStructs.Num(); ++Index)
			{
				FInstancedStruct& Data = Table->ColumnsStructs[Index];
				if (!Data.IsValid())
				{
					continue;
				}
				FChooserColumnBase* Column = Data.GetMutablePtr<FChooserColumnBase>();
				if (!Column || !ImportCellValue(*Column, Data.GetScriptStruct(), RowIndex, (*Cells)[Index], OutError))
				{
					return false;
				}
			}
		}

		const TSharedPtr<FJsonObject>* Inputs = nullptr;
		if (Args.IsValid() && Args->TryGetObjectField(TEXT("inputs"), Inputs) && Inputs && Inputs->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Inputs)->Values)
			{
				const int32 Index = ResolveColumnIndex(Table, Pair.Key);
				if (!Table->ColumnsStructs.IsValidIndex(Index))
				{
					OutError = FString::Printf(TEXT("找不到 Chooser 列：%s"), *Pair.Key);
					return false;
				}
				FInstancedStruct& Data = Table->ColumnsStructs[Index];
				FChooserColumnBase* Column = Data.GetMutablePtr<FChooserColumnBase>();
				if (!Column || !ImportCellValue(*Column, Data.GetScriptStruct(), RowIndex, Pair.Value, OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::AddColumn(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("table"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
		const FString ColumnType = RequiredString(Args, TEXT("columnType"));
		UScriptStruct* ColumnStruct = FindDerivedStruct(ColumnType, FChooserColumnBase::StaticStruct());
		if (!ColumnStruct)
		{
			return ErrorJson(FString::Printf(TEXT("找不到 Chooser 列结构：%s"), *ColumnType));
		}

		Table->Modify();
		FInstancedStruct ColumnData;
		ColumnData.InitializeAs(ColumnStruct);
		FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>();
		if (!Column)
		{
			return ErrorJson(TEXT("列结构不是 FChooserColumnBase。"));
		}
		Column->Initialize(Table);
		const FString InputStructName = RequiredString(Args, TEXT("inputStruct"));
		if (!InputStructName.IsEmpty())
		{
			const UScriptStruct* InputBase = Column->GetInputBaseType();
			UScriptStruct* InputStruct = InputBase ? FindDerivedStruct(InputStructName, InputBase) : nullptr;
			if (!InputStruct)
			{
				return ErrorJson(FString::Printf(TEXT("找不到与列兼容的输入结构：%s"), *InputStructName));
			}
			Column->SetInputType(InputStruct);
		}
		const FString BoundProperty = RequiredString(Args, TEXT("boundProperty"));
		if (!BoundProperty.IsEmpty())
		{
			FInstancedStruct* Input = Column->GetInputValuePtr();
			FStructProperty* BindingProperty = Input && Input->IsValid() ? FindFProperty<FStructProperty>(Input->GetScriptStruct(), TEXT("Binding")) : nullptr;
			if (!BindingProperty || !BindingProperty->Struct->IsChildOf(FChooserPropertyBinding::StaticStruct()))
			{
				return ErrorJson(TEXT("所选输入结构不支持属性绑定。"));
			}
			FChooserPropertyBinding* Binding = reinterpret_cast<FChooserPropertyBinding*>(BindingProperty->ContainerPtrToValuePtr<void>(Input->GetMutableMemory()));
			Binding->PropertyBindingChain = { FName(*BoundProperty) };
		}
		const FString EnumPath = RequiredString(Args, TEXT("enumPath"));
		if (!EnumPath.IsEmpty())
		{
			FInstancedStruct* Input = Column->GetInputValuePtr();
			FStructProperty* BindingProperty = Input && Input->IsValid() ? FindFProperty<FStructProperty>(Input->GetScriptStruct(), TEXT("Binding")) : nullptr;
			UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath);
			FObjectPropertyBase* EnumProperty = BindingProperty ? FindFProperty<FObjectPropertyBase>(BindingProperty->Struct, TEXT("Enum")) : nullptr;
			if (!Enum || !EnumProperty)
			{
				return ErrorJson(FString::Printf(TEXT("无法把枚举绑定到该列：%s"), *EnumPath));
			}
			void* BindingMemory = BindingProperty->ContainerPtrToValuePtr<void>(Input->GetMutableMemory());
			EnumProperty->SetObjectPropertyValue_InContainer(BindingMemory, Enum);
		}
#if WITH_EDITORONLY_DATA
		Column->SetNumRows(Table->ResultsStructs.Num());
#endif
		const int32 NewIndex = Table->ColumnsStructs.Add(MoveTemp(ColumnData));
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("columnIndex"), NewIndex);
		Result->SetObjectField(TEXT("column"), MakeColumnJson(Table->ColumnsStructs[NewIndex], NewIndex));
		return CommitTable(Table, TEXT("add_column"), Result, true);
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::ListRows(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("table"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
		TArray<TSharedPtr<FJsonValue>> Rows;
#if WITH_EDITORONLY_DATA
		for (int32 RowIndex = 0; RowIndex < Table->ResultsStructs.Num(); ++RowIndex)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("index"), RowIndex);
			Row->SetBoolField(TEXT("disabled"), Table->IsRowDisabled(RowIndex));
			const TSharedRef<FJsonObject> Output = MakeResultJson(Table->ResultsStructs[RowIndex]);
			Row->SetStringField(TEXT("resultType"), Output->GetStringField(TEXT("resultType")));
			Row->SetStringField(TEXT("output"), Output->GetStringField(TEXT("output")));
			TArray<TSharedPtr<FJsonValue>> Cells;
			for (FInstancedStruct& Data : Table->ColumnsStructs)
			{
				FString Text;
				if (Data.IsValid())
				{
					if (FChooserColumnBase* Column = Data.GetMutablePtr<FChooserColumnBase>())
					{
						Text = ExportCellValue(*Column, Data.GetScriptStruct(), RowIndex);
					}
				}
				Cells.Add(MakeShared<FJsonValueString>(Text));
			}
			Row->SetArrayField(TEXT("cells"), MoveTemp(Cells));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
#endif
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("table"), Table->GetPathName());
		Result->SetNumberField(TEXT("rowCount"), Rows.Num());
		Result->SetArrayField(TEXT("rows"), MoveTemp(Rows));
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::AddRow(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("table"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
#if !WITH_EDITORONLY_DATA
		return ErrorJson(TEXT("当前目标不包含 Chooser 编辑器数据。"));
#else
		Table->Modify();
		FString OutputType = RequiredString(Args, TEXT("outputType"));
		const FString Output = RequiredString(Args, TEXT("output"));
		FInstancedStruct ResultData;
		if (!SetResult(ResultData, OutputType, Output, Error))
		{
			return ErrorJson(Error);
		}
		const int32 RowIndex = Table->ResultsStructs.Add(MoveTemp(ResultData));
		Table->DisabledRows.Add(false);
		for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
		{
			if (FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>())
			{
				Column->SetNumRows(Table->ResultsStructs.Num());
			}
		}
		if (!SetRowCells(Table, RowIndex, Args, Error))
		{
			int32 DeleteIndex = RowIndex;
			for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
			{
				if (FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>())
				{
					Column->DeleteRows(TArrayView<int32>(&DeleteIndex, 1));
				}
			}
			Table->ResultsStructs.RemoveAt(RowIndex);
			Table->DisabledRows.RemoveAt(RowIndex);
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), RowIndex);
		Result->SetObjectField(TEXT("rowOutput"), MakeResultJson(Table->ResultsStructs[RowIndex]));
		return CommitTable(Table, TEXT("add_row"), Result, true);
#endif
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::SetRow(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("table"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
#if !WITH_EDITORONLY_DATA
		return ErrorJson(TEXT("当前目标不包含 Chooser 编辑器数据。"));
#else
		double IndexNumber = -1.0;
		if (!Args.IsValid() || !Args->TryGetNumberField(TEXT("index"), IndexNumber))
		{
			return ErrorJson(TEXT("缺少必填 index。"));
		}
		const int32 RowIndex = FMath::RoundToInt(IndexNumber);
		if (!Table->ResultsStructs.IsValidIndex(RowIndex))
		{
			return ErrorJson(TEXT("Chooser 行索引越界。"));
		}
		Table->Modify();
		if (Args->HasField(TEXT("output")) || Args->HasField(TEXT("outputType")))
		{
			const FString Output = RequiredString(Args, TEXT("output"));
			const FString OutputType = RequiredString(Args, TEXT("outputType"));
			if (!SetResult(Table->ResultsStructs[RowIndex], OutputType, Output, Error))
			{
				return ErrorJson(Error);
			}
		}
		bool bDisabled = false;
		if (Args->TryGetBoolField(TEXT("disabled"), bDisabled))
		{
			Table->DisabledRows.SetNum(Table->ResultsStructs.Num());
			Table->DisabledRows[RowIndex] = bDisabled;
		}
		if (!SetRowCells(Table, RowIndex, Args, Error))
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), RowIndex);
		Result->SetBoolField(TEXT("disabled"), Table->IsRowDisabled(RowIndex));
		return CommitTable(Table, TEXT("set_row"), Result, true);
#endif
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::DeleteRow(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("table"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
#if !WITH_EDITORONLY_DATA
		return ErrorJson(TEXT("当前目标不包含 Chooser 编辑器数据。"));
#else
		double IndexNumber = -1.0;
		if (!Args.IsValid() || !Args->TryGetNumberField(TEXT("index"), IndexNumber))
		{
			return ErrorJson(TEXT("缺少必填 index。"));
		}
		const int32 RowIndex = FMath::RoundToInt(IndexNumber);
		if (!Table->ResultsStructs.IsValidIndex(RowIndex))
		{
			return ErrorJson(TEXT("Chooser 行索引越界。"));
		}
		Table->Modify();
		int32 DeleteIndex = RowIndex;
		for (FInstancedStruct& ColumnData : Table->ColumnsStructs)
		{
			if (FChooserColumnBase* Column = ColumnData.GetMutablePtr<FChooserColumnBase>())
			{
				Column->DeleteRows(TArrayView<int32>(&DeleteIndex, 1));
			}
		}
		Table->ResultsStructs.RemoveAt(RowIndex);
		if (Table->DisabledRows.IsValidIndex(RowIndex))
		{
			Table->DisabledRows.RemoveAt(RowIndex);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("deletedIndex"), RowIndex);
		return CommitTable(Table, TEXT("delete_row"), Result, true);
#endif
	}
}
