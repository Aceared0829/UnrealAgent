// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPToolRegistry.cpp
 * @brief MCP 工具注册、目录合并与纯数据一致性校验。
 */

#include "Core/Tooling/UnrealAgentMCPToolRegistry.h"

#include "Core/Schema/UnrealAgentMCPJsonSchema.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Hash/Blake3.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAgentMCP::ToolRegistry
{
	namespace
	{
		/**
		 * @brief 将工具定义 JSON 字符串反序列化为 JSON 值数组。
		 * @param DefinitionsJson  符合 MCP 工具定义协议的 JSON 字符串（期望为数组结构）。
		 * @param OutDefinitions    反序列化成功后写入的解析结果，失败时内容不保证可用。
		 * @return 反序列化是否成功；JSON 格式非法时返回 false。
		 */
		bool ParseToolDefinitions(const FString& DefinitionsJson, TArray<TSharedPtr<FJsonValue>>& OutDefinitions)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DefinitionsJson);
			return FJsonSerializer::Deserialize(Reader, OutDefinitions);
		}

		/**
		 * @brief 合并工具定义并按工具名去重，先到先得。
		 *
		 * 解析 DefinitionsJson 后逐项校验：仅接受包含非空 "name" 字段的对象，
		 * 且该名称未在 SeenToolNames 中出现过的条目才会被追加到 OutDefinitions。
		 * 已记录的名称会写入 SeenToolNames，从而保证后续同名工具被忽略——
		 * 这使得调用方可以通过传入顺序决定主/备定义的优先级。
		 *
		 * @param DefinitionsJson  待合并的工具定义 JSON 字符串。
		 * @param OutDefinitions    累积输出的去重后工具定义列表。
		 * @param SeenToolNames     已登记的工具名集合，跨多次调用共享以维持去重状态。
		 */
		void AppendUniqueToolDefinitions(const FString& DefinitionsJson, TArray<TSharedPtr<FJsonValue>>& OutDefinitions, TSet<FString>& SeenToolNames)
		{
			TArray<TSharedPtr<FJsonValue>> ParsedDefinitions;
			if (!ParseToolDefinitions(DefinitionsJson, ParsedDefinitions))
			{
				// 解析失败时静默跳过：保留已有合并结果，避免破坏上层调用链。
				return;
			}

			for (const TSharedPtr<FJsonValue>& DefinitionValue : ParsedDefinitions)
			{
				// 仅处理合法的对象型条目，跳过数组/标量等异常结构。
				const TSharedPtr<FJsonObject> Definition = DefinitionValue.IsValid() && DefinitionValue->Type == EJson::Object ? DefinitionValue->AsObject() : nullptr;
				FString ToolName;
				// 跳过缺少 "name" 字段、名称为空或已登记过的重复定义。
				if (!Definition.IsValid() || !Definition->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty() || SeenToolNames.Contains(ToolName))
				{
					continue;
				}

				SeenToolNames.Add(ToolName);
				OutDefinitions.Add(DefinitionValue);
			}
		}
	}

	bool TryDispatch(const TConstArrayView<FMcpToolRegistration> Registrations, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FString& OutResult)
	{
		for (const FMcpToolRegistration& Registration : Registrations)
		{
			if (Registration.Name != nullptr && Registration.Handler != nullptr && ToolName == Registration.Name)
			{
				OutResult = Registration.Handler(Arguments);
				return true;
			}
		}

		return false;
	}

	TArray<FString> GetRegisteredToolNames(const TConstArrayView<FMcpToolRegistration> Registrations)
	{
		TArray<FString> Names;
		Names.Reserve(Registrations.Num());
		for (const FMcpToolRegistration& Registration : Registrations)
		{
			if (Registration.Name != nullptr && Registration.Handler != nullptr)
			{
				Names.Emplace(Registration.Name);
			}
		}
		return Names;
	}

	FString MergeToolDefinitionCatalogs(const FString& PrimaryDefinitionsJson, const FString& SecondaryDefinitionsJson)
	{
		TArray<TSharedPtr<FJsonValue>> MergedDefinitions;
		TSet<FString> SeenToolNames;
		AppendUniqueToolDefinitions(PrimaryDefinitionsJson, MergedDefinitions, SeenToolNames);
		AppendUniqueToolDefinitions(SecondaryDefinitionsJson, MergedDefinitions, SeenToolNames);

		FString Result;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
		FJsonSerializer::Serialize(MergedDefinitions, Writer);
		return Result;
	}

	FMcpToolCatalogValidationResult ValidateToolCatalog(const FString& ToolDefinitionsJson, const TArray<FString>& RegisteredHandlerNames)
	{
		FMcpToolCatalogValidationResult Result;
		TArray<TSharedPtr<FJsonValue>> Definitions;
		if (!ParseToolDefinitions(ToolDefinitionsJson, Definitions))
		{
			Result.Errors.Add(TEXT("工具目录不是有效的 JSON 数组。"));
			return Result;
		}

		TSet<FString> SchemaNames;
		for (int32 DefinitionIndex = 0; DefinitionIndex < Definitions.Num(); ++DefinitionIndex)
		{
			const TSharedPtr<FJsonObject> Definition =
				Definitions[DefinitionIndex].IsValid() && Definitions[DefinitionIndex]->Type == EJson::Object ? Definitions[DefinitionIndex]->AsObject() : nullptr;
			if (!Definition.IsValid())
			{
				Result.Errors.Add(FString::Printf(TEXT("工具目录第 %d 项不是对象。"), DefinitionIndex));
				continue;
			}

			FString ToolName;
			if (!Definition->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty())
			{
				Result.Errors.Add(FString::Printf(TEXT("工具目录第 %d 项缺少非空 name。"), DefinitionIndex));
				continue;
			}

			if (SchemaNames.Contains(ToolName))
			{
				Result.Errors.Add(FString::Printf(TEXT("工具 Schema 名称重复：%s"), *ToolName));
				continue;
			}
			SchemaNames.Add(ToolName);

			const TSharedPtr<FJsonObject>* InputSchema = nullptr;
			if (!Definition->TryGetObjectField(TEXT("inputSchema"), InputSchema) || InputSchema == nullptr || !InputSchema->IsValid())
			{
				Result.Errors.Add(FString::Printf(TEXT("工具 %s 缺少 inputSchema 对象。"), *ToolName));
			}
		}

		TSet<FString> HandlerNames;
		for (const FString& HandlerName : RegisteredHandlerNames)
		{
			if (HandlerName.IsEmpty())
			{
				Result.Errors.Add(TEXT("处理器注册表包含空名称。"));
				continue;
			}
			if (HandlerNames.Contains(HandlerName))
			{
				Result.Errors.Add(FString::Printf(TEXT("工具处理器名称重复：%s"), *HandlerName));
				continue;
			}
			HandlerNames.Add(HandlerName);
		}

		for (const FString& SchemaName : SchemaNames)
		{
			if (!HandlerNames.Contains(SchemaName))
			{
				Result.Errors.Add(FString::Printf(TEXT("工具 %s 有 Schema 但没有处理器。"), *SchemaName));
			}
		}
		for (const FString& HandlerName : HandlerNames)
		{
			if (!SchemaNames.Contains(HandlerName))
			{
				Result.Errors.Add(FString::Printf(TEXT("工具 %s 有处理器但没有 Schema。"), *HandlerName));
			}
		}

		return Result;
	}
}

namespace UnrealAgentMCP
{
	namespace
	{
		bool ParseRuntimeDefinitions(const FString& DefinitionsJson, TArray<TSharedPtr<FJsonValue>>& OutDefinitions)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DefinitionsJson);
			return FJsonSerializer::Deserialize(Reader, OutDefinitions);
		}

		FString SerializeDefinitions(const TArray<FMcpToolDescriptor>& Descriptors)
		{
			TArray<TSharedPtr<FJsonValue>> Definitions;
			Definitions.Reserve(Descriptors.Num());
			for (const FMcpToolDescriptor& Descriptor : Descriptors)
			{
				Definitions.Add(MakeShared<FJsonValueObject>(Descriptor.ToJsonObject()));
			}

			FString Result;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
			FJsonSerializer::Serialize(Definitions, Writer);
			return Result;
		}

		FString CalculateCatalogHash(const FString& DefinitionsJson)
		{
			const FTCHARToUTF8 Utf8(*DefinitionsJson);
			const FBlake3Hash Hash = FBlake3::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length()));
			return TEXT("blake3:") + LexToString(Hash).ToLower();
		}

		FMcpToolRegistrySnapshotRef BuildSnapshot(const uint64 Generation, TArray<FMcpToolDescriptor> Descriptors)
		{
			Descriptors.Sort(
				[](const FMcpToolDescriptor& Left, const FMcpToolDescriptor& Right)
				{
					const int32 NameOrder = Left.Name.Compare(Right.Name, ESearchCase::CaseSensitive);
					return NameOrder == 0 ? Left.QualifiedName.Compare(Right.QualifiedName, ESearchCase::CaseSensitive) < 0 : NameOrder < 0;
				});

			TSharedRef<FMcpToolRegistrySnapshot, ESPMode::ThreadSafe> Snapshot = MakeShared<FMcpToolRegistrySnapshot, ESPMode::ThreadSafe>();
			Snapshot->Generation = Generation;
			Snapshot->Tools = MoveTemp(Descriptors);
			for (int32 Index = 0; Index < Snapshot->Tools.Num(); ++Index)
			{
				const FMcpToolDescriptor& Descriptor = Snapshot->Tools[Index];
				Snapshot->ByName.Add(Descriptor.Name, Index);
				Snapshot->ByQualifiedName.Add(Descriptor.QualifiedName, Index);
				Snapshot->ByProvider.FindOrAdd(Descriptor.Provider).Add(Index);
				if (!Descriptor.Toolset.IsEmpty())
				{
					Snapshot->ByToolset.FindOrAdd(FName(*Descriptor.Toolset)).Add(Index);
				}
			}
			Snapshot->ToolDefinitionsJson = SerializeDefinitions(Snapshot->Tools);
			Snapshot->CatalogHash = CalculateCatalogHash(Snapshot->ToolDefinitionsJson);
			return Snapshot;
		}
	}

	FMcpToolRuntimeRegistry::FMcpToolRuntimeRegistry() : CurrentSnapshot(BuildSnapshot(0, {}))
	{
	}

	FMcpToolRegistrySnapshotRef FMcpToolRuntimeRegistry::GetSnapshot() const
	{
		FReadScopeLock Lock(SnapshotLock);
		check(CurrentSnapshot.IsValid());
		return CurrentSnapshot.ToSharedRef();
	}

	void FMcpToolRuntimeRegistry::PublishSnapshot(FMcpToolRegistrySnapshotRef NewSnapshot)
	{
		FWriteScopeLock Lock(SnapshotLock);
		CurrentSnapshot = NewSnapshot;
	}

	bool FMcpToolRuntimeRegistry::RegisterDescriptorsAtomically(TArray<FMcpToolDescriptor> Descriptors, TArray<FString>& OutErrors)
	{
		const int32 InitialErrorCount = OutErrors.Num();
		if (Descriptors.IsEmpty())
		{
			OutErrors.Add(TEXT("Provider 未枚举任何工具。"));
			return false;
		}

		TSet<FString> PendingNames;
		TSet<FString> PendingQualifiedNames;
		for (const FMcpToolDescriptor& Descriptor : Descriptors)
		{
			FString ValidationError;
			if (!Descriptor.Validate(ValidationError))
			{
				OutErrors.Add(ValidationError);
			}
			if (PendingNames.Contains(Descriptor.Name))
			{
				OutErrors.Add(FString::Printf(TEXT("本批工具名称重复：%s"), *Descriptor.Name));
			}
			else
			{
				PendingNames.Add(Descriptor.Name);
			}
			if (PendingQualifiedNames.Contains(Descriptor.QualifiedName))
			{
				OutErrors.Add(FString::Printf(TEXT("本批工具全限定名称重复：%s"), *Descriptor.QualifiedName));
			}
			else
			{
				PendingQualifiedNames.Add(Descriptor.QualifiedName);
			}
		}
		if (OutErrors.Num() != InitialErrorCount)
		{
			return false;
		}

		TSharedPtr<const FMcpToolRegistrySnapshot, ESPMode::ThreadSafe> PublishedSnapshot;
		{
			FScopeLock Mutation(&MutationLock);
			const FMcpToolRegistrySnapshotRef Base = GetSnapshot();
			for (const FMcpToolDescriptor& Descriptor : Descriptors)
			{
				if (Base->ByName.Contains(Descriptor.Name))
				{
					OutErrors.Add(FString::Printf(TEXT("工具名称已注册：%s"), *Descriptor.Name));
				}
				if (Base->ByQualifiedName.Contains(Descriptor.QualifiedName))
				{
					OutErrors.Add(FString::Printf(TEXT("工具全限定名称已注册：%s"), *Descriptor.QualifiedName));
				}
			}
			if (OutErrors.Num() != InitialErrorCount)
			{
				return false;
			}

			const uint64 NewGeneration = Base->Generation + 1;
			for (FMcpToolDescriptor& Descriptor : Descriptors)
			{
				Descriptor.RegistrationGeneration = NewGeneration;
			}
			TArray<FMcpToolDescriptor> Combined = Base->Tools;
			Combined.Append(MoveTemp(Descriptors));
			const FMcpToolRegistrySnapshotRef NewSnapshot = BuildSnapshot(NewGeneration, MoveTemp(Combined));
			PublishSnapshot(NewSnapshot);
			PublishedSnapshot = NewSnapshot;
		}
		RegistryChanged.Broadcast(PublishedSnapshot.ToSharedRef());
		return true;
	}

	bool FMcpToolRuntimeRegistry::RegisterDescriptor(FMcpToolDescriptor Descriptor, FString& OutError)
	{
		TArray<FMcpToolDescriptor> Descriptors;
		Descriptors.Add(MoveTemp(Descriptor));
		TArray<FString> Errors;
		const bool bRegistered = RegisterDescriptorsAtomically(MoveTemp(Descriptors), Errors);
		OutError = Errors.IsEmpty() ? FString() : Errors[0];
		return bRegistered;
	}

	bool FMcpToolRuntimeRegistry::RegisterProvider(const TSharedRef<IMcpToolProvider>& Provider, TArray<FString>& OutErrors)
	{
		const int32 InitialErrorCount = OutErrors.Num();
		const FName ProviderName = Provider->GetProviderName();
		if (ProviderName.IsNone())
		{
			OutErrors.Add(TEXT("工具 Provider 名称不能为空。"));
			return false;
		}
		const uint32 ProviderApiVersion = Provider->GetProviderApiVersion();
		if (ProviderApiVersion != McpToolProviderApiVersion)
		{
			OutErrors.Add(FString::Printf(TEXT("Provider %s 的 SDK 主版本 %u 与当前版本 %u 不兼容。"), *ProviderName.ToString(), ProviderApiVersion, McpToolProviderApiVersion));
			return false;
		}

		TArray<FMcpToolDescriptor> Descriptors;
		Provider->EnumerateTools(Descriptors, OutErrors);
		for (FMcpToolDescriptor& Descriptor : Descriptors)
		{
			if (Descriptor.Provider.IsNone())
			{
				Descriptor.Provider = ProviderName;
			}
			else if (Descriptor.Provider != ProviderName)
			{
				OutErrors.Add(FString::Printf(TEXT("工具 %s 的 Provider 与枚举来源不一致。"), *Descriptor.Name));
				continue;
			}
			Descriptor.ProviderApiVersion = ProviderApiVersion;
		}
		if (OutErrors.Num() != InitialErrorCount)
		{
			return false;
		}
		return RegisterDescriptorsAtomically(MoveTemp(Descriptors), OutErrors);
	}

	bool FMcpToolRuntimeRegistry::RegisterTool(const FString& Owner, const TSharedRef<FJsonObject>& Definition, FMcpDynamicToolHandler Handler, FString& OutError)
	{
		FMcpToolDescriptor Descriptor;
		if (!ToolDescriptor::FromJsonDefinition(Owner, Definition, MoveTemp(Handler), Descriptor, OutError))
		{
			return false;
		}
		return RegisterDescriptor(MoveTemp(Descriptor), OutError);
	}

	bool FMcpToolRuntimeRegistry::RegisterCatalog(const FString& Owner, const FString& DefinitionsJson, const TConstArrayView<FMcpToolRegistration> Registrations,
		TArray<FString>& OutErrors)
	{
		const int32 InitialErrorCount = OutErrors.Num();
		const TArray<FString> HandlerNames = ToolRegistry::GetRegisteredToolNames(Registrations);
		const FMcpToolCatalogValidationResult Validation = ToolRegistry::ValidateToolCatalog(DefinitionsJson, HandlerNames);
		OutErrors.Append(Validation.Errors);
		if (!Validation.IsValid())
		{
			return false;
		}

		TArray<TSharedPtr<FJsonValue>> Definitions;
		if (!ParseRuntimeDefinitions(DefinitionsJson, Definitions))
		{
			OutErrors.Add(TEXT("Tool catalog is not a valid JSON array."));
			return false;
		}

		TMap<FString, FMcpToolHandler> HandlersByName;
		TMap<FString, FMcpToolDescriptorConfigurator> ConfiguratorsByName;
		for (const FMcpToolRegistration& Registration : Registrations)
		{
			HandlersByName.Add(Registration.Name, Registration.Handler);
			if (Registration.ConfigureDescriptor)
			{
				ConfiguratorsByName.Add(Registration.Name, Registration.ConfigureDescriptor);
			}
		}

		TArray<FMcpToolDescriptor> PendingTools;
		PendingTools.Reserve(Definitions.Num());
		for (const TSharedPtr<FJsonValue>& DefinitionValue : Definitions)
		{
			const TSharedPtr<FJsonObject> Definition = DefinitionValue->AsObject();
			FString ToolName;
			Definition->TryGetStringField(TEXT("name"), ToolName);
			const FMcpToolHandler LegacyHandler = HandlersByName.FindRef(ToolName);
			FMcpDynamicToolHandler DynamicHandler = [LegacyHandler](const TSharedPtr<FJsonObject>& Arguments)
			{
				return LegacyHandler(Arguments);
			};
			FMcpToolDescriptor Descriptor;
			FString DescriptorError;
			if (!ToolDescriptor::FromJsonDefinition(Owner, Definition.ToSharedRef(), MoveTemp(DynamicHandler), Descriptor, DescriptorError))
			{
				OutErrors.Add(DescriptorError);
				continue;
			}
			if (const FMcpToolDescriptorConfigurator* Configure = ConfiguratorsByName.Find(ToolName))
			{
				(*Configure)(Descriptor);
			}
			PendingTools.Add(MoveTemp(Descriptor));
		}
		if (OutErrors.Num() != InitialErrorCount)
		{
			return false;
		}

		return RegisterDescriptorsAtomically(MoveTemp(PendingTools), OutErrors);
	}

	bool FMcpToolRuntimeRegistry::TryDispatch(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FString& OutResult,
		const uint64 ExpectedRegistrationGeneration) const
	{
		const FMcpToolRegistrySnapshotRef Snapshot = GetSnapshot();
		const int32* ToolIndex = Snapshot->ByName.Find(ToolName);
		if (ToolIndex == nullptr)
		{
			return false;
		}
		const FMcpToolDescriptor& Descriptor = Snapshot->Tools[*ToolIndex];
		if (ExpectedRegistrationGeneration != 0 && Descriptor.RegistrationGeneration != ExpectedRegistrationGeneration)
		{
			return false;
		}
		const TSharedPtr<FJsonObject> SafeArguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();

		if (Descriptor.bInputSchemaEnforced)
		{
			const JsonSchema::FValidationResult Validation = JsonSchema::ValidateObject(Descriptor.InputSchema.ToSharedRef(), SafeArguments);
			if (!Validation.IsValid())
			{
				OutResult = JsonSchema::MakeValidationErrorJson(ToolName, Validation);
				return true;
			}
		}
		OutResult = Descriptor.Invoker(SafeArguments);
		return true;
	}

	FString FMcpToolRuntimeRegistry::GetToolDefinitionsJson() const
	{
		return GetSnapshot()->ToolDefinitionsJson;
	}

	TArray<FString> FMcpToolRuntimeRegistry::GetRegisteredToolNames() const
	{
		const FMcpToolRegistrySnapshotRef Snapshot = GetSnapshot();
		TArray<FString> Names;
		Names.Reserve(Snapshot->Tools.Num());
		for (const FMcpToolDescriptor& Descriptor : Snapshot->Tools)
		{
			Names.Add(Descriptor.Name);
		}
		return Names;
	}

	TArray<FMcpToolDescriptor> FMcpToolRuntimeRegistry::GetDescriptors() const
	{
		return GetSnapshot()->Tools;
	}

	bool FMcpToolRuntimeRegistry::TryGetDescriptor(const FString& ToolName, FMcpToolDescriptor& OutDescriptor) const
	{
		const FMcpToolRegistrySnapshotRef Snapshot = GetSnapshot();
		const int32* ToolIndex = Snapshot->ByName.Find(ToolName);
		if (!ToolIndex)
		{
			return false;
		}
		OutDescriptor = Snapshot->Tools[*ToolIndex];
		return true;
	}

	int32 FMcpToolRuntimeRegistry::UnregisterOwner(const FString& Owner)
	{
		if (Owner.IsEmpty())
		{
			return 0;
		}
		int32 RemovedCount = 0;
		TSharedPtr<const FMcpToolRegistrySnapshot, ESPMode::ThreadSafe> PublishedSnapshot;
		{
			FScopeLock Mutation(&MutationLock);
			const FMcpToolRegistrySnapshotRef Base = GetSnapshot();
			TArray<FMcpToolDescriptor> Remaining;
			Remaining.Reserve(Base->Tools.Num());
			for (const FMcpToolDescriptor& Descriptor : Base->Tools)
			{
				if (Descriptor.Provider.ToString() == Owner)
				{
					++RemovedCount;
					continue;
				}
				Remaining.Add(Descriptor);
			}
			if (RemovedCount > 0)
			{
				const FMcpToolRegistrySnapshotRef NewSnapshot = BuildSnapshot(Base->Generation + 1, MoveTemp(Remaining));
				PublishSnapshot(NewSnapshot);
				PublishedSnapshot = NewSnapshot;
			}
		}
		if (PublishedSnapshot.IsValid())
		{
			RegistryChanged.Broadcast(PublishedSnapshot.ToSharedRef());
		}
		return RemovedCount;
	}

	int32 FMcpToolRuntimeRegistry::Num() const
	{
		return GetSnapshot()->Tools.Num();
	}
}
