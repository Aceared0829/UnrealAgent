// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPChooserMigrationTests.cpp
 * @brief ChooserTable 九项迁移能力的资产级黑盒集成测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Chooser/UnrealAgentMCPUnrealChooserAdapter.h"
#include "Application/Domains/Chooser/UnrealAgentMCPChooserService.h"
#include "Application/Ports/UnrealAgentMCPChooserPort.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Chooser.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Texture2D.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPChooserMigrationIntegrationTest, "WorldData.UnrealAgent.Chooser.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPChooserMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString Root = TEXT("/Game/UnrealAgentTests");
		const FString TableName = TEXT("CT_MCP_") + Suffix;
		const FString TablePackage = Root + TEXT("/") + TableName;
		const FString TablePath = TablePackage + TEXT(".") + TableName;

		auto MakeTextureAsset = [&Root, &Suffix](const FString& Prefix)
		{
			const FString Name = Prefix + Suffix;
			UPackage* Package = CreatePackage(*(Root + TEXT("/") + Name));
			UTexture2D* Asset = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Asset);
			return Asset;
		};
		UTexture2D* AssetA = MakeTextureAsset(TEXT("TX_A_"));
		UTexture2D* AssetB = MakeTextureAsset(TEXT("TX_B_"));
		TArray<UObject*> CleanupObjects = { AssetA, AssetB };

		TSharedRef<IUnrealAgentMCPChooserPort> Port = MakeShared<FUnrealAgentMCPUnrealChooserAdapter>();
		FUnrealAgentMCPChooserService Service(Port);
		auto Execute = [&Service, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			const FString Result = Service.Execute(Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1200)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};
		auto TableArgs = [&TablePath]()
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("table"), TablePath);
			return Args;
		};

		TSharedRef<FJsonObject> CreateArgs = MakeShared<FJsonObject>();
		CreateArgs->SetStringField(TEXT("name"), TableName);
		CreateArgs->SetStringField(TEXT("packagePath"), Root);
		CreateArgs->SetStringField(TEXT("onConflict"), TEXT("error"));
		const FString CreateResult = Execute(TEXT("create"), CreateArgs);
		TestTrue(TEXT("创建动作生成真实 ChooserTable 资产"), CreateResult.Contains(TablePath) && LoadObject<UChooserTable>(nullptr, *TablePath) != nullptr);
		UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *TablePath);
		if (Table)
		{
			CleanupObjects.Insert(Table, 0);
		}

		TSharedRef<FJsonObject> ColumnArgs = TableArgs();
		ColumnArgs->SetStringField(TEXT("columnType"), TEXT("RandomizeColumn"));
		const FString ColumnResult = Execute(TEXT("add_column"), ColumnArgs);
		TestTrue(TEXT("布尔列通过反射结构创建"), ColumnResult.Contains(TEXT("RandomizeColumn")) && ColumnResult.Contains(TEXT("\"columnIndex\":0")));

		const FString EmptyDescription = Execute(TEXT("describe"), TableArgs());
		TestTrue(TEXT("描述动作返回列类型与单元格类型"), EmptyDescription.Contains(TEXT("RandomizeColumn")) && EmptyDescription.Contains(TEXT("\"rowCount\":0")));

		auto AddRow = [&Execute, &TableArgs](UObject* Output, const double CellValue)
		{
			TSharedRef<FJsonObject> Args = TableArgs();
			Args->SetStringField(TEXT("output"), Output->GetPathName());
			Args->SetStringField(TEXT("outputType"), TEXT("asset"));
			Args->SetArrayField(TEXT("cells"), { MakeShared<FJsonValueNumber>(CellValue) });
			return Execute(TEXT("add_row"), Args);
		};
		AddRow(AssetA, 1.5);
		AddRow(AssetB, 2.5);
		const FString RowsBeforeEdit = Execute(TEXT("list_rows"), TableArgs());
		TestTrue(TEXT("行列表返回输出对象与可往返单元格文本"),
			RowsBeforeEdit.Contains(AssetA->GetPathName()) && RowsBeforeEdit.Contains(AssetB->GetPathName()) && RowsBeforeEdit.Contains(TEXT("1.5")) &&
				RowsBeforeEdit.Contains(TEXT("2.5")));

		TSharedRef<FJsonObject> SetArgs = TableArgs();
		SetArgs->SetNumberField(TEXT("index"), 0);
		SetArgs->SetBoolField(TEXT("disabled"), true);
		SetArgs->SetObjectField(TEXT("inputs"), MakeShared<FJsonObject>());
		SetArgs->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("0"), 3.5);
		Execute(TEXT("set_row"), SetArgs);
		const FString RowsAfterEdit = Execute(TEXT("list_rows"), TableArgs());
		TestTrue(TEXT("行编辑保留输出并更新禁用状态"), RowsAfterEdit.Contains(TEXT("\"disabled\":true")) && RowsAfterEdit.Contains(AssetA->GetPathName()));

		TSharedRef<FJsonObject> ListReferencesArgs = MakeShared<FJsonObject>();
		ListReferencesArgs->SetStringField(TEXT("assetPath"), TablePath);
		const FString ReferencesBefore = Execute(TEXT("list_object_references"), ListReferencesArgs);
		TestTrue(TEXT("引用枚举定位到两个真实结果资产"),
			ReferencesBefore.Contains(AssetA->GetPathName()) && ReferencesBefore.Contains(AssetB->GetPathName()) && ReferencesBefore.Contains(TEXT("ResultsStructs[0].Asset")));

		auto RemapArgs = [&TablePath, AssetA, AssetB](const bool bDryRun)
		{
			TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("assetPath"), TablePath);
			Args->SetStringField(TEXT("from"), AssetA->GetPathName());
			Args->SetStringField(TEXT("to"), AssetB->GetPathName());
			Args->SetBoolField(TEXT("dryRun"), bDryRun);
			return Args;
		};
		const FString DryRunResult = Execute(TEXT("remap_object_references"), RemapArgs(true));
		TestTrue(TEXT("引用重映射默认路径支持只读预演"),
			DryRunResult.Contains(TEXT("\"dryRun\":true")) && DryRunResult.Contains(TEXT("\"matchedCount\":1")) && DryRunResult.Contains(TEXT("\"appliedCount\":0")));
		const FString ApplyResult = Execute(TEXT("remap_object_references"), RemapArgs(false));
		TestTrue(TEXT("引用重映射应用后保持资产为脏而不自动保存"), ApplyResult.Contains(TEXT("\"appliedCount\":1")) && ApplyResult.Contains(TEXT("\"saved\":false")));
		const FString ReferencesAfter = Execute(TEXT("list_object_references"), ListReferencesArgs);
		TestFalse(TEXT("应用后旧对象路径已消失"), ReferencesAfter.Contains(AssetA->GetPathName()));

		TSharedRef<FJsonObject> DeleteArgs = TableArgs();
		DeleteArgs->SetNumberField(TEXT("index"), 1);
		Execute(TEXT("delete_row"), DeleteArgs);
		const FString FinalRows = Execute(TEXT("list_rows"), TableArgs());
		TestTrue(TEXT("删除动作同步缩减结果与全部列单元格"), FinalRows.Contains(TEXT("\"rowCount\":1")));

		ObjectTools::DeleteObjectsUnchecked(CleanupObjects);
		return true;
	}
}

#endif
