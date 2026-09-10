#pragma once

/**
 * @file UnrealAgentMCPTestRun.h
 * @brief 为真实副作用自动化提供 RunId、逆序清理栈与 orphan 清单。
 */

#include "CoreMinimal.h"

class FAutomationTestBase;

namespace UnrealAgentMCP::Tests
{
	class FScopedTestRun
	{
	public:
		explicit FScopedTestRun(FAutomationTestBase& InTest);
		~FScopedTestRun();

		const FString& GetRunId() const;
		const FString& GetSavedRoot() const;
		FString MakeRelativeProjectPath(const FString& Leaf) const;

		void Defer(FString Label, TFunction<bool()> Cleanup);
		void TrackFile(const FString& Path);
		void TrackDirectory(const FString& Path);
		bool Close();

		static TArray<FString> FindOrphanManifests();

	private:
		struct FCleanupEntry
		{
			FString Label;
			TFunction<bool()> Cleanup;
		};

		void WriteOrphanManifest(const TArray<FString>& Failures) const;

		FAutomationTestBase& Test;
		FString RunId;
		FString SavedRoot;
		TArray<FCleanupEntry> Entries;
		bool bClosed = false;
	};
}
