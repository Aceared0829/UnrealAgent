// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPHttpSecurity.cpp
 * @brief MCP 本地 HTTP 安全规则的纯逻辑实现。
 */

#include "Infrastructure/Http/UnrealAgentMCPHttpSecurity.h"

namespace UnrealAgentMCP::HttpSecurity
{
	FString ExtractAuthorityHost(FString Authority)
	{
		Authority.TrimStartAndEndInline();
		if (Authority.IsEmpty())
		{
			return FString();
		}

		const int32 SchemeSeparatorIndex = Authority.Find(TEXT("://"));
		if (SchemeSeparatorIndex != INDEX_NONE)
		{
			Authority.RightChopInline(SchemeSeparatorIndex + 3);
		}

		int32 PathSeparatorIndex = INDEX_NONE;
		if (Authority.FindChar(TEXT('/'), PathSeparatorIndex))
		{
			Authority.LeftInline(PathSeparatorIndex);
		}

		if (Authority.StartsWith(TEXT("[")))
		{
			int32 ClosingBracketIndex = INDEX_NONE;
			if (!Authority.FindChar(TEXT(']'), ClosingBracketIndex))
			{
				return FString();
			}
			Authority = Authority.Mid(1, ClosingBracketIndex - 1);
		}
		else
		{
			int32 PortSeparatorIndex = INDEX_NONE;
			if (Authority.FindChar(TEXT(':'), PortSeparatorIndex))
			{
				Authority.LeftInline(PortSeparatorIndex);
			}
		}

		Authority.TrimStartAndEndInline();
		Authority.ToLowerInline();
		return Authority;
	}

	bool IsLoopbackAuthority(const FString& Authority)
	{
		const FString Host = ExtractAuthorityHost(Authority);
		return Host == TEXT("127.0.0.1") || Host == TEXT("localhost") || Host == TEXT("::1");
	}

	bool IsHostHeaderAllowed(const FString& HostHeaderValue)
	{
		return HostHeaderValue.IsEmpty() || IsLoopbackAuthority(HostHeaderValue);
	}

	bool IsOriginHeaderAllowed(const FString& OriginHeaderValue)
	{
		return OriginHeaderValue.IsEmpty() || OriginHeaderValue.Equals(TEXT("null"), ESearchCase::IgnoreCase) || IsLoopbackAuthority(OriginHeaderValue);
	}

	bool ConstantTimeEquals(const FString& Expected, const FString& Actual)
	{
		const FTCHARToUTF8 ExpectedUtf8(*Expected);
		const FTCHARToUTF8 ActualUtf8(*Actual);
		const int32 ExpectedLength = ExpectedUtf8.Length();
		const int32 ActualLength = ActualUtf8.Length();
		const int32 MaximumLength = FMath::Max(ExpectedLength, ActualLength);

		int32 Difference = ExpectedLength ^ ActualLength;
		for (int32 ByteIndex = 0; ByteIndex < MaximumLength; ++ByteIndex)
		{
			const uint8 ExpectedByte = ByteIndex < ExpectedLength ? static_cast<uint8>(ExpectedUtf8.Get()[ByteIndex]) : 0;
			const uint8 ActualByte = ByteIndex < ActualLength ? static_cast<uint8>(ActualUtf8.Get()[ByteIndex]) : 0;
			Difference |= ExpectedByte ^ ActualByte;
		}
		return Difference == 0;
	}
}
