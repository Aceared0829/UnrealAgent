// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.h
 * @brief 使用 Unreal 公共编辑器 API 实现资产领域 Port。
 */

#include "Application/Ports/UnrealAgentMCPAssetPort.h"

struct FAssetData;
class FJsonObject;
class UEditorAssetSubsystem;
class UObject;

namespace UnrealAgentMCP
{
	class FUnrealAgentMCPUnrealAssetAdapter final : public IUnrealAgentMCPAssetPort
	{
	public:
		virtual FString ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args) override;

		/** 参数别名归一化辅助函数，供同一 Adapter 的拆分实现文件复用。 */
		static UEditorAssetSubsystem* GetAssetSubsystem();
		static FString GetStringArgument(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names);
		static TArray<FString> GetStringArrayArgument(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names);
		static FString NormalizeAssetPath(const FString& Path);
		static FString ToPackageName(const FString& Path);
		static TSharedRef<FJsonObject> MakeAssetJson(const FAssetData& Asset);

	private:
		FString Discover(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Inspect(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Mutate(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString QueryRegistry(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Tables(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Textures(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Meshes(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Definitions(const FString& Action, const TSharedPtr<FJsonObject>& Args);
		FString Advanced(const FString& Action, const TSharedPtr<FJsonObject>& Args);
	};
}
