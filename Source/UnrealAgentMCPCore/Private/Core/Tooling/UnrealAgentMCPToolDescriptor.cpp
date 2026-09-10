// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPToolDescriptor.cpp
 * @brief 统一工具描述符的校验、序列化与旧目录升级实现。
 */

#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> MakePermissiveObjectSchema()
		{
			TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
			Schema->SetBoolField(TEXT("additionalProperties"), true);
			return Schema;
		}

		bool ReadAnnotationBool(const TSharedPtr<FJsonObject>& Annotations, const TCHAR* Name, const bool Default)
		{
			bool Value = Default;
			if (Annotations.IsValid())
			{
				Annotations->TryGetBoolField(Name, Value);
			}
			return Value;
		}

		bool IsEvidenceGatedHighRiskTool(const FString& Name)
		{
			return Name == TEXT("call_tool") || Name == TEXT("delete_actor") || Name == TEXT("delete_file") || Name == TEXT("execute_python_blocking") ||
				Name == TEXT("rename_file") || Name == TEXT("write_file");
		}

		void AddStringSchemaProperty(const TSharedRef<FJsonObject>& Schema, const FString& Name, const FString& Description)
		{
			const TSharedPtr<FJsonObject>* ExistingProperties = nullptr;
			TSharedRef<FJsonObject> Properties = Schema->TryGetObjectField(TEXT("properties"), ExistingProperties) && ExistingProperties && ExistingProperties->IsValid()
				? ExistingProperties->ToSharedRef()
				: MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("type"), TEXT("string"));
			Property->SetStringField(TEXT("description"), Description);
			Properties->SetObjectField(Name, Property);
			Schema->SetObjectField(TEXT("properties"), Properties);
		}
	}

	bool FMcpToolDescriptor::Validate(FString& OutError) const
	{
		OutError.Reset();
		if (Provider.IsNone())
		{
			OutError = TEXT("工具 Provider 不能为空。");
			return false;
		}
		if (ProviderApiVersion != McpToolProviderApiVersion)
		{
			OutError = FString::Printf(TEXT("工具 %s 的 Provider SDK 主版本 %u 与当前版本 %u 不兼容。"), *Name, ProviderApiVersion, McpToolProviderApiVersion);
			return false;
		}
		if (Name.IsEmpty())
		{
			OutError = TEXT("工具名称不能为空。");
			return false;
		}
		if (QualifiedName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("工具 %s 缺少 QualifiedName。"), *Name);
			return false;
		}
		if (!InputSchema.IsValid())
		{
			OutError = FString::Printf(TEXT("工具 %s 缺少 inputSchema。"), *QualifiedName);
			return false;
		}
		if (!OutputSchema.IsValid())
		{
			OutError = FString::Printf(TEXT("工具 %s 缺少 outputSchema。"), *QualifiedName);
			return false;
		}
		if (!Invoker)
		{
			OutError = FString::Printf(TEXT("工具 %s 缺少 Invoker。"), *QualifiedName);
			return false;
		}
		if (ExecutionMode != EMcpToolExecutionMode::Synchronous && (ThreadPolicy == EMcpToolThreadPolicy::GameThread || ThreadPolicy == EMcpToolThreadPolicy::StagedGameThread) &&
			!StagedTaskFactory && (bRequiresResumableTask || !ActionContractResolver))
		{
			OutError = FString::Printf(TEXT("GameThread task tool %s requires a resumable task factory."), *QualifiedName);
			return false;
		}
		if (bReadOnly && Risk != EMcpToolRisk::ReadOnly)
		{
			OutError = FString::Printf(TEXT("只读工具 %s 的风险等级必须为 ReadOnly。"), *QualifiedName);
			return false;
		}
		if (bRequiresConfirmation && (ConfirmationArgument.IsEmpty() || ConfirmationValue.IsEmpty()))
		{
			OutError = FString::Printf(TEXT("Tool %s requires a non-empty confirmation argument and value."), *QualifiedName);
			return false;
		}
		return true;
	}

	TSharedRef<FJsonObject> FMcpToolDescriptor::ToJsonObject() const
	{
		TSharedRef<FJsonObject> Definition = MakeShared<FJsonObject>();
		Definition->SetStringField(TEXT("name"), Name);
		Definition->SetStringField(TEXT("description"), Description);
		Definition->SetObjectField(TEXT("inputSchema"), InputSchema);
		Definition->SetObjectField(TEXT("outputSchema"), OutputSchema);

		TSharedRef<FJsonObject> Annotations = MakeShared<FJsonObject>();
		Annotations->SetStringField(TEXT("title"), QualifiedName.IsEmpty() ? Name : QualifiedName);
		Annotations->SetBoolField(TEXT("readOnlyHint"), bReadOnly);
		Annotations->SetBoolField(TEXT("destructiveHint"),
			Risk == EMcpToolRisk::FileMutation || Risk == EMcpToolRisk::Destructive || Risk == EMcpToolRisk::CodeExecution || Risk == EMcpToolRisk::ExternalProcess);
		Annotations->SetBoolField(TEXT("idempotentHint"), bIdempotent);
		Annotations->SetBoolField(TEXT("openWorldHint"), false);
		Definition->SetObjectField(TEXT("annotations"), Annotations);

		TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
		Metadata->SetStringField(TEXT("provider"), Provider.ToString());
		Metadata->SetNumberField(TEXT("providerApiVersion"), static_cast<double>(ProviderApiVersion));
		Metadata->SetStringField(TEXT("toolset"), Toolset);
		Metadata->SetStringField(TEXT("transportName"), Name);
		Metadata->SetStringField(TEXT("qualifiedName"), QualifiedName);
		Metadata->SetStringField(TEXT("contractVersion"), ContractVersion);
		Metadata->SetStringField(TEXT("risk"), ToolDescriptor::RiskToString(Risk));
		Metadata->SetStringField(TEXT("threadPolicy"), ToolDescriptor::ThreadPolicyToString(ThreadPolicy));
		Metadata->SetStringField(TEXT("executionMode"), ToolDescriptor::ExecutionModeToString(ExecutionMode));
		Metadata->SetStringField(TEXT("transactionPolicy"), ToolDescriptor::TransactionPolicyToString(TransactionPolicy));
		Metadata->SetBoolField(TEXT("cancelable"), bCancelable);
		Metadata->SetBoolField(TEXT("resumable"), static_cast<bool>(StagedTaskFactory));
		Metadata->SetBoolField(TEXT("requiresResumableTask"), bRequiresResumableTask);
		if (bRequiresConfirmation)
		{
			Metadata->SetBoolField(TEXT("requiresConfirmation"), true);
			Metadata->SetStringField(TEXT("confirmationArgument"), ConfirmationArgument);
			Metadata->SetStringField(TEXT("confirmationValue"), ConfirmationValue);
		}
		TArray<TSharedPtr<FJsonValue>> FilePathValues;
		for (const FString& ArgumentName : FilePathArguments)
		{
			FilePathValues.Add(MakeShared<FJsonValueString>(ArgumentName));
		}
		Metadata->SetArrayField(TEXT("filePathArguments"), FilePathValues);
		TArray<TSharedPtr<FJsonValue>> AssetPathValues;
		for (const FString& ArgumentName : AssetPathArguments)
		{
			AssetPathValues.Add(MakeShared<FJsonValueString>(ArgumentName));
		}
		Metadata->SetArrayField(TEXT("assetPathArguments"), AssetPathValues);
		Metadata->SetBoolField(TEXT("dynamicFilePathPolicy"), static_cast<bool>(FilePathArgumentsResolver));
		Metadata->SetBoolField(TEXT("dynamicAssetPathPolicy"), static_cast<bool>(AssetPathArgumentsResolver));
		Metadata->SetBoolField(TEXT("inputSchemaExplicit"), bInputSchemaExplicit);
		Metadata->SetBoolField(TEXT("outputSchemaExplicit"), bOutputSchemaExplicit);
		Metadata->SetBoolField(TEXT("inputSchemaEnforced"), bInputSchemaEnforced);
		Definition->SetObjectField(TEXT("_meta"), Metadata);
		return Definition;
	}
}

namespace UnrealAgentMCP::ToolDescriptor
{
	FString RiskToString(const EMcpToolRisk Value)
	{
		switch (Value)
		{
		case EMcpToolRisk::ReadOnly:
			return TEXT("ReadOnly");
		case EMcpToolRisk::EditorState:
			return TEXT("EditorState");
		case EMcpToolRisk::ContentMutation:
			return TEXT("ContentMutation");
		case EMcpToolRisk::FileMutation:
			return TEXT("FileMutation");
		case EMcpToolRisk::Destructive:
			return TEXT("Destructive");
		case EMcpToolRisk::CodeExecution:
			return TEXT("CodeExecution");
		case EMcpToolRisk::ExternalProcess:
			return TEXT("ExternalProcess");
		default:
			return TEXT("ContentMutation");
		}
	}

	FString ThreadPolicyToString(const EMcpToolThreadPolicy Value)
	{
		switch (Value)
		{
		case EMcpToolThreadPolicy::Inline:
			return TEXT("Inline");
		case EMcpToolThreadPolicy::GameThread:
			return TEXT("GameThread");
		case EMcpToolThreadPolicy::BackgroundThread:
			return TEXT("BackgroundThread");
		case EMcpToolThreadPolicy::StagedGameThread:
			return TEXT("StagedGameThread");
		case EMcpToolThreadPolicy::LongRunning:
			return TEXT("LongRunning");
		case EMcpToolThreadPolicy::NativeAsync:
			return TEXT("NativeAsync");
		default:
			return TEXT("GameThread");
		}
	}

	FString ExecutionModeToString(const EMcpToolExecutionMode Value)
	{
		switch (Value)
		{
		case EMcpToolExecutionMode::Synchronous:
			return TEXT("Synchronous");
		case EMcpToolExecutionMode::Asynchronous:
			return TEXT("Asynchronous");
		case EMcpToolExecutionMode::Task:
			return TEXT("Task");
		default:
			return TEXT("Synchronous");
		}
	}

	FString TransactionPolicyToString(const EMcpToolTransactionPolicy Value)
	{
		switch (Value)
		{
		case EMcpToolTransactionPolicy::None:
			return TEXT("None");
		case EMcpToolTransactionPolicy::ReadOnly:
			return TEXT("ReadOnly");
		case EMcpToolTransactionPolicy::ScopedTransaction:
			return TEXT("ScopedTransaction");
		case EMcpToolTransactionPolicy::Atomic:
			return TEXT("Atomic");
		case EMcpToolTransactionPolicy::Compensating:
			return TEXT("Compensating");
		default:
			return TEXT("None");
		}
	}

	bool FromJsonDefinition(const FString& Owner, const TSharedRef<FJsonObject>& Definition, FMcpDynamicToolHandler Handler, FMcpToolDescriptor& OutDescriptor, FString& OutError)
	{
		OutDescriptor = FMcpToolDescriptor();
		OutError.Reset();
		FString Name;
		if (Owner.IsEmpty())
		{
			OutError = TEXT("工具 Owner 不能为空。");
			return false;
		}
		if (!Definition->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
		{
			OutError = TEXT("工具定义必须包含非空 name。");
			return false;
		}
		const TSharedPtr<FJsonObject>* InputSchema = nullptr;
		if (!Definition->TryGetObjectField(TEXT("inputSchema"), InputSchema) || !InputSchema || !InputSchema->IsValid())
		{
			OutError = FString::Printf(TEXT("工具 %s 缺少 inputSchema。"), *Name);
			return false;
		}

		OutDescriptor.Provider = FName(*Owner);
		OutDescriptor.Name = Name;
		OutDescriptor.QualifiedName = Name;
		Definition->TryGetStringField(TEXT("description"), OutDescriptor.Description);
		OutDescriptor.InputSchema = MakeShared<FJsonObject>(**InputSchema);
		OutDescriptor.bInputSchemaEnforced = false;

		const TSharedPtr<FJsonObject>* OutputSchema = nullptr;
		if (Definition->TryGetObjectField(TEXT("outputSchema"), OutputSchema) && OutputSchema && OutputSchema->IsValid())
		{
			OutDescriptor.OutputSchema = MakeShared<FJsonObject>(**OutputSchema);
		}
		else
		{
			OutDescriptor.OutputSchema = MakePermissiveObjectSchema();
			OutDescriptor.bOutputSchemaExplicit = false;
		}

		const TSharedPtr<FJsonObject>* AnnotationsValue = nullptr;
		const TSharedPtr<FJsonObject> Annotations = Definition->TryGetObjectField(TEXT("annotations"), AnnotationsValue) && AnnotationsValue ? *AnnotationsValue : nullptr;
		OutDescriptor.bReadOnly = ReadAnnotationBool(Annotations, TEXT("readOnlyHint"), false);
		OutDescriptor.bIdempotent = ReadAnnotationBool(Annotations, TEXT("idempotentHint"), false);
		const bool bDestructive = ReadAnnotationBool(Annotations, TEXT("destructiveHint"), false);
		OutDescriptor.Risk = OutDescriptor.bReadOnly ? EMcpToolRisk::ReadOnly : (bDestructive ? EMcpToolRisk::Destructive : EMcpToolRisk::ContentMutation);
		if (Name == TEXT("execute_python_blocking"))
		{
			OutDescriptor.Risk = EMcpToolRisk::CodeExecution;
		}
		else if (Name == TEXT("read_file"))
		{
			OutDescriptor.FilePathArguments = { TEXT("file_path") };
		}
		else if (Name == TEXT("write_file") || Name == TEXT("delete_file"))
		{
			OutDescriptor.Risk = EMcpToolRisk::FileMutation;
			OutDescriptor.FilePathArguments = { TEXT("file_path") };
		}
		else if (Name == TEXT("rename_file"))
		{
			OutDescriptor.Risk = EMcpToolRisk::FileMutation;
			OutDescriptor.FilePathArguments = { TEXT("old_path"), TEXT("new_path") };
		}
		else if (Name == TEXT("read_asset"))
		{
			OutDescriptor.AssetPathArguments = { TEXT("assetPath") };
		}
		else if (Name == TEXT("create_asset") || Name == TEXT("create_blueprint_asset") || Name == TEXT("modify_material_instance") || Name == TEXT("create_pcg_graph_from_recipe"))
		{
			OutDescriptor.AssetPathArguments = { TEXT("assetPath"), TEXT("path") };
		}
		OutDescriptor.TransactionPolicy = OutDescriptor.bReadOnly ? EMcpToolTransactionPolicy::ReadOnly : EMcpToolTransactionPolicy::None;
		if (IsEvidenceGatedHighRiskTool(Name))
		{
			OutDescriptor.bInputSchemaEnforced = true;
			OutDescriptor.InputSchema->SetBoolField(TEXT("additionalProperties"), false);
			OutDescriptor.bRequiresConfirmation = true;
			if (Name == TEXT("execute_python_blocking"))
			{
				OutDescriptor.ConfirmationArgument = TEXT("unsafe_confirm");
				OutDescriptor.ConfirmationValue = TEXT("I understand this runs arbitrary Unreal Python");
				AddStringSchemaProperty(OutDescriptor.InputSchema.ToSharedRef(), TEXT("taskSummary"), TEXT("Non-sensitive audit summary; raw code is never recorded."));
			}
			else
			{
				OutDescriptor.ConfirmationArgument = TEXT("confirmation");
				OutDescriptor.ConfirmationValue = TEXT("I understand this operation may modify project data");
				AddStringSchemaProperty(OutDescriptor.InputSchema.ToSharedRef(), TEXT("confirmation"), TEXT("Must explicitly acknowledge the project-data mutation."));
			}
			if (Name == TEXT("delete_actor"))
			{
				OutDescriptor.InputSchema->RemoveField(TEXT("required"));
				OutDescriptor.TransactionPolicy = EMcpToolTransactionPolicy::ScopedTransaction;
			}
			else if (Name == TEXT("write_file") || Name == TEXT("rename_file"))
			{
				OutDescriptor.TransactionPolicy = EMcpToolTransactionPolicy::Atomic;
			}
		}
		OutDescriptor.Invoker = MoveTemp(Handler);
		return OutDescriptor.Validate(OutError);
	}
}
