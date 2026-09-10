// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPClientConfiguration.cpp
 * @brief MCP 客户端配置纯合并实现。
 */

#include "Infrastructure/Configuration/UnrealAgentMCPClientConfiguration.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP::ClientConfiguration
{
	namespace
	{
		void RemoveManagedJsonServerEntries(const TSharedPtr<FJsonObject>& Servers)
		{
			if (!Servers.IsValid())
			{
				return;
			}

			TArray<FString> KeysToRemove;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Servers->Values)
			{
				const TSharedPtr<FJsonObject> Entry = Pair.Value.IsValid() && Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
				FString GeneratedBy;
				const bool bGeneratedByThisPlugin =
					Entry.IsValid() && Entry->TryGetStringField(TEXT("generatedBy"), GeneratedBy) && (GeneratedBy == TEXT("UnrealAgent") || GeneratedBy == TEXT("UnrealAgentMCP"));
				if (bGeneratedByThisPlugin || Pair.Key.StartsWith(TEXT("world_data_")))
				{
					KeysToRemove.Add(Pair.Key);
				}
			}

			for (const FString& Key : KeysToRemove)
			{
				Servers->RemoveField(Key);
			}
		}

		TSharedRef<FJsonObject> MakeAuthHeadersObject(const FMcpClientConnectionDescriptor& Connection)
		{
			TSharedRef<FJsonObject> Headers = MakeShared<FJsonObject>();
			Headers->SetStringField(Connection.AccessTokenHeaderName, Connection.AccessToken);
			return Headers;
		}

		FString EscapeTomlString(const FString& Value)
		{
			FString EscapedValue = Value;
			EscapedValue.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
			EscapedValue.ReplaceInline(TEXT("\""), TEXT("\\\""));
			return EscapedValue;
		}

		FString MakeTomlStringArray(const TArray<FString>& Values)
		{
			TArray<FString> Encoded;
			Encoded.Reserve(Values.Num());
			for (const FString& Value : Values)
			{
				Encoded.Add(FString::Printf(TEXT("\"%s\""), *EscapeTomlString(Value)));
			}
			return FString::Printf(TEXT("[%s]"), *FString::Join(Encoded, TEXT(", ")));
		}

		bool TryParseTomlSectionName(const FString& Line, FString& OutSectionName)
		{
			FString TrimmedLine = Line;
			TrimmedLine.TrimStartAndEndInline();
			if (!TrimmedLine.StartsWith(TEXT("[")) || !TrimmedLine.EndsWith(TEXT("]")))
			{
				return false;
			}

			OutSectionName = TrimmedLine.Mid(1, TrimmedLine.Len() - 2).TrimStartAndEnd();
			OutSectionName.ReplaceInline(TEXT("\""), TEXT(""));
			return true;
		}

		bool IsManagedCodexSection(const FString& SectionName, const FString& CurrentServerName)
		{
			if (!SectionName.StartsWith(TEXT("mcp_servers.")))
			{
				return false;
			}

			const FString ServerAndSubTable = SectionName.RightChop(12);
			FString ServerName = ServerAndSubTable;
			int32 SubTableSeparatorIndex = INDEX_NONE;
			if (ServerAndSubTable.FindChar(TEXT('.'), SubTableSeparatorIndex))
			{
				ServerName = ServerAndSubTable.Left(SubTableSeparatorIndex);
			}
			return ServerName == CurrentServerName || ServerName.StartsWith(TEXT("world_data_"));
		}
	}

	TSharedRef<FJsonObject> MergeJsonClientConfiguration(const TSharedPtr<FJsonObject>& ExistingRoot, const FMcpClientConnectionDescriptor& Connection)
	{
		const TSharedRef<FJsonObject> Root = ExistingRoot.IsValid() ? ExistingRoot.ToSharedRef() : MakeShared<FJsonObject>();

		const TSharedPtr<FJsonObject>* ExistingServers = nullptr;
		TSharedPtr<FJsonObject> Servers;
		if (Root->TryGetObjectField(TEXT("mcpServers"), ExistingServers) && ExistingServers != nullptr && ExistingServers->IsValid())
		{
			Servers = *ExistingServers;
		}
		else
		{
			Servers = MakeShared<FJsonObject>();
		}
		RemoveManagedJsonServerEntries(Servers);

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (Connection.UsesStdioHost())
		{
			Entry->SetStringField(TEXT("type"), TEXT("stdio"));
			Entry->SetStringField(TEXT("command"), Connection.Command);
			TArray<TSharedPtr<FJsonValue>> Arguments;
			Arguments.Reserve(Connection.Arguments.Num());
			for (const FString& Argument : Connection.Arguments)
			{
				Arguments.Add(MakeShared<FJsonValueString>(Argument));
			}
			Entry->SetArrayField(TEXT("args"), Arguments);
		}
		else
		{
			Entry->SetStringField(TEXT("type"), TEXT("http"));
			Entry->SetStringField(TEXT("url"), Connection.Url);
			Entry->SetObjectField(TEXT("headers"), MakeAuthHeadersObject(Connection));
			Entry->SetStringField(TEXT("protocolVersion"), Connection.ProtocolVersion);
			Entry->SetStringField(TEXT("accessTokenHeader"), Connection.AccessTokenHeaderName);
		}
		Entry->SetNumberField(TEXT("tool_timeout_sec"), 120);
		Entry->SetStringField(TEXT("generatedBy"), TEXT("UnrealAgent"));
		Entry->SetStringField(TEXT("projectId"), Connection.ProjectId);
		Entry->SetStringField(TEXT("projectName"), Connection.ProjectName);
		Entry->SetStringField(TEXT("serverName"), Connection.ServerName);

		Servers->SetObjectField(Connection.ServerName, Entry);
		Root->SetObjectField(TEXT("mcpServers"), Servers);
		return Root;
	}

	FString MergeCodexTomlConfiguration(const FString& ExistingConfiguration, const FMcpClientConnectionDescriptor& Connection)
	{
		TArray<FString> KeptLines;
		bool bSkippingManagedSection = false;
		TArray<FString> ExistingLines;
		ExistingConfiguration.ParseIntoArrayLines(ExistingLines, false);
		for (const FString& Line : ExistingLines)
		{
			FString TrimmedLine = Line;
			TrimmedLine.TrimStartAndEndInline();
			if (TrimmedLine.StartsWith(TEXT("# Managed by Unreal Agent")) || TrimmedLine.StartsWith(TEXT("# Managed by UnrealAgentMCP")))
			{
				continue;
			}
			FString SectionName;
			if (TryParseTomlSectionName(Line, SectionName))
			{
				bSkippingManagedSection = IsManagedCodexSection(SectionName, Connection.ServerName);
			}
			if (!bSkippingManagedSection)
			{
				KeptLines.Add(Line);
			}
		}

		while (!KeptLines.IsEmpty() && KeptLines.Last().TrimStartAndEnd().IsEmpty())
		{
			KeptLines.RemoveAt(KeptLines.Num() - 1);
		}

		FString Output = FString::Join(KeptLines, TEXT("\n"));
		if (!Output.IsEmpty())
		{
			Output += TEXT("\n\n");
		}

		Output += FString::Printf(TEXT("# Managed by Unreal Agent (%s). Use Configure External Clients to rewrite this section.\n") TEXT("[mcp_servers.%s]\n"),
			*Connection.ProjectName, *Connection.ServerName);
		if (Connection.UsesStdioHost())
		{
			Output += FString::Printf(TEXT("command = \"%s\"\n") TEXT("args = %s\n") TEXT("startup_timeout_sec = 30\n") TEXT("tool_timeout_sec = 120\n"),
				*EscapeTomlString(Connection.Command), *MakeTomlStringArray(Connection.Arguments));
		}
		else
		{
			Output += FString::Printf(TEXT("url = \"%s\"\n") TEXT("http_headers = { \"%s\" = \"%s\" }\n") TEXT("startup_timeout_sec = 30\n") TEXT("tool_timeout_sec = 120\n"),
				*EscapeTomlString(Connection.Url), *EscapeTomlString(Connection.AccessTokenHeaderName), *EscapeTomlString(Connection.AccessToken));
		}
		return Output;
	}

	TSharedRef<FJsonObject> MergeClaudeProjectSettings(const TSharedPtr<FJsonObject>& ExistingRoot)
	{
		const TSharedRef<FJsonObject> Root = ExistingRoot.IsValid() ? ExistingRoot.ToSharedRef() : MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("enableAllProjectMcpServers"), true);
		return Root;
	}
}
