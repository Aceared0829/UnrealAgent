// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentCodexACPClient.cpp
 * @brief Codex ACP 客户端会话、权限模式与 UI 状态协调。
 */

#include "Application/ACP/UnrealAgentCodexACPClient.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"

#include "Core/Common/UnrealAgentMCPBrand.h"
#include "Infrastructure/ACP/UnrealAgentCodexACPClientInternal.h"
#include "Application/ACP/UnrealAgentCodexACPClientRules.h"
#include "Infrastructure/Http/UnrealAgentMCPServer.h"

DEFINE_LOG_CATEGORY(LogUnrealAgentCodexACP);

namespace AcpRules = WorldDataCodexAcpRules;

FString FUnrealAgentCodexACPClient::GetAgentDisplayName() const
{
	return UnrealAgentACPProviderModel::GetProvider(AgentProvider).DisplayName.ToString();
}

namespace
{
	bool IsConfigKind(const FUnrealAgentAcpConfigOption& Option, const FString& Id, const FString& Category)
	{
		return Option.Id.Equals(Id, ESearchCase::IgnoreCase) || Option.Category.Equals(Category, ESearchCase::IgnoreCase);
	}

	bool IsReasoningConfig(const FUnrealAgentAcpConfigOption& Option)
	{
		return Option.Id.Equals(TEXT("model_reasoning_effort"), ESearchCase::IgnoreCase) || Option.Id.Equals(TEXT("reasoning_effort"), ESearchCase::IgnoreCase) ||
			Option.Category.Equals(TEXT("reasoning"), ESearchCase::IgnoreCase) || Option.Category.Equals(TEXT("thought_level"), ESearchCase::IgnoreCase);
	}

	bool IsSpeedConfig(const FUnrealAgentAcpConfigOption& Option)
	{
		return Option.Id.Equals(TEXT("service_tier"), ESearchCase::IgnoreCase) || Option.Id.Equals(TEXT("fast-mode"), ESearchCase::IgnoreCase) ||
			Option.Category.Equals(TEXT("speed"), ESearchCase::IgnoreCase);
	}

	FString GetConfigScalarString(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		const TSharedPtr<FJsonValue> Value = Object->TryGetField(FieldName);
		if (!Value.IsValid())
		{
			return FString();
		}
		if (Value->Type == EJson::String)
		{
			return Value->AsString();
		}
		if (Value->Type == EJson::Boolean)
		{
			return Value->AsBool() ? TEXT("on") : TEXT("off");
		}
		if (Value->Type == EJson::Number)
		{
			return FString::SanitizeFloat(Value->AsNumber());
		}
		return FString();
	}

	FString ToRemoteSpeedValue(const FUnrealAgentAcpConfigOption& Option, const FString& LocalValue)
	{
		if (Option.Id.Equals(TEXT("fast-mode"), ESearchCase::IgnoreCase))
		{
			return LocalValue.Equals(TEXT("fast"), ESearchCase::IgnoreCase) ? TEXT("on") : TEXT("off");
		}
		return LocalValue;
	}

	FString ToLocalSpeedValue(const FString& RemoteValue)
	{
		return RemoteValue.Equals(TEXT("fast"), ESearchCase::IgnoreCase) || RemoteValue.Equals(TEXT("on"), ESearchCase::IgnoreCase) ||
				RemoteValue.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			? TEXT("fast")
			: TEXT("standard");
	}

	FString GetReasoningDisplayName(const FString& Effort)
	{
		if (Effort == TEXT("low"))
		{
			return TEXT("轻度");
		}
		if (Effort == TEXT("medium"))
		{
			return TEXT("中");
		}
		if (Effort == TEXT("high"))
		{
			return TEXT("高");
		}
		if (Effort == TEXT("xhigh"))
		{
			return TEXT("极高");
		}
		if (Effort == TEXT("max"))
		{
			return TEXT("最高");
		}
		if (Effort == TEXT("ultra"))
		{
			return TEXT("超高");
		}
		return Effort;
	}
}

FUnrealAgentCodexACPClient::FUnrealAgentCodexACPClient(TSharedRef<IUnrealAgentMCPApplicationService> InApplicationService) : ApplicationService(MoveTemp(InApplicationService))
{
	McpApprovalClientId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	EnsureToolApprovalHandlerRegistered();
}

void FUnrealAgentCodexACPClient::EnsureToolApprovalHandlerRegistered()
{
	if (bToolApprovalHandlerRegistered)
	{
		return;
	}
	FUnrealAgentMCPServer::RegisterToolApprovalHandler(McpApprovalClientId,
		FUnrealAgentMCPToolApprovalHandler::CreateRaw(this, &FUnrealAgentCodexACPClient::RequestHttpMcpToolApproval));
	bToolApprovalHandlerRegistered = true;
}

void FUnrealAgentCodexACPClient::Connect()
{
	EnsureToolApprovalHandlerRegistered();
	LastError.Empty();
	if (ModelCatalog.IsEmpty())
	{
		RefreshModelCatalog();
	}
	if (EnsureProcess() && bInitialized)
	{
		StartSessionIfReady();
	}
}

bool FUnrealAgentCodexACPClient::BeginNewSession()
{
	if (HasActiveTurn())
	{
		return false;
	}

	LastError.Empty();
	PendingPrompt.Empty();
	PendingPromptImages.Empty();
	ActivePrompt.Empty();
	ActivePromptImages.Empty();
	PromptModelDiagnostic.Empty();
	PendingPermissionIds.Empty();
	PendingLocalMcpPermissions.Empty();
	DenyPendingHttpMcpPermissions();
	ToolCallTitles.Empty();
	bRetriedAfterMissingModelMetadata = false;
	bMcpServerReady = false;

	// initialize/session/new 本身不含对话内容。若预热仍在创建首个空 session，
	// 直接把它交给新任务即可，避免重复发送 session/new。
	if (bCreatingSession)
	{
		return true;
	}

	SessionId.Empty();
	AppliedModelId.Empty();
	SessionRpcId = 0;
	PromptRpcId = 0;
	ConfigOptionRpcIds.Empty();
	// SessionConfigOptions/ConfigOptions 是前台稳定快照。新 session 后台创建
	// 期间继续展示并允许选择，待返回后再用真实能力原位刷新。
	if (!SelectedModelId.IsEmpty())
	{
		DeferredConfigIds.Add(TEXT("model"));
	}
	if (!SelectedReasoningEffort.IsEmpty())
	{
		DeferredConfigIds.Add(TEXT("model_reasoning_effort"));
	}
	if (!SelectedSpeed.IsEmpty())
	{
		DeferredConfigIds.Add(TEXT("service_tier"));
	}

	if (EnsureProcess() && bInitialized)
	{
		StartSessionIfReady();
	}
	return IsRunning();
}

void FUnrealAgentCodexACPClient::SendPrompt(const FString& Prompt)
{
	SendPrompt(Prompt, TArray<FUnrealAgentAcpPromptImage>());
}

void FUnrealAgentCodexACPClient::SendPrompt(const FString& Prompt, const TArray<FUnrealAgentAcpPromptImage>& Images)
{
	const FString TrimmedPrompt = Prompt.TrimStartAndEnd();
	if (TrimmedPrompt.IsEmpty())
	{
		return;
	}
	PendingPrompt = TrimmedPrompt;
	PendingPromptImages = Images;
	if (Images.ContainsByPredicate(
			[](const FUnrealAgentAcpPromptImage& Image)
			{
				return Image.Base64Data.IsEmpty();
			}))
	{
		Fail(TEXT("ACP 图片 Prompt 包含空的 Base64 正文，已阻止发送。"));
		return;
	}
	ActivePrompt.Empty();
	ActivePromptImages.Empty();
	PromptModelDiagnostic.Empty();
	ToolCallTitles.Empty();
	bRetriedAfterMissingModelMetadata = false;
	LastError.Empty();
	EmitTurnStatus(EWorldDataCodexTurnState::Sending, FString::Printf(TEXT("正在发送到 %s…"), *GetAgentDisplayName()));

	if (!EnsureProcess())
	{
		// EnsureProcess 的失败分支统一经 Fail 广播一次终态，避免重复调度队列。
		return;
	}

	if (bInitialized)
	{
		StartSessionIfReady();
		SendPendingPromptIfReady();
	}
}

void FUnrealAgentCodexACPClient::SetExecutionPolicy(const EWorldDataAgentMode InAgentMode, const EWorldDataApprovalPolicy InApprovalPolicy,
	const EWorldDataSelfRepairPolicy InSelfRepairPolicy)
{
	AgentMode = InAgentMode;
	ApprovalPolicy = InApprovalPolicy;
	SelfRepairPolicy = InSelfRepairPolicy;
}

EWorldDataAgentMode FUnrealAgentCodexACPClient::GetAgentMode() const
{
	return AgentMode;
}

EWorldDataApprovalPolicy FUnrealAgentCodexACPClient::GetApprovalPolicy() const
{
	return ApprovalPolicy;
}

EWorldDataSelfRepairPolicy FUnrealAgentCodexACPClient::GetSelfRepairPolicy() const
{
	return SelfRepairPolicy;
}

EWorldDataAgentMode FUnrealAgentCodexACPClient::GetEffectiveAgentMode() const
{
	return bPromptInFlight ? ActiveAgentMode : AgentMode;
}

EWorldDataApprovalPolicy FUnrealAgentCodexACPClient::GetEffectiveApprovalPolicy() const
{
	return bPromptInFlight ? ActiveApprovalPolicy : ApprovalPolicy;
}

EWorldDataSelfRepairPolicy FUnrealAgentCodexACPClient::GetEffectiveSelfRepairPolicy() const
{
	return bPromptInFlight ? ActiveSelfRepairPolicy : SelfRepairPolicy;
}

EUnrealAgentMCPToolApprovalAction FUnrealAgentCodexACPClient::ResolveToolApprovalAction(const FUnrealAgentMCPToolApprovalRequest& Request) const
{
	return AcpRules::ResolveMcpToolApprovalAction(GetEffectiveAgentMode(), GetEffectiveApprovalPolicy(), Request);
}

void FUnrealAgentCodexACPClient::RequestHttpMcpToolApproval(const FUnrealAgentMCPToolApprovalRequest& Request, FUnrealAgentMCPToolApprovalCompletion Completion)
{
	if (!Completion)
	{
		return;
	}
	const EUnrealAgentMCPToolApprovalAction Action = ResolveToolApprovalAction(Request);
	if (Action != EUnrealAgentMCPToolApprovalAction::Ask)
	{
		Completion(Action == EUnrealAgentMCPToolApprovalAction::Allow);
		return;
	}
	if (!OnPermission.IsBound())
	{
		Completion(false);
		return;
	}

	const int32 RequestId = NextPermissionRequestId++;
	FPendingHttpMcpPermission Pending;
	Pending.Completion = MoveTemp(Completion);
	Pending.ToolName = Request.DisplayToolName;
	PendingHttpMcpPermissions.Add(RequestId, MoveTemp(Pending));

	FUnrealAgentAcpPermissionRequest PermissionRequest;
	PermissionRequest.RequestId = RequestId;
	PermissionRequest.Title = FString::Printf(TEXT("是否允许执行项目修改：%s"), *Request.DisplayToolName);
	PermissionRequest.ToolName = Request.DisplayToolName;
	PermissionRequest.AllowOptionId = TEXT("allow_once");
	PermissionRequest.DenyOptionId = TEXT("deny_once");
	OnPermission.Execute(PermissionRequest);
	EmitStatus(FString::Printf(TEXT("等待权限确认：%s"), *Request.DisplayToolName));
}

bool FUnrealAgentCodexACPClient::ResolveHttpMcpPermission(const int32 RequestId, const bool bAllow)
{
	FPendingHttpMcpPermission* Pending = PendingHttpMcpPermissions.Find(RequestId);
	if (!Pending)
	{
		return false;
	}
	FUnrealAgentMCPToolApprovalCompletion Completion = MoveTemp(Pending->Completion);
	PendingHttpMcpPermissions.Remove(RequestId);
	if (Completion)
	{
		Completion(bAllow);
	}
	return true;
}

void FUnrealAgentCodexACPClient::DenyPendingHttpMcpPermissions()
{
	TArray<FUnrealAgentMCPToolApprovalCompletion> Completions;
	Completions.Reserve(PendingHttpMcpPermissions.Num());
	for (TPair<int32, FPendingHttpMcpPermission>& Pair : PendingHttpMcpPermissions)
	{
		Completions.Add(MoveTemp(Pair.Value.Completion));
	}
	PendingHttpMcpPermissions.Empty();
	for (FUnrealAgentMCPToolApprovalCompletion& Completion : Completions)
	{
		if (Completion)
		{
			Completion(false);
		}
	}
}

void FUnrealAgentCodexACPClient::SetCodexCliPath(const FString& InCodexCliPath, const bool bRefreshCatalog)
{
	const FString NormalizedPath = InCodexCliPath.TrimStartAndEnd();
	if (CodexCliPath == NormalizedPath)
	{
		return;
	}

	CodexCliPath = NormalizedPath;
	CachedLaunchSpec.Reset();
	CliModelCatalog.Empty();
	ModelCatalog.Empty();
	RebuildConfigOptions();
	if (bRefreshCatalog)
	{
		RefreshModelCatalog();
	}
}

void FUnrealAgentCodexACPClient::SetAgentProvider(EUnrealAgentACPProvider InProvider, const FString& InAgentCliPath)
{
	const FString NormalizedAgentPath = InAgentCliPath.TrimStartAndEnd();
	if (AgentProvider == InProvider && AgentCliPath == NormalizedAgentPath)
	{
		return;
	}

	Stop();
	AgentProvider = InProvider;
	AgentCliPath = NormalizedAgentPath;
	CachedLaunchSpec.Reset();
	ActiveAdapterVersion.Empty();
	CliModelCatalog.Empty();
	ModelCatalog.Empty();
	SessionConfigOptions.Empty();
	ConfigOptions.Empty();
	SelectedModelId.Empty();
	AppliedModelId.Empty();
	SelectedReasoningEffort.Empty();
	SelectedSpeed = TEXT("standard");
	AdapterReasoningEfforts.Empty();
	DeferredConfigIds.Empty();
	RebuildConfigOptions();

	if (AgentProvider == EUnrealAgentACPProvider::Codex)
	{
		RefreshModelCatalog();
	}
}

bool FUnrealAgentCodexACPClient::SetConfigOption(const FString& ConfigId, const FString& Value)
{
	if (ConfigId.IsEmpty() || Value.IsEmpty())
	{
		return false;
	}

	const bool bIsModel = ConfigId.Equals(TEXT("model"), ESearchCase::IgnoreCase);
	const bool bIsReasoning = ConfigId.Equals(TEXT("model_reasoning_effort"), ESearchCase::IgnoreCase);
	const bool bIsSpeed = ConfigId.Equals(TEXT("service_tier"), ESearchCase::IgnoreCase);

	const FUnrealAgentAcpConfigOption* PublicOption = ConfigOptions.FindByPredicate(
		[&ConfigId](const FUnrealAgentAcpConfigOption& Option)
		{
			return Option.Id.Equals(ConfigId, ESearchCase::IgnoreCase);
		});
	if (PublicOption)
	{
		const FUnrealAgentAcpConfigOptionValue* PublicValue = PublicOption->Options.FindByPredicate(
			[&Value](const FUnrealAgentAcpConfigOptionValue& Candidate)
			{
				return Candidate.Value == Value;
			});
		if (!PublicValue || !PublicValue->bEnabled)
		{
			EmitStatus(PublicValue ? PublicValue->Description : FString::Printf(TEXT("当前 %s 配置中没有这个选项。"), *GetAgentDisplayName()));
			return false;
		}
	}

	if (bIsModel || bIsReasoning || bIsSpeed)
	{
		if (!CanChangeConfig())
		{
			return false;
		}

		FString PreviousValue;
		if (bIsModel)
		{
			PreviousValue = SelectedModelId;
			SelectedModelId = Value;
		}
		else if (bIsReasoning)
		{
			PreviousValue = SelectedReasoningEffort;
			SelectedReasoningEffort = Value;
		}
		else
		{
			PreviousValue = SelectedSpeed;
			SelectedSpeed = Value;
		}

		RebuildConfigOptions();
		DeferredConfigIds.Add(ConfigId);
		if (AcpRules::ShouldQueueLatestConfigSelection(ConfigOptionRpcIds.Num()))
		{
			EmitStatus(TEXT("配置正在同步，已保留最新选择并将在后台继续应用。"));
			return true;
		}
		if (TrySetSessionConfigOption(ConfigId, Value, PreviousValue))
		{
			EmitStatus(bIsModel ? TEXT("正在当前 ACP 会话内切换模型…") : TEXT("正在更新当前 ACP 会话配置…"));
		}
		else
		{
			EmitStatus(bIsModel    ? TEXT("模型已保存，将在新对话生效。")
					: bIsReasoning ? TEXT("推理强度已保存；当前适配器未提供该档位的会话内切换，将在新对话生效。")
								   : TEXT("速度设置已保存；当前适配器未提供会话内切换，将在新对话生效。"));
		}
		return true;
	}

	if (SessionId.IsEmpty())
	{
		return false;
	}

	QueueSessionConfigOption(ConfigId, ConfigId, Value, FString());
	return true;
}

const FUnrealAgentAcpConfigOption* FUnrealAgentCodexACPClient::FindSessionConfigOption(const FString& LocalConfigId) const
{
	const bool bReasoning = LocalConfigId.Equals(TEXT("model_reasoning_effort"), ESearchCase::IgnoreCase);
	const bool bSpeed = LocalConfigId.Equals(TEXT("service_tier"), ESearchCase::IgnoreCase);
	return SessionConfigOptions.FindByPredicate(
		[&LocalConfigId, bReasoning, bSpeed](const FUnrealAgentAcpConfigOption& Option)
		{
			if (bReasoning)
			{
				return IsReasoningConfig(Option);
			}
			if (bSpeed)
			{
				return IsSpeedConfig(Option);
			}
			return Option.Id.Equals(LocalConfigId, ESearchCase::IgnoreCase);
		});
}

bool FUnrealAgentCodexACPClient::TrySetSessionConfigOption(const FString& LocalConfigId, const FString& Value, const FString& PreviousValue)
{
	if (SessionId.IsEmpty())
	{
		return false;
	}

	const FUnrealAgentAcpConfigOption* SessionOption = FindSessionConfigOption(LocalConfigId);
	if (LocalConfigId.Equals(TEXT("model"), ESearchCase::IgnoreCase))
	{
		QueueSessionConfigOption(LocalConfigId, SessionOption ? SessionOption->Id : TEXT("model"), Value, PreviousValue);
		return true;
	}
	const FString RemoteValue = LocalConfigId.Equals(TEXT("service_tier"), ESearchCase::IgnoreCase) && SessionOption ? ToRemoteSpeedValue(*SessionOption, Value) : Value;
	if (!SessionOption ||
		!SessionOption->Options.ContainsByPredicate(
			[&RemoteValue](const FUnrealAgentAcpConfigOptionValue& Candidate)
			{
				return Candidate.Value == RemoteValue;
			}))
	{
		return false;
	}

	if (SessionOption->CurrentValue == RemoteValue)
	{
		DeferredConfigIds.Remove(LocalConfigId);
		return true;
	}

	QueueSessionConfigOption(LocalConfigId, SessionOption->Id, RemoteValue, PreviousValue);
	return true;
}

bool FUnrealAgentCodexACPClient::ApplyNextDeferredConfigOption()
{
	if (SessionId.IsEmpty() || !ConfigOptionRpcIds.IsEmpty())
	{
		return false;
	}
	if (DeferredConfigIds.Contains(TEXT("model")))
	{
		if (!ReconcileSessionModel())
		{
			return !ConfigOptionRpcIds.IsEmpty();
		}
		DeferredConfigIds.Remove(TEXT("model"));
	}

	const TArray<FString> OrderedConfigIds = { TEXT("model_reasoning_effort"), TEXT("service_tier") };
	for (const FString& ConfigId : OrderedConfigIds)
	{
		if (!DeferredConfigIds.Contains(ConfigId))
		{
			continue;
		}

		const bool bReasoning = ConfigId == TEXT("model_reasoning_effort");
		const FString DesiredValue = bReasoning ? SelectedReasoningEffort : SelectedSpeed;
		if (DesiredValue.IsEmpty())
		{
			DeferredConfigIds.Remove(ConfigId);
			continue;
		}

		const FUnrealAgentAcpConfigOption* SessionOption = FindSessionConfigOption(ConfigId);
		const FString PreviousValue = SessionOption ? (bReasoning ? SessionOption->CurrentValue : ToLocalSpeedValue(SessionOption->CurrentValue)) : FString();
		if (!TrySetSessionConfigOption(ConfigId, DesiredValue, PreviousValue))
		{
			// 当前适配器未暴露热切换能力时保留用户偏好和稳定 UI；
			// 后续 session 能力刷新后会再次尝试，不清空选择器。
			continue;
		}
		if (!ConfigOptionRpcIds.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

void FUnrealAgentCodexACPClient::QueueSessionConfigOption(const FString& LocalConfigId, const FString& RemoteConfigId, const FString& Value, const FString& PreviousValue)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), SessionId);
	Params->SetStringField(TEXT("configId"), RemoteConfigId);
	Params->SetStringField(TEXT("value"), Value);

	FPendingConfigOptionRequest Request;
	Request.LocalConfigId = LocalConfigId;
	Request.RequestedValue = Value;
	Request.PreviousValue = PreviousValue;
	ConfigOptionRpcIds.Add(SendRpc(TEXT("session/set_config_option"), Params), MoveTemp(Request));
}

const TArray<FUnrealAgentAcpConfigOption>& FUnrealAgentCodexACPClient::GetConfigOptions() const
{
	return ConfigOptions;
}

void FUnrealAgentCodexACPClient::RespondToPermission(int32 RequestId, const FString& OptionId)
{
	if (ResolveHttpMcpPermission(RequestId, AcpRules::IsAllowPermissionOption(OptionId)))
	{
		return;
	}
	if (ResolveLocalMcpPermission(RequestId, AcpRules::IsAllowPermissionOption(OptionId)))
	{
		return;
	}
	TSharedPtr<FJsonValue>* PendingId = PendingPermissionIds.Find(RequestId);
	if (!PendingId)
	{
		EmitText(TEXT("\n\n[系统] 权限请求已失效或已处理。\n"));
		return;
	}

	const FString SelectedOptionId = OptionId.IsEmpty() ? TEXT("deny") : OptionId;
	SendPermissionOutcome(*PendingId, SelectedOptionId);
	PendingPermissionIds.Remove(RequestId);

	EmitText(AcpRules::IsAllowPermissionOption(SelectedOptionId) ? TEXT("\n\n[系统] 已允许本次工具权限请求。\n") : TEXT("\n\n[系统] 已拒绝本次工具权限请求。\n"));
}

bool FUnrealAgentCodexACPClient::IsRunning() const
{
	return Process.IsValid() && Process->IsRunning();
}

bool FUnrealAgentCodexACPClient::IsReady() const
{
	return IsRunning() && bInitialized && bMcpServerReady && !SessionId.IsEmpty();
}

bool FUnrealAgentCodexACPClient::HasSession() const
{
	return IsRunning() && (bCreatingSession || !SessionId.IsEmpty());
}

bool FUnrealAgentCodexACPClient::IsProcessing() const
{
	return bPromptInFlight || bCreatingSession || !ConfigOptionRpcIds.IsEmpty() || !PendingPermissionIds.IsEmpty() || !PendingLocalMcpPermissions.IsEmpty() ||
		!PendingHttpMcpPermissions.IsEmpty();
}

bool FUnrealAgentCodexACPClient::HasActiveTurn() const
{
	return AcpRules::HasActiveTurnState(!PendingPrompt.IsEmpty(), bPromptInFlight, PendingPermissionIds.Num() + PendingLocalMcpPermissions.Num() + PendingHttpMcpPermissions.Num());
}

bool FUnrealAgentCodexACPClient::CanAcceptPrompt() const
{
	return !HasActiveTurn();
}

bool FUnrealAgentCodexACPClient::CanChangeConfig() const
{
	return AcpRules::CanChangeConfigState(bPromptInFlight, PendingPermissionIds.Num() + PendingLocalMcpPermissions.Num() + PendingHttpMcpPermissions.Num());
}

FString FUnrealAgentCodexACPClient::GetLastError() const
{
	return LastError;
}

FString FUnrealAgentCodexACPClient::GetAppliedModelId() const
{
	return AppliedModelId;
}

bool FUnrealAgentCodexACPClient::DoesAgentSupportImagePrompts() const
{
	return bInitialized && bAgentSupportsImagePrompts;
}

bool FUnrealAgentCodexACPClient::StartSessionIfReady()
{
	if (!bInitialized || !SessionId.IsEmpty() || bCreatingSession)
	{
		return !SessionId.IsEmpty();
	}

	if (!ApplicationService->IsServerRunning())
	{
		ApplicationService->StartServer(ApplicationService->LoadConfiguredPort());
	}
	if (!bMcpServerReady)
	{
		if (!bMcpReadinessCheckInFlight)
		{
			bMcpReadinessCheckInFlight = true;
			EmitStatus(TEXT("正在验证 UnrealAgent MCP initialize 与 tools/list..."));
			const TWeakPtr<FUnrealAgentCodexACPClient> WeakSelf = AsShared();
			ApplicationService->VerifyServerReadinessAsync(
				[WeakSelf](const FUnrealAgentMCPReadiness& Readiness)
				{
					const TSharedPtr<FUnrealAgentCodexACPClient> Self = WeakSelf.Pin();
					if (!Self.IsValid())
					{
						return;
					}
					Self->bMcpReadinessCheckInFlight = false;
					Self->bMcpServerReady = Readiness.IsReady();
					if (!Readiness.IsReady())
					{
						Self->Fail(FString::Printf(TEXT("UnrealAgent MCP 未通过启动门禁：%s"), *Readiness.Error));
						return;
					}
					Self->EmitStatus(FString::Printf(TEXT("UnrealAgent MCP Listening / ACP Ready（%d 个工具）"), Readiness.ToolCount));
					Self->StartSessionIfReady();
				});
		}
		return false;
	}

	FString WorkingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::MakePlatformFilename(WorkingDirectory);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("cwd"), WorkingDirectory);

	TArray<TSharedPtr<FJsonValue>> McpServers;
	McpServers.Add(MakeShared<FJsonValueObject>(
		AcpRules::BuildHttpMcpServer(ApplicationService->GetServerName(), ApplicationService->GetMcpUrl(), ApplicationService->GetAccessTokenHeaderName(),
			ApplicationService->GetAccessToken(), McpApprovalClientId, UnrealAgentACPProviderModel::GetProvider(AgentProvider).Id.ToString())));
	Params->SetArrayField(TEXT("mcpServers"), McpServers);

	bCreatingSession = true;
	SessionRpcId = SendRpc(TEXT("session/new"), Params);
	EmitStatus(FString::Printf(TEXT("正在创建 %s ACP 会话..."), *UnrealAgentACPProviderModel::GetProvider(AgentProvider).DisplayName.ToString()));
	return false;
}

void FUnrealAgentCodexACPClient::SendPendingPromptIfReady()
{
	if (PendingPrompt.IsEmpty() || SessionId.IsEmpty() || (AgentProvider == EUnrealAgentACPProvider::Codex && AppliedModelId.IsEmpty()) || bPromptInFlight ||
		!ConfigOptionRpcIds.IsEmpty())
	{
		return;
	}

	if (!ReconcileSessionModel())
	{
		return;
	}
	if (!PendingPromptImages.IsEmpty() && !DoesAgentSupportImagePrompts())
	{
		Fail(FString::Printf(TEXT("%s ACP 未在 initialize 中声明图片 Prompt 能力，已阻止静默丢图。"), *GetAgentDisplayName()));
		return;
	}

	ActiveAgentMode = AgentMode;
	ActiveApprovalPolicy = ApprovalPolicy;
	ActiveSelfRepairPolicy = SelfRepairPolicy;
	const FString ModelInstruction = AcpRules::BuildConfirmedModelInstruction(AppliedModelId, GetModelDisplayName(AppliedModelId));
	const FString PromptText =
		FString::Printf(TEXT("你是嵌入 Unreal Editor 的 %s。优先使用已连接的 Unreal Agent MCP tools/resources 理解当前项目。"), UnrealAgentMCP::Brand::ProductName) + TEXT("\n") +
		AcpRules::GetExecutionPolicyInstruction(ActiveAgentMode, ActiveApprovalPolicy, ActiveSelfRepairPolicy) + TEXT("\n") + ModelInstruction +
		TEXT("\n回答用户时使用中文，必要时说明你调用了哪些 MCP 工具。") + TEXT("\n\n---\n用户消息：\n") + PendingPrompt;

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), SessionId);

	if (!AcpRules::IsSessionPromptJsonRpcWithinLimit(PromptText, PendingPromptImages))
	{
		Fail(TEXT("ACP Prompt JSON-RPC 超过大小上限，已阻止发送。"));
		return;
	}

	Params->SetArrayField(TEXT("prompt"), AcpRules::BuildPromptContentBlocks(PromptText, PendingPromptImages));

	ActivePrompt = PendingPrompt;
	ActivePromptImages = MoveTemp(PendingPromptImages);
	PendingPrompt.Empty();
	PromptModelDiagnostic.Empty();
	bPromptInFlight = true;
	PromptRpcId = SendRpc(TEXT("session/prompt"), Params);
	const FString ProviderName = UnrealAgentACPProviderModel::GetProvider(AgentProvider).DisplayName.ToString();
	EmitStatus(FString::Printf(TEXT("%s 已接收消息，正在思考..."), *ProviderName));
	EmitTurnStatus(EWorldDataCodexTurnState::Received, FString::Printf(TEXT("%s 已接收消息，正在思考…"), *ProviderName));
}

void FUnrealAgentCodexACPClient::HandleSessionUpdate(const FString& AcpSessionId, const TSharedPtr<FJsonObject>& Update)
{
	const FString UpdateType = AcpRules::GetOptionalString(Update, TEXT("sessionUpdate"));
	FString McpStartupError;
	if (AcpRules::TryGetMcpStartupFailure(Update, ApplicationService->GetServerName(), McpStartupError))
	{
		bMcpServerReady = false;
		SessionId.Empty();
		Fail(FString::Printf(TEXT("UnrealAgent MCP 的 ACP 注册失败：%s"), *McpStartupError));
		return;
	}
	const FString ProviderName = UnrealAgentACPProviderModel::GetProvider(AgentProvider).DisplayName.ToString();
	if (UpdateType == TEXT("config_option_update"))
	{
		UpdateConfigOptions(Update);
		return;
	}
	if (UpdateType == TEXT("agent_message_chunk") && Update->HasField(TEXT("content")))
	{
		const TSharedPtr<FJsonObject> Content = Update->GetObjectField(TEXT("content"));
		if (AcpRules::GetOptionalString(Content, TEXT("type")) == TEXT("text"))
		{
			const FString Text = AcpRules::GetOptionalString(Content, TEXT("text"));
			if (Text.Contains(TEXT("Model metadata for"), ESearchCase::IgnoreCase) && Text.Contains(TEXT("not found"), ESearchCase::IgnoreCase))
			{
				PromptModelDiagnostic = Text;
				EmitStatus(FString::Printf(TEXT("检测到模型元数据不兼容：codex-acp %s / %s。"), ActiveAdapterVersion.IsEmpty() ? TEXT("未知版本") : *ActiveAdapterVersion,
					AppliedModelId.IsEmpty() ? TEXT("未知模型") : *AppliedModelId));
				return;
			}
			EmitTurnStatus(EWorldDataCodexTurnState::Generating, FString::Printf(TEXT("%s 正在生成回复…"), *ProviderName));
			EmitText(Text);
		}
		return;
	}

	if (UpdateType == TEXT("agent_thought_chunk"))
	{
		EmitTurnStatus(EWorldDataCodexTurnState::Thinking, FString::Printf(TEXT("%s 已接收消息，正在思考…"), *ProviderName));
		return;
	}

	if (UpdateType == TEXT("tool_call"))
	{
		const FString ToolCallId = AcpRules::GetOptionalString(Update, TEXT("toolCallId"));
		FString Title = AcpRules::GetOptionalString(Update, TEXT("title"));
		if (Title.IsEmpty())
		{
			Title = ToolCallId;
		}
		if (!ToolCallId.IsEmpty())
		{
			ToolCallTitles.Add(ToolCallId, Title);
		}
		const FString InitialStatus = AcpRules::GetOptionalString(Update, TEXT("status"));
		const EUnrealAgentAcpToolCallState InitialState = InitialStatus == TEXT("completed") ? EUnrealAgentAcpToolCallState::Completed
			: InitialStatus == TEXT("failed")                                                ? EUnrealAgentAcpToolCallState::Failed
																							 : EUnrealAgentAcpToolCallState::Running;
		EmitToolCall(ToolCallId, Title, InitialState);
		EmitTurnStatus(InitialState == EUnrealAgentAcpToolCallState::Completed ? EWorldDataCodexTurnState::Thinking
				: InitialState == EUnrealAgentAcpToolCallState::Failed         ? EWorldDataCodexTurnState::Failed
																			   : EWorldDataCodexTurnState::RunningTool,
			InitialState == EUnrealAgentAcpToolCallState::Completed    ? FString::Printf(TEXT("工具执行完成，%s 正在继续思考…"), *ProviderName)
				: InitialState == EUnrealAgentAcpToolCallState::Failed ? FString::Printf(TEXT("工具执行失败：%s"), *Title)
																	   : FString::Printf(TEXT("%s 正在执行工具：%s"), *ProviderName, *Title));
		return;
	}

	if (UpdateType == TEXT("tool_call_update"))
	{
		const FString ToolCallId = AcpRules::GetOptionalString(Update, TEXT("toolCallId"));
		FString Title = AcpRules::GetOptionalString(Update, TEXT("title"));
		if (Title.IsEmpty() && !ToolCallId.IsEmpty())
		{
			if (const FString* CachedTitle = ToolCallTitles.Find(ToolCallId))
			{
				Title = *CachedTitle;
			}
		}
		if (Title.IsEmpty())
		{
			Title = ToolCallId;
		}
		const FString Status = AcpRules::GetOptionalString(Update, TEXT("status"));
		if (Status == TEXT("in_progress") || Status == TEXT("pending"))
		{
			EmitToolCall(ToolCallId, Title, EUnrealAgentAcpToolCallState::Running);
		}
		if (Status == TEXT("completed") || Status == TEXT("failed"))
		{
			FUnrealAgentAcpToolCallUpdate ResultDetails;
			AcpRules::ExtractToolCallResultDetails(Update, ResultDetails);
			EmitToolCall(ToolCallId, Title, Status == TEXT("completed") ? EUnrealAgentAcpToolCallState::Completed : EUnrealAgentAcpToolCallState::Failed, &ResultDetails);
			EmitTurnStatus(Status == TEXT("completed") ? EWorldDataCodexTurnState::Thinking : EWorldDataCodexTurnState::Failed,
				Status == TEXT("completed") ? FString::Printf(TEXT("工具执行完成，%s 正在继续思考…"), *ProviderName) : FString(TEXT("工具执行失败。")));
		}
	}
}

void FUnrealAgentCodexACPClient::UpdateConfigOptions(const TSharedPtr<FJsonObject>& Payload)
{
	if (!Payload.IsValid())
	{
		return;
	}
	const bool bKeepDesiredReasoning = DeferredConfigIds.Contains(TEXT("model_reasoning_effort"));
	const bool bKeepDesiredSpeed = DeferredConfigIds.Contains(TEXT("service_tier"));

	const TArray<TSharedPtr<FJsonValue>>* OptionValues = nullptr;
	if (!Payload->TryGetArrayField(TEXT("configOptions"), OptionValues) || !OptionValues)
	{
		return;
	}

	TArray<FUnrealAgentAcpConfigOption> NewOptions;
	for (const TSharedPtr<FJsonValue>& OptionValue : *OptionValues)
	{
		const TSharedPtr<FJsonObject> OptionObject = OptionValue.IsValid() ? OptionValue->AsObject() : nullptr;
		if (!OptionObject.IsValid())
		{
			continue;
		}

		FUnrealAgentAcpConfigOption Option;
		Option.Id = AcpRules::GetOptionalString(OptionObject, TEXT("id"));
		Option.Name = AcpRules::GetOptionalString(OptionObject, TEXT("name"));
		Option.Description = AcpRules::GetOptionalString(OptionObject, TEXT("description"));
		Option.Category = AcpRules::GetOptionalString(OptionObject, TEXT("category"));
		Option.CurrentValue = GetConfigScalarString(OptionObject, TEXT("currentValue"));
		if (Option.Id.IsEmpty())
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (OptionObject->TryGetArrayField(TEXT("options"), Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> ValueObject = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!ValueObject.IsValid())
				{
					continue;
				}

				FUnrealAgentAcpConfigOptionValue Entry;
				Entry.Value = GetConfigScalarString(ValueObject, TEXT("value"));
				Entry.Name = AcpRules::GetOptionalString(ValueObject, TEXT("name"));
				Entry.Description = AcpRules::GetOptionalString(ValueObject, TEXT("description"));
				if (!Entry.Value.IsEmpty())
				{
					Option.Options.Add(MoveTemp(Entry));
				}
			}
		}

		NewOptions.Add(MoveTemp(Option));
	}

	SessionConfigOptions = MoveTemp(NewOptions);
	const FUnrealAgentAcpConfigOption* ConfirmedModel = FindSessionConfigOption(TEXT("model"));
	AppliedModelId = ConfirmedModel ? ConfirmedModel->CurrentValue : FString();
	const FUnrealAgentAcpConfigOption* ConfirmedReasoning = FindSessionConfigOption(TEXT("model_reasoning_effort"));
	if (ConfirmedReasoning && !ConfirmedReasoning->CurrentValue.IsEmpty() && !bKeepDesiredReasoning)
	{
		SelectedReasoningEffort = ConfirmedReasoning->CurrentValue;
	}
	const FUnrealAgentAcpConfigOption* ConfirmedSpeed = FindSessionConfigOption(TEXT("service_tier"));
	if (ConfirmedSpeed && !ConfirmedSpeed->CurrentValue.IsEmpty() && !bKeepDesiredSpeed)
	{
		SelectedSpeed = ToLocalSpeedValue(ConfirmedSpeed->CurrentValue);
	}
	AdapterReasoningEfforts.Empty();
	for (const FUnrealAgentAcpConfigOption& Option : SessionConfigOptions)
	{
		if (!IsReasoningConfig(Option))
		{
			continue;
		}
		for (const FUnrealAgentAcpConfigOptionValue& Value : Option.Options)
		{
			AdapterReasoningEfforts.Add(Value.Value);
		}
	}
	RebuildConfigOptions();
}

FString FUnrealAgentCodexACPClient::GetModelDisplayName(const FString& ModelId) const
{
	const FWorldDataCodexModelCatalogEntry* Model = CliModelCatalog.FindByPredicate(
		[&ModelId](const FWorldDataCodexModelCatalogEntry& Entry)
		{
			return Entry.Id == ModelId;
		});
	return Model ? Model->DisplayName : FString();
}

void FUnrealAgentCodexACPClient::ApplyModelCatalog(TArray<FWorldDataCodexModelCatalogEntry>&& Catalog)
{
	CliModelCatalog = MoveTemp(Catalog);
	const int32 RawModelCount = CliModelCatalog.Num();
	RebuildCompatibleModelCatalog();
	const int32 CompatibleModelCount = ModelCatalog.Num();

	if (!SessionId.IsEmpty() && !ReconcileSessionModel())
	{
		return;
	}

	if (CompatibleModelCount < RawModelCount)
	{
		EmitStatus(FString::Printf(TEXT("已同步 Codex CLI 模型目录；当前 codex-acp %s "
										"将 %d 个缺少运行时元数据的模型标记为不可用。"),
			ActiveAdapterVersion.IsEmpty() ? TEXT("未知版本") : *ActiveAdapterVersion, RawModelCount - CompatibleModelCount));
	}
	else
	{
		EmitStatus(TEXT("已从当前 Codex CLI 实时同步模型、推理强度和速度。"));
	}
	SendPendingPromptIfReady();
}

void FUnrealAgentCodexACPClient::RebuildCompatibleModelCatalog()
{
	ModelCatalog.Empty(CliModelCatalog.Num());
	for (const FWorldDataCodexModelCatalogEntry& Entry : CliModelCatalog)
	{
		if (AcpRules::IsModelCompatibleWithAdapter(Entry.Id, ActiveAdapterVersion))
		{
			ModelCatalog.Add(Entry);
		}
	}

	if (!SelectedModelId.IsEmpty() && !AcpRules::IsModelCompatibleWithAdapter(SelectedModelId, ActiveAdapterVersion))
	{
		SelectedModelId = GetCompatibleFallbackModelId(SelectedModelId);
	}

	RebuildConfigOptions();
}

FString FUnrealAgentCodexACPClient::GetCompatibleFallbackModelId(const FString& ExcludedModelId) const
{
	const FString Preferred = AcpRules::GetSafeFallbackModelIdForAdapter(ActiveAdapterVersion);
	if (!Preferred.IsEmpty() && Preferred != ExcludedModelId &&
		ModelCatalog.ContainsByPredicate(
			[&Preferred](const FWorldDataCodexModelCatalogEntry& Entry)
			{
				return Entry.Id == Preferred;
			}))
	{
		return Preferred;
	}

	for (const FWorldDataCodexModelCatalogEntry& Entry : ModelCatalog)
	{
		if (Entry.Id != ExcludedModelId && AcpRules::IsModelCompatibleWithAdapter(Entry.Id, ActiveAdapterVersion))
		{
			return Entry.Id;
		}
	}

	return Preferred != ExcludedModelId ? Preferred : FString();
}

bool FUnrealAgentCodexACPClient::ReconcileSessionModel()
{
	if (SessionId.IsEmpty() || AppliedModelId.IsEmpty())
	{
		return true;
	}

	const bool bAppliedModelCompatible = AcpRules::IsModelCompatibleWithAdapter(AppliedModelId, ActiveAdapterVersion);
	if (!bAppliedModelCompatible &&
		(SelectedModelId.IsEmpty() || SelectedModelId == AppliedModelId || !AcpRules::IsModelCompatibleWithAdapter(SelectedModelId, ActiveAdapterVersion)))
	{
		SelectedModelId = GetCompatibleFallbackModelId(AppliedModelId);
		RebuildConfigOptions();
	}
	else if (SelectedModelId.IsEmpty())
	{
		SelectedModelId = AppliedModelId;
		RebuildConfigOptions();
	}

	if (SelectedModelId.IsEmpty())
	{
		Fail(FString::Printf(TEXT("codex-acp %s 无法执行模型 %s，且没有找到兼容回退模型。"), ActiveAdapterVersion.IsEmpty() ? TEXT("未知版本") : *ActiveAdapterVersion,
			*AppliedModelId));
		return false;
	}

	if (AppliedModelId == SelectedModelId)
	{
		return true;
	}

	for (const TPair<int32, FPendingConfigOptionRequest>& Request : ConfigOptionRpcIds)
	{
		if (Request.Value.LocalConfigId.Equals(TEXT("model"), ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	DeferredConfigIds.Add(TEXT("model"));
	if (!TrySetSessionConfigOption(TEXT("model"), SelectedModelId, AppliedModelId))
	{
		Fail(FString::Printf(TEXT("codex-acp %s 当前模型 %s 不可用，且无法切换到兼容模型 %s。"), ActiveAdapterVersion.IsEmpty() ? TEXT("未知版本") : *ActiveAdapterVersion,
			*AppliedModelId, *SelectedModelId));
		return false;
	}

	if (bAppliedModelCompatible)
	{
		EmitStatus(FString::Printf(TEXT("正在确认所选模型：%s…"), *SelectedModelId));
	}
	else
	{
		EmitStatus(FString::Printf(TEXT("当前 codex-acp %s 缺少 %s 的模型元数据，"
										"正在无缝切换到 %s…"),
			ActiveAdapterVersion.IsEmpty() ? TEXT("未知版本") : *ActiveAdapterVersion, *AppliedModelId, *SelectedModelId));
	}
	return false;
}

bool FUnrealAgentCodexACPClient::TryRecoverFromMissingModelMetadata(const FString& ErrorMessage)
{
	if (PromptModelDiagnostic.IsEmpty() || bRetriedAfterMissingModelMetadata || ActivePrompt.IsEmpty())
	{
		return false;
	}

	const FString FailedModelId = AppliedModelId;
	const FString FallbackModelId = GetCompatibleFallbackModelId(FailedModelId);
	if (FallbackModelId.IsEmpty())
	{
		return false;
	}

	bPromptInFlight = false;
	bRetriedAfterMissingModelMetadata = true;
	PendingPrompt = ActivePrompt;
	PendingPromptImages = ActivePromptImages;
	SelectedModelId = FallbackModelId;
	DeferredConfigIds.Add(TEXT("model"));
	RebuildConfigOptions();
	if (!TrySetSessionConfigOption(TEXT("model"), FallbackModelId, FailedModelId))
	{
		PendingPrompt.Empty();
		PendingPromptImages.Empty();
		return false;
	}

	EmitStatus(FString::Printf(TEXT("模型 %s 缺少运行时元数据（%s），"
									"已切换到 %s 并自动重试本条消息。"),
		FailedModelId.IsEmpty() ? TEXT("未知") : *FailedModelId, ErrorMessage.IsEmpty() ? TEXT("Internal error") : *ErrorMessage, *FallbackModelId));
	EmitTurnStatus(EWorldDataCodexTurnState::Thinking, FString::Printf(TEXT("模型不可用，已切换到 %s，正在自动重试…"), *FallbackModelId));
	return true;
}

void FUnrealAgentCodexACPClient::RebuildConfigOptions()
{
	ConfigOptions.Empty();
	if (ModelCatalog.IsEmpty())
	{
		// Cursor 等 Provider 的目录完全来自 session。切换/新建期间沿用
		// 上一个已确认快照，并将用户刚选择的目标值立即反映到前台。
		for (FUnrealAgentAcpConfigOption Option : SessionConfigOptions)
		{
			if (IsConfigKind(Option, TEXT("model"), TEXT("model")) && !SelectedModelId.IsEmpty())
			{
				Option.CurrentValue = SelectedModelId;
			}
			else if (IsReasoningConfig(Option) && !SelectedReasoningEffort.IsEmpty())
			{
				Option.CurrentValue = SelectedReasoningEffort;
			}
			else if (IsSpeedConfig(Option) && !SelectedSpeed.IsEmpty())
			{
				Option.CurrentValue = ToRemoteSpeedValue(Option, SelectedSpeed);
			}
			ConfigOptions.Add(MoveTemp(Option));
		}
		if (OnConfigOptionsChanged.IsBound())
		{
			OnConfigOptionsChanged.Execute(ConfigOptions);
		}
		return;
	}

	const FUnrealAgentAcpConfigOption* SessionModel = nullptr;
	const FUnrealAgentAcpConfigOption* SessionReasoning = nullptr;
	for (const FUnrealAgentAcpConfigOption& Option : SessionConfigOptions)
	{
		if (IsConfigKind(Option, TEXT("model"), TEXT("model")))
		{
			SessionModel = &Option;
			continue;
		}
		if (IsReasoningConfig(Option))
		{
			SessionReasoning = &Option;
			continue;
		}
		if (IsSpeedConfig(Option))
		{
			continue;
		}
		ConfigOptions.Add(Option);
	}

	if (SelectedModelId.IsEmpty())
	{
		SelectedModelId = SessionModel && !SessionModel->CurrentValue.IsEmpty() ? SessionModel->CurrentValue : ModelCatalog[0].Id;
	}

	const FWorldDataCodexModelCatalogEntry* SelectedModel = ModelCatalog.FindByPredicate(
		[this](const FWorldDataCodexModelCatalogEntry& Entry)
		{
			return Entry.Id == SelectedModelId;
		});
	if (!SelectedModel)
	{
		SelectedModel = &ModelCatalog[0];
		SelectedModelId = SelectedModel->Id;
	}

	FUnrealAgentAcpConfigOption ModelOption;
	ModelOption.Id = TEXT("model");
	ModelOption.Name = TEXT("模型");
	ModelOption.Description = TEXT("由当前 Codex CLI 版本实时提供；ACP 不兼容项会保留显示并标明原因");
	ModelOption.Category = TEXT("model");
	ModelOption.CurrentValue = AppliedModelId.IsEmpty() ? SelectedModelId : AppliedModelId;
	for (const FWorldDataCodexModelCatalogEntry& Entry : CliModelCatalog)
	{
		FUnrealAgentAcpConfigOptionValue Value;
		Value.Value = Entry.Id;
		Value.Name = Entry.DisplayName;
		Value.bEnabled = AcpRules::IsModelCompatibleWithAdapter(Entry.Id, ActiveAdapterVersion);
		int32 DuplicateNames = 0;
		for (const FWorldDataCodexModelCatalogEntry& Candidate : CliModelCatalog)
		{
			if (Candidate.DisplayName == Entry.DisplayName)
			{
				++DuplicateNames;
			}
		}
		if (DuplicateNames > 1)
		{
			Value.Name += FString::Printf(TEXT(" · %s"), *Entry.Id);
		}
		Value.Description = Entry.Description;
		if (!Value.bEnabled)
		{
			if (!Value.Description.IsEmpty())
			{
				Value.Description += TEXT(" ");
			}
			Value.Description += FString::Printf(TEXT("当前 codex-acp %s 缺少该模型的运行时元数据；"
													  "升级适配器后会自动启用。"),
				ActiveAdapterVersion.IsEmpty() ? TEXT("未知版本") : *ActiveAdapterVersion);
		}
		ModelOption.Options.Add(MoveTemp(Value));
	}
	ConfigOptions.Add(MoveTemp(ModelOption));

	const bool bReasoningStillSupported = SelectedModel->ReasoningEfforts.ContainsByPredicate(
		[this](const FUnrealAgentAcpConfigOptionValue& Value)
		{
			return Value.Value == SelectedReasoningEffort;
		});
	if (SelectedReasoningEffort.IsEmpty() || !bReasoningStillSupported)
	{
		SelectedReasoningEffort = SelectedModel->DefaultReasoningEffort;
	}
	if (SelectedReasoningEffort.IsEmpty() && !SelectedModel->ReasoningEfforts.IsEmpty())
	{
		SelectedReasoningEffort = SelectedModel->ReasoningEfforts[0].Value;
	}
	const auto IsReasoningEnabled = [](const FString& Effort)
	{
		// ModelCatalog 来自当前 Codex CLI 的实时能力目录，因此其中的
		// max / ultra 也是可选能力。ACP session 未公开某档位只表示
		// 不能在当前会话热切换；RebuildConfigOptions 会提示新会话生效。
		return !Effort.IsEmpty();
	};
	if (!IsReasoningEnabled(SelectedReasoningEffort))
	{
		const FUnrealAgentAcpConfigOptionValue* Fallback = SelectedModel->ReasoningEfforts.FindByPredicate(
			[SelectedModel, &IsReasoningEnabled](const FUnrealAgentAcpConfigOptionValue& Value)
			{
				return Value.Value == SelectedModel->DefaultReasoningEffort && IsReasoningEnabled(Value.Value);
			});
		if (!Fallback)
		{
			for (int32 Index = SelectedModel->ReasoningEfforts.Num() - 1; Index >= 0; --Index)
			{
				if (IsReasoningEnabled(SelectedModel->ReasoningEfforts[Index].Value))
				{
					Fallback = &SelectedModel->ReasoningEfforts[Index];
					break;
				}
			}
		}
		SelectedReasoningEffort = Fallback ? Fallback->Value : FString();
	}

	FUnrealAgentAcpConfigOption ReasoningOption;
	ReasoningOption.Id = TEXT("model_reasoning_effort");
	ReasoningOption.Name = TEXT("推理强度");
	ReasoningOption.Description = TEXT("可用档位随所选模型实时变化");
	ReasoningOption.Category = TEXT("reasoning");
	ReasoningOption.CurrentValue = SelectedReasoningEffort;
	for (FUnrealAgentAcpConfigOptionValue Value : SelectedModel->ReasoningEfforts)
	{
		Value.Name = GetReasoningDisplayName(Value.Value);
		Value.bEnabled = IsReasoningEnabled(Value.Value);
		if (!Value.bEnabled)
		{
			if (!Value.Description.IsEmpty())
			{
				Value.Description += TEXT(" ");
			}
			Value.Description += TEXT("当前 codex-acp 尚未公开此档位；升级适配器后将自动启用。");
		}
		else if (SessionReasoning &&
			!SessionReasoning->Options.ContainsByPredicate(
				[&Value](const FUnrealAgentAcpConfigOptionValue& Candidate)
				{
					return Candidate.Value == Value.Value;
				}))
		{
			if (!Value.Description.IsEmpty())
			{
				Value.Description += TEXT(" ");
			}
			Value.Description += TEXT("当前会话不支持热切换，选择后在新对话生效。");
		}
		ReasoningOption.Options.Add(MoveTemp(Value));
	}
	ConfigOptions.Add(MoveTemp(ReasoningOption));

	const bool bFastSupported = SelectedModel->SpeedOptions.ContainsByPredicate(
		[](const FUnrealAgentAcpConfigOptionValue& Value)
		{
			return Value.Value == TEXT("fast");
		});
	if (!bFastSupported)
	{
		SelectedSpeed = TEXT("standard");
	}

	FUnrealAgentAcpConfigOption SpeedOption;
	SpeedOption.Id = TEXT("service_tier");
	SpeedOption.Name = TEXT("速度");
	SpeedOption.Description = TEXT("Fast 可用性随所选模型实时变化");
	SpeedOption.Category = TEXT("speed");
	SpeedOption.CurrentValue = SelectedSpeed;
	FUnrealAgentAcpConfigOptionValue Standard;
	Standard.Value = TEXT("standard");
	Standard.Name = TEXT("标准");
	Standard.Description = TEXT("默认速度");
	SpeedOption.Options.Add(MoveTemp(Standard));
	for (FUnrealAgentAcpConfigOptionValue Value : SelectedModel->SpeedOptions)
	{
		if (Value.Value == TEXT("fast"))
		{
			Value.Name = TEXT("快速");
			if (Value.Description.IsEmpty())
			{
				Value.Description = TEXT("1.5 倍速度，用量更多");
			}
			SpeedOption.Options.Add(MoveTemp(Value));
		}
	}
	ConfigOptions.Add(MoveTemp(SpeedOption));

	if (OnConfigOptionsChanged.IsBound())
	{
		OnConfigOptionsChanged.Execute(ConfigOptions);
	}
}

void FUnrealAgentCodexACPClient::Fail(const FString& Message, const bool bHadUserTurnBeforeCleanup)
{
	LastError = Message;
	UE_LOG(LogUnrealAgentCodexACP, Warning, TEXT("%s"), *Message);
	const bool bShouldEndUserTurn = AcpRules::ShouldFailCloseUserTurn(bPromptInFlight, !PendingPrompt.IsEmpty(), !PendingPromptImages.IsEmpty(), bHadUserTurnBeforeCleanup);
	PendingPrompt.Empty();
	PendingPromptImages.Empty();
	if (OnError.IsBound())
	{
		OnError.Execute(Message);
	}
	if (bShouldEndUserTurn)
	{
		bPromptInFlight = false;
		EmitTurnStatus(EWorldDataCodexTurnState::Failed, FString::Printf(TEXT("请求失败：%s"), *Message));
	}
}

void FUnrealAgentCodexACPClient::EmitStatus(const FString& Message)
{
	if (OnStatus.IsBound())
	{
		OnStatus.Execute(Message);
	}
}

void FUnrealAgentCodexACPClient::EmitText(const FString& Text)
{
	if (OnText.IsBound())
	{
		OnText.Execute(Text);
	}
}

void FUnrealAgentCodexACPClient::EmitTurnStatus(EWorldDataCodexTurnState State, const FString& Message, const FString& StopReason, const int32 InputTokens,
	const int32 OutputTokens, const bool bHasUsage)
{
	if (!OnTurnStatus.IsBound())
	{
		return;
	}

	FWorldDataCodexTurnStatus Status;
	Status.State = State;
	Status.Message = Message;
	Status.StopReason = StopReason;
	Status.InputTokens = InputTokens;
	Status.OutputTokens = OutputTokens;
	Status.bHasUsage = bHasUsage;
	OnTurnStatus.Execute(Status);
}

void FUnrealAgentCodexACPClient::EmitToolCall(const FString& ToolCallId, const FString& Title, const EUnrealAgentAcpToolCallState State,
	const FUnrealAgentAcpToolCallUpdate* ResultDetails)
{
	if (!OnToolCall.IsBound())
	{
		return;
	}

	FUnrealAgentAcpToolCallUpdate Update = ResultDetails ? *ResultDetails : FUnrealAgentAcpToolCallUpdate();
	Update.ToolCallId = ToolCallId;
	Update.Title = Title;
	Update.State = State;
	OnToolCall.Execute(Update);
}
