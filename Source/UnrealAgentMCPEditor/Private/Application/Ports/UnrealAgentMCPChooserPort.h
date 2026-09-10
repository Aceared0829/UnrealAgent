// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPChooserPort.h
 * @brief ChooserTable 资产编写与引用迁移的稳定应用端口。
 */

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	class IUnrealAgentMCPChooserPort
	{
	public:
		virtual ~IUnrealAgentMCPChooserPort() = default;

		virtual FString Create(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString Describe(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddColumn(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListRows(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString AddRow(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString SetRow(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString DeleteRow(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString ListObjectReferences(const TSharedPtr<FJsonObject>& Args) = 0;
		virtual FString RemapObjectReferences(const TSharedPtr<FJsonObject>& Args) = 0;
	};
}
