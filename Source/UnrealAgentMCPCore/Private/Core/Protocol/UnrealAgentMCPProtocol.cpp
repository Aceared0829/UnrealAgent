// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPProtocol.cpp
 * @brief MCP/JSON-RPC 纯协议逻辑实现。
 */

#include "Core/Protocol/UnrealAgentMCPProtocol.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Hash/Blake3.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAgentMCP::Protocol
{
	namespace
	{
		constexpr int32 MaximumToolResultBytes = 512 * 1024;

		FString GetStableJsonRpcErrorCode(const int32 ErrorCode)
		{
			switch (ErrorCode)
			{
			case -32700:
				return TEXT("parse_error");
			case -32600:
				return TEXT("invalid_request");
			case -32601:
				return TEXT("method_not_found");
			case -32602:
				return TEXT("invalid_params");
			default:
				return TEXT("server_error");
			}
		}

		FString GetJsonRpcRecoveryHint(const int32 ErrorCode)
		{
			switch (ErrorCode)
			{
			case -32700:
				return TEXT("Send one valid UTF-8 JSON-RPC object.");
			case -32600:
				return TEXT("Use jsonrpc=2.0 with a valid string, number, or null id and a method string.");
			case -32601:
				return TEXT("Initialize the session, then discover supported methods and tools before retrying.");
			case -32602:
				return TEXT("Inspect the declared input schema or refresh the catalog cursor, then correct the request.");
			default:
				return TEXT("Inspect server state before deciding whether a new request is safe; do not automatically replay mutations.");
			}
		}

		bool IsValidRequestId(const TSharedPtr<FJsonValue>& RequestId)
		{
			if (!RequestId.IsValid() || RequestId->IsNull())
			{
				return true;
			}
			if (RequestId->Type == EJson::String)
			{
				return true;
			}
			if (RequestId->Type != EJson::Number)
			{
				return false;
			}
			double Number = 0.0;
			return RequestId->TryGetNumber(Number) && FMath::IsFinite(Number);
		}

		const TArray<FString>& GetProtocolVersionsStorage()
		{
			// 函数内静态对象避免跨编译单元初始化顺序影响服务器静态状态。
			static const TArray<FString> Versions = { TEXT("2025-06-18"), TEXT("2025-03-26"), TEXT("2024-11-05") };
			return Versions;
		}
	}

	const TCHAR* GetJsonRpcVersion()
	{
		return TEXT("2.0");
	}

	const TArray<FString>& GetSupportedProtocolVersions()
	{
		return GetProtocolVersionsStorage();
	}

	const FString& GetLatestProtocolVersion()
	{
		const TArray<FString>& SupportedVersions = GetProtocolVersionsStorage();
		check(!SupportedVersions.IsEmpty());
		return SupportedVersions[0];
	}

	FString NegotiateProtocolVersion(const FString& RequestedVersion)
	{
		if (!RequestedVersion.IsEmpty() && GetProtocolVersionsStorage().Contains(RequestedVersion))
		{
			return RequestedVersion;
		}

		return GetLatestProtocolVersion();
	}

	FString CalculateStableRevision(const FString& Content)
	{
		FTCHARToUTF8 Utf8(*Content);
		const FBlake3Hash Hash = FBlake3::HashBuffer(MakeMemoryView(Utf8.Get(), Utf8.Length()));
		return LexToString(Hash).ToLower();
	}

	FString MakeJsonRpcRequestKey(const TSharedPtr<FJsonValue>& RequestId)
	{
		if (!IsValidRequestId(RequestId))
		{
			return FString();
		}
		if (!RequestId.IsValid() || RequestId->IsNull())
		{
			return TEXT("null");
		}
		switch (RequestId->Type)
		{
		case EJson::String:
			return TEXT("s:") + RequestId->AsString();
		case EJson::Number:
		{
			FString NumberText;
			return RequestId->TryGetString(NumberText) ? TEXT("n:") + NumberText : FString();
		}
		default:
			return FString();
		}
	}

	bool PaginateJsonValues(const TSharedPtr<FJsonObject>& Params, const TArray<TSharedPtr<FJsonValue>>& Source, const FString& Scope, const FString& Revision,
		FMcpPaginationResult& OutPage, FString& OutError, const int32 DefaultPageSize, const int32 MaximumPageSize)
	{
		OutPage = FMcpPaginationResult();
		OutError.Empty();
		const int32 SafeMaximum = FMath::Max(1, MaximumPageSize);
		int32 PageSize = FMath::Clamp(DefaultPageSize, 1, SafeMaximum);
		int32 Offset = 0;

		if (Params.IsValid())
		{
			const TSharedPtr<FJsonValue> RequestedPageSize = Params->TryGetField(TEXT("pageSize"));
			if (RequestedPageSize.IsValid())
			{
				int32 IntegralSize = 0;
				if (RequestedPageSize->Type != EJson::Number || !RequestedPageSize->TryGetNumber(IntegralSize) || IntegralSize < 1 || IntegralSize > SafeMaximum)
				{
					OutError = FString::Printf(TEXT("pageSize must be an integer between 1 and %d."), SafeMaximum);
					return false;
				}
				PageSize = IntegralSize;
			}

			FString Cursor;
			if (Params->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.IsEmpty())
			{
				FString Decoded;
				TArray<FString> Parts;
				if (!FBase64::Decode(Cursor, Decoded, EBase64Mode::UrlSafe))
				{
					OutError = TEXT("cursor is not valid URL-safe Base64.");
					return false;
				}
				Decoded.ParseIntoArray(Parts, TEXT("\t"), false);
				if (Parts.Num() != 4 || Parts[0] != TEXT("v1") || Parts[1] != Scope || Parts[2] != Revision || !LexTryParseString(Offset, *Parts[3]) || Offset < 0 ||
					Offset > Source.Num())
				{
					OutError = TEXT("cursor is invalid, expired, or belongs to another catalog.");
					return false;
				}
			}
		}

		const int32 Count = FMath::Min(PageSize, Source.Num() - Offset);
		OutPage.Items.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			OutPage.Items.Add(Source[Offset + Index]);
		}
		const int32 NextOffset = Offset + Count;
		if (NextOffset < Source.Num())
		{
			const FString Payload = FString::Printf(TEXT("v1\t%s\t%s\t%d"), *Scope, *Revision, NextOffset);
			OutPage.NextCursor = FBase64::Encode(Payload, EBase64Mode::UrlSafe);
		}
		return true;
	}

	TSharedPtr<FJsonObject> MakeJsonRpcErrorResponse(const TSharedPtr<FJsonValue>& RequestId, const int32 ErrorCode, const FString& ErrorMessage)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), ErrorCode);
		Error->SetStringField(TEXT("message"), ErrorMessage);
		TSharedRef<FJsonObject> ErrorData = MakeShared<FJsonObject>();
		ErrorData->SetStringField(TEXT("errorCode"), GetStableJsonRpcErrorCode(ErrorCode));
		ErrorData->SetBoolField(TEXT("retryable"), false);
		ErrorData->SetStringField(TEXT("recovery"), GetJsonRpcRecoveryHint(ErrorCode));
		Error->SetObjectField(TEXT("data"), ErrorData);

		TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("jsonrpc"), GetJsonRpcVersion());
		Response->SetField(TEXT("id"), IsValidRequestId(RequestId) ? (RequestId.IsValid() ? RequestId : MakeShared<FJsonValueNull>()) : MakeShared<FJsonValueNull>());
		Response->SetObjectField(TEXT("error"), Error);
		return Response;
	}

	int32 GetMaximumToolResultBytes()
	{
		return MaximumToolResultBytes;
	}

	TSharedRef<FJsonObject> MakeJsonRpcNotification(const FString& Method, TSharedPtr<FJsonObject> Params)
	{
		TSharedRef<FJsonObject> Notification = MakeShared<FJsonObject>();
		Notification->SetStringField(TEXT("jsonrpc"), GetJsonRpcVersion());
		Notification->SetStringField(TEXT("method"), Method);
		Notification->SetObjectField(TEXT("params"), Params.IsValid() ? MoveTemp(Params) : MakeShared<FJsonObject>());
		return Notification;
	}

	TSharedPtr<FJsonObject> MakeCallToolResult(const FString& ToolResultText)
	{
		const FTCHARToUTF8 Utf8Result(*ToolResultText);
		if (Utf8Result.Length() > MaximumToolResultBytes)
		{
			TSharedRef<FJsonObject> BudgetError = MakeShared<FJsonObject>();
			BudgetError->SetBoolField(TEXT("success"), false);
			BudgetError->SetStringField(TEXT("code"), TEXT("response_budget_exceeded"));
			BudgetError->SetStringField(TEXT("message"), TEXT("The tool completed without returning its oversized payload."));
			BudgetError->SetStringField(TEXT("operationOutcome"), TEXT("completed_result_omitted"));
			BudgetError->SetBoolField(TEXT("replaySafe"), false);
			BudgetError->SetNumberField(TEXT("resultBytes"), Utf8Result.Length());
			BudgetError->SetNumberField(TEXT("maximumResultBytes"), MaximumToolResultBytes);
			BudgetError->SetStringField(TEXT("recovery"),
				TEXT("Do not replay a mutating call. Query task, artifact, or domain state and retry only with a narrower read request."));

			FString BudgetErrorText;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BudgetErrorText);
			FJsonSerializer::Serialize(BudgetError, Writer);
			TSharedRef<FJsonObject> TextContent = MakeShared<FJsonObject>();
			TextContent->SetStringField(TEXT("type"), TEXT("text"));
			TextContent->SetStringField(TEXT("text"), BudgetErrorText);
			TArray<TSharedPtr<FJsonValue>> Content;
			Content.Add(MakeShared<FJsonValueObject>(TextContent));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("content"), Content);
			Result->SetObjectField(TEXT("structuredContent"), BudgetError);
			Result->SetBoolField(TEXT("isError"), true);
			return Result;
		}

		TSharedPtr<FJsonObject> ParsedObject;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolResultText);
			FJsonSerializer::Deserialize(Reader, ParsedObject);
		}

		bool bIsError = false;
		TSharedRef<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		if (ParsedObject.IsValid())
		{
			StructuredContent = ParsedObject.ToSharedRef();
			bool bSuccess = true;
			if (ParsedObject->TryGetBoolField(TEXT("success"), bSuccess))
			{
				bIsError = !bSuccess;
			}
		}
		else
		{
			StructuredContent->SetStringField(TEXT("text"), ToolResultText);
			bIsError = true;
		}

		TSharedRef<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ToolResultText);
		TArray<TSharedPtr<FJsonValue>> Content;
		Content.Add(MakeShared<FJsonValueObject>(TextContent));

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("content"), Content);
		Result->SetObjectField(TEXT("structuredContent"), StructuredContent);
		Result->SetBoolField(TEXT("isError"), bIsError);
		return Result;
	}
}
