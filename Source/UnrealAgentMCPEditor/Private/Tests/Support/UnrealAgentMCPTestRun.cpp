// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPTestRun.cpp
 * @brief 真实副作用自动化的确定性清理与 orphan 证据实现。
 */

#include "Tests/Support/UnrealAgentMCPTestRun.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAgentMCP::Tests
{
	namespace
	{
		FString Serialize(const TSharedRef<FJsonObject>& Object)
		{
			FString Json;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
			FJsonSerializer::Serialize(Object, Writer);
			return Json;
		}
	}

	FScopedTestRun::FScopedTestRun(FAutomationTestBase& InTest)
		: Test(InTest), RunId(FGuid::NewGuid().ToString(EGuidFormats::Digits)),
		  SavedRoot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgentTests"), RunId)))
	{
		IFileManager::Get().MakeDirectory(*SavedRoot, true);
		TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
		State->SetStringField(TEXT("runId"), RunId);
		State->SetStringField(TEXT("state"), TEXT("active"));
		State->SetStringField(TEXT("startedAt"), FDateTime::UtcNow().ToIso8601());
		FFileHelper::SaveStringToFile(Serialize(State), *FPaths::Combine(SavedRoot, TEXT("run-state.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FScopedTestRun::~FScopedTestRun()
	{
		Close();
	}

	const FString& FScopedTestRun::GetRunId() const
	{
		return RunId;
	}

	const FString& FScopedTestRun::GetSavedRoot() const
	{
		return SavedRoot;
	}

	FString FScopedTestRun::MakeRelativeProjectPath(const FString& Leaf) const
	{
		return FPaths::Combine(TEXT("Saved/UnrealAgentTests"), RunId, Leaf);
	}

	void FScopedTestRun::Defer(FString Label, TFunction<bool()> Cleanup)
	{
		FCleanupEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Label = MoveTemp(Label);
		Entry.Cleanup = MoveTemp(Cleanup);
	}

	void FScopedTestRun::TrackFile(const FString& Path)
	{
		Defer(FString::Printf(TEXT("删除文件 %s"), *Path),
			[Path]()
			{
				return !IFileManager::Get().FileExists(*Path) || IFileManager::Get().Delete(*Path, false, true, true);
			});
	}

	void FScopedTestRun::TrackDirectory(const FString& Path)
	{
		Defer(FString::Printf(TEXT("删除目录 %s"), *Path),
			[Path]()
			{
				return !IFileManager::Get().DirectoryExists(*Path) || IFileManager::Get().DeleteDirectory(*Path, false, true);
			});
	}

	bool FScopedTestRun::Close()
	{
		if (bClosed)
		{
			return true;
		}
		bClosed = true;
		TArray<FString> Failures;
		for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
		{
			FCleanupEntry& Entry = Entries[Index];
			if (!Entry.Cleanup || !Entry.Cleanup())
			{
				Failures.Add(Entry.Label);
			}
		}
		if (Failures.IsEmpty() && IFileManager::Get().DirectoryExists(*SavedRoot) && !IFileManager::Get().DeleteDirectory(*SavedRoot, false, true))
		{
			Failures.Add(TEXT("删除 RunId 根目录"));
		}
		if (!Failures.IsEmpty())
		{
			WriteOrphanManifest(Failures);
			Test.AddError(FString::Printf(TEXT("RunId %s 清理失败：%s"), *RunId, *FString::Join(Failures, TEXT(" | "))));
			return false;
		}
		return true;
	}

	void FScopedTestRun::WriteOrphanManifest(const TArray<FString>& Failures) const
	{
		IFileManager::Get().MakeDirectory(*SavedRoot, true);
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Failure : Failures)
		{
			Values.Add(MakeShared<FJsonValueString>(Failure));
		}
		TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
		Manifest->SetStringField(TEXT("runId"), RunId);
		Manifest->SetStringField(TEXT("detectedAt"), FDateTime::UtcNow().ToIso8601());
		Manifest->SetArrayField(TEXT("cleanupFailures"), Values);
		FFileHelper::SaveStringToFile(Serialize(Manifest), *FPaths::Combine(SavedRoot, TEXT("orphan-manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TArray<FString> FScopedTestRun::FindOrphanManifests()
	{
		TArray<FString> Manifests;
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgentTests")));
		IFileManager::Get().FindFilesRecursive(Manifests, *Root, TEXT("orphan-manifest.json"), true, false);
		Manifests.Sort();
		return Manifests;
	}
}
