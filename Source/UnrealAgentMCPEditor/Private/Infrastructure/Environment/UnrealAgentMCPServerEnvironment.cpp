// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPServerEnvironment.cpp
 * @brief MCP 服务工程环境与安全文件读写实现。
 */

#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Aclapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_UNIX
#include <sys/stat.h>
#endif

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Protocol/UnrealAgentMCPProtocol.h"

namespace UnrealAgentMCP::ServerEnvironment
{
	namespace
	{
		bool TryApplyOwnerOnlyFileAccess(const FString& Path, FString& OutError)
		{
			OutError.Reset();
#if PLATFORM_WINDOWS
			HANDLE TokenHandle = nullptr;
			if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &TokenHandle))
			{
				OutError = FString::Printf(TEXT("OpenProcessToken failed with Win32 error %lu."), ::GetLastError());
				return false;
			}

			DWORD TokenInformationSize = 0;
			::GetTokenInformation(TokenHandle, TokenUser, nullptr, 0, &TokenInformationSize);
			TArray<uint8> TokenInformation;
			TokenInformation.SetNumUninitialized(TokenInformationSize);
			if (TokenInformationSize == 0 || !::GetTokenInformation(TokenHandle, TokenUser, TokenInformation.GetData(), TokenInformationSize, &TokenInformationSize))
			{
				const DWORD ErrorCode = ::GetLastError();
				::CloseHandle(TokenHandle);
				OutError = FString::Printf(TEXT("GetTokenInformation failed with Win32 error %lu."), ErrorCode);
				return false;
			}
			::CloseHandle(TokenHandle);

			TOKEN_USER* TokenUserInformation = reinterpret_cast<TOKEN_USER*>(TokenInformation.GetData());
			EXPLICIT_ACCESSW Access = {};
			Access.grfAccessPermissions = FILE_ALL_ACCESS;
			Access.grfAccessMode = SET_ACCESS;
			Access.grfInheritance = NO_INHERITANCE;
			Access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
			Access.Trustee.TrusteeType = TRUSTEE_IS_USER;
			Access.Trustee.ptstrName = static_cast<LPWSTR>(TokenUserInformation->User.Sid);
			PACL AccessControlList = nullptr;
			const DWORD AclResult = ::SetEntriesInAclW(1, &Access, nullptr, &AccessControlList);
			if (AclResult != ERROR_SUCCESS || !AccessControlList)
			{
				OutError = FString::Printf(TEXT("SetEntriesInAcl failed with Win32 error %lu."), AclResult);
				return false;
			}

			const FString FullPath = FPaths::ConvertRelativePathToFull(Path);
			const DWORD SecurityResult = ::SetNamedSecurityInfoW(const_cast<LPWSTR>(*FullPath), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
				nullptr, nullptr, AccessControlList, nullptr);
			::LocalFree(AccessControlList);
			if (SecurityResult != ERROR_SUCCESS)
			{
				OutError = FString::Printf(TEXT("SetNamedSecurityInfo failed with Win32 error %lu."), SecurityResult);
				return false;
			}
			return true;
#elif PLATFORM_UNIX
			const FTCHARToUTF8 Utf8Path(*Path);
			if (::chmod(Utf8Path.Get(), S_IRUSR | S_IWUSR) != 0)
			{
				OutError = TEXT("chmod failed while applying owner-only file access.");
				return false;
			}
			return true;
#else
			OutError = TEXT("Owner-only file access is unsupported on this platform.");
			return false;
#endif
		}

		uint32 GetProjectHash()
		{
			FString Identity = GetProjectFilePath();
			if (Identity.IsEmpty())
			{
				Identity = GetProjectDirectory();
			}
			Identity.ToLowerInline();
			return FCrc::StrCrc32(*Identity);
		}

		FString GetHomeDirectory()
		{
			FString HomeDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
			if (HomeDirectory.IsEmpty())
			{
				HomeDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
			}
			return HomeDirectory;
		}

		FString StripTomlComment(const FString& Line)
		{
			bool bInsideString = false;
			bool bEscaped = false;
			for (int32 CharacterIndex = 0; CharacterIndex < Line.Len(); ++CharacterIndex)
			{
				const TCHAR Character = Line[CharacterIndex];
				if (bEscaped)
				{
					bEscaped = false;
					continue;
				}
				if (bInsideString)
				{
					if (Character == TEXT('\\'))
					{
						bEscaped = true;
					}
					else if (Character == TEXT('"'))
					{
						bInsideString = false;
					}
					continue;
				}
				if (Character == TEXT('"'))
				{
					bInsideString = true;
					continue;
				}
				if (Character == TEXT('#'))
				{
					return Line.Left(CharacterIndex).TrimStartAndEnd();
				}
			}
			return Line.TrimStartAndEnd();
		}

		FString ParseTomlValue(FString Value)
		{
			Value.TrimStartAndEndInline();
			if (Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
			{
				Value = Value.Mid(1, Value.Len() - 2);
				Value.ReplaceInline(TEXT("\\\""), TEXT("\""));
				Value.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
			}
			return Value;
		}

		bool IsSafeCodexPolicyKey(const FString& Key)
		{
			static const TSet<FString> SafeKeys = { TEXT("approval_policy"), TEXT("sandbox_mode"), TEXT("model"), TEXT("model_reasoning_effort"), TEXT("profile"), TEXT("enabled"),
				TEXT("command"), TEXT("args"), TEXT("cwd"), TEXT("url"), TEXT("type") };
			return SafeKeys.Contains(Key);
		}

		TSharedRef<FJsonObject> MakeCodexPolicySnapshot()
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);

			const FString ConfigPath = GetCodexClientConfigPath();
			Result->SetStringField(TEXT("configPath"), ConfigPath);
			Result->SetBoolField(TEXT("exists"), !ConfigPath.IsEmpty() && FPaths::FileExists(ConfigPath));
			Result->SetStringField(TEXT("source"), TEXT("~/.codex/config.toml"));

			if (ConfigPath.IsEmpty() || !FPaths::FileExists(ConfigPath))
			{
				Result->SetStringField(TEXT("message"), TEXT("Codex config.toml was not found."));
				return Result;
			}

			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *ConfigPath))
			{
				Result->SetBoolField(TEXT("success"), false);
				Result->SetStringField(TEXT("error"), TEXT("Codex config.toml exists but could not be read."));
				return Result;
			}

			TSharedRef<FJsonObject> RootPolicy = MakeShared<FJsonObject>();
			TMap<FString, TSharedPtr<FJsonObject>> ProfilesByName;
			TMap<FString, TSharedPtr<FJsonObject>> McpServersByName;
			FString ActiveProfile;
			FString Section;

			TArray<FString> Lines;
			Content.ParseIntoArrayLines(Lines, false);
			for (FString Line : Lines)
			{
				Line = StripTomlComment(Line);
				if (Line.IsEmpty())
				{
					continue;
				}

				if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
				{
					Section = Line.Mid(1, Line.Len() - 2).TrimStartAndEnd();
					continue;
				}

				FString Key;
				FString RawValue;
				if (!Line.Split(TEXT("="), &Key, &RawValue))
				{
					continue;
				}
				Key.TrimStartAndEndInline();
				if (!IsSafeCodexPolicyKey(Key))
				{
					continue;
				}

				const FString Value = ParseTomlValue(RawValue);
				if (Section.IsEmpty())
				{
					RootPolicy->SetStringField(Key, Value);
					if (Key == TEXT("profile"))
					{
						ActiveProfile = Value;
					}
					continue;
				}

				if (Section.StartsWith(TEXT("profiles.")))
				{
					const FString ProfileName = Section.RightChop(9);
					TSharedPtr<FJsonObject>& Profile = ProfilesByName.FindOrAdd(ProfileName);
					if (!Profile.IsValid())
					{
						Profile = MakeShared<FJsonObject>();
						Profile->SetStringField(TEXT("name"), ProfileName);
					}
					Profile->SetStringField(Key, Value);
					continue;
				}

				if (Section.StartsWith(TEXT("mcp_servers.")) && !Section.Contains(TEXT(".env")))
				{
					const FString ServerName = Section.RightChop(12);
					TSharedPtr<FJsonObject>& Server = McpServersByName.FindOrAdd(ServerName);
					if (!Server.IsValid())
					{
						Server = MakeShared<FJsonObject>();
						Server->SetStringField(TEXT("name"), ServerName);
					}
					Server->SetStringField(Key, Value);
				}
			}

			Result->SetObjectField(TEXT("rootPolicy"), RootPolicy);
			Result->SetStringField(TEXT("activeProfile"), ActiveProfile);

			TArray<TSharedPtr<FJsonValue>> Profiles;
			for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : ProfilesByName)
			{
				Profiles.Add(MakeShared<FJsonValueObject>(Pair.Value.ToSharedRef()));
			}
			Result->SetArrayField(TEXT("profiles"), Profiles);

			TSharedRef<FJsonObject> EffectivePolicy = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RootPolicy->Values)
			{
				EffectivePolicy->SetField(Pair.Key, Pair.Value);
			}
			if (!ActiveProfile.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* ActiveProfileObject = ProfilesByName.Find(ActiveProfile);
				if (ActiveProfileObject != nullptr && ActiveProfileObject->IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ActiveProfileObject)->Values)
					{
						if (Pair.Key != TEXT("name"))
						{
							EffectivePolicy->SetField(Pair.Key, Pair.Value);
						}
					}
				}
			}
			Result->SetObjectField(TEXT("effectivePolicy"), EffectivePolicy);

			TArray<TSharedPtr<FJsonValue>> McpServers;
			for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : McpServersByName)
			{
				McpServers.Add(MakeShared<FJsonValueObject>(Pair.Value.ToSharedRef()));
			}
			Result->SetArrayField(TEXT("mcpServers"), McpServers);

			TArray<TSharedPtr<FJsonValue>> Notes;
			Notes.Add(MakeShared<FJsonValueString>(TEXT("Only safe, non-secret Codex config keys are returned; env sections and unknown keys are intentionally omitted.")));
			Notes.Add(MakeShared<FJsonValueString>(
				TEXT("This snapshot reads local Codex configuration. It does not expose hidden system/developer prompts or private runtime policy from Codex.")));
			Result->SetArrayField(TEXT("notes"), Notes);
			return Result;
		}
	}

	FString GenerateAccessToken()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}

	bool IsStrongAccessToken(const FString& Token)
	{
		return Token.Len() >= 32;
	}

	FString GetProjectFilePath()
	{
		FString ProjectFile = FPaths::GetProjectFilePath();
		if (!ProjectFile.IsEmpty())
		{
			ProjectFile = FPaths::ConvertRelativePathToFull(ProjectFile);
			FPaths::MakePlatformFilename(ProjectFile);
		}
		return ProjectFile;
	}

	FString GetProjectDirectory()
	{
		FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::MakePlatformFilename(ProjectDirectory);
		return ProjectDirectory;
	}

	FString SanitizeNamePart(const FString& Name)
	{
		FString SanitizedName;
		SanitizedName.Reserve(Name.Len());
		bool bLastCharacterWasUnderscore = false;
		for (const TCHAR Character : Name)
		{
			if (FChar::IsAlnum(Character))
			{
				SanitizedName.AppendChar(FChar::ToLower(Character));
				bLastCharacterWasUnderscore = false;
			}
			else if (!bLastCharacterWasUnderscore)
			{
				SanitizedName.AppendChar(TEXT('_'));
				bLastCharacterWasUnderscore = true;
			}
		}
		while (SanitizedName.StartsWith(TEXT("_")))
		{
			SanitizedName.RightChopInline(1);
		}
		while (SanitizedName.EndsWith(TEXT("_")))
		{
			SanitizedName.LeftChopInline(1);
		}
		return SanitizedName.IsEmpty() ? TEXT("project") : SanitizedName;
	}

	FString GetProjectHashString()
	{
		return FString::Printf(TEXT("%08x"), GetProjectHash());
	}

	int32 GetDefaultPort()
	{
		return 5753 + static_cast<int32>(GetProjectHash() % 20000);
	}

	FString GetSavedConfigPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("config.json"));
	}

	FString GetConnectionPath()
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"));
		const FString PrimaryPath = FPaths::Combine(Directory, TEXT("mcp.json"));
		const FString RecoveryPath = FPaths::Combine(Directory, TEXT("mcp.recovered.json"));
		if (FPaths::FileExists(RecoveryPath))
		{
			return RecoveryPath;
		}
		if (FPaths::FileExists(PrimaryPath))
		{
			FString Probe;
			if (!FFileHelper::LoadFileToString(Probe, *PrimaryPath))
			{
				return RecoveryPath;
			}
		}
		return PrimaryPath;
	}

	FString GetCursorClientConfigPath()
	{
		const FString HomeDirectory = GetHomeDirectory();
		if (HomeDirectory.IsEmpty())
		{
			return FString();
		}
		FString Path = FPaths::Combine(HomeDirectory, TEXT(".cursor"), TEXT("mcp.json"));
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::MakePlatformFilename(Path);
		return Path;
	}

	FString GetCodexClientConfigPath()
	{
		const FString HomeDirectory = GetHomeDirectory();
		return HomeDirectory.IsEmpty() ? FString() : FPaths::Combine(HomeDirectory, TEXT(".codex"), TEXT("config.toml"));
	}

	TSharedPtr<FJsonObject> LoadJsonObjectFile(const FString& Path)
	{
		FString ResolvedPath = Path;
		if (!FPaths::FileExists(ResolvedPath) && FPaths::IsSamePath(ResolvedPath, GetSavedConfigPath()))
		{
			const FString LegacyPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgentMCP"), TEXT("config.json"));
			if (FPaths::FileExists(LegacyPath))
			{
				ResolvedPath = LegacyPath;
			}
		}

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *ResolvedPath))
		{
			return MakeShared<FJsonObject>();
		}

		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			return MakeShared<FJsonObject>();
		}
		return JsonObject;
	}

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& JsonText)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			return nullptr;
		}
		return JsonObject;
	}

	bool IsFileAccessRestrictedToCurrentUser(const FString& Path, FString& OutError)
	{
		OutError.Reset();
#if PLATFORM_WINDOWS
		HANDLE TokenHandle = nullptr;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &TokenHandle))
		{
			OutError = TEXT("Unable to open the current process token.");
			return false;
		}
		DWORD TokenInformationSize = 0;
		::GetTokenInformation(TokenHandle, TokenUser, nullptr, 0, &TokenInformationSize);
		TArray<uint8> TokenInformation;
		TokenInformation.SetNumUninitialized(TokenInformationSize);
		if (TokenInformationSize == 0 || !::GetTokenInformation(TokenHandle, TokenUser, TokenInformation.GetData(), TokenInformationSize, &TokenInformationSize))
		{
			::CloseHandle(TokenHandle);
			OutError = TEXT("Unable to read the current process user SID.");
			return false;
		}
		::CloseHandle(TokenHandle);
		TOKEN_USER* TokenUserInformation = reinterpret_cast<TOKEN_USER*>(TokenInformation.GetData());

		PACL AccessControlList = nullptr;
		PSECURITY_DESCRIPTOR SecurityDescriptor = nullptr;
		const FString FullPath = FPaths::ConvertRelativePathToFull(Path);
		const DWORD SecurityResult =
			::GetNamedSecurityInfoW(const_cast<LPWSTR>(*FullPath), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &AccessControlList, nullptr, &SecurityDescriptor);
		if (SecurityResult != ERROR_SUCCESS || !AccessControlList || !SecurityDescriptor)
		{
			if (SecurityDescriptor)
			{
				::LocalFree(SecurityDescriptor);
			}
			OutError = TEXT("Unable to read the file DACL.");
			return false;
		}

		SECURITY_DESCRIPTOR_CONTROL Control = 0;
		DWORD Revision = 0;
		const bool bIsProtected = ::GetSecurityDescriptorControl(SecurityDescriptor, &Control, &Revision) && (Control & SE_DACL_PROTECTED) != 0;
		ACL_SIZE_INFORMATION AclInformation = {};
		const bool bHasSingleEntry = ::GetAclInformation(AccessControlList, &AclInformation, sizeof(AclInformation), AclSizeInformation) && AclInformation.AceCount == 1;
		void* Entry = nullptr;
		const bool bHasEntry = bHasSingleEntry && ::GetAce(AccessControlList, 0, &Entry);
		ACCESS_ALLOWED_ACE* AllowedEntry = bHasEntry ? static_cast<ACCESS_ALLOWED_ACE*>(Entry) : nullptr;
		const PSID AllowedSid = AllowedEntry ? static_cast<PSID>(&AllowedEntry->SidStart) : nullptr;
		const bool bMatchesCurrentUser = AllowedEntry && AllowedEntry->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && ::EqualSid(AllowedSid, TokenUserInformation->User.Sid);
		::LocalFree(SecurityDescriptor);
		if (!bIsProtected || !bHasSingleEntry || !bMatchesCurrentUser)
		{
			OutError = TEXT("File DACL is not protected and limited to the current user.");
			return false;
		}
		return true;
#elif PLATFORM_UNIX
		struct stat FileStatus = {};
		const FTCHARToUTF8 Utf8Path(*Path);
		if (::stat(Utf8Path.Get(), &FileStatus) != 0)
		{
			OutError = TEXT("Unable to read file mode.");
			return false;
		}
		return (FileStatus.st_mode & (S_IRWXG | S_IRWXO)) == 0;
#else
		OutError = TEXT("Owner-only file access is unsupported on this platform.");
		return false;
#endif
	}

	bool RestrictFileAccessToCurrentUser(const FString& Path, FString& OutError)
	{
		return TryApplyOwnerOnlyFileAccess(Path, OutError) && IsFileAccessRestrictedToCurrentUser(Path, OutError);
	}

	bool WriteStringAtomically(const FString& Content, const FString& TargetPath, const bool bRestrictToCurrentUser)
	{
		FString IgnoredError;
		return WriteStringAtomically(Content, TargetPath, bRestrictToCurrentUser, IgnoredError);
	}

	bool WriteStringAtomically(const FString& Content, const FString& TargetPath, const bool bRestrictToCurrentUser, FString& OutError)
	{
		OutError.Reset();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(TargetPath), true);
		const FString TemporaryPath = FString::Printf(TEXT("%s.%s.tmp"), *TargetPath, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!FFileHelper::SaveStringToFile(Content, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Unable to write temporary file '%s'."), *TemporaryPath);
			return false;
		}
		if (bRestrictToCurrentUser)
		{
			FString AccessError;
			if (!TryApplyOwnerOnlyFileAccess(TemporaryPath, AccessError))
			{
				OutError = MoveTemp(AccessError);
				IFileManager::Get().Delete(*TemporaryPath);
				return false;
			}
		}

		if (bRestrictToCurrentUser && FPaths::FileExists(TargetPath))
		{
			FString ExistingAccessError;
			if (!TryApplyOwnerOnlyFileAccess(TargetPath, ExistingAccessError))
			{
				OutError = FString::Printf(TEXT("Unable to repair target ACL before replacement: %s"), *ExistingAccessError);
				IFileManager::Get().Delete(*TemporaryPath);
				return false;
			}
		}

		bool bMoved = false;
#if PLATFORM_WINDOWS
		bMoved = ::MoveFileExW(*FPaths::ConvertRelativePathToFull(TemporaryPath), *FPaths::ConvertRelativePathToFull(TargetPath),
					 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
		if (!bMoved)
		{
			OutError = FString::Printf(TEXT("MoveFileEx failed with Win32 error %lu."), ::GetLastError());
		}
#else
		bMoved = IFileManager::Get().Move(*TargetPath, *TemporaryPath, true);
		if (!bMoved)
		{
			OutError = TEXT("Unable to atomically replace the target file.");
		}
#endif
		if (!bMoved)
		{
			IFileManager::Get().Delete(*TemporaryPath);
			return false;
		}
		if (bRestrictToCurrentUser)
		{
			FString AccessError;
			if (!TryApplyOwnerOnlyFileAccess(TargetPath, AccessError) || !IsFileAccessRestrictedToCurrentUser(TargetPath, AccessError))
			{
				OutError = MoveTemp(AccessError);
				IFileManager::Get().Delete(*TargetPath);
				return false;
			}
		}
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> MakeSupportedProtocolVersionsArray()
	{
		TArray<TSharedPtr<FJsonValue>> Versions;
		for (const FString& Version : Protocol::GetSupportedProtocolVersions())
		{
			Versions.Add(MakeShared<FJsonValueString>(Version));
		}
		return Versions;
	}

	TSharedRef<FJsonObject> MakeAuthHeadersObject(const FString& AccessTokenHeaderName, const FString& AccessToken)
	{
		TSharedRef<FJsonObject> Headers = MakeShared<FJsonObject>();
		Headers->SetStringField(AccessTokenHeaderName, AccessToken);
		return Headers;
	}

	FString GetCodexPolicySnapshotJson()
	{
		return JsonObjectToString(MakeCodexPolicySnapshot());
	}
}
