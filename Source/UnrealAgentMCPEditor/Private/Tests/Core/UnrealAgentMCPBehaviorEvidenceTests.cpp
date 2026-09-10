// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPBehaviorEvidenceTests.cpp
 * @brief 高风险 Tool 的真实副作用、拒绝、事务、审计、取消与清理证据。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"
#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"
#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"
#include "Adapters/Unreal/PCG/UnrealAgentMCPUnrealPCGAdapter.h"
#include "Adapters/Tooling/UnrealAgentMCPExtractedTools.h"
#include "Application/Domains/Blueprint/UnrealAgentMCPBlueprintService.h"
#include "Application/Domains/PCG/UnrealAgentMCPPCGService.h"
#include "Application/Ports/UnrealAgentMCPBlueprintPort.h"
#include "Application/Ports/UnrealAgentMCPPCGPort.h"
#include "Application/Telemetry/UnrealAgentMCPPythonExecutionAudit.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Core/Execution/UnrealAgentMCPToolExecutionService.h"
#include "Core/Protocol/UnrealAgentMCPProtocol.h"
#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PCGGraph.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/Support/UnrealAgentMCPTestRun.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		const TCHAR* MutationConfirmation = TEXT("I understand this operation may modify project data");
		const TCHAR* PythonConfirmation = TEXT("I understand this runs arbitrary Unreal Python");

		FString InvokeTool(FAutomationTestBase& Test, const FString& ToolName, const TSharedRef<FJsonObject>& Arguments, bool& OutIsError)
		{
			OutIsError = true;
			TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("name"), ToolName);
			Params->SetObjectField(TEXT("arguments"), Arguments);
			TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
			Request->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
			Request->SetStringField(TEXT("id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Request->SetStringField(TEXT("method"), TEXT("tools/call"));
			Request->SetObjectField(TEXT("params"), Params);

			const TSharedPtr<FJsonObject> Response = FUnrealAgentMCPServer::DispatchJsonRpcRequest(Request);
			const TSharedPtr<FJsonObject>* Result = nullptr;
			if (!Response.IsValid() || !Response->TryGetObjectField(TEXT("result"), Result) || Result == nullptr || !Result->IsValid())
			{
				Test.AddError(FString::Printf(TEXT("Tool %s 未返回 MCP result。"), *ToolName));
				return FString();
			}
			(*Result)->TryGetBoolField(TEXT("isError"), OutIsError);
			const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
			if (!(*Result)->TryGetArrayField(TEXT("content"), Content) || Content == nullptr || Content->IsEmpty() || !(*Content)[0].IsValid() ||
				(*Content)[0]->Type != EJson::Object)
			{
				Test.AddError(FString::Printf(TEXT("Tool %s 未返回文本内容。"), *ToolName));
				return FString();
			}
			FString Text;
			(*Content)[0]->AsObject()->TryGetStringField(TEXT("text"), Text);
			return Text;
		}

		AActor* FindActorByLabel(UWorld* World, const FString& Label)
		{
			if (World == nullptr)
			{
				return nullptr;
			}
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (IsValid(*It) && It->GetActorLabel() == Label)
				{
					return *It;
				}
			}
			return nullptr;
		}

		FMcpToolDescriptor MakeCompensatingEvidenceDescriptor(const FString& Name, const FString& Description, FMcpDynamicToolHandler Invoker)
		{
			FMcpToolDescriptor Descriptor;
			Descriptor.Provider = TEXT("Automation");
			Descriptor.Toolset = TEXT("Tests.Evidence");
			Descriptor.Name = Name;
			Descriptor.QualifiedName = TEXT("worlddata.tests.") + Name;
			Descriptor.Description = Description;
			Descriptor.InputSchema = MakeShared<FJsonObject>();
			Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
			Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
			Descriptor.OutputSchema = MakeShared<FJsonObject>();
			Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
			Descriptor.Risk = EMcpToolRisk::ContentMutation;
			Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
			Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
			const FString CanonicalToolId = Descriptor.QualifiedName;
			Descriptor.ActionContractResolver = [CanonicalToolId](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
			{
				OutContract.CanonicalToolId = CanonicalToolId;
				OutContract.Risk = EMcpToolRisk::ContentMutation;
				OutContract.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
				return true;
			};
			Descriptor.Invoker = MoveTemp(Invoker);
			return Descriptor;
		}

		bool TryReadJsonStringField(const FString& Json, const FString& Field, FString& OutValue)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			return FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid() && Object->TryGetStringField(Field, OutValue);
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPFileMutationEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.FileMutationLifecycle",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPFileMutationEvidenceTest::RunTest(const FString& Parameters)
	{
		FScopedTestRun Run(*this);
		const FString SourceRelative = Run.MakeRelativeProjectPath(TEXT("source.txt"));
		const FString TargetRelative = Run.MakeRelativeProjectPath(TEXT("target.txt"));
		const FString SourceFull = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), SourceRelative));
		const FString TargetFull = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TargetRelative));
		const FString OutsideRelative = FPaths::Combine(TEXT("../UnrealAgentTestsOutside"), Run.GetRunId(), TEXT("outside.txt"));
		const FString OutsideFull = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), OutsideRelative));
		const FString OutsideRoot = FPaths::GetPath(OutsideFull);
		Run.TrackFile(SourceFull);
		Run.TrackFile(TargetFull);
		Run.TrackDirectory(OutsideRoot);

		bool bIsError = false;
		TSharedRef<FJsonObject> MissingConfirmation = MakeShared<FJsonObject>();
		MissingConfirmation->SetStringField(TEXT("file_path"), SourceRelative);
		MissingConfirmation->SetStringField(TEXT("content"), TEXT("blocked"));
		const FString ConfirmationResult = InvokeTool(*this, TEXT("write_file"), MissingConfirmation, bIsError);
		TestTrue(TEXT("写文件缺少确认时拒绝"), bIsError);
		TestTrue(TEXT("拒绝返回稳定确认策略码"), ConfirmationResult.Contains(TEXT("confirmation_required")));
		TestFalse(TEXT("确认拒绝不创建文件"), IFileManager::Get().FileExists(*SourceFull));

		TSharedRef<FJsonObject> InvalidSchema = MakeShared<FJsonObject>();
		InvalidSchema->SetStringField(TEXT("file_path"), SourceRelative);
		InvalidSchema->SetStringField(TEXT("content"), TEXT("blocked"));
		InvalidSchema->SetStringField(TEXT("confirmation"), MutationConfirmation);
		InvalidSchema->SetStringField(TEXT("unexpected"), TEXT("rejected"));
		const FString SchemaResult = InvokeTool(*this, TEXT("write_file"), InvalidSchema, bIsError);
		TestTrue(TEXT("写文件未知字段被拒绝"), bIsError);
		TestTrue(TEXT("Schema 拒绝返回校验错误"), SchemaResult.Contains(TEXT("invalid_arguments")) && SchemaResult.Contains(TEXT("$.unexpected")));
		TestFalse(TEXT("Schema 拒绝不创建文件"), IFileManager::Get().FileExists(*SourceFull));

		TSharedRef<FJsonObject> Outside = MakeShared<FJsonObject>();
		Outside->SetStringField(TEXT("file_path"), OutsideRelative);
		Outside->SetStringField(TEXT("content"), TEXT("blocked"));
		Outside->SetStringField(TEXT("confirmation"), MutationConfirmation);
		const FString SandboxResult = InvokeTool(*this, TEXT("write_file"), Outside, bIsError);
		TestTrue(TEXT("越界路径被拒绝"), bIsError);
		TestTrue(TEXT("越界路径返回稳定沙箱策略码"), SandboxResult.Contains(TEXT("file_path_outside_sandbox")));
		TestFalse(TEXT("越界路径拒绝后没有外部副作用"), IFileManager::Get().FileExists(*OutsideFull));

		TSharedRef<FJsonObject> Write = MakeShared<FJsonObject>();
		Write->SetStringField(TEXT("file_path"), SourceRelative);
		Write->SetStringField(TEXT("content"), TEXT("p3-evidence"));
		Write->SetStringField(TEXT("confirmation"), MutationConfirmation);
		InvokeTool(*this, TEXT("write_file"), Write, bIsError);
		TestFalse(TEXT("受确认写文件成功"), bIsError);
		FString Written;
		TestTrue(TEXT("写入内容可回读"), FFileHelper::LoadFileToString(Written, *SourceFull));
		TestEqual(TEXT("写入内容精确"), Written, FString(TEXT("p3-evidence")));
		Write->SetStringField(TEXT("content"), TEXT("p3-evidence-replaced"));
		InvokeTool(*this, TEXT("write_file"), Write, bIsError);
		TestFalse(TEXT("原子替换已有文件成功"), bIsError);
		TestTrue(TEXT("原子替换内容可回读"), FFileHelper::LoadFileToString(Written, *SourceFull));
		TestEqual(TEXT("原子替换内容精确"), Written, FString(TEXT("p3-evidence-replaced")));
		TArray<FString> StagedFiles;
		IFileManager::Get().FindFilesRecursive(StagedFiles, *Run.GetSavedRoot(), TEXT("*.uebridge-stage-*"), true, false);
		TestTrue(TEXT("原子写入没有遗留 staging 文件"), StagedFiles.IsEmpty());

		TSharedRef<FJsonObject> Rename = MakeShared<FJsonObject>();
		Rename->SetStringField(TEXT("old_path"), SourceRelative);
		Rename->SetStringField(TEXT("new_path"), TargetRelative);
		Rename->SetStringField(TEXT("confirmation"), MutationConfirmation);
		InvokeTool(*this, TEXT("rename_file"), Rename, bIsError);
		TestFalse(TEXT("受确认重命名成功"), bIsError);
		TestFalse(TEXT("重命名后源文件不存在"), IFileManager::Get().FileExists(*SourceFull));
		TestTrue(TEXT("重命名后目标存在"), IFileManager::Get().FileExists(*TargetFull));

		TSharedRef<FJsonObject> Delete = MakeShared<FJsonObject>();
		Delete->SetStringField(TEXT("file_path"), TargetRelative);
		Delete->SetStringField(TEXT("confirmation"), MutationConfirmation);
		InvokeTool(*this, TEXT("delete_file"), Delete, bIsError);
		TestFalse(TEXT("受确认删除文件成功"), bIsError);
		TestFalse(TEXT("删除后目标文件不存在"), IFileManager::Get().FileExists(*TargetFull));
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPFileCompensationEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.FileCompensation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPFileCompensationEvidenceTest::RunTest(const FString& Parameters)
	{
		using namespace Execution;
		(void)Parameters;

		FScopedTestRun Run(*this);
		const FString RelativePath = Run.MakeRelativeProjectPath(TEXT("compensating-write.txt"));
		const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), RelativePath));
		Run.TrackFile(FullPath);
		TestTrue(TEXT("创建补偿测试原始文件"), FFileHelper::SaveStringToFile(TEXT("original"), *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

		FMcpToolRuntimeRegistry Registry;
		FMcpToolDescriptor Descriptor;
		Descriptor.Provider = TEXT("Automation");
		Descriptor.Toolset = TEXT("Tests.Evidence");
		Descriptor.Name = TEXT("file_compensation_evidence");
		Descriptor.QualifiedName = TEXT("worlddata.tests.file_compensation_evidence");
		Descriptor.Description = TEXT("真实文件写入补偿测试工具。");
		Descriptor.InputSchema = MakeShared<FJsonObject>();
		Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
		Descriptor.OutputSchema = MakeShared<FJsonObject>();
		Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.Risk = EMcpToolRisk::FileMutation;
		Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
		Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
		Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
		{
			OutContract.CanonicalToolId = TEXT("worlddata.tests.file_compensation_evidence");
			OutContract.Risk = EMcpToolRisk::FileMutation;
			OutContract.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
			return true;
		};
		Descriptor.Invoker = BindInvocationContext(
			[](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext&)
			{
				const FString WriteResult = ExtractedTools::WriteFile(Arguments);
				if (!WriteResult.Contains(TEXT("\"success\":true")) && !WriteResult.Contains(TEXT("\"success\": true")))
				{
					return WriteResult;
				}
				return FString(TEXT("{\"success\":false,\"error\":\"synthetic failure\"}"));
			});

		FString Error;
		TestTrue(TEXT("文件补偿测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
		FMcpToolExecutionService Service(Registry);
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("file_path"), RelativePath);
		Arguments->SetStringField(TEXT("content"), TEXT("replacement"));
		FString ResultJson;
		TestTrue(TEXT("覆盖写入失败进入补偿事务"), Service.Execute(TEXT("file_compensation_evidence"), Arguments, ResultJson));
		TestTrue(TEXT("覆盖写入返回工具原始失败"), ResultJson.Contains(TEXT("synthetic failure")));
		FString RestoredContent;
		TestTrue(TEXT("补偿后原始文件可读取"), FFileHelper::LoadFileToString(RestoredContent, *FullPath));
		TestEqual(TEXT("补偿恢复原始文件字节内容"), RestoredContent, FString(TEXT("original")));

		TestTrue(TEXT("删除原始文件以测试新建文件补偿"), IFileManager::Get().Delete(*FullPath, false, true, true));
		TestTrue(TEXT("新建写入失败进入补偿事务"), Service.Execute(TEXT("file_compensation_evidence"), Arguments, ResultJson));
		TestFalse(TEXT("补偿删除本次调用新建的文件"), IFileManager::Get().FileExists(*FullPath));
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPAssetImportCompensationEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.AssetImportCompensation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPAssetImportCompensationEvidenceTest::RunTest(const FString& Parameters)
	{
		using namespace Execution;
		(void)Parameters;

		FScopedTestRun Run(*this);
		const FString AssetName = TEXT("MCP_CompensatingImport_") + Run.GetRunId();
		const FString SourcePath = FPaths::Combine(Run.GetSavedRoot(), AssetName + TEXT(".bmp"));
		const FString DestinationPath = TEXT("/Game/UnrealAgentAutomation/Compensation_") + Run.GetRunId();
		const FString AssetPath = DestinationPath + TEXT("/") + AssetName + TEXT(".") + AssetName;
		Run.TrackFile(SourcePath);

		TArray<uint8> Bitmap = { 0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
			0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xFF, 0x00 };
		TestTrue(TEXT("创建资产导入补偿测试位图"), FFileHelper::SaveArrayToFile(Bitmap, *SourcePath));

		Run.Defer(TEXT("清理资产导入补偿测试夹具"),
			[AssetPath]()
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
				if (!AssetData.IsValid())
				{
					return true;
				}
				UObject* Asset = AssetData.GetAsset();
				return Asset && ObjectTools::DeleteObjectsUnchecked({ Asset }) == 1;
			});

		FMcpToolRuntimeRegistry Registry;
		FMcpToolDescriptor Descriptor;
		Descriptor.Provider = TEXT("Automation");
		Descriptor.Toolset = TEXT("Tests.Evidence");
		Descriptor.Name = TEXT("asset_import_compensation_evidence");
		Descriptor.QualifiedName = TEXT("worlddata.tests.asset_import_compensation_evidence");
		Descriptor.Description = TEXT("真实资产导入补偿测试工具。");
		Descriptor.InputSchema = MakeShared<FJsonObject>();
		Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
		Descriptor.OutputSchema = MakeShared<FJsonObject>();
		Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.Risk = EMcpToolRisk::ContentMutation;
		Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
		Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
		Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
		{
			OutContract.CanonicalToolId = TEXT("worlddata.tests.asset_import_compensation_evidence");
			OutContract.Risk = EMcpToolRisk::ContentMutation;
			OutContract.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
			return true;
		};
		Descriptor.Invoker = BindInvocationContext(
			[](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext&)
			{
				FUnrealAgentMCPUnrealAssetAdapter Adapter;
				const FString ImportResult = Adapter.ExecuteAction(TEXT("import_texture"), Arguments);
				if (!ImportResult.Contains(TEXT("\"success\":true")) && !ImportResult.Contains(TEXT("\"success\": true")))
				{
					return ImportResult;
				}
				return FString(TEXT("{\"success\":false,\"error\":\"synthetic import failure\"}"));
			});

		FString Error;
		TestTrue(TEXT("资产导入补偿测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
		FMcpToolExecutionService Service(Registry);
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("sourcePath"), SourcePath);
		Arguments->SetStringField(TEXT("destinationPath"), DestinationPath);
		FString ResultJson;
		TestTrue(TEXT("资产导入失败进入补偿事务"), Service.Execute(TEXT("asset_import_compensation_evidence"), Arguments, ResultJson));
		TestTrue(TEXT("资产导入返回工具原始失败"), ResultJson.Contains(TEXT("synthetic import failure")));

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData RemainingAsset = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		TestFalse(TEXT("补偿后资产注册表不再包含本次导入资产"), RemainingAsset.IsValid());
		TestNull(TEXT("补偿后内存中不再存在本次导入资产"), FindObject<UObject>(nullptr, *AssetPath));

		Arguments->SetBoolField(TEXT("replaceExisting"), true);
		TestTrue(TEXT("覆盖式资产导入稳定返回"), Service.Execute(TEXT("asset_import_compensation_evidence"), Arguments, ResultJson));
		TestTrue(TEXT("Compensating 导入明确拒绝覆盖模式"), ResultJson.Contains(TEXT("replaceExisting=true")));
		TestNull(TEXT("覆盖模式拒绝没有创建资产"), FindObject<UObject>(nullptr, *AssetPath));
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPTableImportCompensationEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.TableImportCompensation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPTableImportCompensationEvidenceTest::RunTest(const FString& Parameters)
	{
		using namespace Execution;
		(void)Parameters;

		FScopedTestRun Run(*this);
		const FString AssetName = TEXT("ST_Compensation_") + Run.GetRunId();
		const FString AssetPath = TEXT("/Game/UnrealAgentAutomation/") + AssetName + TEXT(".") + AssetName;
		FUnrealAgentMCPUnrealAssetAdapter SetupAdapter;
		TSharedRef<FJsonObject> CreateArguments = MakeShared<FJsonObject>();
		CreateArguments->SetStringField(TEXT("assetPath"), AssetPath);
		const FString CreateResult = SetupAdapter.ExecuteAction(TEXT("create_stringtable"), CreateArguments);
		if (!TestTrue(TEXT("创建 StringTable 补偿测试资产"), CreateResult.Contains(TEXT("\"success\":true"))))
		{
			return Run.Close();
		}

		Run.Defer(TEXT("清理 StringTable 补偿测试资产"),
			[AssetPath]()
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
				if (!AssetData.IsValid())
				{
					return true;
				}
				UObject* Asset = AssetData.GetAsset();
				return Asset && ObjectTools::DeleteObjectsUnchecked({ Asset }) == 1;
			});

		TSharedRef<FJsonObject> InitialArguments = MakeShared<FJsonObject>();
		InitialArguments->SetStringField(TEXT("assetPath"), AssetPath);
		InitialArguments->SetStringField(TEXT("key"), TEXT("Greeting"));
		InitialArguments->SetStringField(TEXT("value"), TEXT("原始值"));
		const FString InitialResult = SetupAdapter.ExecuteAction(TEXT("set_stringtable_entry"), InitialArguments);
		TestTrue(TEXT("写入 StringTable 原始值"), InitialResult.Contains(TEXT("\"success\":true")));

		FMcpToolRuntimeRegistry Registry;
		FMcpToolDescriptor Descriptor;
		Descriptor.Provider = TEXT("Automation");
		Descriptor.Toolset = TEXT("Tests.Evidence");
		Descriptor.Name = TEXT("table_import_compensation_evidence");
		Descriptor.QualifiedName = TEXT("worlddata.tests.table_import_compensation_evidence");
		Descriptor.Description = TEXT("真实表格导入补偿测试工具。");
		Descriptor.InputSchema = MakeShared<FJsonObject>();
		Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
		Descriptor.OutputSchema = MakeShared<FJsonObject>();
		Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.Risk = EMcpToolRisk::ContentMutation;
		Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
		Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
		Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
		{
			OutContract.CanonicalToolId = TEXT("worlddata.tests.table_import_compensation_evidence");
			OutContract.Risk = EMcpToolRisk::ContentMutation;
			OutContract.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
			return true;
		};
		Descriptor.Invoker = BindInvocationContext(
			[](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext&)
			{
				FUnrealAgentMCPUnrealAssetAdapter Adapter;
				const FString ImportResult = Adapter.ExecuteAction(TEXT("import_stringtable"), Arguments);
				if (!ImportResult.Contains(TEXT("\"success\":true")) && !ImportResult.Contains(TEXT("\"success\": true")))
				{
					return ImportResult;
				}
				return FString(TEXT("{\"success\":false,\"error\":\"synthetic table failure\"}"));
			});

		FString Error;
		TestTrue(TEXT("表格导入补偿测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
		FMcpToolExecutionService Service(Registry);
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("assetPath"), AssetPath);
		TSharedRef<FJsonObject> Entries = MakeShared<FJsonObject>();
		Entries->SetStringField(TEXT("Greeting"), TEXT("覆盖值"));
		Entries->SetStringField(TEXT("Added"), TEXT("新增值"));
		Arguments->SetObjectField(TEXT("entries"), Entries);
		FString ResultJson;
		TestTrue(TEXT("StringTable 导入失败进入补偿事务"), Service.Execute(TEXT("table_import_compensation_evidence"), Arguments, ResultJson));
		TestTrue(TEXT("StringTable 导入返回工具原始失败"), ResultJson.Contains(TEXT("synthetic table failure")));

		TSharedRef<FJsonObject> ReadArguments = MakeShared<FJsonObject>();
		ReadArguments->SetStringField(TEXT("assetPath"), AssetPath);
		ReadArguments->SetStringField(TEXT("key"), TEXT("Greeting"));
		const FString RestoredResult = SetupAdapter.ExecuteAction(TEXT("get_stringtable_entry"), ReadArguments);
		TestTrue(TEXT("补偿恢复 StringTable 原始条目"), RestoredResult.Contains(TEXT("原始值")) && !RestoredResult.Contains(TEXT("覆盖值")));
		ReadArguments->SetStringField(TEXT("key"), TEXT("Added"));
		const FString AddedResult = SetupAdapter.ExecuteAction(TEXT("get_stringtable_entry"), ReadArguments);
		TestTrue(TEXT("补偿删除本次导入新增的 StringTable 条目"), AddedResult.Contains(TEXT("\"success\":false")));
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPCGImportCompensationEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.PCGImportCompensation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPPCGImportCompensationEvidenceTest::RunTest(const FString& Parameters)
	{
		using namespace Execution;
		(void)Parameters;

		FScopedTestRun Run(*this);
		const FString GraphName = TEXT("PCG_Compensation_") + Run.GetRunId();
		const FString GraphPath = TEXT("/Game/UnrealAgentAutomation/") + GraphName + TEXT(".") + GraphName;
		TSharedRef<FUnrealAgentMCPUnrealPCGAdapter> SetupAdapter = MakeShared<FUnrealAgentMCPUnrealPCGAdapter>();
		TSharedRef<IUnrealAgentMCPPCGPort> SetupPort = SetupAdapter;
		FUnrealAgentMCPPCGService SetupService(SetupPort);
		TSharedRef<FJsonObject> CreateArguments = MakeShared<FJsonObject>();
		CreateArguments->SetStringField(TEXT("action"), TEXT("create_graph"));
		CreateArguments->SetStringField(TEXT("name"), GraphName);
		CreateArguments->SetStringField(TEXT("packagePath"), TEXT("/Game/UnrealAgentAutomation"));
		const FString CreateResult = SetupService.Execute(CreateArguments);
		if (!TestTrue(TEXT("创建 PCGGraph 补偿测试资产"), CreateResult.Contains(TEXT("\"success\":true"))))
		{
			return Run.Close();
		}

		Run.Defer(TEXT("清理 PCGGraph 补偿测试资产"),
			[GraphPath]()
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(GraphPath));
				if (!AssetData.IsValid())
				{
					return true;
				}
				UObject* Asset = AssetData.GetAsset();
				return Asset && ObjectTools::DeleteObjectsUnchecked({ Asset }) == 1;
			});

		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath);
		if (!TestNotNull(TEXT("加载 PCGGraph 补偿测试资产"), Graph))
		{
			return Run.Close();
		}
		const int32 InitialNodeCount = Graph->GetNodes().Num();

		FMcpToolRuntimeRegistry Registry;
		FMcpToolDescriptor Descriptor;
		Descriptor.Provider = TEXT("Automation");
		Descriptor.Toolset = TEXT("Tests.Evidence");
		Descriptor.Name = TEXT("pcg_import_compensation_evidence");
		Descriptor.QualifiedName = TEXT("worlddata.tests.pcg_import_compensation_evidence");
		Descriptor.Description = TEXT("真实 PCGGraph 导入补偿测试工具。");
		Descriptor.InputSchema = MakeShared<FJsonObject>();
		Descriptor.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), true);
		Descriptor.OutputSchema = MakeShared<FJsonObject>();
		Descriptor.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
		Descriptor.Risk = EMcpToolRisk::ContentMutation;
		Descriptor.ThreadPolicy = EMcpToolThreadPolicy::GameThread;
		Descriptor.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
		Descriptor.ActionContractResolver = [](const TSharedPtr<FJsonObject>&, FMcpResolvedToolContract& OutContract)
		{
			OutContract.CanonicalToolId = TEXT("worlddata.tests.pcg_import_compensation_evidence");
			OutContract.Risk = EMcpToolRisk::ContentMutation;
			OutContract.TransactionPolicy = EMcpToolTransactionPolicy::Compensating;
			return true;
		};
		Descriptor.Invoker = BindInvocationContext(
			[](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext&)
			{
				TSharedRef<FUnrealAgentMCPUnrealPCGAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealPCGAdapter>();
				TSharedRef<IUnrealAgentMCPPCGPort> Port = Adapter;
				FUnrealAgentMCPPCGService PCGService(Port);
				Arguments->SetStringField(TEXT("action"), TEXT("import_graph"));
				const FString ImportResult = PCGService.Execute(Arguments);
				if (!ImportResult.Contains(TEXT("\"success\":true")) && !ImportResult.Contains(TEXT("\"success\": true")))
				{
					return ImportResult;
				}
				return FString(TEXT("{\"success\":false,\"error\":\"synthetic pcg failure\"}"));
			});

		FString Error;
		TestTrue(TEXT("PCGGraph 导入补偿测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
		FMcpToolExecutionService Service(Registry);
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("assetPath"), GraphPath);
		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("name"), TEXT("CompensatedNode"));
		Node->SetStringField(TEXT("class"), TEXT("PCGTransformPointsSettings"));
		Arguments->SetArrayField(TEXT("nodes"), { MakeShared<FJsonValueObject>(Node) });
		Arguments->SetArrayField(TEXT("connections"), {});
		Arguments->SetBoolField(TEXT("replace"), false);
		FString ResultJson;
		TestTrue(TEXT("PCGGraph 导入失败进入补偿事务"), Service.Execute(TEXT("pcg_import_compensation_evidence"), Arguments, ResultJson));
		TestTrue(TEXT("PCGGraph 导入返回工具原始失败"), ResultJson.Contains(TEXT("synthetic pcg failure")));
		TestEqual(TEXT("补偿恢复 PCGGraph 节点数量"), Graph->GetNodes().Num(), InitialNodeCount);

		TSharedRef<FJsonObject> ExportArguments = MakeShared<FJsonObject>();
		ExportArguments->SetStringField(TEXT("action"), TEXT("export_graph"));
		ExportArguments->SetStringField(TEXT("assetPath"), GraphPath);
		const FString ExportResult = SetupService.Execute(ExportArguments);
		TestFalse(TEXT("补偿后的持久化图谱不包含导入节点"), ExportResult.Contains(TEXT("CompensatedNode")));
		TestFalse(TEXT("补偿后的 PCGGraph 包已经重新保存"), Graph->GetOutermost()->IsDirty());
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPMaterialImportCompensationEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.MaterialImportCompensation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPMaterialImportCompensationEvidenceTest::RunTest(const FString& Parameters)
	{
		using namespace Execution;
		(void)Parameters;

		FScopedTestRun Run(*this);
		const FString MaterialName = TEXT("M_Compensation_") + Run.GetRunId();
		const FString MaterialPath = TEXT("/Game/UnrealAgentAutomation/") + MaterialName + TEXT(".") + MaterialName;
		TSharedRef<FUnrealAgentMCPUnrealMaterialAdapter> SetupAdapter = MakeShared<FUnrealAgentMCPUnrealMaterialAdapter>();
		FUnrealAgentMCPUnrealMaterialAdapter& SetupService = SetupAdapter.Get();
		TSharedRef<FJsonObject> CreateArguments = MakeShared<FJsonObject>();
		CreateArguments->SetStringField(TEXT("action"), TEXT("create_simple"));
		CreateArguments->SetStringField(TEXT("name"), MaterialName);
		CreateArguments->SetStringField(TEXT("packagePath"), TEXT("/Game/UnrealAgentAutomation"));
		const FString CreateResult = SetupService.Execute(CreateArguments);
		if (!TestTrue(TEXT("创建材质图补偿测试资产"), CreateResult.Contains(TEXT("\"success\":true"))))
		{
			return Run.Close();
		}

		Run.Defer(TEXT("清理材质图补偿测试资产"),
			[MaterialPath]()
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(MaterialPath));
				if (!AssetData.IsValid())
				{
					return true;
				}
				UObject* Asset = AssetData.GetAsset();
				return Asset && ObjectTools::DeleteObjectsUnchecked({ Asset }) == 1;
			});

		UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
		if (!TestNotNull(TEXT("加载材质图补偿测试资产"), Material))
		{
			return Run.Close();
		}
		const int32 InitialExpressionCount = UMaterialEditingLibrary::GetNumMaterialExpressions(Material);

		FMcpToolRuntimeRegistry Registry;
		FMcpToolDescriptor Descriptor = MakeCompensatingEvidenceDescriptor(TEXT("material_import_compensation_evidence"), TEXT("真实材质图导入补偿测试工具。"),
			BindInvocationContext(
				[](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext&)
				{
					TSharedRef<FUnrealAgentMCPUnrealMaterialAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealMaterialAdapter>();
					FUnrealAgentMCPUnrealMaterialAdapter& MaterialAdapter = Adapter.Get();
					Arguments->SetStringField(TEXT("action"), TEXT("import_graph"));
					const FString ImportResult = MaterialAdapter.Execute(Arguments);
					if (!ImportResult.Contains(TEXT("\"success\":true")) && !ImportResult.Contains(TEXT("\"success\": true")))
					{
						return ImportResult;
					}
					TSharedRef<FJsonObject> ExportArguments = MakeShared<FJsonObject>();
					ExportArguments->SetStringField(TEXT("action"), TEXT("export_graph"));
					ExportArguments->SetStringField(TEXT("assetPath"), Arguments->GetStringField(TEXT("assetPath")));
					const FString ImportedGraph = MaterialAdapter.Execute(ExportArguments);
					if (!ImportedGraph.Contains(TEXT("CompensatedMaterialExpression")))
					{
						return FString(TEXT("{\"success\":false,\"error\":"
											"\"material import did not mutate graph\"}"));
					}
					return FString(TEXT("{\"success\":false,\"error\":"
										"\"synthetic material failure\"}"));
				}));

		FString Error;
		TestTrue(TEXT("材质图导入补偿测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
		FMcpToolExecutionService Service(Registry);
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("assetPath"), MaterialPath);
		TSharedRef<FJsonObject> Node = MakeShared<FJsonObject>();
		Node->SetStringField(TEXT("id"), TEXT("compensated"));
		Node->SetStringField(TEXT("expressionType"), TEXT("Constant3Vector"));
		Node->SetStringField(TEXT("name"), TEXT("CompensatedMaterialExpression"));
		Node->SetArrayField(TEXT("value"), { MakeShared<FJsonValueNumber>(0.8), MakeShared<FJsonValueNumber>(0.2), MakeShared<FJsonValueNumber>(0.1) });
		Arguments->SetArrayField(TEXT("nodes"), { MakeShared<FJsonValueObject>(Node) });
		Arguments->SetArrayField(TEXT("connections"), {});
		TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
		Output->SetStringField(TEXT("source"), TEXT("compensated"));
		Output->SetStringField(TEXT("property"), TEXT("BaseColor"));
		Arguments->SetArrayField(TEXT("propertyConnections"), { MakeShared<FJsonValueObject>(Output) });
		FString ResultJson;
		TestTrue(TEXT("材质图导入失败进入补偿事务"), Service.Execute(TEXT("material_import_compensation_evidence"), Arguments, ResultJson));
		TestTrue(TEXT("材质图导入返回工具原始失败"), ResultJson.Contains(TEXT("synthetic material failure")));
		TestEqual(TEXT("补偿恢复材质表达式数量"), UMaterialEditingLibrary::GetNumMaterialExpressions(Material), InitialExpressionCount);

		TSharedRef<FJsonObject> ExportArguments = MakeShared<FJsonObject>();
		ExportArguments->SetStringField(TEXT("action"), TEXT("export_graph"));
		ExportArguments->SetStringField(TEXT("assetPath"), MaterialPath);
		const FString ExportResult = SetupService.Execute(ExportArguments);
		TestFalse(TEXT("补偿后的持久化材质图不包含导入表达式"), ExportResult.Contains(TEXT("CompensatedMaterialExpression")));
		TestFalse(TEXT("补偿后的材质包已经重新保存"), Material->GetOutermost()->IsDirty());
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPBlueprintImportCompensationEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.BlueprintImportCompensation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPBlueprintImportCompensationEvidenceTest::RunTest(const FString& Parameters)
	{
		using namespace Execution;
		(void)Parameters;

		FScopedTestRun Run(*this);
		const FString BlueprintName = TEXT("BP_Compensation_") + Run.GetRunId();
		const FString BlueprintPath = TEXT("/Game/UnrealAgentAutomation/") + BlueprintName + TEXT(".") + BlueprintName;
		TSharedRef<FUnrealAgentMCPUnrealBlueprintAdapter> SetupAdapter = MakeShared<FUnrealAgentMCPUnrealBlueprintAdapter>();
		TSharedRef<IUnrealAgentMCPBlueprintPort> SetupPort = SetupAdapter;
		FUnrealAgentMCPBlueprintService SetupService(SetupPort);
		TSharedRef<FJsonObject> CreateArguments = MakeShared<FJsonObject>();
		CreateArguments->SetStringField(TEXT("action"), TEXT("create"));
		CreateArguments->SetStringField(TEXT("assetPath"), BlueprintPath);
		CreateArguments->SetStringField(TEXT("parentClass"), TEXT("Actor"));
		const FString CreateResult = SetupService.Execute(CreateArguments);
		if (!TestTrue(TEXT("创建 Blueprint 节点补偿测试资产"), CreateResult.Contains(TEXT("\"success\":true"))))
		{
			return Run.Close();
		}

		Run.Defer(TEXT("清理 Blueprint 节点补偿测试资产"),
			[BlueprintPath]()
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(BlueprintPath));
				if (!AssetData.IsValid())
				{
					return true;
				}
				UObject* Asset = AssetData.GetAsset();
				return Asset && ObjectTools::DeleteObjectsUnchecked({ Asset }) == 1;
			});

		TSharedRef<FJsonObject> AddArguments = MakeShared<FJsonObject>();
		AddArguments->SetStringField(TEXT("action"), TEXT("add_node"));
		AddArguments->SetStringField(TEXT("assetPath"), BlueprintPath);
		AddArguments->SetStringField(TEXT("nodeClass"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
		const FString AddResult = SetupService.Execute(AddArguments);
		TSharedPtr<FJsonObject> AddResultObject;
		const TSharedRef<TJsonReader<>> AddResultReader = TJsonReaderFactory<>::Create(AddResult);
		const TSharedPtr<FJsonObject>* AddedNode = nullptr;
		FString NodeId;
		const bool bReadNodeId = FJsonSerializer::Deserialize(AddResultReader, AddResultObject) && AddResultObject.IsValid() &&
			AddResultObject->TryGetObjectField(TEXT("node"), AddedNode) && AddedNode && AddedNode->IsValid() && (*AddedNode)->TryGetStringField(TEXT("nodeId"), NodeId);
		if (!TestTrue(TEXT("创建并解析 Blueprint 源节点"), bReadNodeId))
		{
			return Run.Close();
		}

		TSharedRef<FJsonObject> ExportArguments = MakeShared<FJsonObject>();
		ExportArguments->SetStringField(TEXT("action"), TEXT("export_nodes_t3d"));
		ExportArguments->SetStringField(TEXT("assetPath"), BlueprintPath);
		ExportArguments->SetStringField(TEXT("nodeId"), NodeId);
		const FString ExportResult = SetupService.Execute(ExportArguments);
		FString NodesText;
		if (!TestTrue(TEXT("导出 Blueprint 源节点 T3D"), TryReadJsonStringField(ExportResult, TEXT("text"), NodesText) && !NodesText.IsEmpty()))
		{
			return Run.Close();
		}

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!TestNotNull(TEXT("加载 Blueprint 节点补偿测试资产"), Blueprint))
		{
			return Run.Close();
		}
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		UEdGraph* EventGraph = nullptr;
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName().Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
			{
				EventGraph = Graph;
				break;
			}
		}
		if (!TestNotNull(TEXT("定位 Blueprint EventGraph"), EventGraph))
		{
			return Run.Close();
		}
		const int32 InitialNodeCount = EventGraph->Nodes.Num();

		FMcpToolRuntimeRegistry Registry;
		FMcpToolDescriptor Descriptor = MakeCompensatingEvidenceDescriptor(TEXT("blueprint_import_compensation_evidence"), TEXT("真实 Blueprint 节点导入补偿测试工具。"),
			BindInvocationContext(
				[EventGraph, InitialNodeCount](const TSharedPtr<FJsonObject>& Arguments, const FMcpTaskExecutionContext&)
				{
					TSharedRef<FUnrealAgentMCPUnrealBlueprintAdapter> Adapter = MakeShared<FUnrealAgentMCPUnrealBlueprintAdapter>();
					TSharedRef<IUnrealAgentMCPBlueprintPort> Port = Adapter;
					FUnrealAgentMCPBlueprintService BlueprintService(Port);
					Arguments->SetStringField(TEXT("action"), TEXT("import_nodes_t3d"));
					const FString ImportResult = BlueprintService.Execute(Arguments);
					if (!ImportResult.Contains(TEXT("\"success\":true")) && !ImportResult.Contains(TEXT("\"success\": true")))
					{
						return ImportResult;
					}
					if (!EventGraph || EventGraph->Nodes.Num() <= InitialNodeCount)
					{
						return FString(TEXT("{\"success\":false,\"error\":"
											"\"blueprint import did not mutate graph\"}"));
					}
					return FString(TEXT("{\"success\":false,\"error\":"
										"\"synthetic blueprint failure\"}"));
				}));

		FString Error;
		TestTrue(TEXT("Blueprint 节点导入补偿测试工具注册成功"), Registry.RegisterDescriptor(MoveTemp(Descriptor), Error));
		FMcpToolExecutionService Service(Registry);
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("assetPath"), BlueprintPath);
		Arguments->SetStringField(TEXT("graphName"), TEXT("EventGraph"));
		Arguments->SetStringField(TEXT("text"), NodesText);
		FString ResultJson;
		TestTrue(TEXT("Blueprint 节点导入失败进入补偿事务"), Service.Execute(TEXT("blueprint_import_compensation_evidence"), Arguments, ResultJson));
		TestTrue(TEXT("Blueprint 节点导入返回工具原始失败"), ResultJson.Contains(TEXT("synthetic blueprint failure")));
		TestEqual(TEXT("补偿恢复 Blueprint EventGraph 节点数量"), EventGraph->Nodes.Num(), InitialNodeCount);
		TestFalse(TEXT("补偿后的 Blueprint 包已经重新保存"), Blueprint->GetOutermost()->IsDirty());
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPActorDeleteUndoEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.ActorDeleteUndo",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPActorDeleteUndoEvidenceTest::RunTest(const FString& Parameters)
	{
		FScopedTestRun Run(*this);
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!TestNotNull(TEXT("编辑器世界可用"), World))
		{
			return false;
		}
		UPackage* Package = World->GetOutermost();
		const bool bWasDirty = Package && Package->IsDirty();
		Run.Defer(TEXT("恢复世界 Dirty 状态"),
			[Package, bWasDirty]()
			{
				if (Package)
					Package->SetDirtyFlag(bWasDirty);
				return true;
			});
		const FString Label = TEXT("MCP_P3_Delete_") + Run.GetRunId();
		AActor* Actor = World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("测试 Actor 创建成功"), Actor))
		{
			return false;
		}
		Actor->SetActorLabel(Label);
		Run.Defer(TEXT("删除测试 Actor"),
			[World, Label]()
			{
				AActor* Existing = FindActorByLabel(World, Label);
				return Existing == nullptr || World->EditorDestroyActor(Existing, true);
			});

		bool bIsError = false;
		TSharedRef<FJsonObject> Rejected = MakeShared<FJsonObject>();
		Rejected->SetStringField(TEXT("name"), Label);
		InvokeTool(*this, TEXT("delete_actor"), Rejected, bIsError);
		TestTrue(TEXT("删除 Actor 缺少确认时拒绝"), bIsError);
		TestNotNull(TEXT("确认拒绝后 Actor 仍存在"), FindActorByLabel(World, Label));

		TSharedRef<FJsonObject> Accepted = MakeShared<FJsonObject>();
		Accepted->SetStringField(TEXT("name"), Label);
		Accepted->SetStringField(TEXT("confirmation"), MutationConfirmation);
		InvokeTool(*this, TEXT("delete_actor"), Accepted, bIsError);
		TestFalse(TEXT("受确认删除 Actor 成功"), bIsError);
		TestNull(TEXT("删除后 Actor 不存在"), FindActorByLabel(World, Label));
		TestTrue(TEXT("删除事务可撤销"), GEditor && GEditor->UndoTransaction());
		TestNotNull(TEXT("Undo 后 Actor 恢复"), FindActorByLabel(World, Label));
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPPythonAuditEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.PythonConfirmationAndAudit",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPPythonAuditEvidenceTest::RunTest(const FString& Parameters)
	{
		FScopedTestRun Run(*this);
		FUnrealAgentMCPPythonExecutionAudit::ResetForTests();
		Run.Defer(TEXT("清空 Python 审计夹具"),
			[]()
			{
				FUnrealAgentMCPPythonExecutionAudit::ResetForTests();
				return true;
			});
		const FString Code = TEXT("assert 2 + 2 == 4");
		bool bIsError = false;
		TSharedRef<FJsonObject> Legacy = MakeShared<FJsonObject>();
		const FString LegacyResult = InvokeTool(*this, TEXT("execute_python"), Legacy, bIsError);
		TestTrue(TEXT("Legacy Python entry is rejected"), bIsError);
		TestTrue(TEXT("Legacy Python entry names the explicit escape hatch"), LegacyResult.Contains(TEXT("execute_python_blocking")));

		TSharedRef<FJsonObject> Rejected = MakeShared<FJsonObject>();
		Rejected->SetStringField(TEXT("code"), Code);
		Rejected->SetStringField(TEXT("unsafe_confirm"), TEXT("no"));
		Rejected->SetNumberField(TEXT("expected_max_ms"), 50.0);
		Rejected->SetStringField(TEXT("task_summary"), TEXT("P3 rejected verification"));
		InvokeTool(*this, TEXT("execute_python_blocking"), Rejected, bIsError);
		TestTrue(TEXT("Python 错误确认被拒绝"), bIsError);
		TestTrue(TEXT("拒绝时不生成 Python 执行审计"), FUnrealAgentMCPPythonExecutionAudit::Snapshot().IsEmpty());

		TSharedRef<FJsonObject> NativeCrashRejected = MakeShared<FJsonObject>();
		NativeCrashRejected->SetStringField(TEXT("code"),
			TEXT("import unreal\nunreal.MaterialEditingLibrary.get_material_property_input_node(None, unreal.MaterialProperty.MP_MAX)"));
		NativeCrashRejected->SetStringField(TEXT("unsafe_confirm"), PythonConfirmation);
		NativeCrashRejected->SetNumberField(TEXT("expected_max_ms"), 50.0);
		NativeCrashRejected->SetStringField(TEXT("task_summary"), TEXT("P3 native crash rejection"));
		const FString NativeCrashResult = InvokeTool(*this, TEXT("execute_python_blocking"), NativeCrashRejected, bIsError);
		TestTrue(TEXT("已知 MaterialEditor 原生崩溃调用被拒绝"), bIsError);
		TestTrue(TEXT("拒绝结果给出结构化工具替代路径"), NativeCrashResult.Contains(TEXT("worlddata.material.read")));
		TestTrue(TEXT("原生崩溃保护拒绝时不生成执行审计"), FUnrealAgentMCPPythonExecutionAudit::Snapshot().IsEmpty());

		TSharedRef<FJsonObject> Accepted = MakeShared<FJsonObject>();
		Accepted->SetStringField(TEXT("code"), Code);
		Accepted->SetStringField(TEXT("unsafe_confirm"), PythonConfirmation);
		Accepted->SetNumberField(TEXT("expected_max_ms"), 50.0);
		Accepted->SetStringField(TEXT("task_summary"), TEXT("P3 benign verification"));
		const FString AcceptedResult = InvokeTool(*this, TEXT("execute_python_blocking"), Accepted, bIsError);
		TestFalse(TEXT("受确认 Python 执行成功"), bIsError);
		TestTrue(TEXT("Blocking Python reports measured duration"), AcceptedResult.Contains(TEXT("duration_ms")));
		const TArray<FPythonExecutionAuditRecord> Records = FUnrealAgentMCPPythonExecutionAudit::Snapshot();
		TestEqual(TEXT("产生一条脱敏审计"), Records.Num(), 1);
		if (Records.Num() == 1)
		{
			TestTrue(TEXT("审计只保存 BLAKE3 指纹"), Records[0].CodeFingerprint.StartsWith(TEXT("blake3:")) && !Records[0].CodeFingerprint.Contains(Code));
			TestEqual(TEXT("审计保存代码字符数"), Records[0].CodeCharacterCount, Code.Len());
			TestEqual(TEXT("审计保存非敏感摘要"), Records[0].TaskSummary, FString(TEXT("P3 benign verification")));
		}
		return Run.Close();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPCallToolAuditEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.CallToolPolicyAndAudit",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPCallToolAuditEvidenceTest::RunTest(const FString& Parameters)
	{
		bool bIsError = false;
		TSharedRef<FJsonObject> Rejected = MakeShared<FJsonObject>();
		Rejected->SetStringField(TEXT("toolset"), TEXT("Missing.Toolset"));
		Rejected->SetStringField(TEXT("tool"), TEXT("missing"));
		const FString RejectedResult = InvokeTool(*this, TEXT("call_tool"), Rejected, bIsError);
		TestTrue(TEXT("反射调用缺少确认时拒绝"), bIsError);
		TestTrue(TEXT("反射调用返回确认策略码"), RejectedResult.Contains(TEXT("confirmation_required")));

		TSharedRef<FJsonObject> Routed = MakeShared<FJsonObject>();
		Routed->SetStringField(TEXT("toolset"), TEXT("UnrealAgentMCP.System"));
		Routed->SetStringField(TEXT("tool"), TEXT("ping"));
		Routed->SetObjectField(TEXT("input"), MakeShared<FJsonObject>());
		const FString RoutedResult = InvokeTool(*this, TEXT("call_tool"), Routed, bIsError);
		TestFalse(TEXT("受确认反射调用真实路由成功"), bIsError);
		TestTrue(TEXT("真实反射调用返回 pong"), RoutedResult.Contains(TEXT("pong")));

		TSharedRef<FJsonObject> Accepted = MakeShared<FJsonObject>();
		Accepted->SetStringField(TEXT("toolset"), TEXT("Missing.Toolset"));
		Accepted->SetStringField(TEXT("tool"), TEXT("missing"));
		Accepted->SetStringField(TEXT("confirmation"), MutationConfirmation);
		InvokeTool(*this, TEXT("call_tool"), Accepted, bIsError);
		TestTrue(TEXT("未知反射目标稳定失败"), bIsError);

		TSharedRef<FJsonObject> AuditArgs = MakeShared<FJsonObject>();
		AuditArgs->SetNumberField(TEXT("maxResults"), 1000);
		const FString Audit = InvokeTool(*this, TEXT("list_tool_audit"), AuditArgs, bIsError);
		TestFalse(TEXT("审计目录可读取"), bIsError);
		TestTrue(TEXT("审计包含反射调用"), Audit.Contains(TEXT("call_tool")));
		TestTrue(TEXT("审计包含确认拒绝决策"), Audit.Contains(TEXT("confirmation_required")));
		TestFalse(TEXT("审计不包含确认密语"), Audit.Contains(MutationConfirmation));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPCancellationDeferredEvidenceTest, "WorldData.UnrealAgent.Evidence.HighRisk.CancellationDeferred",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPCancellationDeferredEvidenceTest::RunTest(const FString& Parameters)
	{
		using namespace Execution;
		FMcpTaskManager Manager(FTimespan::FromMinutes(1));
		FEvent* Started = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* Finish = FPlatformProcess::GetSynchEventFromPool(true);
		ON_SCOPE_EXIT
		{
			Finish->Trigger();
			FPlatformProcess::ReturnSynchEventToPool(Started);
			FPlatformProcess::ReturnSynchEventToPool(Finish);
		};
		FMcpTaskRequest Request;
		Request.ToolName = TEXT("p3_deferred_cancellation");
		Request.Owner = TEXT("Evidence");
		Request.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
		Request.bCancelable = true;
		Request.Work = [Started, Finish](FMcpTaskExecutionContext& Context)
		{
			Started->Trigger();
			Finish->Wait(5000);
			return FMcpTaskWorkResult::Succeeded(TEXT("{\"success\":true}"));
		};
		FGuid TaskId;
		FString SubmitError;
		TestTrue(TEXT("延迟取消任务提交成功"), Manager.Submit(MoveTemp(Request), TaskId, SubmitError));
		TestTrue(TEXT("延迟取消任务已开始"), Started->Wait(5000));
		TestEqual(TEXT("运行中任务收到取消请求"), Manager.Cancel(TaskId, TEXT("P3 cancellation evidence")), EMcpTaskCancelResult::CancellationRequested);
		Finish->Trigger();
		FMcpTaskSnapshot Snapshot;
		const double Deadline = FPlatformTime::Seconds() + 5.0;
		do
		{
			if (Manager.TryRead(TaskId, Snapshot) && Snapshot.IsTerminal())
			{
				break;
			}
			FPlatformProcess::SleepNoStats(0.001f);
		} while (FPlatformTime::Seconds() < Deadline);
		TestEqual(TEXT("忽略取消并完成副作用的任务不能报告 Cancelled"), Snapshot.State, EMcpTaskState::Completed);
		TestTrue(TEXT("任务明确标记 cancellationDeferred"), Snapshot.bCancellationDeferred);
		TestEqual(TEXT("任务明确报告副作用已在取消后完成"), Snapshot.SideEffectState, FString(TEXT("completed_after_cancellation")));

		FEvent* FailedStarted = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* FailedFinish = FPlatformProcess::GetSynchEventFromPool(true);
		ON_SCOPE_EXIT
		{
			FailedFinish->Trigger();
			FPlatformProcess::ReturnSynchEventToPool(FailedStarted);
			FPlatformProcess::ReturnSynchEventToPool(FailedFinish);
		};
		FMcpTaskRequest FailedRequest;
		FailedRequest.ToolName = TEXT("p3_deferred_failed_cancellation");
		FailedRequest.Owner = TEXT("Evidence");
		FailedRequest.ThreadPolicy = EMcpToolThreadPolicy::BackgroundThread;
		FailedRequest.bCancelable = true;
		FailedRequest.Work = [FailedStarted, FailedFinish](FMcpTaskExecutionContext& Context)
		{
			FailedStarted->Trigger();
			FailedFinish->Wait(5000);
			return FMcpTaskWorkResult::Failed(TEXT("independent failure"));
		};
		FGuid FailedTaskId;
		TestTrue(TEXT("忽略取消的失败任务提交成功"), Manager.Submit(MoveTemp(FailedRequest), FailedTaskId, SubmitError));
		TestTrue(TEXT("忽略取消的失败任务已开始"), FailedStarted->Wait(5000));
		TestEqual(TEXT("失败任务收到取消请求"), Manager.Cancel(FailedTaskId, TEXT("P3 failed cancellation evidence")), EMcpTaskCancelResult::CancellationRequested);
		FailedFinish->Trigger();
		const double FailedDeadline = FPlatformTime::Seconds() + 5.0;
		do
		{
			if (Manager.TryRead(FailedTaskId, Snapshot) && Snapshot.IsTerminal())
			{
				break;
			}
			FPlatformProcess::SleepNoStats(0.001f);
		} while (FPlatformTime::Seconds() < FailedDeadline);
		TestEqual(TEXT("未观察取消的失败任务保持 Failed"), Snapshot.State, EMcpTaskState::Failed);
		TestTrue(TEXT("未观察取消的失败任务标记 cancellationDeferred"), Snapshot.bCancellationDeferred);
		TestEqual(TEXT("失败任务报告取消后的真实副作用状态"), Snapshot.SideEffectState, FString(TEXT("failed_after_cancellation")));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPOrphanCleanupEvidenceTest, "WorldData.UnrealAgent.Evidence.ZCleanup.OrphanScan",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPOrphanCleanupEvidenceTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Orphans = FScopedTestRun::FindOrphanManifests();
		TestTrue(*FString::Printf(TEXT("无测试 orphan：%s"), *FString::Join(Orphans, TEXT(" | "))), Orphans.IsEmpty());
		return true;
	}
}

#endif
