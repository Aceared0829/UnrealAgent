// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealChooserAdapter.cpp
 * @brief ChooserTable 创建、加载、描述和公共提交逻辑。
 */

#include "Adapters/Unreal/Chooser/UnrealAgentMCPUnrealChooserAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Chooser.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IChooserColumn.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FString NormalizeChooserPackagePath(FString Path)
		{
			Path.TrimStartAndEndInline();
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (Path.EndsWith(TEXT("/")))
			{
				Path.LeftChopInline(1);
			}
			return Path.IsEmpty() ? TEXT("/Game") : Path;
		}

		FString ReadChooserString(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field)
		{
			FString Value;
			if (Args.IsValid())
			{
				Args->TryGetStringField(Field, Value);
			}
			Value.TrimStartAndEndInline();
			return Value;
		}

		bool SaveChooser(UChooserTable* Table, FString& OutError)
		{
			if (!Table)
			{
				OutError = TEXT("ChooserTable 为空。");
				return false;
			}
			UPackage* Package = Table->GetOutermost();
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(Package, Table, *Filename, SaveArgs))
			{
				OutError = TEXT("ChooserTable 资产保存失败。");
				return false;
			}
			return true;
		}

		FString ResultTypeName(const UChooserTable* Table)
		{
			const UEnum* Enum = StaticEnum<EObjectChooserResultType>();
			return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Table->ResultType)) : TEXT("ObjectResult");
		}
	}

	UChooserTable* FUnrealAgentMCPUnrealChooserAdapter::LoadTablePath(const FString& Path, FString& OutError)
	{
		FString Normalized = Path;
		Normalized.TrimStartAndEndInline();
		if (Normalized.IsEmpty())
		{
			OutError = TEXT("缺少 ChooserTable 资产路径。");
			return nullptr;
		}
		UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *Normalized);
		if (!Table && Normalized.StartsWith(TEXT("/Game/")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Normalized);
			Table = LoadObject<UChooserTable>(nullptr, *(Normalized + TEXT(".") + AssetName));
		}
		if (!Table)
		{
			OutError = FString::Printf(TEXT("无法加载 ChooserTable：%s"), *Normalized);
		}
		return Table;
	}

	UChooserTable* FUnrealAgentMCPUnrealChooserAdapter::LoadTable(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutError)
	{
		return LoadTablePath(ReadChooserString(Args, Field), OutError);
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::CommitTable(UChooserTable* Table, const FString& Action, const TSharedRef<FJsonObject>& Result, const bool bSave)
	{
		if (!Table)
		{
			return ErrorJson(TEXT("ChooserTable 为空。"));
		}
		Table->Compile(true);
		Table->MarkPackageDirty();
		if (bSave)
		{
			FString SaveError;
			if (!SaveChooser(Table, SaveError))
			{
				return ErrorJson(SaveError);
			}
		}
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("table"), Table->GetPathName());
#if WITH_EDITORONLY_DATA
		Result->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
#endif
		Result->SetNumberField(TEXT("columnCount"), Table->ColumnsStructs.Num());
		Result->SetBoolField(TEXT("saved"), bSave);
		return JsonObjectToString(Result);
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealChooserAdapter::MakeColumnJson(const FInstancedStruct& ColumnData, const int32 Index)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), Index);
		if (!ColumnData.IsValid())
		{
			Result->SetStringField(TEXT("columnType"), TEXT("Invalid"));
			return Result;
		}
		const UScriptStruct* Struct = ColumnData.GetScriptStruct();
		Result->SetStringField(TEXT("name"), Struct->GetDisplayNameText().ToString());
		Result->SetStringField(TEXT("columnType"), Struct->GetName());
		const FChooserColumnBase* Column = ColumnData.GetPtr<FChooserColumnBase>();
		FString CellType;
		FString InputType;
		if (Column)
		{
			FChooserColumnBase* MutableColumn = const_cast<FChooserColumnBase*>(Column);
			if (const UScriptStruct* Input = MutableColumn->GetInputType())
			{
				InputType = Input->GetName();
			}
			const FName RowPropertyName = MutableColumn->RowValuesPropertyName();
			if (const FArrayProperty* RowProperty = FindFProperty<FArrayProperty>(Struct, RowPropertyName))
			{
				CellType = RowProperty->Inner->GetCPPType();
			}
		}
		Result->SetStringField(TEXT("inputType"), InputType);
		Result->SetStringField(TEXT("cellType"), CellType);
		return Result;
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealChooserAdapter::MakeResultJson(const FInstancedStruct& ResultData)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!ResultData.IsValid())
		{
			Result->SetStringField(TEXT("resultType"), TEXT("None"));
			Result->SetStringField(TEXT("output"), TEXT(""));
			return Result;
		}
		Result->SetStringField(TEXT("resultType"), ResultData.GetScriptStruct()->GetName());
		FString Output;
		if (const FObjectChooserBase* Chooser = ResultData.GetPtr<FObjectChooserBase>())
		{
#if WITH_EDITOR
			if (UObject* Object = Chooser->GetReferencedObject())
			{
				Output = Object->GetPathName();
			}
#endif
		}
		Result->SetStringField(TEXT("output"), Output);
		return Result;
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::Create(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Name = ReadChooserString(Args, TEXT("name"));
		if (Name.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 name。"));
		}
		const FString PackagePath = NormalizeChooserPackagePath(ReadChooserString(Args, TEXT("packagePath")));
		const FString LongPackageName = PackagePath + TEXT("/") + Name;
		if (!FPackageName::IsValidLongPackageName(LongPackageName))
		{
			return ErrorJson(FString::Printf(TEXT("ChooserTable 包路径无效：%s"), *LongPackageName));
		}

		FString Conflict = ReadChooserString(Args, TEXT("onConflict"));
		if (Conflict.IsEmpty())
		{
			Conflict = TEXT("skip");
		}
		FString LoadError;
		UChooserTable* Existing = LoadTablePath(LongPackageName, LoadError);
		if (Existing)
		{
			if (Conflict.Equals(TEXT("skip"), ESearchCase::IgnoreCase))
			{
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("success"), true);
				Result->SetBoolField(TEXT("created"), false);
				Result->SetBoolField(TEXT("existed"), true);
				Result->SetStringField(TEXT("table"), Existing->GetPathName());
				return JsonObjectToString(Result);
			}
			if (!Conflict.Equals(TEXT("overwrite"), ESearchCase::IgnoreCase))
			{
				return ErrorJson(TEXT("目标 ChooserTable 已存在，onConflict=error。"));
			}
			TArray<UObject*> ObjectsToDelete = { Existing };
			ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		}

		UPackage* Package = CreatePackage(*LongPackageName);
		UChooserTable* Table = NewObject<UChooserTable>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		if (!Table)
		{
			return ErrorJson(TEXT("创建 ChooserTable 对象失败。"));
		}
		Table->OutputObjectType = UObject::StaticClass();
#if WITH_EDITORONLY_DATA
		Table->Version = UChooserTable::CurrentVersion;
#endif
		FAssetRegistryModule::AssetCreated(Table);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("created"), true);
		Result->SetBoolField(TEXT("existed"), false);
		return CommitTable(Table, TEXT("create"), Result, true);
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::Describe(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("table"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
		TArray<TSharedPtr<FJsonValue>> Columns;
		for (int32 Index = 0; Index < Table->ColumnsStructs.Num(); ++Index)
		{
			Columns.Add(MakeShared<FJsonValueObject>(MakeColumnJson(Table->ColumnsStructs[Index], Index)));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("table"), Table->GetPathName());
		Result->SetStringField(TEXT("resultType"), ResultTypeName(Table));
		Result->SetStringField(TEXT("outputClass"), Table->OutputObjectType ? Table->OutputObjectType->GetPathName() : TEXT(""));
#if WITH_EDITORONLY_DATA
		Result->SetNumberField(TEXT("rowCount"), Table->ResultsStructs.Num());
#else
		Result->SetNumberField(TEXT("rowCount"), 0);
#endif
		Result->SetNumberField(TEXT("columnCount"), Table->ColumnsStructs.Num());
		Result->SetArrayField(TEXT("columns"), MoveTemp(Columns));
		Result->SetObjectField(TEXT("fallback"), MakeResultJson(Table->FallbackResult));
		return JsonObjectToString(Result);
	}
}
