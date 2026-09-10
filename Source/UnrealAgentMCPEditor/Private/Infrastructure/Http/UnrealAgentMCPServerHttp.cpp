// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPServerHttp.cpp
 * @brief MCP Streamable HTTP 安全入口、会话与 JSON-RPC 方法编排。
 */

#include "Infrastructure/Http/UnrealAgentMCPServer.h"

#include "Core/Common/UnrealAgentMCPBrand.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPToolExecutionService.h"
#include "Infrastructure/Http/UnrealAgentMCPHttpSecurity.h"
#include "Core/Protocol/UnrealAgentMCPProtocol.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

using namespace UnrealAgentMCP;

namespace
{
	constexpr int32 MaximumRequestBodyBytes = 1024 * 1024;

	FString GetFirstHeaderValue(const FHttpServerRequest& Request, const FString& HeaderName)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(HeaderName, ESearchCase::IgnoreCase) && !Pair.Value.IsEmpty())
			{
				return Pair.Value[0];
			}
		}
		return FString();
	}

	FString GetAcpProviderForAudit(const FString& HeaderValue)
	{
		const FString Provider = HeaderValue.TrimStartAndEnd().ToLower();
		if (Provider == TEXT("codex") || Provider == TEXT("cursor"))
		{
			return Provider;
		}
		return TEXT("unknown");
	}

	TUniquePtr<FHttpServerResponse> MakeEmptyResponse(const int32 Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), TEXT("text/plain"));
		Response->Code = static_cast<EHttpServerResponseCodes>(Code);
		return Response;
	}

	TUniquePtr<FHttpServerResponse> MakeSseResponse(const FString& Body, const bool bHasAdditionalWrites, const bool bSkipHeaders)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("text/event-stream"));
		Response->Code = EHttpServerResponseCodes::Ok;
		Response->HttpVersion = HttpVersion::EHttpServerHttpVersion::HTTP_VERSION_1_1;
		Response->Flags = EHttpServerResponseFlags::MultipleWriteStream;
		if (bHasAdditionalWrites)
		{
			Response->Flags |= EHttpServerResponseFlags::HasAdditionalWrites;
		}
		if (bSkipHeaders)
		{
			Response->Flags |= EHttpServerResponseFlags::SkipHeaderWrite;
		}
		return Response;
	}

	FString ExtractTaskId(const TSharedPtr<FJsonObject>& JsonRpcResponse)
	{
		const TSharedPtr<FJsonObject>* Result = nullptr;
		const TSharedPtr<FJsonObject>* Structured = nullptr;
		if (!JsonRpcResponse.IsValid() || !JsonRpcResponse->TryGetObjectField(TEXT("result"), Result) || !Result || !Result->IsValid() ||
			!(*Result)->TryGetObjectField(TEXT("structuredContent"), Structured) || !Structured || !Structured->IsValid())
		{
			return FString();
		}

		FString TaskId;
		if ((*Structured)->TryGetStringField(TEXT("taskId"), TaskId))
		{
			return TaskId;
		}
		const TSharedPtr<FJsonObject>* Task = nullptr;
		return (*Structured)->TryGetObjectField(TEXT("task"), Task) && Task && Task->IsValid() && (*Task)->TryGetStringField(TEXT("taskId"), TaskId) ? TaskId : FString();
	}
}

TUniquePtr<FHttpServerResponse> FUnrealAgentMCPServer::MakeJsonResponse(const int32 Code, const FString& Body)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
	Response->Code = static_cast<EHttpServerResponseCodes>(Code);
	return Response;
}

bool FUnrealAgentMCPServer::HandleMCPOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	OnComplete(MakeJsonResponse(200, TEXT("{}")));
	return true;
}

bool FUnrealAgentMCPServer::HandleMCPGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString HostHeader = GetFirstHeaderValue(Request, TEXT("Host"));
	const FString OriginHeader = GetFirstHeaderValue(Request, TEXT("Origin"));
	const FString ProvidedToken = GetFirstHeaderValue(Request, GetAccessTokenHeaderName());
	if (!HttpSecurity::IsHostHeaderAllowed(HostHeader) || !HttpSecurity::IsOriginHeaderAllowed(OriginHeader))
	{
		OnComplete(MakeJsonResponse(403, TEXT("{\"error\":\"Forbidden request authority.\"}")));
		return true;
	}
	if (!HttpSecurity::ConstantTimeEquals(GetAccessToken(), ProvidedToken))
	{
		OnComplete(MakeJsonResponse(401, TEXT("{\"error\":\"Missing or invalid MCP access token.\"}")));
		return true;
	}

	const FString SessionId = GetFirstHeaderValue(Request, TEXT("MCP-Session-Id"));
	const FString ProtocolVersion = GetFirstHeaderValue(Request, TEXT("MCP-Protocol-Version"));
	const Protocol::EMcpSessionValidation Validation = SessionRegistry.Validate(SessionId, ProtocolVersion, true);
	if (Validation != Protocol::EMcpSessionValidation::Ready)
	{
		const int32 Code = Validation == Protocol::EMcpSessionValidation::UnknownSession ? 404 : Validation == Protocol::EMcpSessionValidation::NotInitialized ? 409 : 400;
		OnComplete(MakeJsonResponse(Code, TEXT("{\"error\":\"SSE requires an initialized Session and matching protocol version.\"}")));
		return true;
	}
	const FString Accept = GetFirstHeaderValue(Request, TEXT("Accept"));
	if (!Accept.IsEmpty() && !Accept.Contains(TEXT("text/event-stream"), ESearchCase::IgnoreCase))
	{
		OnComplete(MakeJsonResponse(406, TEXT("{\"error\":\"Accept must include text/event-stream.\"}")));
		return true;
	}

	const TSharedPtr<FHttpResultCallback> Callback = MakeShared<FHttpResultCallback>(OnComplete);
	TSharedPtr<FHttpResultCallback> PreviousCallback;
	{
		FScopeLock Lock(&GetStateMutex());
		PreviousCallback = SseCallbacks.FindRef(SessionId);
		SseCallbacks.Add(SessionId, Callback);
	}
	if (PreviousCallback.IsValid() && *PreviousCallback)
	{
		(*PreviousCallback)(MakeSseResponse(TEXT("event: worlddata.reconnect\ndata: {\"reason\":\"replaced\"}\n\n"), false, true));
	}

	TUniquePtr<FHttpServerResponse> Open =
		MakeSseResponse(FString::Printf(TEXT("event: worlddata.session\ndata: {\"sessionId\":\"%s\",\"protocolVersion\":\"%s\"}\n\n"), *SessionId, *ProtocolVersion), true, false);
	Open->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache, no-transform") });
	Open->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
	Open->Headers.Add(TEXT("MCP-Session-Id"), { SessionId });
	Open->Headers.Add(TEXT("MCP-Protocol-Version"), { ProtocolVersion });
	OnComplete(MoveTemp(Open));
	return true;
}

bool FUnrealAgentMCPServer::HandleMCPDelete(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString HostHeader = GetFirstHeaderValue(Request, TEXT("Host"));
	const FString OriginHeader = GetFirstHeaderValue(Request, TEXT("Origin"));
	const FString ExpectedToken = GetAccessToken();
	const FString ProvidedToken = GetFirstHeaderValue(Request, GetAccessTokenHeaderName());
	const FString SessionId = GetFirstHeaderValue(Request, TEXT("Mcp-Session-Id"));
	const FString ProtocolVersion = GetFirstHeaderValue(Request, TEXT("MCP-Protocol-Version"));
	if (!HttpSecurity::IsHostHeaderAllowed(HostHeader) || !HttpSecurity::IsOriginHeaderAllowed(OriginHeader))
	{
		OnComplete(MakeJsonResponse(403, TEXT("{\"error\":\"Forbidden request authority.\"}")));
		return true;
	}
	if (ExpectedToken.IsEmpty() || !HttpSecurity::ConstantTimeEquals(ExpectedToken, ProvidedToken))
	{
		OnComplete(MakeJsonResponse(401, TEXT("{\"error\":\"Missing or invalid MCP access token.\"}")));
		return true;
	}
	const Protocol::EMcpSessionValidation Validation = SessionRegistry.Validate(SessionId, ProtocolVersion, false);
	if (Validation == Protocol::EMcpSessionValidation::ProtocolMismatch || Validation == Protocol::EMcpSessionValidation::MissingSessionId)
	{
		OnComplete(MakeJsonResponse(400, TEXT("{\"error\":\"DELETE requires a Session and matching protocol version.\"}")));
		return true;
	}

	TSharedPtr<FHttpResultCallback> SseCallback;
	{
		FScopeLock Lock(&GetStateMutex());
		SseCallback = SseCallbacks.FindRef(SessionId);
		SseCallbacks.Remove(SessionId);
	}
	const bool bRemoved = SessionRegistry.Close(SessionId);
	if (SseCallback.IsValid() && *SseCallback)
	{
		(*SseCallback)(MakeSseResponse(TEXT("event: worlddata.session_closed\ndata: {\"closed\":true}\n\n"), false, true));
	}
	OnComplete(MakeJsonResponse(bRemoved ? 200 : 404, bRemoved ? TEXT("{\"closed\":true}") : TEXT("{\"error\":\"Unknown or expired MCP session.\"}")));
	return true;
}

bool FUnrealAgentMCPServer::HandleMCPPost(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString HostHeader = GetFirstHeaderValue(Request, TEXT("Host"));
	if (!HttpSecurity::IsHostHeaderAllowed(HostHeader))
	{
		OnComplete(MakeJsonResponse(403, TEXT("{\"error\":\"Only loopback Host headers (127.0.0.1/localhost) are accepted.\"}")));
		return true;
	}

	const FString OriginHeader = GetFirstHeaderValue(Request, TEXT("Origin"));
	if (!HttpSecurity::IsOriginHeaderAllowed(OriginHeader))
	{
		OnComplete(MakeJsonResponse(403, TEXT("{\"error\":\"Only loopback Origin values are accepted.\"}")));
		return true;
	}

	const FString ExpectedToken = GetAccessToken();
	const FString ProvidedToken = GetFirstHeaderValue(Request, GetAccessTokenHeaderName());
	if (ExpectedToken.IsEmpty() || !HttpSecurity::ConstantTimeEquals(ExpectedToken, ProvidedToken))
	{
		OnComplete(MakeJsonResponse(401, TEXT("{\"error\":\"Missing or invalid MCP access token.\"}")));
		return true;
	}

	if (Request.Body.Num() > MaximumRequestBodyBytes)
	{
		OnComplete(MakeJsonResponse(413, TEXT("{\"error\":\"MCP request body is too large.\"}")));
		return true;
	}

	FString Body;
	if (!Request.Body.IsEmpty())
	{
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		Body = FString(Converter.Length(), Converter.Get());
	}

	if (Body.IsEmpty())
	{
		OnComplete(MakeJsonResponse(400, JsonObjectToString(Protocol::MakeJsonRpcErrorResponse(MakeShared<FJsonValueNull>(), -32700, TEXT("Empty request body")).ToSharedRef())));
		return true;
	}

	const TSharedPtr<FJsonObject> JsonRequest = ServerEnvironment::ParseJsonObject(Body);
	if (!JsonRequest.IsValid())
	{
		OnComplete(MakeJsonResponse(400, JsonObjectToString(Protocol::MakeJsonRpcErrorResponse(MakeShared<FJsonValueNull>(), -32700, TEXT("Invalid JSON")).ToSharedRef())));
		return true;
	}

	FString Method;
	JsonRequest->TryGetStringField(TEXT("method"), Method);
	const FString ProvidedSessionId = GetFirstHeaderValue(Request, TEXT("Mcp-Session-Id"));
	Execution::FMcpToolExecutionOptions ExecutionOptions;
	const FString ApprovalClientId = GetFirstHeaderValue(Request, TEXT("X-WorldData-ACP-Client"));
	ExecutionOptions.PolicyContext.ClientId = !ApprovalClientId.IsEmpty() ? ApprovalClientId.Left(128) : GetFirstHeaderValue(Request, TEXT("User-Agent")).Left(128);
	if (ExecutionOptions.PolicyContext.ClientId.IsEmpty())
	{
		ExecutionOptions.PolicyContext.ClientId = TEXT("streamable-http-client");
	}
	ExecutionOptions.PolicyContext.SessionId = ProvidedSessionId;
	ExecutionOptions.PolicyContext.Source = TEXT("StreamableHTTP");
	ExecutionOptions.PolicyContext.Provider = GetAcpProviderForAudit(GetFirstHeaderValue(Request, TEXT("X-WorldData-ACP-Provider")));
	ExecutionOptions.PolicyContext.bAuthenticated = true;
	ExecutionOptions.PolicyContext.bLocalConnection = true;
	ExecutionOptions.ProtocolRequestId = Protocol::MakeJsonRpcRequestKey(JsonRequest->TryGetField(TEXT("id")));
	if (Method != TEXT("initialize"))
	{
		if (ProvidedSessionId.IsEmpty())
		{
			OnComplete(MakeJsonResponse(400, TEXT("{\"error\":\"Mcp-Session-Id is required after initialize.\"}")));
			return true;
		}
		const FString ProvidedProtocolVersion = GetFirstHeaderValue(Request, TEXT("MCP-Protocol-Version"));
		const Protocol::EMcpSessionValidation Validation = SessionRegistry.Validate(ProvidedSessionId, ProvidedProtocolVersion, true);
		if (Validation == Protocol::EMcpSessionValidation::UnknownSession)
		{
			OnComplete(MakeJsonResponse(404, TEXT("{\"error\":\"Unknown or expired MCP session. Re-initialize to obtain a new session.\"}")));
			return true;
		}
		if (Validation == Protocol::EMcpSessionValidation::ProtocolMismatch)
		{
			OnComplete(MakeJsonResponse(400, TEXT("{\"error\":\"MCP-Protocol-Version must match the initialized Session.\"}")));
			return true;
		}
		if (Method == TEXT("notifications/initialized"))
		{
			SessionRegistry.MarkInitialized(ProvidedSessionId);
			OnComplete(MakeEmptyResponse(202));
			return true;
		}
		if (Validation == Protocol::EMcpSessionValidation::NotInitialized)
		{
			OnComplete(MakeJsonResponse(409, TEXT("{\"error\":\"Send notifications/initialized before using this MCP session.\"}")));
			return true;
		}
		if (Method == TEXT("notifications/cancelled"))
		{
			const TSharedPtr<FJsonObject>* Params = nullptr;
			JsonRequest->TryGetObjectField(TEXT("params"), Params);
			FString TaskId;
			if (Params && Params->IsValid())
			{
				(*Params)->TryGetStringField(TEXT("taskId"), TaskId);
				if (TaskId.IsEmpty())
				{
					const TSharedPtr<FJsonValue> CancelledRequestId = (*Params)->TryGetField(TEXT("requestId"));
					SessionRegistry.ResolveTaskForRequest(ProvidedSessionId, Protocol::MakeJsonRpcRequestKey(CancelledRequestId), TaskId);
				}
			}
			if (!TaskId.IsEmpty())
			{
				TSharedRef<FJsonObject> CancelArgs = MakeShared<FJsonObject>();
				CancelArgs->SetStringField(TEXT("taskId"), TaskId);
				CancelArgs->SetStringField(TEXT("reason"), TEXT("MCP notifications/cancelled"));
				DispatchTool(TEXT("cancel_task"), JsonObjectToString(CancelArgs), ExecutionOptions);
			}
			OnComplete(MakeEmptyResponse(202));
			return true;
		}
	}

	if (!JsonRequest->HasField(TEXT("id")))
	{
		OnComplete(MakeEmptyResponse(202));
		return true;
	}

	const FString RequestProtocolVersion = GetFirstHeaderValue(Request, TEXT("MCP-Protocol-Version"));
	const TSharedPtr<FHttpResultCallback> CompletionCallback = MakeShared<FHttpResultCallback>(OnComplete);
	const TFunction<void(bool, bool)> CompleteRequest =
		[JsonRequest, ExecutionOptions, Method, ProvidedSessionId, RequestProtocolVersion, CompletionCallback](const bool bAllowed, const bool bConfirmed) mutable
	{
		ExecutionOptions.PolicyContext.bConfirmed = bConfirmed;
		const TSharedPtr<FJsonObject> Result = bAllowed
			? DispatchJsonRpcRequestWithContext(JsonRequest, ExecutionOptions)
			: Protocol::MakeJsonRpcErrorResponse(JsonRequest->TryGetField(TEXT("id")), -32003, TEXT("用户已在 WorldData 审批卡片中拒绝本次工具调用。"));
		TUniquePtr<FHttpServerResponse> Response = MakeJsonResponse(200, JsonObjectToString(Result.ToSharedRef()));

		FString InitializedProtocolVersion = Protocol::GetLatestProtocolVersion();
		if (Method == TEXT("initialize"))
		{
			const TSharedPtr<FJsonObject>* Params = nullptr;
			if (JsonRequest->TryGetObjectField(TEXT("params"), Params) && Params && Params->IsValid())
			{
				FString RequestedProtocolVersion;
				(*Params)->TryGetStringField(TEXT("protocolVersion"), RequestedProtocolVersion);
				InitializedProtocolVersion = Protocol::NegotiateProtocolVersion(RequestedProtocolVersion);
			}
		}
		const FString ResponseSessionId = Method == TEXT("initialize") ? RegisterSession(InitializedProtocolVersion) : ProvidedSessionId;
		const FString ResponseProtocolVersion = Method == TEXT("initialize") ? InitializedProtocolVersion : RequestProtocolVersion;
		Response->Headers.Add(TEXT("MCP-Protocol-Version"), { ResponseProtocolVersion });
		if (!ResponseSessionId.IsEmpty())
		{
			Response->Headers.Add(TEXT("MCP-Session-Id"), { ResponseSessionId });
		}
		if (bAllowed && Method == TEXT("tools/call"))
		{
			const FString TaskId = ExtractTaskId(Result);
			const FString RequestKey = Protocol::MakeJsonRpcRequestKey(JsonRequest->TryGetField(TEXT("id")));
			SessionRegistry.BindRequestToTask(ResponseSessionId, RequestKey, TaskId);
		}
		if (CompletionCallback.IsValid() && *CompletionCallback)
		{
			(*CompletionCallback)(MoveTemp(Response));
		}
	};
	const TFunction<void(int32, const FString&)> CompleteWithError = [JsonRequest, CompletionCallback](const int32 ErrorCode, const FString& ErrorMessage)
	{
		TUniquePtr<FHttpServerResponse> Response =
			MakeJsonResponse(200, JsonObjectToString(Protocol::MakeJsonRpcErrorResponse(JsonRequest->TryGetField(TEXT("id")), ErrorCode, ErrorMessage).ToSharedRef()));
		if (CompletionCallback.IsValid() && *CompletionCallback)
		{
			(*CompletionCallback)(MoveTemp(Response));
		}
	};

	if (Method == TEXT("tools/call"))
	{
		FUnrealAgentMCPToolApprovalHandler ApprovalHandler;
		const TSharedPtr<FJsonObject>* Params = nullptr;
		const TSharedPtr<FJsonObject>* Arguments = nullptr;
		FString ToolName;
		const bool bAcpRequest = !ApprovalClientId.IsEmpty();
		if (bAcpRequest && !FindToolApprovalHandler(ApprovalClientId, ApprovalHandler))
		{
			CompleteWithError(-32003, TEXT("The UnrealAgent ACP client is detached. Reopen the panel and start a new session."));
			return true;
		}
		if (bAcpRequest && JsonRequest->TryGetObjectField(TEXT("params"), Params) && Params && Params->IsValid() && (*Params)->TryGetStringField(TEXT("name"), ToolName))
		{
			(*Params)->TryGetObjectField(TEXT("arguments"), Arguments);
			FUnrealAgentMCPToolApprovalRequest ApprovalRequest;
			if (DescribeToolApprovalRequest(ToolName, Arguments && Arguments->IsValid() ? *Arguments : MakeShared<FJsonObject>(), ApprovalRequest))
			{
				if (!ApprovalRequest.bReadOnly && !TryAcquireAcpMutationPermit(ApprovalClientId))
				{
					CompleteWithError(-32004, TEXT("Mutation rate limit exceeded. Batch the operation or retry after 100 ms."));
					return true;
				}
				ApprovalHandler.Execute(ApprovalRequest,
					[CompleteRequest](const bool bAllow)
					{
						CompleteRequest(bAllow, bAllow);
					});
				return true;
			}
			CompleteWithError(-32003, TEXT("ACP tool metadata could not be resolved; execution was denied."));
			return true;
		}
		if (bAcpRequest)
		{
			CompleteWithError(-32602, TEXT("Invalid ACP tools/call parameters."));
			return true;
		}
	}

	CompleteRequest(true, false);
	return true;
}

FString FUnrealAgentMCPServer::RegisterSession(const FString& ProtocolVersion)
{
	FString EvictedSessionId;
	const FString SessionId = SessionRegistry.Open(ProtocolVersion, &EvictedSessionId);
	if (!EvictedSessionId.IsEmpty())
	{
		TSharedPtr<FHttpResultCallback> EvictedCallback;
		{
			FScopeLock Lock(&GetStateMutex());
			EvictedCallback = SseCallbacks.FindRef(EvictedSessionId);
			SseCallbacks.Remove(EvictedSessionId);
		}
		if (EvictedCallback.IsValid() && *EvictedCallback)
		{
			(*EvictedCallback)(MakeSseResponse(TEXT("event: worlddata.session_closed\ndata: {\"reason\":\"capacity_eviction\"}\n\n"), false, true));
		}
	}
	return SessionId;
}

void FUnrealAgentMCPServer::CloseAllSseStreams(const FString& Reason)
{
	TArray<TSharedPtr<FHttpResultCallback>> Callbacks;
	{
		FScopeLock Lock(&GetStateMutex());
		SseCallbacks.GenerateValueArray(Callbacks);
		SseCallbacks.Reset();
	}
	const FString Event = FString::Printf(TEXT("event: worlddata.session_closed\ndata: {\"reason\":\"%s\"}\n\n"), *Reason.ReplaceCharWithEscapedChar());
	for (const TSharedPtr<FHttpResultCallback>& Callback : Callbacks)
	{
		if (Callback.IsValid() && *Callback)
		{
			(*Callback)(MakeSseResponse(Event, false, true));
		}
	}
}

void FUnrealAgentMCPServer::ScheduleToolsListChangedBroadcast()
{
	uint64 ScheduleSerial = 0;
	{
		FScopeLock Lock(&GetStateMutex());
		if (bToolsListChangedBroadcastScheduled)
		{
			return;
		}
		bToolsListChangedBroadcastScheduled = true;
		ScheduleSerial = ++ToolsListChangedScheduleSerial;
	}

	const FTSTicker::FDelegateHandle TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[ScheduleSerial](const float)
		{
			BroadcastToolsListChanged(ScheduleSerial);
			return false;
		}));
	if (!TickerHandle.IsValid())
	{
		FScopeLock Lock(&GetStateMutex());
		if (ToolsListChangedScheduleSerial == ScheduleSerial)
		{
			bToolsListChangedBroadcastScheduled = false;
		}
	}
}

void FUnrealAgentMCPServer::BroadcastToolsListChanged(const uint64 ScheduleSerial)
{
	TArray<TSharedPtr<FHttpResultCallback>> Callbacks;
	{
		FScopeLock Lock(&GetStateMutex());
		if (!bToolsListChangedBroadcastScheduled || ToolsListChangedScheduleSerial != ScheduleSerial)
		{
			return;
		}
		bToolsListChangedBroadcastScheduled = false;
		SseCallbacks.GenerateValueArray(Callbacks);
	}

	const TSharedRef<FJsonObject> Notification = Protocol::MakeJsonRpcNotification(TEXT("notifications/tools/list_changed"));
	const FString Event = FString::Printf(TEXT("event: message\ndata: %s\n\n"), *JsonObjectToString(Notification));
	for (const TSharedPtr<FHttpResultCallback>& Callback : Callbacks)
	{
		if (Callback.IsValid() && *Callback)
		{
			(*Callback)(MakeSseResponse(Event, true, true));
		}
	}
}

TSharedPtr<FJsonObject> FUnrealAgentMCPServer::DispatchJsonRpcRequest(const TSharedPtr<FJsonObject>& Request)
{
	return DispatchJsonRpcRequestWithContext(Request, Execution::FMcpToolExecutionOptions());
}

TSharedPtr<FJsonObject> FUnrealAgentMCPServer::DispatchJsonRpcRequestWithContext(const TSharedPtr<FJsonObject>& Request, const Execution::FMcpToolExecutionOptions& Options)
{
	if (!Request.IsValid())
	{
		return Protocol::MakeJsonRpcErrorResponse(MakeShared<FJsonValueNull>(), -32600, TEXT("JSON-RPC request must be an object."));
	}
	TSharedPtr<FJsonValue> RequestId = Request->TryGetField(TEXT("id"));
	if (!RequestId.IsValid())
	{
		RequestId = MakeShared<FJsonValueNull>();
	}
	if (Protocol::MakeJsonRpcRequestKey(RequestId).IsEmpty())
	{
		return Protocol::MakeJsonRpcErrorResponse(MakeShared<FJsonValueNull>(), -32600, TEXT("JSON-RPC id must be a string, number, or null."));
	}

	FString JsonRpcVersion;
	if (!Request->TryGetStringField(TEXT("jsonrpc"), JsonRpcVersion) || JsonRpcVersion != Protocol::GetJsonRpcVersion())
	{
		return Protocol::MakeJsonRpcErrorResponse(RequestId, -32600, TEXT("JSON-RPC version must be '2.0'."));
	}

	FString Method;
	if (!Request->TryGetStringField(TEXT("method"), Method) || Method.IsEmpty())
	{
		return Protocol::MakeJsonRpcErrorResponse(RequestId, -32600, TEXT("Missing JSON-RPC method."));
	}

	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (Request->HasField(TEXT("params")) && (!Request->TryGetObjectField(TEXT("params"), Params) || Params == nullptr || !Params->IsValid()))
	{
		return Protocol::MakeJsonRpcErrorResponse(RequestId, -32602, TEXT("MCP params must be a JSON object."));
	}
	const TSharedPtr<FJsonObject> ParamsObject = Params != nullptr && Params->IsValid() ? *Params : MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> ResultObject;
	if (Method == TEXT("initialize"))
	{
		ResultObject = CreateInitializeResponse(ParamsObject);
	}
	else if (Method == TEXT("tools/list"))
	{
		FString Error;
		ResultObject = CreateToolsListResponse(ParamsObject, Error);
		if (!Error.IsEmpty())
		{
			return Protocol::MakeJsonRpcErrorResponse(RequestId, -32602, Error);
		}
	}
	else if (Method == TEXT("tools/call"))
	{
		Execution::FMcpToolExecutionOptions EffectiveOptions = Options;
		if (!EffectiveOptions.RequestId.IsValid())
		{
			EffectiveOptions.RequestId = FGuid::NewGuid();
		}
		if (!EffectiveOptions.TraceId.IsValid())
		{
			EffectiveOptions.TraceId = FGuid::NewGuid();
		}
		ResultObject = ExecuteToolCall(ParamsObject, EffectiveOptions);
		TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
		Metadata->SetStringField(TEXT("traceId"), EffectiveOptions.TraceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		Metadata->SetStringField(TEXT("requestId"), EffectiveOptions.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
		ResultObject->SetObjectField(TEXT("_meta"), Metadata);
	}
	else if (Method == TEXT("resources/list"))
	{
		FString Error;
		ResultObject = CreateResourcesListResponse(ParamsObject, Error);
		if (!Error.IsEmpty())
		{
			return Protocol::MakeJsonRpcErrorResponse(RequestId, -32602, Error);
		}
	}
	else if (Method == TEXT("resources/read"))
	{
		ResultObject = CreateResourceReadResponse(ParamsObject);
	}
	else if (Method == TEXT("ping"))
	{
		ResultObject = MakeShared<FJsonObject>();
	}
	else
	{
		return Protocol::MakeJsonRpcErrorResponse(RequestId, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
	}

	TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), Protocol::GetJsonRpcVersion());
	Response->SetField(TEXT("id"), RequestId);
	Response->SetObjectField(TEXT("result"), ResultObject);
	return Response;
}

FString FUnrealAgentMCPServer::GetServerInstructions()
{
	return FString::Printf(TEXT("This MCP server exposes a live Unreal Engine editor session for project '%s'. "
								"Start by reading the resource worlddata://context/bootstrap for a compact, read-only orientation "
								"(engine/level state and a recommended read order). "
								"Use search_tools for compact ranked discovery and request includeSchema only for likely matches; "
								"tools/list remains the complete standards-compatible catalog. "
								"Use read-only tools (get_current_project_info, list_level_actors, get_selected_actors, get_actor_details, "
								"find_assets, read_asset, get_content_summary, get_codex_policy_snapshot) to gather context first, then use "
								"mutating tools (spawn_actor, transform_actor, delete_actor, attach_actor, set_actor_property, "
								"save_current_level, create_asset, create_blueprint_asset, modify_material_instance, create_pcg_graph_from_recipe, select_actor) "
								"only after you understand the scene. "
								"Unreal Agent also includes standalone extracted tools such as list_resources, "
								"read_resource, read_log, execute_python, project file tools, PIE controls, and PCG recipe tools. "
								"On UE 5.8+, call get_toolset_status then use list_toolsets, describe_toolset, and call_tool "
								"to reach Unreal Agent's independent reflection registry without injecting hundreds of schemas up front. "
								"If call_tool returns pending=true, poll get_toolset_call_result with its opaque callId. "
								"All tools act on the user's currently open editor world; prefer precise filters and small maxResults to keep responses compact. "
								"Tool results over 512 KiB are omitted with response_budget_exceeded and must not cause an automatic replay of a mutation."),
		*GetProjectName());
}

TSharedPtr<FJsonObject> FUnrealAgentMCPServer::CreateInitializeResponse(const TSharedPtr<FJsonObject>& Params)
{
	FString RequestedVersion;
	Params->TryGetStringField(TEXT("protocolVersion"), RequestedVersion);
	const FString ProtocolVersion = Protocol::NegotiateProtocolVersion(RequestedVersion);
	{
		FScopeLock Lock(&GetStateMutex());
		NegotiatedProtocolVersion = ProtocolVersion;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), ProtocolVersion);
	Result->SetArrayField(TEXT("supportedProtocolVersions"), ServerEnvironment::MakeSupportedProtocolVersionsArray());
	Result->SetStringField(TEXT("accessTokenHeader"), GetAccessTokenHeaderName());
	Result->SetBoolField(TEXT("requiresAccessToken"), true);
	Result->SetBoolField(TEXT("requiresSessionHeader"), true);

	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> ToolsCapability = MakeShared<FJsonObject>();
	ToolsCapability->SetBoolField(TEXT("listChanged"), true);
	Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);
	TSharedRef<FJsonObject> ResourcesCapability = MakeShared<FJsonObject>();
	ResourcesCapability->SetBoolField(TEXT("listChanged"), false);
	ResourcesCapability->SetBoolField(TEXT("subscribe"), false);
	Capabilities->SetObjectField(TEXT("resources"), ResourcesCapability);
	Result->SetObjectField(TEXT("capabilities"), Capabilities);

	TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), GetServerName());
	ServerInfo->SetStringField(TEXT("title"), FString::Printf(TEXT("%s (%s)"), UnrealAgentMCP::Brand::ProductName, *GetProjectName()));
	ServerInfo->SetStringField(TEXT("version"), TEXT("0.3.0"));
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
	Result->SetStringField(TEXT("instructions"), GetServerInstructions());
	return Result;
}

TSharedPtr<FJsonObject> FUnrealAgentMCPServer::CreateToolsListResponse(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const FString DefinitionsJson = GetToolDefinitionsJson();
	TArray<TSharedPtr<FJsonValue>> Tools;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DefinitionsJson);
	FJsonSerializer::Deserialize(Reader, Tools);
	Protocol::FMcpPaginationResult Page;
	if (!Protocol::PaginateJsonValues(Params, Tools, TEXT("tools"), Protocol::CalculateStableRevision(DefinitionsJson), Page, OutError))
	{
		return MakeShared<FJsonObject>();
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), Page.Items);
	if (!Page.NextCursor.IsEmpty())
	{
		Result->SetStringField(TEXT("nextCursor"), Page.NextCursor);
	}
	return Result;
}

TSharedPtr<FJsonObject> FUnrealAgentMCPServer::ExecuteToolCall(const TSharedPtr<FJsonObject>& Params, const Execution::FMcpToolExecutionOptions& Options)
{
	FString ToolName;
	if (!Params->TryGetStringField(TEXT("name"), ToolName))
	{
		ToolName = TEXT("<missing>");
	}

	FString ArgumentsJson = TEXT("{}");
	const TSharedPtr<FJsonObject>* ArgumentsObject = nullptr;
	if (Params->TryGetObjectField(TEXT("arguments"), ArgumentsObject) && ArgumentsObject != nullptr && ArgumentsObject->IsValid())
	{
		ArgumentsJson = JsonObjectToString(ArgumentsObject->ToSharedRef());
	}

	const FString ToolResult = DispatchTool(ToolName, ArgumentsJson, Options);
	return Protocol::MakeCallToolResult(ToolResult);
}

TSharedPtr<FJsonObject> FUnrealAgentMCPServer::CreateResourcesListResponse(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const FString ResourceJson = GetResourceListJson();
	const TSharedPtr<FJsonObject> Parsed = ServerEnvironment::ParseJsonObject(ResourceJson);
	if (!Parsed.IsValid())
	{
		OutError = TEXT("Resource catalog is not valid JSON.");
		return MakeShared<FJsonObject>();
	}
	const TArray<TSharedPtr<FJsonValue>>* Resources = nullptr;
	if (!Parsed->TryGetArrayField(TEXT("resources"), Resources) || !Resources)
	{
		OutError = TEXT("Resource catalog does not contain a resources array.");
		return MakeShared<FJsonObject>();
	}
	Protocol::FMcpPaginationResult Page;
	if (!Protocol::PaginateJsonValues(Params, *Resources, TEXT("resources"), Protocol::CalculateStableRevision(ResourceJson), Page, OutError))
	{
		return MakeShared<FJsonObject>();
	}
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("resources"), Page.Items);
	if (!Page.NextCursor.IsEmpty())
	{
		Result->SetStringField(TEXT("nextCursor"), Page.NextCursor);
	}
	return Result;
}

TSharedPtr<FJsonObject> FUnrealAgentMCPServer::CreateResourceReadResponse(const TSharedPtr<FJsonObject>& Params)
{
	FString Uri;
	Params->TryGetStringField(TEXT("uri"), Uri);
	const FString Text = Uri.IsEmpty() ? ErrorJson(TEXT("Missing required resource uri.")) : ReadResource(Uri);

	TSharedRef<FJsonObject> ContentItem = MakeShared<FJsonObject>();
	ContentItem->SetStringField(TEXT("uri"), Uri);
	ContentItem->SetStringField(TEXT("mimeType"), TEXT("application/json"));
	ContentItem->SetStringField(TEXT("text"), Text);
	TArray<TSharedPtr<FJsonValue>> Contents;
	Contents.Add(MakeShared<FJsonValueObject>(ContentItem));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("contents"), Contents);
	return Result;
}
