// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPFabMigrationTests.cpp
 * @brief Fab 八项迁移能力的缓存、导入和动态模块黑盒测试。
 */

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Adapters/Unreal/Fab/UnrealAgentMCPUnrealFabAdapter.h"
#include "Application/Domains/Fab/UnrealAgentMCPFabService.h"
#include "Application/Ports/UnrealAgentMCPFabPort.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAgentMCP::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPFabMigrationIntegrationTest, "WorldData.UnrealAgent.Fab.MigratedActionsIntegration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPFabMigrationIntegrationTest::RunTest(const FString& Parameters)
	{
		AddExpectedError(TEXT("EOS_Auth_Logout - One or more parameters are null"), EAutomationExpectedErrorFlags::Contains, -1, false);
		AddExpectedError(TEXT("EOS_Auth_Logout failed"), EAutomationExpectedErrorFlags::Contains, -1, false);
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString OldOverride = FPlatformMisc::GetEnvironmentVariable(TEXT("UEBRIDGEMCP_FAB_CACHE_ROOT"));
		const FString TestCacheRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("FabCacheTests"), Suffix));
		FPlatformMisc::SetEnvironmentVar(TEXT("UEBRIDGEMCP_FAB_CACHE_ROOT"), *TestCacheRoot);
		IFileManager::Get().MakeDirectory(*TestCacheRoot, true);

		const FString AssetName = TEXT("Fab_") + Suffix;
		const FString SourcePath = FPaths::Combine(TestCacheRoot, AssetName + TEXT(".bmp"));
		TArray<uint8> Bitmap = { 0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
			0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xFF, 0x00 };
		TestTrue(TEXT("测试位图写入独立缓存"), FFileHelper::SaveArrayToFile(Bitmap, *SourcePath));

		TSharedRef<IUnrealAgentMCPFabPort> Port = MakeShared<FUnrealAgentMCPUnrealFabAdapter>();
		FUnrealAgentMCPFabService Service(Port);
		auto Execute = [&Service](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			Arguments->SetStringField(TEXT("action"), Action);
			return Service.Execute(Arguments);
		};
		auto ExecuteSuccess = [&Execute, this](const FString& Action, const TSharedRef<FJsonObject>& Arguments)
		{
			const FString Result = Execute(Action, Arguments);
			TestTrue(*FString::Printf(TEXT("%s 返回成功结果：%s"), *Action, *Result.Left(1200)), Result.Contains(TEXT("\"success\":true")));
			return Result;
		};

		const FString StatusResult = ExecuteSuccess(TEXT("status"), MakeShared<FJsonObject>());
		TestTrue(TEXT("状态同时报告 Fab 模块和独立导入能力"),
			StatusResult.Contains(TEXT("pluginAvailable")) && StatusResult.Contains(TEXT("\"nativeImportAvailable\":true")) && StatusResult.Contains(TestCacheRoot));

		for (const FString& SessionAction : { TEXT("login"), TEXT("logout"), TEXT("sync_library") })
		{
			const FString Result = Execute(SessionAction, MakeShared<FJsonObject>());
			TestFalse(*FString::Printf(TEXT("%s 已进入动态 Fab 适配器而非未迁移分支"), *SessionAction), Result.Contains(TEXT("尚未迁移")));
			TestTrue(*FString::Printf(TEXT("%s 返回成功或明确的插件能力原因"), *SessionAction),
				Result.Contains(TEXT("\"success\":true")) || Result.Contains(TEXT("\"success\":false")));
		}

		const FString CacheInfo = ExecuteSuccess(TEXT("cache_info"), MakeShared<FJsonObject>());
		const FString CachedList = ExecuteSuccess(TEXT("list_cached"), MakeShared<FJsonObject>());
		TestTrue(TEXT("缓存统计与目录枚举包含测试源文件"), CacheInfo.Contains(TEXT("\"entryCount\":1")) && CachedList.Contains(AssetName + TEXT(".bmp")));

		TSharedRef<FJsonObject> ImportArgs = MakeShared<FJsonObject>();
		ImportArgs->SetStringField(TEXT("source"), SourcePath);
		ImportArgs->SetStringField(TEXT("destination"), TEXT("/Game/UnrealAgentTests"));
		const FString ImportResult = ExecuteSuccess(TEXT("import_file"), ImportArgs);
		const FString ImportedPath = TEXT("/Game/UnrealAgentTests/") + AssetName + TEXT(".") + AssetName;
		UObject* Imported = LoadObject<UObject>(nullptr, *ImportedPath);
		TestTrue(TEXT("本地文件通过 AssetTools 生成真实纹理资产"), ImportResult.Contains(ImportedPath) && Imported != nullptr);

		const FString ClearResult = ExecuteSuccess(TEXT("clear_cache"), MakeShared<FJsonObject>());
		TestTrue(TEXT("缓存清理只作用于测试专用 Saved 子目录"), ClearResult.Contains(TEXT("\"removedEntries\":1")) && !IFileManager::Get().FileExists(*SourcePath));

		if (Imported)
		{
			TArray<UObject*> ObjectsToDelete = { Imported };
			ObjectTools::DeleteObjectsUnchecked(ObjectsToDelete);
		}
		IFileManager::Get().DeleteDirectory(*TestCacheRoot, false, true);
		FPlatformMisc::SetEnvironmentVar(TEXT("UEBRIDGEMCP_FAB_CACHE_ROOT"), *OldOverride);
		return true;
	}
}

#endif
