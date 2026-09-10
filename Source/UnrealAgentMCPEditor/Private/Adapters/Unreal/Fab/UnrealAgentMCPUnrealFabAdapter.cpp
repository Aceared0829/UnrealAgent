// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealFabAdapter.cpp
 * @brief Fab 动态会话入口、独立缓存和本地文件导入实现。
 */

#include "Adapters/Unreal/Fab/UnrealAgentMCPUnrealFabAdapter.h"

#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Infrastructure/Transactions/UnrealAgentMCPAssetImportCompensation.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool GFabWindowOpened = false;

		struct FFabCacheStats
		{
			int64 TotalBytes = 0;
			int32 FileCount = 0;
			TArray<FString> Files;
		};

		FFabCacheStats ScanFabCache(const FString& Root)
		{
			FFabCacheStats Stats;
			if (!IFileManager::Get().DirectoryExists(*Root))
			{
				return Stats;
			}
			IFileManager::Get().IterateDirectoryRecursively(*Root,
				[&Stats](const TCHAR* Filename, const bool bDirectory)
				{
					if (!bDirectory)
					{
						Stats.Files.Add(Filename);
						Stats.TotalBytes += IFileManager::Get().FileSize(Filename);
						++Stats.FileCount;
					}
					return true;
				});
			Stats.Files.Sort();
			return Stats;
		}

		FString FormatBytes(const int64 Bytes)
		{
			if (Bytes >= 1024LL * 1024LL * 1024LL)
			{
				return FString::Printf(TEXT("%.2f GB"), static_cast<double>(Bytes) / (1024.0 * 1024.0 * 1024.0));
			}
			if (Bytes >= 1024LL * 1024LL)
			{
				return FString::Printf(TEXT("%.2f MB"), static_cast<double>(Bytes) / (1024.0 * 1024.0));
			}
			if (Bytes >= 1024LL)
			{
				return FString::Printf(TEXT("%.2f KB"), static_cast<double>(Bytes) / 1024.0);
			}
			return FString::Printf(TEXT("%lld B"), Bytes);
		}

		bool ExecuteFabConsoleCommand(const TArray<FString>& Names, const FString& Suffix, FString& OutCommand)
		{
			for (const FString& Name : Names)
			{
				if (IConsoleManager::Get().FindConsoleObject(*Name))
				{
					OutCommand = Name + Suffix;
					return GEngine && GEngine->Exec(nullptr, *OutCommand);
				}
			}
			return false;
		}
	}

	FString FUnrealAgentMCPUnrealFabAdapter::GetCacheRoot()
	{
		const FString DefaultRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("FabCache")));
		const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("UEBRIDGEMCP_FAB_CACHE_ROOT"));
		if (!Override.IsEmpty())
		{
			const FString FullOverride = FPaths::ConvertRelativePathToFull(Override);
			const FString SavedRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
			if (FullOverride.StartsWith(SavedRoot, ESearchCase::IgnoreCase))
			{
				return FullOverride;
			}
		}
		return DefaultRoot;
	}

	bool FUnrealAgentMCPUnrealFabAdapter::IsFabPluginAvailable(FString& OutReason)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Fab"));
		if (!Plugin.IsValid())
		{
			OutReason = TEXT("当前引擎安装未发现 Fab 插件。");
			return false;
		}
		if (!Plugin->IsEnabled())
		{
			OutReason = TEXT("Fab 插件已安装但未启用。");
			return false;
		}
		return true;
	}

	FString FUnrealAgentMCPUnrealFabAdapter::Status(const TSharedPtr<FJsonObject>& Args)
	{
		FString Reason;
		const bool bAvailable = IsFabPluginAvailable(Reason);
		bool bModuleLoaded = false;
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Fab")); Plugin.IsValid())
		{
			for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
			{
				bModuleLoaded |= FModuleManager::Get().IsModuleLoaded(Module.Name);
			}
		}
		const FString CacheRoot = GetCacheRoot();
		const FFabCacheStats Stats = ScanFabCache(CacheRoot);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("pluginAvailable"), bAvailable);
		Result->SetBoolField(TEXT("moduleLoaded"), bModuleLoaded);
		Result->SetBoolField(TEXT("nativeImportAvailable"), true);
		Result->SetBoolField(TEXT("windowOpenedThisSession"), GFabWindowOpened);
		Result->SetStringField(TEXT("cacheLocation"), CacheRoot);
		Result->SetNumberField(TEXT("cacheSizeBytes"), static_cast<double>(Stats.TotalBytes));
		Result->SetNumberField(TEXT("cacheEntryCount"), Stats.FileCount);
		if (!bAvailable)
		{
			Result->SetStringField(TEXT("reason"), Reason);
		}
		Result->SetBoolField(TEXT("independent"), true);
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealFabAdapter::Login(const TSharedPtr<FJsonObject>& Args)
	{
		FString Reason;
		if (!IsFabPluginAvailable(Reason))
		{
			return ErrorJson(Reason);
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Fab"));
		for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
		{
			FModuleManager::Get().LoadModule(Module.Name);
		}
		const TArray<FName> CandidateTabs = { TEXT("Fab"), TEXT("FabWindow"), TEXT("FabBrowser") };
		for (const FName TabName : CandidateTabs)
		{
			if (FGlobalTabmanager::Get()->HasTabSpawner(TabName) && FGlobalTabmanager::Get()->TryInvokeTab(TabName).IsValid())
			{
				GFabWindowOpened = true;
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("success"), true);
				Result->SetBoolField(TEXT("flowOpened"), true);
				Result->SetStringField(TEXT("tab"), TabName.ToString());
				Result->SetBoolField(TEXT("asynchronous"), true);
				return JsonObjectToString(Result);
			}
		}
		return ErrorJson(TEXT("Fab 模块已加载，但未找到公开的登录窗口入口。"));
	}

	FString FUnrealAgentMCPUnrealFabAdapter::Logout(const TSharedPtr<FJsonObject>& Args)
	{
		FString Reason;
		if (!IsFabPluginAvailable(Reason))
		{
			return ErrorJson(Reason);
		}
		FString Command;
		if (!ExecuteFabConsoleCommand({ TEXT("Fab.Logout"), TEXT("Fab.Auth.Logout") }, TEXT(""), Command))
		{
			return ErrorJson(TEXT("Fab 插件未公开可调用的登出命令。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("command"), Command);
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealFabAdapter::SyncLibrary(const TSharedPtr<FJsonObject>& Args)
	{
		FString Reason;
		if (!IsFabPluginAvailable(Reason))
		{
			return ErrorJson(Reason);
		}
		double RequestedBatch = 100.0;
		Args->TryGetNumberField(TEXT("batchSize"), RequestedBatch);
		const int32 BatchSize = FMath::Clamp(FMath::RoundToInt(RequestedBatch), 1, 1000);
		FString Command;
		if (!ExecuteFabConsoleCommand({ TEXT("Fab.SyncLibrary"), TEXT("Fab.Library.Sync") }, FString::Printf(TEXT(" %d"), BatchSize), Command))
		{
			return ErrorJson(TEXT("Fab 插件未公开可调用的库同步命令。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("asynchronous"), true);
		Result->SetNumberField(TEXT("batchSize"), BatchSize);
		Result->SetStringField(TEXT("command"), Command);
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealFabAdapter::ListCached(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Root = GetCacheRoot();
		IFileManager::Get().MakeDirectory(*Root, true);
		const FFabCacheStats Stats = ScanFabCache(Root);
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FString& File : Stats.Files)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), File);
			Entry->SetStringField(TEXT("relativePath"), File.Mid(Root.Len()).TrimStartAndEnd());
			const int64 Size = IFileManager::Get().FileSize(*File);
			Entry->SetNumberField(TEXT("sizeBytes"), static_cast<double>(Size));
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("cacheLocation"), Root);
		Result->SetNumberField(TEXT("count"), Entries.Num());
		Result->SetArrayField(TEXT("entries"), MoveTemp(Entries));
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealFabAdapter::CacheInfo(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Root = GetCacheRoot();
		IFileManager::Get().MakeDirectory(*Root, true);
		const FFabCacheStats Stats = ScanFabCache(Root);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("cacheLocation"), Root);
		Result->SetNumberField(TEXT("sizeBytes"), static_cast<double>(Stats.TotalBytes));
		Result->SetStringField(TEXT("sizeDisplay"), FormatBytes(Stats.TotalBytes));
		Result->SetNumberField(TEXT("entryCount"), Stats.FileCount);
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealFabAdapter::ClearCache(const TSharedPtr<FJsonObject>& Args)
	{
		const FString Root = GetCacheRoot();
		const FFabCacheStats Before = ScanFabCache(Root);
		if (IFileManager::Get().DirectoryExists(*Root) && !IFileManager::Get().DeleteDirectory(*Root, false, true))
		{
			return ErrorJson(TEXT("Fab 独立缓存目录清理失败。"));
		}
		IFileManager::Get().MakeDirectory(*Root, true);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("cacheLocation"), Root);
		Result->SetNumberField(TEXT("removedEntries"), Before.FileCount);
		Result->SetNumberField(TEXT("removedBytes"), static_cast<double>(Before.TotalBytes));
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealFabAdapter::ImportFile(const TSharedPtr<FJsonObject>& Args)
	{
		FString Source;
		FString Destination;
		Args->TryGetStringField(TEXT("source"), Source);
		Args->TryGetStringField(TEXT("destination"), Destination);
		Source = FPaths::ConvertRelativePathToFull(Source);
		if (!IFileManager::Get().FileExists(*Source))
		{
			return ErrorJson(FString::Printf(TEXT("Fab 导入源文件不存在：%s"), *Source));
		}
		Destination.TrimStartAndEndInline();
		if (Destination.IsEmpty())
		{
			Destination = TEXT("/Game/Fab/Imported");
		}
		if (!Destination.StartsWith(TEXT("/Game")))
		{
			return ErrorJson(TEXT("destination 必须位于 /Game 下。"));
		}
		FUnrealAgentMCPAssetImportCompensation Compensation(Destination);
		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->Filenames = { Source };
		ImportData->DestinationPath = Destination;
		ImportData->bReplaceExisting = false;
		ImportData->bSkipReadOnly = true;
		TArray<UObject*> Imported = FAssetToolsModule::GetModule().Get().ImportAssetsAutomated(ImportData);
		if (Imported.IsEmpty())
		{
			return ErrorJson(TEXT("AssetTools 未生成任何导入资产。"));
		}
		FString CompensationError;
		if (!Compensation.RegisterCreatedAssets(Imported, CompensationError))
		{
			return ErrorJson(CompensationError);
		}
		TArray<TSharedPtr<FJsonValue>> Assets;
		for (UObject* Asset : Imported)
		{
			if (Asset)
			{
				Assets.Add(MakeShared<FJsonValueString>(Asset->GetPathName()));
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("source"), Source);
		Result->SetStringField(TEXT("destination"), Destination);
		Result->SetNumberField(TEXT("importedCount"), Assets.Num());
		Result->SetArrayField(TEXT("assets"), MoveTemp(Assets));
		Result->SetStringField(TEXT("pipeline"), TEXT("Unreal AssetTools automated import"));
		return JsonObjectToString(Result);
	}
}
