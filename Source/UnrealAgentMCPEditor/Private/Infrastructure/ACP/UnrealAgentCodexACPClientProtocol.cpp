// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentCodexACPClientProtocol.cpp
 * @brief Codex ACP JSON-RPC 编解码、响应匹配与服务端方法路由。
 */

#include "Application/ACP/UnrealAgentCodexACPClient.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"

#include "Infrastructure/ACP/UnrealAgentCodexACPClientInternal.h"
#include "Application/ACP/UnrealAgentCodexACPClientRules.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"

namespace AcpRules = WorldDataCodexAcpRules;

namespace
{
	bool IsReadOnlyToolName(const FString& ToolName)
	{
		const FString Name = ToolName.ToLower();
		static const TCHAR* ReadOnlyPrefixes[] = { TEXT("get_"), TEXT("read_"), TEXT("list_"), TEXT("find_"), TEXT("search_"), TEXT("describe_"), TEXT("validate_"), TEXT("ping"),
			TEXT("status"), TEXT("project.get"), TEXT("project.read"), TEXT("project.list") };
		for (const TCHAR* Prefix : ReadOnlyPrefixes)
		{
			if (Name.StartsWith(Prefix) || Name.Contains(FString(TEXT(".")) + Prefix))
			{
				return true;
			}
		}
		return false;
	}

	FString GetMcpToolName(const TSharedPtr<FJsonObject>& McpMessage)
	{
		if (!McpMessage.IsValid())
		{
			return FString();
		}
		const TSharedPtr<FJsonObject>* Params = nullptr;
		if (McpMessage->TryGetObjectField(TEXT("params"), Params) && Params && Params->IsValid())
		{
			FString ToolName;
			(*Params)->TryGetStringField(TEXT("name"), ToolName);
			const TSharedPtr<FJsonObject>* Arguments = nullptr;
			if ((*Params)->TryGetObjectField(TEXT("arguments"), Arguments) && Arguments && Arguments->IsValid())
			{
				FString Action;
				if ((*Arguments)->TryGetStringField(TEXT("action"), Action) && !Action.IsEmpty())
				{
					ToolName += TEXT(".") + Action;
				}
			}
			return ToolName;
		}
		return FString();
	}

	FString GetMcpTransportToolName(const TSharedPtr<FJsonObject>& McpMessage)
	{
		const TSharedPtr<FJsonObject>* Params = nullptr;
		FString ToolName;
		if (McpMessage.IsValid() && McpMessage->TryGetObjectField(TEXT("params"), Params) && Params && Params->IsValid())
		{
			(*Params)->TryGetStringField(TEXT("name"), ToolName);
		}
		return ToolName;
	}

	TSharedPtr<FJsonObject> GetMcpToolArguments(const TSharedPtr<FJsonObject>& McpMessage)
	{
		const TSharedPtr<FJsonObject>* Params = nullptr;
		const TSharedPtr<FJsonObject>* Arguments = nullptr;
		return McpMessage.IsValid() && McpMessage->TryGetObjectField(TEXT("params"), Params) && Params && Params->IsValid() &&
				(*Params)->TryGetObjectField(TEXT("arguments"), Arguments) && Arguments && Arguments->IsValid()
			? *Arguments
			: MakeShared<FJsonObject>();
	}

	void ApplyToolConfirmation(const TSharedPtr<FJsonObject>& McpMessage, const FString& Argument, const FString& Value)
	{
		const TSharedPtr<FJsonObject>* Params = nullptr;
		if (!McpMessage.IsValid() || !McpMessage->TryGetObjectField(TEXT("params"), Params) || !Params || !Params->IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject>* Arguments = nullptr;
		if ((*Params)->TryGetObjectField(TEXT("arguments"), Arguments) && Arguments && Arguments->IsValid())
		{
			(*Arguments)->SetStringField(Argument, Value);
		}
		else
		{
			TSharedPtr<FJsonObject> NewArguments = MakeShared<FJsonObject>();
			NewArguments->SetStringField(Argument, Value);
			(*Params)->SetObjectField(TEXT("arguments"), NewArguments);
		}
	}
}

int32 FUnrealAgentCodexACPClient::SendRpc(const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	const int32 Id = NextRpcId++;
	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetNumberField(TEXT("id"), Id);
	Message->SetStringField(TEXT("method"), Method);
	if (Params.IsValid())
	{
		Message->SetObjectField(TEXT("params"), Params);
	}
	SendRaw(AcpRules::SerializeJsonObject(Message));
	return Id;
}

void FUnrealAgentCodexACPClient::SendRpcResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
	Message->SetObjectField(TEXT("result"), Result.IsValid() ? Result : MakeShared<FJsonObject>());
	SendRaw(AcpRules::SerializeJsonObject(Message));
}

void FUnrealAgentCodexACPClient::SendRpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& MessageText)
{
	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetNumberField(TEXT("code"), Code);
	Error->SetStringField(TEXT("message"), MessageText);

	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
	Message->SetObjectField(TEXT("error"), Error);
	SendRaw(AcpRules::SerializeJsonObject(Message));
}

void FUnrealAgentCodexACPClient::SendPermissionOutcome(const TSharedPtr<FJsonValue>& Id, const FString& OptionId)
{
	TSharedPtr<FJsonObject> Outcome = MakeShared<FJsonObject>();
	Outcome->SetStringField(TEXT("outcome"), TEXT("selected"));
	Outcome->SetStringField(TEXT("optionId"), OptionId.IsEmpty() ? TEXT("deny") : OptionId);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("outcome"), Outcome);
	SendRpcResult(Id, Result);
}

bool FUnrealAgentCodexACPClient::ResolveLocalMcpPermission(const int32 RequestId, const bool bAllow)
{
	FPendingLocalMcpPermission* Pending = PendingLocalMcpPermissions.Find(RequestId);
	if (!Pending)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (bAllow)
	{
		if (!Pending->ConfirmationArgument.IsEmpty() && !Pending->ConfirmationValue.IsEmpty())
		{
			ApplyToolConfirmation(Pending->McpMessage, Pending->ConfirmationArgument, Pending->ConfirmationValue);
		}
		Result->SetObjectField(TEXT("message"), FUnrealAgentMCPServer::DispatchJsonRpcRequest(Pending->McpMessage));
	}
	else
	{
		Result->SetObjectField(TEXT("message"), AcpRules::BuildJsonRpcErrorResponse(Pending->McpMessage, -32003, TEXT("用户已在审批卡片中拒绝本次项目修改。")));
	}
	const TSharedPtr<FJsonValue> ResponseId = Pending->ResponseId;
	PendingLocalMcpPermissions.Remove(RequestId);
	SendRpcResult(ResponseId, Result);
	return true;
}

void FUnrealAgentCodexACPClient::SendRaw(const FString& Json)
{
	if (!Process.IsValid() || !Process->IsRunning())
	{
		Fail(FString::Printf(TEXT("%s ACP 进程未运行。"), *GetAgentDisplayName()));
		return;
	}

	const FString LogSafeJson = AcpRules::BuildLogSafeJsonRpc(Json);
	UE_LOG(LogUnrealAgentCodexACP, Verbose, TEXT("ACP send: %s"), *LogSafeJson);
	Process->SendWhenReady(Json + TEXT("\n"));
}

bool FUnrealAgentCodexACPClient::CancelActivePrompt()
{
	if (!HasActiveTurn() || SessionId.IsEmpty() || !IsRunning())
	{
		return false;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), SessionId);
	TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Message->SetStringField(TEXT("method"), TEXT("session/cancel"));
	Message->SetObjectField(TEXT("params"), Params);
	SendRaw(AcpRules::SerializeJsonObject(Message));
	EmitStatus(FString::Printf(TEXT("正在停止当前 %s 回合并应用引导消息…"), *GetAgentDisplayName()));
	return true;
}

void FUnrealAgentCodexACPClient::ConsumeOutput(const FString& Output)
{
	StdoutBuffer += Output;

	TArray<FString> Frames;
	AcpRules::ExtractCompleteJsonRpcFrames(StdoutBuffer, Frames);
	for (const FString& Frame : Frames)
	{
		ProcessLine(Frame);
	}
}

void FUnrealAgentCodexACPClient::ProcessLine(const FString& Line)
{
	const FString LogSafeLine = AcpRules::BuildLogSafeJsonRpc(Line);
	UE_LOG(LogUnrealAgentCodexACP, Verbose, TEXT("ACP recv: %s"), *LogSafeLine);

	TSharedPtr<FJsonObject> Message;
	if (!AcpRules::ParseJsonObject(Line, Message))
	{
		UE_LOG(LogUnrealAgentCodexACP, Warning, TEXT("ACP stdout line was not JSON: %s"), *Line);
		FString Trimmed = Line;
		Trimmed.TrimStartAndEndInline();
		if (!Trimmed.IsEmpty())
		{
			const FString LowerLine = Trimmed.ToLower();
			EmitText(FString::Printf(TEXT("\n\n[adapter] %s\n"), *Trimmed));
			if (LowerLine.Contains(TEXT("authentication required")))
			{
				Fail(FString::Printf(TEXT("%s ACP 返回 Authentication required。请使用面板账户入口或该 Agent 的官方 CLI 完成登录。"), *GetAgentDisplayName()));
			}
			else if (LowerLine.Contains(TEXT("cannot find module")) || LowerLine.Contains(TEXT("module_not_found")))
			{
				Fail(FString::Printf(TEXT("%s ACP 启动后找不到 Node 模块。请在设置中选择有效的 CLI/adapter，避免 PATH 上已失效的 npm shim。"), *GetAgentDisplayName()));
			}
		}
		return;
	}

	const TSharedPtr<FJsonValue> Id = AcpRules::ExtractRpcId(Message);
	if (Id.IsValid() && (Message->HasField(TEXT("result")) || Message->HasField(TEXT("error"))))
	{
		if (Id->Type == EJson::Number)
		{
			HandleRpcResponse(static_cast<int32>(Id->AsNumber()), Message->HasField(TEXT("result")) ? Message->GetObjectField(TEXT("result")) : nullptr,
				Message->HasField(TEXT("error")) ? Message->GetObjectField(TEXT("error")) : nullptr);
		}
		return;
	}

	if (Message->HasField(TEXT("method")))
	{
		HandleMethod(Message->GetStringField(TEXT("method")), Message);
	}
}

void FUnrealAgentCodexACPClient::HandleRpcResponse(int32 Id, const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Error)
{
	if (Error.IsValid())
	{
		const FString ErrorMessage = AcpRules::GetOptionalString(Error, TEXT("message"));
		const bool bHadUserTurnBeforeCleanup = Id == PromptRpcId && bPromptInFlight;
		FPendingConfigOptionRequest ConfigRequest;
		const bool bWasConfigOptionRequest = ConfigOptionRpcIds.RemoveAndCopyValue(Id, ConfigRequest);
		if (bWasConfigOptionRequest)
		{
			const bool bModel = ConfigRequest.LocalConfigId.Equals(TEXT("model"), ESearchCase::IgnoreCase);
			const bool bReasoning = ConfigRequest.LocalConfigId.Equals(TEXT("model_reasoning_effort"), ESearchCase::IgnoreCase);
			const FString LatestDesiredValue = bModel ? SelectedModelId : bReasoning ? SelectedReasoningEffort : SelectedSpeed;
			const bool bSuperseded = !LatestDesiredValue.IsEmpty() && LatestDesiredValue != ConfigRequest.RequestedValue;
			if (!bSuperseded && bModel)
			{
				SelectedModelId = ConfigRequest.PreviousValue;
			}
			else if (!bSuperseded && bReasoning)
			{
				if (ConfigRequest.PreviousValue != ConfigRequest.RequestedValue)
				{
					SelectedReasoningEffort = ConfigRequest.PreviousValue;
				}
			}
			else if (!bSuperseded && ConfigRequest.LocalConfigId.Equals(TEXT("service_tier"), ESearchCase::IgnoreCase))
			{
				SelectedSpeed = ConfigRequest.PreviousValue;
			}
			DeferredConfigIds.Remove(ConfigRequest.LocalConfigId);
			if (bSuperseded || ConfigRequest.PreviousValue == ConfigRequest.RequestedValue)
			{
				DeferredConfigIds.Add(ConfigRequest.LocalConfigId);
			}
			RebuildConfigOptions();
			if (bSuperseded)
			{
				if (bModel && !ReconcileSessionModel())
				{
					EmitStatus(TEXT("上一次模型切换失败，正在应用最新选择…"));
					return;
				}
				if (ApplyNextDeferredConfigOption())
				{
					EmitStatus(TEXT("上一次配置切换失败，正在应用最新选择…"));
					return;
				}
			}
			EmitStatus(FString::Printf(TEXT("%s 配置切换失败：%s"), *GetAgentDisplayName(), ErrorMessage.IsEmpty() ? TEXT("ACP 返回未知错误。") : *ErrorMessage));
			return;
		}
		if (Id == PromptRpcId && TryRecoverFromMissingModelMetadata(ErrorMessage))
		{
			return;
		}
		const FString EffectiveError = Id == PromptRpcId && !PromptModelDiagnostic.IsEmpty()
			? FString::Printf(TEXT("当前 codex-acp %s 缺少模型 %s 的运行时元数据；"
								   "请改用兼容模型或升级适配器。"),
				  ActiveAdapterVersion.IsEmpty() ? TEXT("未知版本") : *ActiveAdapterVersion, AppliedModelId.IsEmpty() ? TEXT("未知") : *AppliedModelId)
			: ErrorMessage.IsEmpty() ? TEXT("ACP 返回未知错误。")
									 : ErrorMessage;
		// 先释放飞行中状态再广播失败。上层结构化调用可能在 OnError 中立刻
		// 发起一次有限重试；若清理顺序相反，新请求会被误判为 Busy。
		if (Id == PromptRpcId)
		{
			bPromptInFlight = false;
			ActivePrompt.Empty();
			ActivePromptImages.Empty();
			PromptModelDiagnostic.Empty();
		}
		if (Id == SessionRpcId)
		{
			bCreatingSession = false;
		}
		Fail(EffectiveError, bHadUserTurnBeforeCleanup);
		return;
	}

	if (Id == InitRpcId)
	{
		ActiveAdapterVersion.Empty();
		bAgentSupportsImagePrompts = AcpRules::DoesInitializeResultSupportImagePrompts(Result);
		if (Result.IsValid() && Result->HasField(TEXT("agentInfo")))
		{
			const TSharedPtr<FJsonObject> AgentInfo = Result->GetObjectField(TEXT("agentInfo"));
			ActiveAdapterVersion = AcpRules::GetOptionalString(AgentInfo, TEXT("version"));
			RebuildCompatibleModelCatalog();
		}
		bInitialized = true;
		EmitStatus(ActiveAdapterVersion.IsEmpty() ? TEXT("ACP 初始化完成。") : FString::Printf(TEXT("ACP 初始化完成，适配器版本：%s。"), *ActiveAdapterVersion));
		StartSessionIfReady();
		SendPendingPromptIfReady();
		return;
	}

	if (Id == SessionRpcId)
	{
		bCreatingSession = false;
		SessionId = AcpRules::GetOptionalString(Result, TEXT("sessionId"));
		if (SessionId.IsEmpty())
		{
			Fail(TEXT("session/new 没有返回 sessionId，可能是 codex-acp 协议版本不匹配。"));
			return;
		}
		UpdateConfigOptions(Result);
		if (AgentProvider == EUnrealAgentACPProvider::Codex && AppliedModelId.IsEmpty())
		{
			Fail(TEXT("ACP 会话没有返回当前模型，已阻止发送消息以避免 UI 与实际模型不一致。"));
			return;
		}
		if (!ReconcileSessionModel())
		{
			return;
		}
		DeferredConfigIds.Remove(TEXT("model"));
		if (ApplyNextDeferredConfigOption())
		{
			EmitStatus(TEXT("ACP 会话已就绪，正在后台应用已选择的配置…"));
			return;
		}
		const FString ProviderName = UnrealAgentACPProviderModel::GetProvider(AgentProvider).DisplayName.ToString();
		const FString DisplayedModelId = AppliedModelId.IsEmpty() ? FString(TEXT("由 Agent 管理")) : AppliedModelId;
		EmitStatus(FString::Printf(TEXT("%s ACP 会话已创建，实际模型：%s。"), *ProviderName, *DisplayedModelId));
		SendPendingPromptIfReady();
		return;
	}

	FPendingConfigOptionRequest ConfigRequest;
	if (ConfigOptionRpcIds.RemoveAndCopyValue(Id, ConfigRequest))
	{
		UpdateConfigOptions(Result);
		const bool bModel = ConfigRequest.LocalConfigId.Equals(TEXT("model"), ESearchCase::IgnoreCase);
		const bool bReasoning = ConfigRequest.LocalConfigId.Equals(TEXT("model_reasoning_effort"), ESearchCase::IgnoreCase);
		const FString LatestDesiredValue = bModel ? SelectedModelId : bReasoning ? SelectedReasoningEffort : SelectedSpeed;
		const bool bSuperseded = !LatestDesiredValue.IsEmpty() && LatestDesiredValue != ConfigRequest.RequestedValue;
		if (bModel)
		{
			if (AppliedModelId.IsEmpty() || AppliedModelId != ConfigRequest.RequestedValue)
			{
				const FString ReportedModel = AppliedModelId;
				if (!bSuperseded)
				{
					SelectedModelId = ConfigRequest.PreviousValue;
				}
				RebuildConfigOptions();
				if (bSuperseded)
				{
					if (!ReconcileSessionModel())
					{
						EmitStatus(TEXT("ACP 未确认上一次模型，正在应用最新选择…"));
						return;
					}
					DeferredConfigIds.Remove(TEXT("model"));
					if (ApplyNextDeferredConfigOption())
					{
						EmitStatus(TEXT("最新模型已确认，正在同步其余配置…"));
						return;
					}
					EmitStatus(FString::Printf(TEXT("ACP 已确认最新模型：%s。"), *AppliedModelId));
					SendPendingPromptIfReady();
					return;
				}
				EmitStatus(FString::Printf(TEXT("ACP 未确认所选模型 %s；当前报告模型为 %s，"
												"已阻止发送消息。"),
					*ConfigRequest.RequestedValue, ReportedModel.IsEmpty() ? TEXT("未知") : *ReportedModel));
				return;
			}
			if (!bSuperseded)
			{
				SelectedModelId = AppliedModelId;
			}
		}
		if (bSuperseded)
		{
			DeferredConfigIds.Add(ConfigRequest.LocalConfigId);
			if (bModel && !ReconcileSessionModel())
			{
				EmitStatus(TEXT("正在应用最新模型选择…"));
				return;
			}
		}
		else
		{
			DeferredConfigIds.Remove(ConfigRequest.LocalConfigId);
		}
		if (ApplyNextDeferredConfigOption())
		{
			EmitStatus(TEXT("模型与会话配置正在后台同步…"));
			return;
		}
		EmitStatus(ConfigRequest.LocalConfigId.Equals(TEXT("model"), ESearchCase::IgnoreCase) ? FString::Printf(TEXT("ACP 已确认实际模型：%s。"), *AppliedModelId)
																							  : TEXT("ACP 会话配置已无缝更新。"));
		SendPendingPromptIfReady();
		return;
	}

	if (Id == PromptRpcId)
	{
		const FWorldDataCodexTurnStatus CompletionStatus = AcpRules::BuildPromptCompletionStatus(Result, TEXT("已处理"));
		bPromptInFlight = false;
		ActivePrompt.Empty();
		ActivePromptImages.Empty();
		PromptModelDiagnostic.Empty();
		bRetriedAfterMissingModelMetadata = false;
		UE_LOG(LogUnrealAgentCodexACP, Verbose, TEXT("ACP prompt completed: stopReason=%s, usage=%s, input=%d, output=%d"),
			CompletionStatus.StopReason.IsEmpty() ? TEXT("<missing>") : *CompletionStatus.StopReason, CompletionStatus.bHasUsage ? TEXT("present") : TEXT("missing"),
			CompletionStatus.InputTokens, CompletionStatus.OutputTokens);
		EmitStatus(FString::Printf(TEXT("%s 回复完成。"), *UnrealAgentACPProviderModel::GetProvider(AgentProvider).DisplayName.ToString()));
		EmitTurnStatus(CompletionStatus.State, CompletionStatus.Message, CompletionStatus.StopReason, CompletionStatus.InputTokens, CompletionStatus.OutputTokens,
			CompletionStatus.bHasUsage);
	}
}

void FUnrealAgentCodexACPClient::HandleMethod(const FString& Method, const TSharedPtr<FJsonObject>& Message)
{
	const TSharedPtr<FJsonObject> Params = Message->HasField(TEXT("params")) ? Message->GetObjectField(TEXT("params")) : nullptr;
	const TSharedPtr<FJsonValue> Id = AcpRules::ExtractRpcId(Message);
	const bool bHasId = Id.IsValid();
	if (Method == TEXT("session/update") && Params.IsValid())
	{
		const FString AcpSessionId = AcpRules::GetOptionalString(Params, TEXT("sessionId"));
		if (AcpRules::ShouldIgnoreSessionScopedMessage(SessionId, AcpSessionId, bCreatingSession))
		{
			// 复用进程创建新 session 后，旧 session 仍可能有迟到通知。
			// 丢弃它们，避免污染新任务。
			return;
		}
		const TSharedPtr<FJsonObject> Update = Params->HasField(TEXT("update")) ? Params->GetObjectField(TEXT("update")) : nullptr;
		if (Update.IsValid())
		{
			HandleSessionUpdate(AcpSessionId, Update);
		}
		return;
	}

	if (Method == TEXT("mcp/connect") && bHasId)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("connectionId"), FUnrealAgentMCPServer::GetServerName() + TEXT("-conn"));
		SendRpcResult(Id, Result);
		return;
	}

	if (Method == TEXT("mcp/message") && bHasId && Params.IsValid())
	{
		const TSharedPtr<FJsonObject> McpMessage = Params->HasField(TEXT("message")) ? Params->GetObjectField(TEXT("message")) : nullptr;
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		if (McpMessage.IsValid())
		{
			const FString McpMethod = AcpRules::GetOptionalString(McpMessage, TEXT("method"));
			const bool bToolCall = McpMethod == TEXT("tools/call");
			const FString TransportToolName = GetMcpTransportToolName(McpMessage);
			FUnrealAgentMCPToolApprovalRequest ApprovalRequest;
			const bool bHasApprovalMetadata = bToolCall && FUnrealAgentMCPServer::DescribeToolApprovalRequest(TransportToolName, GetMcpToolArguments(McpMessage), ApprovalRequest);
			const EUnrealAgentMCPToolApprovalAction ApprovalAction = bHasApprovalMetadata ? ResolveToolApprovalAction(ApprovalRequest) : EUnrealAgentMCPToolApprovalAction::Allow;
			if (ApprovalAction == EUnrealAgentMCPToolApprovalAction::Deny)
			{
				Result->SetObjectField(TEXT("message"),
					AcpRules::BuildJsonRpcErrorResponse(McpMessage, -32002,
						GetEffectiveAgentMode() == EWorldDataAgentMode::Chat ? TEXT("对话模式仅回答问题，不执行 MCP 工具。") : TEXT("计划模式仅允许只读 MCP 工具。")));
				EmitText(TEXT("\n\n[系统] 当前 Agent 模式已阻止一次 MCP 工具调用。\n"));
			}
			else
			{
				if (ApprovalAction == EUnrealAgentMCPToolApprovalAction::Ask)
				{
					if (!OnPermission.IsBound())
					{
						Result->SetObjectField(TEXT("message"), AcpRules::BuildJsonRpcErrorResponse(McpMessage, -32002, TEXT("修改操作需要面板批准，但审批界面不可用。")));
					}
					else
					{
						const int32 RequestId = NextPermissionRequestId++;
						FPendingLocalMcpPermission Pending;
						Pending.ResponseId = Id;
						Pending.McpMessage = McpMessage;
						Pending.ConfirmationArgument = ApprovalRequest.ConfirmationArgument;
						Pending.ConfirmationValue = ApprovalRequest.ConfirmationValue;
						Pending.ToolName = ApprovalRequest.DisplayToolName;
						PendingLocalMcpPermissions.Add(RequestId, MoveTemp(Pending));

						FUnrealAgentAcpPermissionRequest Request;
						Request.RequestId = RequestId;
						Request.Title = FString::Printf(TEXT("是否允许执行项目修改：%s"), *ApprovalRequest.DisplayToolName);
						Request.ToolName = ApprovalRequest.DisplayToolName;
						Request.AllowOptionId = TEXT("allow_once");
						Request.DenyOptionId = TEXT("deny_once");
						OnPermission.Execute(Request);
						return;
					}
				}
				else
				{
					if (bHasApprovalMetadata && ApprovalRequest.bRequiresConfirmation)
					{
						ApplyToolConfirmation(McpMessage, ApprovalRequest.ConfirmationArgument, ApprovalRequest.ConfirmationValue);
					}
					Result->SetObjectField(TEXT("message"), FUnrealAgentMCPServer::DispatchJsonRpcRequest(McpMessage));
				}
			}
		}
		SendRpcResult(Id, Result);
		return;
	}

	if (Method == TEXT("fs/read_text_file") && bHasId && Params.IsValid())
	{
		const FString Path = AcpRules::GetOptionalString(Params, TEXT("path"));
		FString Content;
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		if (FFileHelper::LoadFileToString(Content, *Path))
		{
			const int32 Line = AcpRules::GetOptionalInt(Params, TEXT("line"));
			const int32 Limit = AcpRules::GetOptionalInt(Params, TEXT("limit"));
			if (Line > 0 || Limit > 0)
			{
				TArray<FString> Lines;
				Content.ParseIntoArrayLines(Lines, false);
				const int32 Start = FMath::Max(0, Line - 1);
				const int32 End = Limit > 0 ? FMath::Min(Start + Limit, Lines.Num()) : Lines.Num();

				Content.Empty();
				for (int32 Index = Start; Index < End; ++Index)
				{
					if (!Content.IsEmpty())
					{
						Content += TEXT("\n");
					}
					Content += Lines[Index];
				}
			}
			Result->SetStringField(TEXT("content"), Content);
			SendRpcResult(Id, Result);
		}
		else
		{
			SendRpcError(Id, -32002, FString::Printf(TEXT("无法读取文件：%s"), *Path));
		}
		return;
	}

	if (Method == TEXT("terminal/create") && bHasId)
	{
		SendRpcError(Id, -32002, TEXT("Shell/terminal 已在 Unreal Agent 中禁用。请使用 MCP tools/resources。"));
		EmitText(TEXT("\n\n[系统] 已阻止一次 shell/terminal 请求。\n"));
		return;
	}

	if (Method == TEXT("session/request_permission") && bHasId && Params.IsValid())
	{
		const FString PermissionSessionId = AcpRules::GetOptionalString(Params, TEXT("sessionId"));
		if (AcpRules::ShouldIgnoreSessionScopedMessage(SessionId, PermissionSessionId, bCreatingSession))
		{
			SendRpcError(Id, -32002, TEXT("该权限请求属于已结束的 ACP 会话。"));
			return;
		}
		const TArray<FUnrealAgentAcpPermissionOption> Options = AcpRules::ExtractPermissionOptions(Params);
		const TSharedPtr<FJsonObject> ToolCall = Params->HasField(TEXT("toolCall")) ? Params->GetObjectField(TEXT("toolCall")) : nullptr;
		const bool bShell = AcpRules::IsShellPermissionRequest(ToolCall);
		FString AllowOptionId = AcpRules::SelectAllowPermissionOptionId(Options);
		if (AllowOptionId.IsEmpty())
		{
			AllowOptionId = TEXT("allow");
		}
		FString DenyOptionId = AcpRules::SelectDenyPermissionOptionId(Options);
		if (DenyOptionId.IsEmpty())
		{
			DenyOptionId = TEXT("deny");
		}
		const FString PermissionTitle = AcpRules::GetPermissionRequestTitle(ToolCall);

		if (bShell)
		{
			SendPermissionOutcome(Id, DenyOptionId);
			EmitText(FString::Printf(TEXT("\n\n[系统] 已阻止 shell/terminal 权限请求：%s\n"), *PermissionTitle));
			return;
		}

		const FString PermissionToolName = AcpRules::GetOptionalString(ToolCall, TEXT("name"));
		FUnrealAgentMCPToolApprovalRequest ApprovalRequest;
		ApprovalRequest.ToolName = PermissionToolName;
		ApprovalRequest.DisplayToolName = PermissionTitle;
		ApprovalRequest.bReadOnly = IsReadOnlyToolName(PermissionToolName);
		const EUnrealAgentMCPToolApprovalAction ApprovalAction = ResolveToolApprovalAction(ApprovalRequest);
		if (ApprovalAction == EUnrealAgentMCPToolApprovalAction::Allow)
		{
			SendPermissionOutcome(Id, AllowOptionId);
			EmitText(FString::Printf(TEXT("\n\n[系统] 当前权限策略已自动允许请求：%s\n"), *PermissionTitle));
			return;
		}

		if (ApprovalAction == EUnrealAgentMCPToolApprovalAction::Deny)
		{
			SendPermissionOutcome(Id, DenyOptionId);
			EmitText(FString::Printf(TEXT("\n\n[系统] 当前 Agent 模式已拒绝权限请求：%s\n"), *PermissionTitle));
			return;
		}

		if (!OnPermission.IsBound())
		{
			SendPermissionOutcome(Id, DenyOptionId);
			EmitText(FString::Printf(TEXT("\n\n[系统] 收到权限请求，但面板审批界面不可用，已拒绝：%s\n"), *PermissionTitle));
			return;
		}

		FUnrealAgentAcpPermissionRequest Request;
		Request.RequestId = NextPermissionRequestId++;
		Request.Title = PermissionTitle;
		Request.ToolName = AcpRules::GetOptionalString(ToolCall, TEXT("name"));
		Request.ToolCallId = AcpRules::GetOptionalString(ToolCall, TEXT("toolCallId"));
		Request.SessionId = AcpRules::GetOptionalString(Params, TEXT("sessionId"));
		Request.AllowOptionId = AllowOptionId;
		Request.DenyOptionId = DenyOptionId;
		Request.Options = Options;

		PendingPermissionIds.Add(Request.RequestId, Id);
		OnPermission.Execute(Request);
		EmitStatus(FString::Printf(TEXT("等待权限确认：%s"), *Request.Title));
		return;
	}

	if (bHasId)
	{
		SendRpcResult(Id, MakeShared<FJsonObject>());
	}
}
