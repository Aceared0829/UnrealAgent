// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPReflection.h
 * @brief Unreal Agent 自有的反射工具发现、Schema 构建与调用基础设施。
 */

#include "CoreMinimal.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"

class FJsonObject;
class UClass;
class UFunction;

namespace UnrealAgentMCP::Reflection
{
	inline constexpr TCHAR ToolsetMetadata[] = TEXT("UnrealAgentMCPToolset");
	inline constexpr TCHAR ToolMetadata[] = TEXT("UnrealAgentMCPTool");
	inline constexpr TCHAR ToolNameMetadata[] = TEXT("UnrealAgentMCPToolName");
	inline constexpr TCHAR ToolDescriptionMetadata[] = TEXT("UnrealAgentMCPToolDescription");
	inline constexpr TCHAR ToolRiskMetadata[] = TEXT("UnrealAgentMCPRisk");
	inline constexpr TCHAR ToolTransactionMetadata[] = TEXT("UnrealAgentMCPTransaction");
	inline constexpr TCHAR ToolIdempotentMetadata[] = TEXT("UnrealAgentMCPIdempotent");
	inline constexpr TCHAR OptionalParametersMetadata[] = TEXT("UnrealAgentMCPOptional");
	inline constexpr TCHAR DefaultValueMetadataPrefix[] = TEXT("UnrealAgentMCPDefault_");
	inline constexpr TCHAR InternalToolsetMetadata[] = TEXT("UnrealAgentMCPInternal");

	/** 单个 C++ 反射工具函数的不可变描述。 */
	struct UNREALAGENTMCPCORE_API FToolDescriptor
	{
		FString ToolsetName;
		FString ToolName;
		FString QualifiedName;
		FString Description;
		TSharedPtr<FJsonObject> InputSchema;
		TSharedPtr<FJsonObject> OutputSchema;
		EMcpToolRisk Risk = EMcpToolRisk::ContentMutation;
		EMcpToolTransactionPolicy TransactionPolicy = EMcpToolTransactionPolicy::None;
		bool bReadOnly = false;
		bool bIdempotent = false;
		UClass* OwnerClass = nullptr;
		UFunction* Function = nullptr;
	};

	struct UNREALAGENTMCPCORE_API FDiscoveryResult
	{
		TArray<FToolDescriptor> Tools;
		TArray<FString> Errors;

		bool IsValid() const
		{
			return Errors.IsEmpty();
		}
	};

	/** 根据 UFunction 的输入或输出参数构建 JSON Schema。 */
	UNREALAGENTMCPCORE_API TSharedRef<FJsonObject> BuildInputSchema(const UFunction* Function);
	UNREALAGENTMCPCORE_API TSharedRef<FJsonObject> BuildOutputSchema(const UFunction* Function);

	/** 发现指定类中带显式元数据标注的 UFunction。 */
	UNREALAGENTMCPCORE_API FDiscoveryResult DiscoverClass(UClass* ToolsetClass);

	/** 发现全部已加载且带有 UnrealAgentMCPToolset 元数据的类。 */
	UNREALAGENTMCPCORE_API FDiscoveryResult DiscoverLoadedClasses();

	/**
	 * 反序列化参数，在类默认对象上调用工具描述符，
	 * 并序列化返回值与输出参数。
	 */
	UNREALAGENTMCPCORE_API bool Invoke(const FToolDescriptor& Descriptor, const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FString& OutError);
}
