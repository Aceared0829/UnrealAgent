// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealChooserAdapter.h
 * @brief 通过 UE Chooser 运行时数据结构实现 ChooserTable 编写。
 */

#include "Application/Ports/UnrealAgentMCPChooserPort.h"

class FJsonValue;
class UChooserTable;
class UObject;
class UScriptStruct;
struct FChooserColumnBase;
struct FInstancedStruct;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealChooserAdapter final : public IUnrealAgentMCPChooserPort
	{
	public:
		virtual FString Create(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString Describe(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString AddColumn(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListRows(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString AddRow(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString SetRow(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString DeleteRow(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString ListObjectReferences(const TSharedPtr<FJsonObject>& Args) override;
		virtual FString RemapObjectReferences(const TSharedPtr<FJsonObject>& Args) override;

	private:
		static UChooserTable* LoadTable(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& OutError);
		static UChooserTable* LoadTablePath(const FString& Path, FString& OutError);
		static FString CommitTable(UChooserTable* Table, const FString& Action, const TSharedRef<FJsonObject>& Result, bool bSave);
		static TSharedRef<FJsonObject> MakeColumnJson(const FInstancedStruct& ColumnData, int32 Index);
		static TSharedRef<FJsonObject> MakeResultJson(const FInstancedStruct& ResultData);
		static bool SetRowCells(UChooserTable* Table, int32 RowIndex, const TSharedPtr<FJsonObject>& Args, FString& OutError);
		static bool SetResult(FInstancedStruct& ResultData, const FString& OutputType, const FString& OutputPath, FString& OutError);
	};
}
