// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPActionManifest.cpp
 * @brief action Manifest 解析、完整性校验与分类 Schema 组合。
 */

#include "Application/Actions/UnrealAgentMCPActionManifest.h"

#include "Core/Serialization/UnrealAgentMCPJsonIntegrity.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP::ActionContracts
{
	namespace
	{
		bool IsCommitHash(const FString& Value)
		{
			if (Value.Len() != 40)
			{
				return false;
			}
			for (const TCHAR Character : Value)
			{
				if (!FChar::IsHexDigit(Character))
				{
					return false;
				}
			}
			return true;
		}

		bool ParseRisk(const FString& Text, EMcpToolRisk& OutValue)
		{
			if (Text == TEXT("ReadOnly"))
				OutValue = EMcpToolRisk::ReadOnly;
			else if (Text == TEXT("EditorState"))
				OutValue = EMcpToolRisk::EditorState;
			else if (Text == TEXT("ContentMutation"))
				OutValue = EMcpToolRisk::ContentMutation;
			else if (Text == TEXT("FileMutation"))
				OutValue = EMcpToolRisk::FileMutation;
			else if (Text == TEXT("Destructive"))
				OutValue = EMcpToolRisk::Destructive;
			else if (Text == TEXT("CodeExecution"))
				OutValue = EMcpToolRisk::CodeExecution;
			else if (Text == TEXT("ExternalProcess"))
				OutValue = EMcpToolRisk::ExternalProcess;
			else
				return false;
			return true;
		}

		bool ParseTransaction(const FString& Text, EMcpToolTransactionPolicy& OutValue)
		{
			if (Text == TEXT("None"))
				OutValue = EMcpToolTransactionPolicy::None;
			else if (Text == TEXT("ReadOnly"))
				OutValue = EMcpToolTransactionPolicy::ReadOnly;
			else if (Text == TEXT("ScopedTransaction"))
				OutValue = EMcpToolTransactionPolicy::ScopedTransaction;
			else if (Text == TEXT("Atomic"))
				OutValue = EMcpToolTransactionPolicy::Atomic;
			else if (Text == TEXT("Compensating"))
				OutValue = EMcpToolTransactionPolicy::Compensating;
			else
				return false;
			return true;
		}

		bool ParseExecutionMode(const FString& Text, EMcpToolExecutionMode& OutValue)
		{
			if (Text == TEXT("Synchronous"))
				OutValue = EMcpToolExecutionMode::Synchronous;
			else if (Text == TEXT("Asynchronous"))
				OutValue = EMcpToolExecutionMode::Asynchronous;
			else if (Text == TEXT("Task"))
				OutValue = EMcpToolExecutionMode::Task;
			else
				return false;
			return true;
		}

		bool ReadRequiredString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Context, FString& OutValue, TArray<FString>& OutErrors)
		{
			if (Object.IsValid() && Object->TryGetStringField(Field, OutValue) && !OutValue.IsEmpty())
			{
				return true;
			}
			OutErrors.Add(FString::Printf(TEXT("%s 缺少字符串字段 %s。"), *Context, Field));
			return false;
		}

		bool ParseAction(const FString& Domain, const TSharedPtr<FJsonObject>& Object, FActionContract& OutAction, TArray<FString>& OutErrors)
		{
			const FString Context = FString::Printf(TEXT("领域 %s 的 action"), *Domain);
			if (!Object.IsValid())
			{
				OutErrors.Add(Context + TEXT(" 不是对象。"));
				return false;
			}
			bool bValid = ReadRequiredString(Object, TEXT("name"), Context, OutAction.Name, OutErrors);
			bValid &= ReadRequiredString(Object, TEXT("toolId"), Context, OutAction.ToolId, OutErrors);
			Object->TryGetStringField(TEXT("status"), OutAction.Status);
			Object->TryGetStringField(TEXT("implementation"), OutAction.Implementation);
			Object->TryGetStringField(TEXT("description"), OutAction.Description);

			const TSharedPtr<FJsonObject>* InputSchema = nullptr;
			const TSharedPtr<FJsonObject>* OutputSchema = nullptr;
			if (!Object->TryGetObjectField(TEXT("inputSchema"), InputSchema) || InputSchema == nullptr || !InputSchema->IsValid())
			{
				OutErrors.Add(Context + TEXT(" 缺少 inputSchema。"));
				bValid = false;
			}
			else
			{
				OutAction.InputSchema = *InputSchema;
			}
			if (!Object->TryGetObjectField(TEXT("outputSchema"), OutputSchema) || OutputSchema == nullptr || !OutputSchema->IsValid())
			{
				OutErrors.Add(Context + TEXT(" 缺少 outputSchema。"));
				bValid = false;
			}
			else
			{
				OutAction.OutputSchema = *OutputSchema;
			}

			FString Risk;
			FString Transaction;
			FString ExecutionMode;
			if (!Object->TryGetStringField(TEXT("risk"), Risk) || !ParseRisk(Risk, OutAction.Risk))
			{
				OutErrors.Add(Context + TEXT(" 的 risk 无效。"));
				bValid = false;
			}
			if (!Object->TryGetStringField(TEXT("transactionPolicy"), Transaction) || !ParseTransaction(Transaction, OutAction.TransactionPolicy))
			{
				OutErrors.Add(Context + TEXT(" 的 transactionPolicy 无效。"));
				bValid = false;
			}
			if (!Object->TryGetStringField(TEXT("executionMode"), ExecutionMode) || !ParseExecutionMode(ExecutionMode, OutAction.ExecutionMode))
			{
				OutErrors.Add(Context + TEXT(" 的 executionMode 无效。"));
				bValid = false;
			}
			Object->TryGetBoolField(TEXT("cancelable"), OutAction.bCancelable);
			return bValid;
		}
	}

	TSharedPtr<FManifest> FManifest::Parse(const FString& Json, TArray<FString>& OutErrors)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErrors.Add(TEXT("ActionContracts.json 不是有效 JSON 对象。"));
			return nullptr;
		}

		double Version = 0.0;
		double DeclaredDomainCount = 0.0;
		double DeclaredActionCount = 0.0;
		FString ContractHash;
		if (!Root->TryGetNumberField(TEXT("version"), Version) || static_cast<int32>(Version) != 1)
		{
			OutErrors.Add(TEXT("Action Manifest 版本必须为 1。"));
		}
		if (!Root->TryGetNumberField(TEXT("domainCount"), DeclaredDomainCount) || DeclaredDomainCount < 0.0 || DeclaredDomainCount != FMath::FloorToDouble(DeclaredDomainCount))
		{
			OutErrors.Add(TEXT("Action Manifest 的 domainCount 无效。"));
		}
		if (!Root->TryGetNumberField(TEXT("actionCount"), DeclaredActionCount) || DeclaredActionCount < 0.0 || DeclaredActionCount != FMath::FloorToDouble(DeclaredActionCount))
		{
			OutErrors.Add(TEXT("Action Manifest 的 actionCount 无效。"));
		}
		if (!Root->TryGetStringField(TEXT("contractHash"), ContractHash) || ContractHash.Len() != 71 || !ContractHash.StartsWith(TEXT("sha256:")))
		{
			OutErrors.Add(TEXT("Action Manifest 缺少 contractHash。"));
		}

		const TSharedPtr<FJsonObject>* GeneratedFrom = nullptr;
		FString SourceRepository;
		FString SourceCommit;
		FString SourceLicense;
		if (!Root->TryGetObjectField(TEXT("generatedFrom"), GeneratedFrom) || GeneratedFrom == nullptr || !GeneratedFrom->IsValid())
		{
			OutErrors.Add(TEXT("Action Manifest 缺少 generatedFrom。"));
		}
		else
		{
			ReadRequiredString(*GeneratedFrom, TEXT("repository"), TEXT("generatedFrom"), SourceRepository, OutErrors);
			ReadRequiredString(*GeneratedFrom, TEXT("commit"), TEXT("generatedFrom"), SourceCommit, OutErrors);
			ReadRequiredString(*GeneratedFrom, TEXT("license"), TEXT("generatedFrom"), SourceLicense, OutErrors);
			if (!SourceCommit.IsEmpty() && !IsCommitHash(SourceCommit))
			{
				OutErrors.Add(TEXT("Action Manifest 的上游 commit 必须是 40 位十六进制。"));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* DomainValues = nullptr;
		if (!Root->TryGetArrayField(TEXT("domains"), DomainValues) || DomainValues == nullptr)
		{
			OutErrors.Add(TEXT("Action Manifest 缺少 domains。"));
			return nullptr;
		}
		const TSharedPtr<FJsonValue> DomainsValue = MakeShared<FJsonValueArray>(*DomainValues);
		FString ExpectedHash;
		FString IntegrityError;
		if (!JsonIntegrity::TryComputeCanonicalSha256(DomainsValue, ExpectedHash, IntegrityError))
		{
			OutErrors.Add(TEXT("Action Manifest 完整性校验失败：") + IntegrityError);
		}
		else if (!ContractHash.Equals(ExpectedHash, ESearchCase::CaseSensitive))
		{
			OutErrors.Add(FString::Printf(TEXT("Action Manifest contractHash 不匹配：期望 %s，实际 %s。"), *ExpectedHash, *ContractHash));
		}

		TSharedPtr<FManifest> Manifest = MakeShared<FManifest>();
		Manifest->ContractHash = MoveTemp(ContractHash);
		TSet<FString> ToolIds;
		for (const TSharedPtr<FJsonValue>& DomainValue : *DomainValues)
		{
			const TSharedPtr<FJsonObject> DomainObject = DomainValue.IsValid() ? DomainValue->AsObject() : nullptr;
			FString DomainName;
			FString TransportTool;
			if (!ReadRequiredString(DomainObject, TEXT("name"), TEXT("领域"), DomainName, OutErrors) ||
				!ReadRequiredString(DomainObject, TEXT("transportTool"), DomainName, TransportTool, OutErrors))
			{
				continue;
			}
			if (Manifest->Domains.Contains(DomainName))
			{
				OutErrors.Add(FString::Printf(TEXT("领域重复：%s。"), *DomainName));
				continue;
			}
			if (TransportTool != DomainName)
			{
				OutErrors.Add(FString::Printf(TEXT("领域 %s 的 transportTool 必须与领域名一致。"), *DomainName));
			}

			FDomainContract Domain;
			Domain.Name = DomainName;
			Domain.TransportTool = MoveTemp(TransportTool);
			const TArray<TSharedPtr<FJsonValue>>* ActionValues = nullptr;
			if (!DomainObject->TryGetArrayField(TEXT("actions"), ActionValues) || ActionValues == nullptr)
			{
				OutErrors.Add(FString::Printf(TEXT("领域 %s 缺少 actions。"), *DomainName));
				continue;
			}
			double DeclaredDomainActionCount = 0.0;
			if (!DomainObject->TryGetNumberField(TEXT("actionCount"), DeclaredDomainActionCount) || DeclaredDomainActionCount < 0.0 ||
				DeclaredDomainActionCount != FMath::FloorToDouble(DeclaredDomainActionCount) || static_cast<int32>(DeclaredDomainActionCount) != ActionValues->Num())
			{
				OutErrors.Add(FString::Printf(TEXT("领域 %s 的 actionCount 无效。"), *DomainName));
			}

			TArray<TSharedPtr<FJsonValue>> SchemaBranches;
			for (const TSharedPtr<FJsonValue>& ActionValue : *ActionValues)
			{
				const TSharedPtr<FJsonObject> ActionObject = ActionValue.IsValid() ? ActionValue->AsObject() : nullptr;
				FActionContract Action;
				if (!ParseAction(DomainName, ActionObject, Action, OutErrors))
				{
					continue;
				}
				if (Domain.Actions.Contains(Action.Name))
				{
					OutErrors.Add(FString::Printf(TEXT("Action 重复：%s.%s。"), *DomainName, *Action.Name));
					continue;
				}
				const FString ExpectedToolId = FString::Printf(TEXT("worlddata.%s.%s"), *DomainName, *Action.Name);
				if (Action.ToolId != ExpectedToolId)
				{
					OutErrors.Add(FString::Printf(TEXT("ToolId 无效：%s，期望 %s。"), *Action.ToolId, *ExpectedToolId));
					continue;
				}
				if (ToolIds.Contains(Action.ToolId))
				{
					OutErrors.Add(TEXT("ToolId 重复：") + Action.ToolId);
					continue;
				}
				ToolIds.Add(Action.ToolId);
				SchemaBranches.Add(MakeShared<FJsonValueObject>(Action.InputSchema));
				Domain.Actions.Add(Action.Name, MoveTemp(Action));
				++Manifest->ActionCount;
			}

			Domain.InputSchema = MakeShared<FJsonObject>();
			Domain.InputSchema->SetArrayField(TEXT("anyOf"), MoveTemp(SchemaBranches));
			Manifest->Domains.Add(DomainName, MoveTemp(Domain));
		}

		if (Manifest->Domains.Num() != static_cast<int32>(DeclaredDomainCount))
		{
			OutErrors.Add(FString::Printf(TEXT("Action Manifest 领域数量无效：%d。"), Manifest->Domains.Num()));
		}
		if (Manifest->ActionCount != static_cast<int32>(DeclaredActionCount))
		{
			OutErrors.Add(FString::Printf(TEXT("Action Manifest action 数量无效：%d。"), Manifest->ActionCount));
		}
		return OutErrors.IsEmpty() ? Manifest : nullptr;
	}

	const FDomainContract* FManifest::FindDomain(const FString& Domain) const
	{
		return Domains.Find(Domain);
	}

	const FActionContract* FManifest::FindAction(const FString& Domain, const FString& Action) const
	{
		const FDomainContract* DomainContract = FindDomain(Domain);
		return DomainContract != nullptr ? DomainContract->Actions.Find(Action) : nullptr;
	}
}
