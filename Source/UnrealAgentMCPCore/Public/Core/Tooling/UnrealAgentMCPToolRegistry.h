// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPToolRegistry.h
 * @brief MCP 工具名称、处理器与 Schema 目录之间的通用配对规则。
 */

#include "CoreMinimal.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"
#include "Delegates/Delegate.h"
#include "HAL/CriticalSection.h"

class FJsonObject;

namespace UnrealAgentMCP
{
	/** 所有工具处理器都接收已解析的 arguments 对象并返回 JSON 文本。 */
	using FMcpToolHandler = FString (*)(const TSharedPtr<FJsonObject>& Arguments);
	using FMcpToolDescriptorConfigurator = void (*)(FMcpToolDescriptor& Descriptor);

	/** 一个对外工具名及其唯一处理器。 */
	struct UNREALAGENTMCPCORE_API FMcpToolRegistration
	{
		const TCHAR* Name = nullptr;
		FMcpToolHandler Handler = nullptr;
		/** 组合根可借此补充动态策略与执行契约。 */
		FMcpToolDescriptorConfigurator ConfigureDescriptor = nullptr;
	};

	/** 目录校验结果；Errors 为空表示 Schema 与处理器完整配对。 */
	struct UNREALAGENTMCPCORE_API FMcpToolCatalogValidationResult
	{
		TArray<FString> Errors;

		bool IsValid() const
		{
			return Errors.IsEmpty();
		}
	};

	/**
	 * Registry 的不可变目录快照。
	 *
	 * 写操作构造完整新快照后一次替换；调用方持有旧快照时不会观察到
	 * 部分注册或部分卸载状态。
	 */
	struct UNREALAGENTMCPCORE_API FMcpToolRegistrySnapshot
	{
		uint64 Generation = 0;
		FString CatalogHash;
		FString ToolDefinitionsJson;
		TArray<FMcpToolDescriptor> Tools;
		TMap<FString, int32> ByName;
		TMap<FString, int32> ByQualifiedName;
		TMap<FName, TArray<int32>> ByProvider;
		TMap<FName, TArray<int32>> ByToolset;
	};

	using FMcpToolRegistrySnapshotRef = TSharedRef<const FMcpToolRegistrySnapshot, ESPMode::ThreadSafe>;

	/** 注册中心发布完整新快照后触发；回调可安全读取或再次修改 Registry。 */
	DECLARE_TS_MULTICAST_DELEGATE_OneParam(FMcpToolRegistryChanged, FMcpToolRegistrySnapshotRef);

	/**
	 * Unreal Agent 自有运行时工具注册中心。
	 * Definition 与 Handler 原子注册，目录、展示、校验和调用共享同一快照。
	 */
	class UNREALAGENTMCPCORE_API FMcpToolRuntimeRegistry
	{
	public:
		FMcpToolRuntimeRegistry();

		/** 注册完整工具契约；这是所有新 Provider 的首选入口。 */
		bool RegisterDescriptor(FMcpToolDescriptor Descriptor, FString& OutError);

		/** 枚举并原子注册一个 Provider 提供的全部工具。 */
		bool RegisterProvider(const TSharedRef<IMcpToolProvider>& Provider, TArray<FString>& OutErrors);

		bool RegisterTool(const FString& Owner, const TSharedRef<FJsonObject>& Definition, FMcpDynamicToolHandler Handler, FString& OutError);

		bool RegisterCatalog(const FString& Owner, const FString& DefinitionsJson, TConstArrayView<FMcpToolRegistration> Registrations, TArray<FString>& OutErrors);

		bool TryDispatch(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FString& OutResult, uint64 ExpectedRegistrationGeneration = 0) const;

		FMcpToolRegistrySnapshotRef GetSnapshot() const;
		FString GetToolDefinitionsJson() const;
		TArray<FString> GetRegisteredToolNames() const;
		TArray<FMcpToolDescriptor> GetDescriptors() const;
		bool TryGetDescriptor(const FString& ToolName, FMcpToolDescriptor& OutDescriptor) const;

		/** 订阅原子目录变更；失败或空操作不会广播。 */
		FMcpToolRegistryChanged& OnChanged()
		{
			return RegistryChanged;
		}

		int32 UnregisterOwner(const FString& Owner);
		int32 Num() const;

	private:
		bool RegisterDescriptorsAtomically(TArray<FMcpToolDescriptor> Descriptors, TArray<FString>& OutErrors);
		void PublishSnapshot(FMcpToolRegistrySnapshotRef NewSnapshot);

		FCriticalSection MutationLock;
		mutable FRWLock SnapshotLock;
		TSharedPtr<const FMcpToolRegistrySnapshot, ESPMode::ThreadSafe> CurrentSnapshot;
		FMcpToolRegistryChanged RegistryChanged;
	};

	namespace ToolRegistry
	{
		/** 精确匹配工具名并执行处理器；未注册时返回 false。 */
		UNREALAGENTMCPCORE_API bool TryDispatch(TConstArrayView<FMcpToolRegistration> Registrations, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
			FString& OutResult);

		/** 从实际注册表导出处理器名称，供目录展示和一致性校验使用。 */
		UNREALAGENTMCPCORE_API TArray<FString> GetRegisteredToolNames(TConstArrayView<FMcpToolRegistration> Registrations);

		/**
		 *
		 * 合并两份 tools/list JSON 数组。
		 * 名称重复时保留 Primary 中的定义，与既有服务器行为一致。
		 */
		UNREALAGENTMCPCORE_API FString MergeToolDefinitionCatalogs(const FString& PrimaryDefinitionsJson, const FString& SecondaryDefinitionsJson);

		/**
		 * 校验目录名称唯一、inputSchema 存在，并确保每个 Schema 与处理器双向配对。
		 */
		UNREALAGENTMCPCORE_API FMcpToolCatalogValidationResult ValidateToolCatalog(const FString& ToolDefinitionsJson, const TArray<FString>& RegisteredHandlerNames);
	}
}
