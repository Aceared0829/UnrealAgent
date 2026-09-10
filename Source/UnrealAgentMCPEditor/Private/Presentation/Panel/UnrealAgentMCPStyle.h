// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPStyle.h
 * @brief WorldData MCP 面板配色、通知与 CLI 路径检测等 Slate 辅助工具。
 */

#include "CoreMinimal.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/SlateColor.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace UnrealAgentMCP
{
	/** 面板 Nomad Tab 注册名。 */
	static const FName PanelTabName(TEXT("UnrealAgentMCPPanel"));

	/** UE 编辑器原生深色控制台配色（背景/表面/主色/状态色）。 */
	namespace Palette
	{
		static FLinearColor Blend(const FLinearColor& Base, const FLinearColor& Overlay, float Amount)
		{
			const float T = FMath::Clamp(Amount, 0.0f, 1.0f);
			return FLinearColor(FMath::Lerp(Base.R, Overlay.R, T), FMath::Lerp(Base.G, Overlay.G, T), FMath::Lerp(Base.B, Overlay.B, T), 1.0f);
		}

		static FLinearColor Background()
		{
			return FLinearColor(0.018f, 0.020f, 0.024f, 1.0f);
		}
		static FLinearColor Surface()
		{
			return FLinearColor(0.026f, 0.029f, 0.035f, 1.0f);
		}
		static FLinearColor SurfaceRaised()
		{
			return FLinearColor(0.038f, 0.043f, 0.052f, 1.0f);
		}
		static FLinearColor Border()
		{
			return FLinearColor(0.090f, 0.100f, 0.120f, 1.0f);
		}
		static FLinearColor BorderStrong()
		{
			return FLinearColor(0.135f, 0.150f, 0.180f, 1.0f);
		}
		static FLinearColor Text()
		{
			return FLinearColor(0.78f, 0.80f, 0.84f, 1.0f);
		}
		static FLinearColor TextSoft()
		{
			return FLinearColor(0.58f, 0.62f, 0.69f, 1.0f);
		}
		static FLinearColor TextMuted()
		{
			return FLinearColor(0.38f, 0.42f, 0.49f, 1.0f);
		}
		static FLinearColor TextDisabled()
		{
			return FLinearColor(0.24f, 0.27f, 0.32f, 1.0f);
		}
		static FLinearColor Primary()
		{
			return FLinearColor(0.055f, 0.255f, 0.82f, 1.0f);
		}
		static FLinearColor PrimaryHover()
		{
			return FLinearColor(0.075f, 0.33f, 0.96f, 1.0f);
		}
		static FLinearColor PrimaryPressed()
		{
			return FLinearColor(0.035f, 0.18f, 0.62f, 1.0f);
		}
		static FLinearColor OnPrimary()
		{
			return FLinearColor::White;
		}
		static FLinearColor Success()
		{
			return FLinearColor(0.055f, 0.55f, 0.20f, 1.0f);
		}
		static FLinearColor Warning()
		{
			return FLinearColor(0.90f, 0.42f, 0.055f, 1.0f);
		}
		static FLinearColor Danger()
		{
			return FLinearColor(0.78f, 0.055f, 0.035f, 1.0f);
		}
		static FLinearColor Control()
		{
			return FLinearColor(0.050f, 0.055f, 0.065f, 1.0f);
		}
		static FLinearColor ControlHover()
		{
			return FLinearColor(0.075f, 0.083f, 0.098f, 1.0f);
		}
		static FLinearColor ControlPressed()
		{
			return FLinearColor(0.035f, 0.040f, 0.050f, 1.0f);
		}
		static FLinearColor Selection()
		{
			return FLinearColor(0.045f, 0.105f, 0.25f, 1.0f);
		}
	}

	static FSlateColor GetStatusColor(const bool bIsRunning)
	{
		return bIsRunning ? FSlateColor(Palette::Success()) : FSlateColor(Palette::Danger());
	}

	static FString GetSettingsFilePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("settings.json"));
	}

	static FString GetConversationHistoryFilePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("conversations.json"));
	}

	static void CopyToClipboard(const FString& Text)
	{
		FPlatformApplicationMisc::ClipboardCopy(*Text);
	}

	static void Notify(const FText& Message)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 2.5f;
		Info.bFireAndForget = true;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	static void ExploreFileParent(const FString& Path)
	{
		const FString Folder = FPaths::GetPath(Path);
		if (!Folder.IsEmpty())
		{
			FPlatformProcess::ExploreFolder(*Folder);
		}
	}

	static FString StripOuterQuotes(const FString& Text)
	{
		FString Trimmed = Text;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.Len() >= 2 && Trimmed[0] == TCHAR('"') && Trimmed[Trimmed.Len() - 1] == TCHAR('"'))
		{
			Trimmed = Trimmed.Mid(1, Trimmed.Len() - 2);
			Trimmed.TrimStartAndEndInline();
		}
		return Trimmed;
	}

	static bool PathExists(const FString& Path)
	{
		const FString Trimmed = StripOuterQuotes(Path);
		return !Trimmed.IsEmpty() && (FPaths::FileExists(Trimmed) || IFileManager::Get().FileSize(*Trimmed) >= 0);
	}

	static FString ResolveLaunchableCliPath(const FString& Path)
	{
		FString Resolved = StripOuterQuotes(Path);
#if PLATFORM_WINDOWS
		if (!Resolved.IsEmpty() && FPaths::GetExtension(Resolved).IsEmpty())
		{
			static const TCHAR* LaunchableExtensions[] = { TEXT(".exe"), TEXT(".com"), TEXT(".cmd"), TEXT(".bat"), TEXT(".ps1") };
			for (const TCHAR* Extension : LaunchableExtensions)
			{
				const FString Candidate = Resolved + Extension;
				if (PathExists(Candidate))
				{
					Resolved = Candidate;
					break;
				}
			}
		}
#endif
		FPaths::MakePlatformFilename(Resolved);
		return Resolved;
	}

	static FString ResolveCommandOnPath(const FString& Command)
	{
		const FString TrimmedCommand = StripOuterQuotes(Command);
		if (TrimmedCommand.IsEmpty())
		{
			return FString();
		}

		int32 ReturnCode = 1;
		FString StdOut;
		FString StdErr;
#if PLATFORM_WINDOWS
		FPlatformProcess::ExecProcess(TEXT("where.exe"), *TrimmedCommand, &ReturnCode, &StdOut, &StdErr);
#else
		FPlatformProcess::ExecProcess(TEXT("which"), *TrimmedCommand, &ReturnCode, &StdOut, &StdErr);
#endif
		if (ReturnCode != 0)
		{
			return FString();
		}

		TArray<FString> Lines;
		StdOut.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (PathExists(Line))
			{
				return ResolveLaunchableCliPath(Line);
			}
		}
		return FString();
	}

	static bool OpenExternalUrl(const FString& Url, FString& OutError)
	{
		OutError.Empty();
		FPlatformProcess::LaunchURL(*Url, nullptr, &OutError);
		return OutError.IsEmpty();
	}

	static FString GetCodexCliInstallUrl()
	{
		return TEXT("https://help.openai.com/en/articles/11096431");
	}

	static FString GetCursorCliInstallUrl()
	{
		return TEXT("https://docs.cursor.com/en/cli/installation");
	}

	static FString GetNodeJsInstallUrl()
	{
		return TEXT("https://nodejs.org/en/download");
	}

	static FString PrettyJson(const FString& JsonText)
	{
		FString Trimmed = JsonText;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.IsEmpty())
		{
			return JsonText;
		}

		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);

		if (Trimmed[0] == TCHAR('['))
		{
			TArray<TSharedPtr<FJsonValue>> Array;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
			if (FJsonSerializer::Deserialize(Reader, Array) && FJsonSerializer::Serialize(Array, Writer))
			{
				return Out;
			}
		}
		else if (Trimmed[0] == TCHAR('{'))
		{
			TSharedPtr<FJsonObject> Object;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
			if (FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid() && FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
			{
				return Out;
			}
		}

		return JsonText;
	}
}
