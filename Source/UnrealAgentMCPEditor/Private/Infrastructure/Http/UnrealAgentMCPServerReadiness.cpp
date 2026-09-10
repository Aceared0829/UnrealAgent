// Copyright ZhaoZining. All Rights Reserved.

#include "Infrastructure/Http/UnrealAgentMCPServer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Core/Protocol/UnrealAgentMCPProtocol.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

using namespace UnrealAgentMCP;

namespace
{
	FString SerializeObject(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Object, Writer);
		return Json;
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Verb, const FString& Url, const FString& TokenHeader, const FString& Token,
		const FString& ProtocolVersion, const FString& SessionId, const FString& Body)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(Verb);
		Request->SetURL(Url);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(TokenHeader, Token);
		if (!ProtocolVersion.IsEmpty())
		{
			Request->SetHeader(TEXT("MCP-Protocol-Version"), ProtocolVersion);
		}
		if (!SessionId.IsEmpty())
		{
			Request->SetHeader(TEXT("MCP-Session-Id"), SessionId);
		}
		if (!Body.IsEmpty())
		{
			Request->SetContentAsString(Body);
		}
		return Request;
	}

	FString MakeRpcBody(const int32 Id, const FString& Method, const TSharedRef<FJsonObject>& Params, const bool bIncludeId = true)
	{
		TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		if (bIncludeId)
		{
			Request->SetNumberField(TEXT("id"), Id);
		}
		Request->SetStringField(TEXT("method"), Method);
		Request->SetObjectField(TEXT("params"), Params);
		return SerializeObject(Request);
	}
}

FUnrealAgentMCPReadiness FUnrealAgentMCPServer::GetReadiness()
{
	FScopeLock Lock(&GetStateMutex());
	return LastReadiness;
}

void FUnrealAgentMCPServer::VerifyReadinessAsync(FUnrealAgentMCPReadinessCallback Completion)
{
	FUnrealAgentMCPReadiness Initial;
	Initial.bListening = IsRunning() && HasBoundListenerState();
	Initial.Port = BoundPort;
	Initial.ServerName = GetServerName();

	auto Finish = MakeShared<FUnrealAgentMCPReadinessCallback>();
	const TSharedRef<bool> bFinished = MakeShared<bool>(false);
	*Finish = [Completion = MoveTemp(Completion), bFinished](const FUnrealAgentMCPReadiness& Result) mutable
	{
		if (*bFinished)
		{
			return;
		}
		*bFinished = true;
		{
			FScopeLock Lock(&FUnrealAgentMCPServer::GetStateMutex());
			FUnrealAgentMCPServer::LastReadiness = Result;
		}
		if (Completion)
		{
			Completion(Result);
		}
	};

	if (!Initial.bListening || Initial.Port <= 0)
	{
		Initial.Error = LastError.IsEmpty() ? TEXT("UnrealAgent MCP HTTP listener is not listening on a valid port.") : LastError;
		(*Finish)(Initial);
		return;
	}

	const FString ManifestPath = ServerEnvironment::GetConnectionPath();
	if (!IFileManager::Get().FileExists(*ManifestPath))
	{
		Initial.Error = FString::Printf(TEXT("UnrealAgent MCP connection manifest is missing: %s"), *ManifestPath);
		(*Finish)(Initial);
		return;
	}

	const TSharedPtr<FJsonObject> Manifest = ServerEnvironment::LoadJsonObjectFile(ManifestPath);
	double ManifestPort = 0.0;
	FString ManifestState;
	FString ManifestServerName;
	FString ManifestUrl;
	const bool bManifestMatches = Manifest.IsValid() && Manifest->TryGetNumberField(TEXT("port"), ManifestPort) && static_cast<int32>(ManifestPort) == BoundPort &&
		Manifest->TryGetStringField(TEXT("state"), ManifestState) && ManifestState == TEXT("listening") && Manifest->TryGetStringField(TEXT("serverName"), ManifestServerName) &&
		ManifestServerName == Initial.ServerName && Manifest->TryGetStringField(TEXT("url"), ManifestUrl) && ManifestUrl == GetMcpUrl();
	if (!bManifestMatches)
	{
		Initial.Error = FString::Printf(TEXT("UnrealAgent MCP connection manifest does not match the active listener: %s"), *ManifestPath);
		(*Finish)(Initial);
		return;
	}
	Initial.bManifestReady = true;

	const FString Url = GetMcpUrl();
	const FString TokenHeader = GetAccessTokenHeaderName();
	const FString Token = GetAccessToken();
	const FString ProtocolVersion = Protocol::GetLatestProtocolVersion();
	TSharedRef<FJsonObject> InitializeParams = MakeShared<FJsonObject>();
	InitializeParams->SetStringField(TEXT("protocolVersion"), ProtocolVersion);
	InitializeParams->SetObjectField(TEXT("capabilities"), MakeShared<FJsonObject>());
	TSharedRef<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
	ClientInfo->SetStringField(TEXT("name"), TEXT("UnrealAgentACPReadiness"));
	ClientInfo->SetStringField(TEXT("version"), TEXT("0.3.0"));
	InitializeParams->SetObjectField(TEXT("clientInfo"), ClientInfo);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> InitializeRequest =
		MakeRequest(TEXT("POST"), Url, TokenHeader, Token, FString(), FString(), MakeRpcBody(1, TEXT("initialize"), InitializeParams));
	InitializeRequest->OnProcessRequestComplete().BindLambda(
		[Finish, Initial, Url, TokenHeader, Token, ProtocolVersion](FHttpRequestPtr, FHttpResponsePtr Response, const bool bConnected) mutable
		{
			const TSharedPtr<FJsonObject> InitializeBody = Response.IsValid() ? ParseObject(Response->GetContentAsString()) : nullptr;
			const FString SessionId = Response.IsValid() ? Response->GetHeader(TEXT("MCP-Session-Id")) : FString();
			if (!bConnected || !Response.IsValid() || Response->GetResponseCode() != 200 || !InitializeBody.IsValid() || !InitializeBody->HasField(TEXT("result")) ||
				SessionId.IsEmpty())
			{
				Initial.Error = TEXT("Local UnrealAgent MCP initialize request failed or returned no session id.");
				(*Finish)(Initial);
				return;
			}
			Initial.bInitializeReady = true;

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> InitializedRequest =
				MakeRequest(TEXT("POST"), Url, TokenHeader, Token, ProtocolVersion, SessionId, MakeRpcBody(0, TEXT("notifications/initialized"), MakeShared<FJsonObject>(), false));
			InitializedRequest->OnProcessRequestComplete().BindLambda(
				[Finish, Initial, Url, TokenHeader, Token, ProtocolVersion, SessionId](FHttpRequestPtr, FHttpResponsePtr InitializedResponse,
					const bool bInitializedConnected) mutable
				{
					if (!bInitializedConnected || !InitializedResponse.IsValid() || InitializedResponse->GetResponseCode() != 202)
					{
						Initial.bInitializeReady = false;
						Initial.Error = TEXT("Local UnrealAgent MCP initialized notification was rejected.");
						(*Finish)(Initial);
						return;
					}

					TSharedRef<FJsonObject> ListParams = MakeShared<FJsonObject>();
					ListParams->SetNumberField(TEXT("pageSize"), 100);
					TSharedRef<IHttpRequest, ESPMode::ThreadSafe> ListRequest =
						MakeRequest(TEXT("POST"), Url, TokenHeader, Token, ProtocolVersion, SessionId, MakeRpcBody(2, TEXT("tools/list"), ListParams));
					ListRequest->OnProcessRequestComplete().BindLambda(
						[Finish, Initial, Url, TokenHeader, Token, ProtocolVersion, SessionId](FHttpRequestPtr, FHttpResponsePtr ListResponse, const bool bListConnected) mutable
						{
							const TSharedPtr<FJsonObject> ListBody = ListResponse.IsValid() ? ParseObject(ListResponse->GetContentAsString()) : nullptr;
							const TSharedPtr<FJsonObject> Result =
								ListBody.IsValid() && ListBody->HasTypedField<EJson::Object>(TEXT("result")) ? ListBody->GetObjectField(TEXT("result")) : nullptr;
							const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
							const bool bHasTools = Result.IsValid() && Result->TryGetArrayField(TEXT("tools"), Tools) && Tools && !Tools->IsEmpty();
							bool bHasNativePing = false;
							if (bHasTools)
							{
								for (const TSharedPtr<FJsonValue>& ToolValue : *Tools)
								{
									const TSharedPtr<FJsonObject> Tool = ToolValue.IsValid() ? ToolValue->AsObject() : nullptr;
									FString Name;
									if (Tool.IsValid() && Tool->TryGetStringField(TEXT("name"), Name) && Name == TEXT("UnrealAgentMCP.System.ping"))
									{
										bHasNativePing = true;
										break;
									}
								}
							}
							Initial.ToolCount = bHasTools ? Tools->Num() : 0;
							Initial.bToolsListReady = bListConnected && ListResponse.IsValid() && ListResponse->GetResponseCode() == 200 && bHasNativePing;
							if (!Initial.bToolsListReady)
							{
								Initial.Error = TEXT("Local UnrealAgent MCP tools/list did not expose UnrealAgentMCP.System.ping.");
							}

							TSharedRef<IHttpRequest, ESPMode::ThreadSafe> DeleteRequest =
								MakeRequest(TEXT("DELETE"), Url, TokenHeader, Token, ProtocolVersion, SessionId, FString());
							DeleteRequest->ProcessRequest();
							(*Finish)(Initial);
						});
					if (!ListRequest->ProcessRequest())
					{
						Initial.Error = TEXT("Failed to submit local UnrealAgent MCP tools/list request.");
						(*Finish)(Initial);
					}
				});
			if (!InitializedRequest->ProcessRequest())
			{
				Initial.Error = TEXT("Failed to submit local UnrealAgent MCP initialized notification.");
				(*Finish)(Initial);
			}
		});
	if (!InitializeRequest->ProcessRequest())
	{
		Initial.Error = TEXT("Failed to submit local UnrealAgent MCP initialize request.");
		(*Finish)(Initial);
	}
}
