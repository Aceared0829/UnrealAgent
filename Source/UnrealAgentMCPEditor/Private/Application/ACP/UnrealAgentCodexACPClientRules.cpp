// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentCodexACPClientRules.cpp
 * @brief Codex ACP 客户端无状态规则实现。
 */

#include "Application/ACP/UnrealAgentCodexACPClientRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace WorldDataCodexAcpRules
{
	namespace
	{
		bool RedactImageData(const TSharedPtr<FJsonObject>& Object)
		{
			if (!Object.IsValid())
			{
				return false;
			}

			bool bRedacted = false;
			FString Type;
			if (Object->TryGetStringField(TEXT("type"), Type) && Type.Equals(TEXT("image"), ESearchCase::IgnoreCase) && Object->HasField(TEXT("data")))
			{
				Object->SetStringField(TEXT("data"), TEXT("<redacted>"));
				bRedacted = true;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
			{
				if (!Field.Value.IsValid())
				{
					continue;
				}
				if (Field.Value->Type == EJson::Object)
				{
					bRedacted = RedactImageData(Field.Value->AsObject()) || bRedacted;
				}
				else if (Field.Value->Type == EJson::Array)
				{
					for (const TSharedPtr<FJsonValue>& Item : Field.Value->AsArray())
					{
						if (Item.IsValid() && Item->Type == EJson::Object)
						{
							bRedacted = RedactImageData(Item->AsObject()) || bRedacted;
						}
					}
				}
			}
			return bRedacted;
		}

		FString FindPermissionOption(const TArray<FUnrealAgentAcpPermissionOption>& Options, const FString& Match)
		{
			const FString LowerMatch = Match.ToLower();
			for (const FUnrealAgentAcpPermissionOption& Option : Options)
			{
				const FString Probe = (Option.Kind + TEXT(" ") + Option.Name + TEXT(" ") + Option.OptionId).ToLower();
				if (Probe.Contains(LowerMatch))
				{
					return Option.OptionId;
				}
			}
			return FString();
		}

		FString ReadBoundedString(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
		{
			FString Value;
			return Object.IsValid() && Object->TryGetStringField(FieldName, Value) ? Value.TrimStartAndEnd().Left(256) : FString();
		}

		void AddSchemaErrorPaths(const TSharedPtr<FJsonObject>& Object, FUnrealAgentAcpToolCallUpdate& InOutDetails)
		{
			const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("schemaErrors"), Errors) || Errors == nullptr)
			{
				return;
			}

			for (const TSharedPtr<FJsonValue>& Value : *Errors)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					continue;
				}
				const FString Path = ReadBoundedString(Value->AsObject(), TEXT("path"));
				if (!Path.IsEmpty() && !InOutDetails.SchemaErrorPaths.Contains(Path) && InOutDetails.SchemaErrorPaths.Num() < 16)
				{
					InOutDetails.SchemaErrorPaths.Add(Path);
				}
			}
		}

		void ApplyToolResultDetails(const TSharedPtr<FJsonObject>& Object, FUnrealAgentAcpToolCallUpdate& InOutDetails)
		{
			if (!Object.IsValid())
			{
				return;
			}

			if (InOutDetails.CanonicalAction.IsEmpty())
			{
				InOutDetails.CanonicalAction = ReadBoundedString(Object, TEXT("tool"));
			}
			if (InOutDetails.Code.IsEmpty())
			{
				InOutDetails.Code = ReadBoundedString(Object, TEXT("code"));
			}
			AddSchemaErrorPaths(Object, InOutDetails);

			const TSharedPtr<FJsonObject>* Meta = nullptr;
			if (InOutDetails.TraceId.IsEmpty() && Object->TryGetObjectField(TEXT("_meta"), Meta) && Meta != nullptr && Meta->IsValid())
			{
				FGuid TraceId;
				const FString Candidate = ReadBoundedString(*Meta, TEXT("traceId"));
				if (FGuid::Parse(Candidate, TraceId))
				{
					InOutDetails.TraceId = TraceId.ToString(EGuidFormats::DigitsWithHyphensLower);
				}
			}
		}

		void ApplyTextContentDetails(const TSharedPtr<FJsonObject>& Result, FUnrealAgentAcpToolCallUpdate& InOutDetails)
		{
			const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
			if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("content"), Content) || Content == nullptr)
			{
				return;
			}

			for (const TSharedPtr<FJsonValue>& Value : *Content)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					continue;
				}
				TSharedPtr<FJsonObject> Parsed;
				if (ParseJsonObject(ReadBoundedString(Value->AsObject(), TEXT("text")), Parsed))
				{
					ApplyToolResultDetails(Parsed, InOutDetails);
				}
			}
		}
	}

	FString BuildLogSafeJsonRpc(const FString& JsonText)
	{
		TSharedPtr<FJsonObject> Root;
		if (!ParseJsonObject(JsonText, Root) || !Root.IsValid())
		{
			return JsonText.Contains(TEXT("\"image\"")) && JsonText.Contains(TEXT("\"data\"")) ? TEXT("<malformed ACP image message redacted>") : JsonText;
		}
		return RedactImageData(Root) ? SerializeJsonObject(Root) : JsonText;
	}

	bool DoesInitializeResultSupportImagePrompts(const TSharedPtr<FJsonObject>& InitializeResult)
	{
		if (!InitializeResult.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonObject>* AgentCapabilities = nullptr;
		const TSharedPtr<FJsonObject>* PromptCapabilities = nullptr;
		bool bSupportsImages = false;
		return InitializeResult->TryGetObjectField(TEXT("agentCapabilities"), AgentCapabilities) && AgentCapabilities && AgentCapabilities->IsValid() &&
			(*AgentCapabilities)->TryGetObjectField(TEXT("promptCapabilities"), PromptCapabilities) && PromptCapabilities && PromptCapabilities->IsValid() &&
			(*PromptCapabilities)->TryGetBoolField(TEXT("image"), bSupportsImages) && bSupportsImages;
	}

	TArray<TSharedPtr<FJsonValue>> BuildPromptContentBlocks(const FString& PromptText, const TArray<FUnrealAgentAcpPromptImage>& Images)
	{
		TArray<TSharedPtr<FJsonValue>> PromptBlocks;
		TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
		TextBlock->SetStringField(TEXT("type"), TEXT("text"));
		TextBlock->SetStringField(TEXT("text"), PromptText);
		PromptBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));

		for (const FUnrealAgentAcpPromptImage& Image : Images)
		{
			TSharedPtr<FJsonObject> ImageBlock = MakeShared<FJsonObject>();
			ImageBlock->SetStringField(TEXT("type"), TEXT("image"));
			ImageBlock->SetStringField(TEXT("mimeType"), Image.MimeType.IsEmpty() ? TEXT("image/png") : Image.MimeType);
			ImageBlock->SetStringField(TEXT("data"), Image.Base64Data);
			PromptBlocks.Add(MakeShared<FJsonValueObject>(ImageBlock));
		}

		return PromptBlocks;
	}

	int32 EstimateSessionPromptJsonRpcChars(const FString& PromptText, const TArray<FUnrealAgentAcpPromptImage>& Images)
	{
		int32 CharacterCount = 256 + PromptText.Len();
		for (const FUnrealAgentAcpPromptImage& Image : Images)
		{
			CharacterCount += 96 + Image.MimeType.Len() + Image.Base64Data.Len();
		}
		return CharacterCount;
	}

	bool IsSessionPromptJsonRpcWithinLimit(const FString& PromptText, const TArray<FUnrealAgentAcpPromptImage>& Images, const int32 MaximumChars)
	{
		return EstimateSessionPromptJsonRpcChars(PromptText, Images) <= MaximumChars;
	}

	FString NormalizeLaunchPath(FString Path)
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::CollapseRelativeDirectories(Path);
		FPaths::MakePlatformFilename(Path);
		return Path;
	}

	FString QuoteCommandLineArgument(FString Argument)
	{
		Argument.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return FString::Printf(TEXT("\"%s\""), *Argument);
	}

	bool IsSessionModeConfigId(const FString& ConfigId)
	{
		return ConfigId.Equals(TEXT("mode"), ESearchCase::IgnoreCase) || ConfigId.EndsWith(TEXT("_mode"), ESearchCase::IgnoreCase) ||
			ConfigId.EndsWith(TEXT(".mode"), ESearchCase::IgnoreCase);
	}

	bool BuildLaunchSpecForResolvedAdapterPath(const FString& AdapterPath, const FString& CommandInterpreterPath, const FString& PowerShellPath,
		FWorldDataCodexAcpLaunchSpec& OutLaunchSpec)
	{
		if (AdapterPath.IsEmpty())
		{
			return false;
		}

		const FString FullPath = NormalizeLaunchPath(AdapterPath);
		const FString Extension = FPaths::GetExtension(FullPath).ToLower();
		OutLaunchSpec = FWorldDataCodexAcpLaunchSpec();
		OutLaunchSpec.DisplayPath = FullPath;

#if PLATFORM_WINDOWS
		if (Extension == TEXT("cmd") || Extension == TEXT("bat"))
		{
			if (CommandInterpreterPath.IsEmpty())
			{
				return false;
			}
			OutLaunchSpec.Executable = CommandInterpreterPath;
			OutLaunchSpec.Arguments = FString::Printf(TEXT("/d /s /c \"\"%s\"\""), *FullPath);
			return true;
		}

		if (Extension == TEXT("ps1"))
		{
			if (PowerShellPath.IsEmpty())
			{
				return false;
			}
			OutLaunchSpec.Executable = PowerShellPath;
			OutLaunchSpec.Arguments = FString::Printf(TEXT("-NoProfile -ExecutionPolicy Bypass -File %s"), *QuoteCommandLineArgument(FullPath));
			return true;
		}
#endif

		OutLaunchSpec.Executable = FullPath;
		OutLaunchSpec.Arguments.Empty();
		return true;
	}

	void AddUniqueNormalizedPath(TArray<FString>& Paths, const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return;
		}

		const FString FullPath = NormalizeLaunchPath(Path);
		if (!Paths.Contains(FullPath))
		{
			Paths.Add(FullPath);
		}
	}

	TArray<FString> GetAdapterCandidateNames()
	{
		TArray<FString> Names;
#if PLATFORM_WINDOWS
		Names.Add(TEXT("codex-acp.exe"));
		Names.Add(TEXT("codex-acp.cmd"));
		Names.Add(TEXT("codex-acp.bat"));
		Names.Add(TEXT("codex-acp.ps1"));
#else
		Names.Add(TEXT("codex-acp"));
#endif
		return Names;
	}

	bool IsBaselineReasoningEffort(const FString& Effort)
	{
		return Effort == TEXT("none") || Effort == TEXT("minimal") || Effort == TEXT("low") || Effort == TEXT("medium") || Effort == TEXT("high") || Effort == TEXT("xhigh");
	}

	FString BuildConfirmedModelInstruction(const FString& AppliedModelId, const FString& DisplayName)
	{
		if (AppliedModelId.IsEmpty())
		{
			return TEXT("当前 ACP 会话尚未返回模型确认值；不要猜测或声称正在使用某个具体模型。");
		}

		const FString Identity = DisplayName.IsEmpty() ? AppliedModelId : FString::Printf(TEXT("%s（模型 ID：%s）"), *DisplayName, *AppliedModelId);
		return FString::Printf(TEXT("当前 ACP 会话已确认实际调用模型为 %s。"
									"该值来自 ACP session config 的 currentValue，不是推测。"
									"用户询问当前模型时，直接准确回答这个名称和模型 ID，"
									"不要声称无法读取模型标识。"),
			*Identity);
	}

	TSharedPtr<FJsonValue> ExtractRpcId(const TSharedPtr<FJsonObject>& Message)
	{
		if (!Message.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonValue>* Field = Message->Values.Find(TEXT("id"));
		return Field ? *Field : nullptr;
	}

	FString GetOptionalString(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(FieldName, Value) ? Value : FString();
	}

	int32 GetOptionalInt(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		if (!Object.IsValid())
		{
			return 0;
		}

		const TSharedPtr<FJsonValue> Value = Object->TryGetField(FieldName);
		if (!Value.IsValid())
		{
			return 0;
		}
		if (Value->Type == EJson::Number)
		{
			return static_cast<int32>(Value->AsNumber());
		}
		if (Value->Type == EJson::String)
		{
			return FCString::Atoi(*Value->AsString());
		}
		return 0;
	}

	bool ParseJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
	{
		OutObject.Reset();
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}

		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	FWorldDataCodexTurnStatus BuildPromptCompletionStatus(const TSharedPtr<FJsonObject>& Result, const FString& Message)
	{
		FWorldDataCodexTurnStatus Status;
		Status.State = EWorldDataCodexTurnState::Completed;
		Status.Message = Message;
		Status.StopReason = GetOptionalString(Result, TEXT("stopReason"));

		const TSharedPtr<FJsonObject>* Usage = nullptr;
		if (Result.IsValid() && Result->TryGetObjectField(TEXT("usage"), Usage) && Usage != nullptr && Usage->IsValid())
		{
			Status.InputTokens = FMath::Max(0, GetOptionalInt(*Usage, TEXT("inputTokens")));
			Status.OutputTokens = FMath::Max(0, GetOptionalInt(*Usage, TEXT("outputTokens")));
			Status.bHasUsage = true;
		}
		return Status;
	}

	TSharedRef<FJsonObject> BuildHttpMcpServer(const FString& Name, const FString& Url, const FString& TokenHeaderName, const FString& Token, const FString& ApprovalClientId,
		const FString& ProviderId)
	{
		TSharedRef<FJsonObject> Server = MakeShared<FJsonObject>();
		Server->SetStringField(TEXT("type"), TEXT("http"));
		Server->SetStringField(TEXT("name"), Name);
		Server->SetStringField(TEXT("url"), Url);

		TArray<TSharedPtr<FJsonValue>> Headers;
		auto AddHeader = [&Headers](const FString& HeaderName, const FString& Value)
		{
			TSharedRef<FJsonObject> Header = MakeShared<FJsonObject>();
			Header->SetStringField(TEXT("name"), HeaderName);
			Header->SetStringField(TEXT("value"), Value);
			Headers.Add(MakeShared<FJsonValueObject>(Header));
		};
		AddHeader(TokenHeaderName, Token);
		AddHeader(TEXT("X-WorldData-ACP-Client"), ApprovalClientId);
		AddHeader(TEXT("X-WorldData-ACP-Provider"), ProviderId);
		Server->SetArrayField(TEXT("headers"), Headers);
		return Server;
	}

	void ExtractToolCallResultDetails(const TSharedPtr<FJsonObject>& Update, FUnrealAgentAcpToolCallUpdate& InOutDetails)
	{
		if (!Update.IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonObject>* RawOutput = nullptr;
		if (!Update->TryGetObjectField(TEXT("rawOutput"), RawOutput) || RawOutput == nullptr || !RawOutput->IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (!(*RawOutput)->TryGetObjectField(TEXT("result"), Result) || Result == nullptr || !Result->IsValid())
		{
			return;
		}

		ApplyToolResultDetails(*Result, InOutDetails);
		const TSharedPtr<FJsonObject>* StructuredContent = nullptr;
		if ((*Result)->TryGetObjectField(TEXT("structuredContent"), StructuredContent) && StructuredContent != nullptr && StructuredContent->IsValid())
		{
			ApplyToolResultDetails(*StructuredContent, InOutDetails);
		}
		ApplyTextContentDetails(*Result, InOutDetails);
	}

	bool TryGetMcpStartupFailure(const TSharedPtr<FJsonObject>& Update, const FString& ExpectedServerName, FString& OutError)
	{
		OutError.Empty();
		if (!Update.IsValid() || GetOptionalString(Update, TEXT("sessionUpdate")) != TEXT("tool_call") || GetOptionalString(Update, TEXT("status")) != TEXT("failed"))
		{
			return false;
		}
		const FString ExpectedTitle = FString::Printf(TEXT("mcp__%s__startup"), *ExpectedServerName);
		const FString ToolCallId = GetOptionalString(Update, TEXT("toolCallId"));
		const FString Title = GetOptionalString(Update, TEXT("title"));
		if (!ToolCallId.StartsWith(TEXT("mcp_startup.")) || Title != ExpectedTitle)
		{
			return false;
		}
		OutError = FString::Printf(TEXT("ACP failed to start the injected UnrealAgent MCP server '%s'."), *ExpectedServerName);
		return true;
	}

	void ExtractCompleteJsonRpcFrames(FString& InOutBuffer, TArray<FString>& OutFrames)
	{
		int32 LineStart = 0;
		for (int32 Index = 0; Index < InOutBuffer.Len(); ++Index)
		{
			if (InOutBuffer[Index] != TEXT('\n'))
			{
				continue;
			}

			const FString Line = InOutBuffer.Mid(LineStart, Index - LineStart).TrimStartAndEnd();
			if (!Line.IsEmpty())
			{
				OutFrames.Add(Line);
			}
			LineStart = Index + 1;
		}
		if (LineStart > 0)
		{
			InOutBuffer.RightChopInline(LineStart);
		}

		// 少数适配器会直接拼接 JSON 对象而不添加换行。
		while (true)
		{
			int32 ObjectStart = INDEX_NONE;
			InOutBuffer.FindChar(TEXT('{'), ObjectStart);
			if (ObjectStart == INDEX_NONE)
			{
				break;
			}
			if (ObjectStart > 0)
			{
				InOutBuffer.RightChopInline(ObjectStart);
			}

			int32 Depth = 0;
			bool bInString = false;
			bool bEscape = false;
			int32 ObjectEnd = INDEX_NONE;
			for (int32 Index = 0; Index < InOutBuffer.Len(); ++Index)
			{
				const TCHAR Character = InOutBuffer[Index];
				if (bEscape)
				{
					bEscape = false;
					continue;
				}
				if (bInString)
				{
					if (Character == TEXT('\\'))
					{
						bEscape = true;
					}
					else if (Character == TEXT('"'))
					{
						bInString = false;
					}
					continue;
				}
				if (Character == TEXT('"'))
				{
					bInString = true;
					continue;
				}
				if (Character == TEXT('{'))
				{
					++Depth;
				}
				else if (Character == TEXT('}'))
				{
					--Depth;
					if (Depth == 0)
					{
						ObjectEnd = Index;
						break;
					}
				}
			}

			if (ObjectEnd == INDEX_NONE)
			{
				break;
			}

			OutFrames.Add(InOutBuffer.Left(ObjectEnd + 1));
			InOutBuffer.RightChopInline(ObjectEnd + 1);
		}
	}

	TSharedPtr<FJsonObject> BuildJsonRpcErrorResponse(const TSharedPtr<FJsonObject>& Request, int32 Code, const FString& Message)
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);

		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		if (Request.IsValid())
		{
			if (const TSharedPtr<FJsonValue>* Id = Request->Values.Find(TEXT("id")))
			{
				Response->SetField(TEXT("id"), *Id);
			}
			else
			{
				Response->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
			}
		}
		Response->SetObjectField(TEXT("error"), Error);
		return Response;
	}

	FString GetExecutionPolicyInstruction(const EWorldDataAgentMode AgentMode, const EWorldDataApprovalPolicy ApprovalPolicy, const EWorldDataSelfRepairPolicy SelfRepairPolicy)
	{
		const TCHAR* AgentText = AgentMode == EWorldDataAgentMode::Chat ? TEXT("对话模式：仅回答，不调用 MCP 工具。")
			: AgentMode == EWorldDataAgentMode::Plan                    ? TEXT("计划模式：可调用只读 MCP 工具理解项目，但不得修改项目。")
																		: TEXT("Agent 模式：可调用项目内 MCP 工具完成任务。");
		const TCHAR* ApprovalText = ApprovalPolicy == EWorldDataApprovalPolicy::FullProject
			? TEXT("项目内完全访问：项目沙箱内普通工具调用自动批准，工具声明的强制确认仍由面板审批，Shell 仍禁用；不得越过项目和资产沙箱。")
			: ApprovalPolicy == EWorldDataApprovalPolicy::RiskBased
			? TEXT("风险审批：只读和普通项目操作自动执行；高风险、破坏性、代码执行或工具强制确认操作直接发起调用并由面板审批。不要要求用户打字批准。")
			: TEXT("请求批准：每次需要修改项目时都直接发起工具调用，由面板显示允许/拒绝按钮；不要在对话中要求用户打字回复批准。");
		const TCHAR* RepairText = SelfRepairPolicy == EWorldDataSelfRepairPolicy::Automatic
			? TEXT("自动修复已开启：任务、工具或验证失败时，自动在项目沙箱内诊断根因、进行最小修补、重新编译/验证；修补无效时回滚并报告，不要停在仅建议方案。")
			: SelfRepairPolicy == EWorldDataSelfRepairPolicy::Suggest ? TEXT("建议修复：发现故障时可以诊断并提出修复方案，但不得自行进入修补、重试或回滚循环。")
																	  : TEXT("修复关闭：失败时报告原因，不得自行修改源码或配置来修复自身执行链。");
		return FString::Printf(TEXT("%s\n%s\n%s"), AgentText, ApprovalText, RepairText);
	}

	EUnrealAgentMCPToolApprovalAction ResolveMcpToolApprovalAction(const EWorldDataAgentMode AgentMode, const EWorldDataApprovalPolicy ApprovalPolicy,
		const FUnrealAgentMCPToolApprovalRequest& Request)
	{
		if (AgentMode == EWorldDataAgentMode::Chat)
		{
			return EUnrealAgentMCPToolApprovalAction::Deny;
		}
		if (AgentMode == EWorldDataAgentMode::Plan)
		{
			return Request.bReadOnly ? EUnrealAgentMCPToolApprovalAction::Allow : EUnrealAgentMCPToolApprovalAction::Deny;
		}
		if (Request.bReadOnly)
		{
			return EUnrealAgentMCPToolApprovalAction::Allow;
		}
		if (ApprovalPolicy == EWorldDataApprovalPolicy::FullProject)
		{
			return Request.bRequiresConfirmation ? EUnrealAgentMCPToolApprovalAction::Ask : EUnrealAgentMCPToolApprovalAction::Allow;
		}
		if (ApprovalPolicy == EWorldDataApprovalPolicy::AlwaysAsk)
		{
			return EUnrealAgentMCPToolApprovalAction::Ask;
		}
		return Request.bHighRisk || Request.bRequiresConfirmation ? EUnrealAgentMCPToolApprovalAction::Ask : EUnrealAgentMCPToolApprovalAction::Allow;
	}

	TArray<FUnrealAgentAcpPermissionOption> ExtractPermissionOptions(const TSharedPtr<FJsonObject>& Params)
	{
		TArray<FUnrealAgentAcpPermissionOption> Options;
		const TArray<TSharedPtr<FJsonValue>>* OptionValues = nullptr;
		if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("options"), OptionValues) || !OptionValues)
		{
			return Options;
		}

		for (const TSharedPtr<FJsonValue>& OptionValue : *OptionValues)
		{
			const TSharedPtr<FJsonObject> OptionObject = OptionValue.IsValid() && OptionValue->Type == EJson::Object ? OptionValue->AsObject() : nullptr;
			if (!OptionObject.IsValid())
			{
				continue;
			}

			FUnrealAgentAcpPermissionOption Option;
			Option.OptionId = GetOptionalString(OptionObject, TEXT("optionId"));
			Option.Name = GetOptionalString(OptionObject, TEXT("name"));
			Option.Kind = GetOptionalString(OptionObject, TEXT("kind"));
			if (!Option.OptionId.IsEmpty())
			{
				Options.Add(MoveTemp(Option));
			}
		}
		return Options;
	}

	bool IsShellPermissionRequest(const TSharedPtr<FJsonObject>& ToolCall)
	{
		const FString Probe = (GetOptionalString(ToolCall, TEXT("title")) + TEXT(" ") + GetOptionalString(ToolCall, TEXT("toolCallId")) + TEXT(" ") +
			GetOptionalString(ToolCall, TEXT("name")) + TEXT(" ") + GetOptionalString(ToolCall, TEXT("kind")))
								  .ToLower();
		return Probe.Contains(TEXT("terminal")) || Probe.Contains(TEXT("shell")) || Probe.Contains(TEXT("command")) || Probe.Contains(TEXT("exec"));
	}

	FString SelectAllowPermissionOptionId(const TArray<FUnrealAgentAcpPermissionOption>& Options)
	{
		FString OptionId = FindPermissionOption(Options, TEXT("allow"));
		if (OptionId.IsEmpty())
		{
			OptionId = FindPermissionOption(Options, TEXT("approve"));
		}
		return OptionId.IsEmpty() && !Options.IsEmpty() ? Options[0].OptionId : OptionId;
	}

	FString SelectDenyPermissionOptionId(const TArray<FUnrealAgentAcpPermissionOption>& Options)
	{
		FString OptionId = FindPermissionOption(Options, TEXT("reject"));
		if (OptionId.IsEmpty())
		{
			OptionId = FindPermissionOption(Options, TEXT("deny"));
		}
		return OptionId.IsEmpty() ? TEXT("deny") : OptionId;
	}

	FString GetPermissionRequestTitle(const TSharedPtr<FJsonObject>& ToolCall)
	{
		FString Title = GetOptionalString(ToolCall, TEXT("title"));
		if (Title.IsEmpty())
		{
			Title = GetOptionalString(ToolCall, TEXT("name"));
		}
		if (Title.IsEmpty())
		{
			Title = GetOptionalString(ToolCall, TEXT("toolCallId"));
		}
		return Title.IsEmpty() ? TEXT("未知工具请求") : Title;
	}

	bool IsAllowPermissionOption(const FString& OptionId)
	{
		const FString LowerOption = OptionId.ToLower();
		return LowerOption.Contains(TEXT("allow")) || LowerOption.Contains(TEXT("approve"));
	}

	bool IsModelCompatibleWithAdapter(const FString& ModelId, const FString& AdapterVersion)
	{
		if (ModelId.IsEmpty() || AdapterVersion.IsEmpty())
		{
			return true;
		}

		const bool bBundledAcp016 = AdapterVersion.Equals(TEXT("0.16.0")) || AdapterVersion.StartsWith(TEXT("0.16.0-"));
		if (!bBundledAcp016)
		{
			return true;
		}

		const FString NormalizedModelId = ModelId.ToLower();
		if (NormalizedModelId.StartsWith(TEXT("gpt-5.6")) || NormalizedModelId == TEXT("gpt-5.3-codex-spark"))
		{
			return false;
		}

		return true;
	}

	FString GetSafeFallbackModelIdForAdapter(const FString& AdapterVersion)
	{
		const bool bBundledAcp016 = AdapterVersion.Equals(TEXT("0.16.0")) || AdapterVersion.StartsWith(TEXT("0.16.0-"));
		return bBundledAcp016 ? TEXT("gpt-5.5") : FString();
	}

	FString FormatModelDisplayName(const FString& DisplayName)
	{
		FString Result = DisplayName;
		if (Result.StartsWith(TEXT("GPT-"), ESearchCase::IgnoreCase))
		{
			Result.RightChopInline(4);
		}
		Result.ReplaceInline(TEXT("-"), TEXT(" "));
		return Result;
	}

	TArray<FString> GetCursorModelModeLabels(const FString& ModelSelector)
	{
		TArray<FString> Labels;
		const int32 ParametersBegin = ModelSelector.Find(TEXT("["));
		const int32 ParametersEnd = ModelSelector.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (ParametersBegin == INDEX_NONE || ParametersEnd <= ParametersBegin + 1)
		{
			return Labels;
		}

		bool bFast = false;
		bool bThinking = false;
		FString ReasoningLevel;
		TArray<FString> Parameters;
		ModelSelector.Mid(ParametersBegin + 1, ParametersEnd - ParametersBegin - 1).ParseIntoArray(Parameters, TEXT(","), true);
		for (FString Parameter : Parameters)
		{
			FString Key;
			FString Value;
			if (!Parameter.Split(TEXT("="), &Key, &Value))
			{
				continue;
			}
			Key.TrimStartAndEndInline();
			Value.TrimStartAndEndInline();
			Key.ToLowerInline();
			Value.ToLowerInline();
			if (Key == TEXT("fast"))
			{
				bFast = Value == TEXT("true");
			}
			else if (Key == TEXT("thinking"))
			{
				bThinking = Value == TEXT("true");
			}
			else if (Key == TEXT("reasoning") || Key == TEXT("effort"))
			{
				ReasoningLevel = MoveTemp(Value);
			}
		}

		if (ReasoningLevel == TEXT("max"))
		{
			Labels.Add(TEXT("Max"));
		}
		if (bFast)
		{
			Labels.Add(TEXT("Fast"));
		}
		if (bThinking)
		{
			Labels.Add(TEXT("Thinking"));
		}
		if (!ReasoningLevel.IsEmpty() && ReasoningLevel != TEXT("max"))
		{
			if (ReasoningLevel == TEXT("xhigh"))
			{
				Labels.Add(TEXT("XHigh"));
			}
			else
			{
				Labels.Add(ReasoningLevel.Left(1).ToUpper() + ReasoningLevel.Mid(1));
			}
		}
		return Labels;
	}

	bool ParseCodexModelCatalog(const FString& JsonText, TArray<FWorldDataCodexModelCatalogEntry>& OutCatalog, FString& OutError)
	{
		OutCatalog.Empty();
		OutError.Empty();

		TSharedPtr<FJsonObject> Root;
		if (!ParseJsonObject(JsonText, Root))
		{
			OutError = TEXT("Codex CLI 返回的模型目录不是有效 JSON。");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* ModelValues = nullptr;
		if (!Root->TryGetArrayField(TEXT("models"), ModelValues) || !ModelValues)
		{
			OutError = TEXT("Codex CLI 模型目录缺少 models 数组。");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& ModelValue : *ModelValues)
		{
			const TSharedPtr<FJsonObject> Model = ModelValue.IsValid() && ModelValue->Type == EJson::Object ? ModelValue->AsObject() : nullptr;
			if (!Model.IsValid())
			{
				continue;
			}

			const FString Visibility = GetOptionalString(Model, TEXT("visibility"));
			if (!Visibility.IsEmpty() && !Visibility.Equals(TEXT("list"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FWorldDataCodexModelCatalogEntry Entry;
			Entry.Id = GetOptionalString(Model, TEXT("slug"));
			if (Entry.Id.IsEmpty())
			{
				continue;
			}
			Entry.DisplayName = FormatModelDisplayName(GetOptionalString(Model, TEXT("display_name")));
			if (Entry.DisplayName.IsEmpty())
			{
				Entry.DisplayName = Entry.Id;
			}
			Entry.Description = GetOptionalString(Model, TEXT("description"));
			Entry.DefaultReasoningEffort = GetOptionalString(Model, TEXT("default_reasoning_level"));

			const TArray<TSharedPtr<FJsonValue>>* ReasoningValues = nullptr;
			if (Model->TryGetArrayField(TEXT("supported_reasoning_levels"), ReasoningValues) && ReasoningValues)
			{
				for (const TSharedPtr<FJsonValue>& ReasoningValue : *ReasoningValues)
				{
					const TSharedPtr<FJsonObject> Reasoning = ReasoningValue.IsValid() && ReasoningValue->Type == EJson::Object ? ReasoningValue->AsObject() : nullptr;
					if (!Reasoning.IsValid())
					{
						continue;
					}

					FUnrealAgentAcpConfigOptionValue Option;
					Option.Value = GetOptionalString(Reasoning, TEXT("effort"));
					Option.Description = GetOptionalString(Reasoning, TEXT("description"));
					if (!Option.Value.IsEmpty())
					{
						Entry.ReasoningEfforts.Add(MoveTemp(Option));
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ServiceTiers = nullptr;
			if (Model->TryGetArrayField(TEXT("service_tiers"), ServiceTiers) && ServiceTiers)
			{
				for (const TSharedPtr<FJsonValue>& TierValue : *ServiceTiers)
				{
					const TSharedPtr<FJsonObject> Tier = TierValue.IsValid() && TierValue->Type == EJson::Object ? TierValue->AsObject() : nullptr;
					if (!Tier.IsValid())
					{
						continue;
					}

					FUnrealAgentAcpConfigOptionValue Option;
					const FString TierId = GetOptionalString(Tier, TEXT("id"));
					Option.Value = TierId.Equals(TEXT("priority"), ESearchCase::IgnoreCase) ? TEXT("fast") : TierId;
					Option.Name = GetOptionalString(Tier, TEXT("name"));
					Option.Description = GetOptionalString(Tier, TEXT("description"));
					if (!Option.Value.IsEmpty() &&
						!Entry.SpeedOptions.ContainsByPredicate(
							[&Option](const FUnrealAgentAcpConfigOptionValue& Existing)
							{
								return Existing.Value == Option.Value;
							}))
					{
						Entry.SpeedOptions.Add(MoveTemp(Option));
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* AdditionalTiers = nullptr;
			if (Model->TryGetArrayField(TEXT("additional_speed_tiers"), AdditionalTiers) && AdditionalTiers)
			{
				for (const TSharedPtr<FJsonValue>& TierValue : *AdditionalTiers)
				{
					const FString Tier = TierValue.IsValid() ? TierValue->AsString() : FString();
					if (!Tier.IsEmpty() &&
						!Entry.SpeedOptions.ContainsByPredicate(
							[&Tier](const FUnrealAgentAcpConfigOptionValue& Existing)
							{
								return Existing.Value == Tier;
							}))
					{
						FUnrealAgentAcpConfigOptionValue Option;
						Option.Value = Tier;
						Entry.SpeedOptions.Add(MoveTemp(Option));
					}
				}
			}

			OutCatalog.Add(MoveTemp(Entry));
		}

		if (OutCatalog.IsEmpty())
		{
			OutError = TEXT("当前 Codex CLI 没有返回可显示的模型。");
			return false;
		}

		OutCatalog[0].bDefault = true;
		return true;
	}

	bool HasActiveTurnState(const bool bHasPendingPrompt, const bool bPromptInFlight, const int32 PendingPermissionCount)
	{
		return bHasPendingPrompt || bPromptInFlight || PendingPermissionCount > 0;
	}

	bool ShouldFailCloseUserTurn(const bool bPromptInFlight, const bool bHasPendingPrompt, const bool bHasPendingPromptImages, const bool bHadUserTurnBeforeCleanup)
	{
		return bHadUserTurnBeforeCleanup || bPromptInFlight || bHasPendingPrompt || bHasPendingPromptImages;
	}

	bool ShouldConsumeContextReplay(const EWorldDataCodexTurnState State)
	{
		return State == EWorldDataCodexTurnState::Received;
	}

	bool CanChangeConfigState(const bool bPromptInFlight, const int32 PendingPermissionCount)
	{
		return !bPromptInFlight && PendingPermissionCount == 0;
	}

	bool ShouldQueueLatestConfigSelection(const int32 PendingConfigRequestCount)
	{
		return PendingConfigRequestCount > 0;
	}

	bool ShouldIgnoreSessionScopedMessage(const FString& CurrentSessionId, const FString& IncomingSessionId, const bool bCreatingSession)
	{
		if (IncomingSessionId.IsEmpty())
		{
			// 兼容没有回传 sessionId 的旧适配器通知。
			return false;
		}
		if (!CurrentSessionId.IsEmpty())
		{
			return IncomingSessionId != CurrentSessionId;
		}
		return bCreatingSession;
	}
}
