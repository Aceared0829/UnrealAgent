// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentACPProviderModel.cpp
 * @brief ACP Provider 注册表与安全的账户显示名识别。
 */

#include "Application/ACP/UnrealAgentACPProviderModel.h"

#include "Application/CLI/WorldDataCliProcessRules.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString ReadStringCandidate(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return FString();
		}

		static const TCHAR* CandidateFields[] = { TEXT("name"), TEXT("display_name"), TEXT("displayName"), TEXT("email"), TEXT("preferred_username"), TEXT("login") };

		for (const TCHAR* Field : CandidateFields)
		{
			FString Value;
			if (Object->TryGetStringField(Field, Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& JsonText)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	FString DecodeJwtAccountLabel(const FString& Token)
	{
		TArray<FString> Parts;
		Token.ParseIntoArray(Parts, TEXT("."), false);
		if (Parts.Num() < 2)
		{
			return FString();
		}

		FString Payload = Parts[1].Replace(TEXT("-"), TEXT("+")).Replace(TEXT("_"), TEXT("/"));
		while ((Payload.Len() % 4) != 0)
		{
			Payload += TEXT("=");
		}

		TArray<uint8> Bytes;
		if (!FBase64::Decode(Payload, Bytes))
		{
			return FString();
		}
		Bytes.Add(0);

		const FString JsonText = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData())));
		return ReadStringCandidate(ParseObject(JsonText));
	}

	FString DetectCodexAccount()
	{
		bool bFoundCredentialFile = false;
		for (const FString& Path : UnrealAgentACPProviderModel::GetCodexAuthCandidatePaths())
		{
			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *Path))
			{
				continue;
			}
			const TSharedPtr<FJsonObject> Root = ParseObject(JsonText);
			if (!Root.IsValid())
			{
				continue;
			}
			bFoundCredentialFile = true;
			FString Account = ReadStringCandidate(Root);
			if (!Account.IsEmpty())
			{
				return Account;
			}

			const TSharedPtr<FJsonObject>* UserObject = nullptr;
			if (Root.IsValid() && Root->TryGetObjectField(TEXT("user"), UserObject) && UserObject && UserObject->IsValid())
			{
				Account = ReadStringCandidate(*UserObject);
				if (!Account.IsEmpty())
				{
					return Account;
				}
			}

			const TSharedPtr<FJsonObject>* TokensObject = nullptr;
			if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("tokens"), TokensObject) || !TokensObject || !TokensObject->IsValid())
			{
				continue;
			}

			FString IdToken;
			if ((*TokensObject)->TryGetStringField(TEXT("id_token"), IdToken))
			{
				Account = DecodeJwtAccountLabel(IdToken);
				if (!Account.IsEmpty())
				{
					return Account;
				}
			}
		}
		return bFoundCredentialFile ? FString(TEXT("Codex 已登录")) : FString();
	}

	FString GetCursorAccountCommandInterpreterPath()
	{
#if PLATFORM_WINDOWS
		FString CommandInterpreter = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
		if (FPaths::FileExists(CommandInterpreter))
		{
			return CommandInterpreter;
		}
		return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32"), TEXT("cmd.exe"));
#else
		return FString();
#endif
	}

	FString GetCursorAccountPowerShellPath()
	{
#if PLATFORM_WINDOWS
		return FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")), TEXT("System32"), TEXT("WindowsPowerShell"), TEXT("v1.0"), TEXT("powershell.exe"));
#else
		return FString();
#endif
	}

	FString ResolveCursorAgentPath(const FString& ConfiguredPath)
	{
		FString Resolved = ConfiguredPath;
		Resolved.TrimStartAndEndInline();
		Resolved.TrimQuotesInline();
		if (!Resolved.IsEmpty())
		{
			return Resolved;
		}

#if PLATFORM_WINDOWS
		int32 ReturnCode = 1;
		FString StandardOutput;
		FString StandardError;
		FPlatformProcess::ExecProcess(TEXT("where.exe"), TEXT("agent"), &ReturnCode, &StandardOutput, &StandardError);
		if (ReturnCode == 0)
		{
			TArray<FString> Lines;
			StandardOutput.ParseIntoArrayLines(Lines, true);
			for (FString Line : Lines)
			{
				Line.TrimStartAndEndInline();
				if (FPaths::FileExists(Line))
				{
					return Line;
				}
			}
		}
#endif
		return FString();
	}

	FUnrealAgentACPAccountState DetectCursorAccount(const FString& CursorCliPath)
	{
		const FString ResolvedPath = ResolveCursorAgentPath(CursorCliPath);
		if (ResolvedPath.IsEmpty())
		{
			return { TEXT("Cursor 未连接"), FString(), false };
		}

		FWorldDataCliProcessLaunchSpec LaunchSpec;
		if (!WorldDataCliProcessRules::BuildLaunchSpec(ResolvedPath, TEXT("status --format json"), GetCursorAccountCommandInterpreterPath(), GetCursorAccountPowerShellPath(),
				LaunchSpec))
		{
			return { TEXT("Cursor 账户"), FString(), false };
		}

		int32 ReturnCode = 1;
		FString StandardOutput;
		FString StandardError;
		FPlatformProcess::ExecProcess(*LaunchSpec.Executable, *LaunchSpec.Arguments, &ReturnCode, &StandardOutput, &StandardError);
		if (ReturnCode != 0)
		{
			return { TEXT("Cursor 未登录"), FString(), false };
		}

		return UnrealAgentACPProviderModel::ParseCursorAccountStateJson(StandardOutput);
	}
}

namespace UnrealAgentACPProviderModel
{
	FUnrealAgentACPAccountState ParseCursorAccountStateJson(const FString& JsonText)
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(JsonText);
		if (!Root.IsValid())
		{
			return { TEXT("Cursor 账户"), FString(), false };
		}

		bool bAuthenticated = false;
		Root->TryGetBoolField(TEXT("isAuthenticated"), bAuthenticated);
		if (!bAuthenticated)
		{
			FString Status;
			Root->TryGetStringField(TEXT("status"), Status);
			bAuthenticated = Status.Equals(TEXT("authenticated"), ESearchCase::IgnoreCase);
		}
		if (!bAuthenticated)
		{
			return { TEXT("Cursor 未登录"), FString(), false };
		}

		const TSharedPtr<FJsonObject>* UserInfo = nullptr;
		if (!Root->TryGetObjectField(TEXT("userInfo"), UserInfo) || UserInfo == nullptr || !UserInfo->IsValid())
		{
			return { TEXT("Cursor 已登录"), FString(), true };
		}

		FString FirstName;
		FString LastName;
		FString Email;
		(*UserInfo)->TryGetStringField(TEXT("firstName"), FirstName);
		(*UserInfo)->TryGetStringField(TEXT("lastName"), LastName);
		(*UserInfo)->TryGetStringField(TEXT("email"), Email);
		FString DisplayName = FirstName;
		if (!LastName.IsEmpty())
		{
			if (!DisplayName.IsEmpty())
			{
				DisplayName += TEXT(" ");
			}
			DisplayName += LastName;
		}
		if (DisplayName.IsEmpty())
		{
			DisplayName = Email.IsEmpty() ? FString(TEXT("Cursor 已登录")) : Email;
		}
		return { DisplayName, Email, true };
	}

	const TArray<FUnrealAgentACPProviderDescriptor>& GetProviders()
	{
		static const TArray<FUnrealAgentACPProviderDescriptor> Providers = {
			{ EUnrealAgentACPProvider::Codex, TEXT("codex"), NSLOCTEXT("UnrealAgentACPProvider", "CodexName", "Codex"),
				NSLOCTEXT("UnrealAgentACPProvider", "CodexDescription", "通过 ACP 连接 Codex，并自动读取会话模型与权限。"), TEXT("codex"), true },
			{ EUnrealAgentACPProvider::Cursor, TEXT("cursor"), NSLOCTEXT("UnrealAgentACPProvider", "CursorName", "Cursor"),
				NSLOCTEXT("UnrealAgentACPProvider", "CursorDescription", "通过官方 `agent acp` 连接 Cursor，并读取 Agent 实时模型与模式。"), TEXT("agent"), true }
		};
		return Providers;
	}

	const FUnrealAgentACPProviderDescriptor& GetProvider(EUnrealAgentACPProvider Provider)
	{
		for (const FUnrealAgentACPProviderDescriptor& Descriptor : GetProviders())
		{
			if (Descriptor.Provider == Provider)
			{
				return Descriptor;
			}
		}
		return GetProviders()[0];
	}

	TArray<FString> GetCodexAuthCandidatePaths()
	{
		TArray<FString> CandidatePaths;
		auto AddCandidate = [&CandidatePaths](const FString& Root, bool bRootIsCodexHome)
		{
			FString CleanRoot = Root;
			CleanRoot.TrimStartAndEndInline();
			CleanRoot.TrimQuotesInline();
			if (CleanRoot.IsEmpty())
			{
				return;
			}

			FString Path = bRootIsCodexHome ? FPaths::Combine(CleanRoot, TEXT("auth.json")) : FPaths::Combine(CleanRoot, TEXT(".codex"), TEXT("auth.json"));
			Path = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Path);
			CandidatePaths.AddUnique(Path);
		};

		AddCandidate(FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_HOME")), true);
		AddCandidate(FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE")), false);
		AddCandidate(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), false);

		const FString HomeDrive = FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEDRIVE"));
		const FString HomePath = FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEPATH"));
		if (!HomeDrive.IsEmpty() && !HomePath.IsEmpty())
		{
			AddCandidate(HomeDrive + HomePath, false);
		}

		AddCandidate(FPlatformProcess::UserDir(), false);
		return CandidatePaths;
	}

	FUnrealAgentACPAccountState DetectAccountState(EUnrealAgentACPProvider Provider, const FString& ProviderCliPath)
	{
		if (Provider == EUnrealAgentACPProvider::Codex)
		{
			const FString CodexAccount = DetectCodexAccount();
			if (!CodexAccount.IsEmpty())
			{
				return { CodexAccount, FString(), true };
			}
			return { TEXT("Codex 未登录"), FString(), false };
		}

		return DetectCursorAccount(ProviderCliPath);
	}

	FString DetectAccountLabel(EUnrealAgentACPProvider Provider)
	{
		return DetectAccountState(Provider).DisplayLabel;
	}

	FString MakeAccountInitials(const FString& AccountLabel)
	{
		FString Label = AccountLabel;
		Label.TrimStartAndEndInline();
		if (Label.IsEmpty())
		{
			return TEXT("U");
		}

		const int32 EmailSeparator = Label.Find(TEXT("@"));
		if (EmailSeparator > 0)
		{
			Label = Label.Left(EmailSeparator);
		}

		TArray<FString> Words;
		Label.ParseIntoArrayWS(Words);
		if (Words.Num() >= 2)
		{
			return Words[0].Left(1).ToUpper() + Words.Last().Left(1).ToUpper();
		}
		return Label.Left(1).ToUpper();
	}
}
