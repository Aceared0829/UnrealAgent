// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentPromptMediaLoader.cpp
 * @brief 图片附件内存编码与视频附件关键帧提取实现。
 *
 * 文件读取和 FFmpeg 必须能在工作线程运行，避免卡住 Editor 主线程。
 * FFmpeg 用可中断等待，而不是同步 ExecProcess。
 */

#include "Application/Media/UnrealAgentPromptMedia.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString GetLowercaseExtension(const FString& Path)
	{
		return FPaths::GetExtension(Path, false).ToLower();
	}

	FString ResolveImageMimeType(const FString& Path)
	{
		const FString Extension = GetLowercaseExtension(Path);
		if (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
		{
			return TEXT("image/jpeg");
		}
		if (Extension == TEXT("webp"))
		{
			return TEXT("image/webp");
		}
		return TEXT("image/png");
	}

	bool ShouldAbortLoad(const FUnrealAgentPromptMediaLoadContext& Context, FString& OutError)
	{
		if (Context.Cancellation.IsValid() && Context.Cancellation->IsRequested())
		{
			OutError = TEXT("媒体准备已取消。");
			return true;
		}
		if (Context.TimeoutSeconds >= 0.0f && Context.StartSeconds > 0.0 && (FPlatformTime::Seconds() - Context.StartSeconds) >= static_cast<double>(Context.TimeoutSeconds))
		{
			OutError = TEXT("媒体准备超时。");
			return true;
		}
		return false;
	}

	float GetRemainingTimeoutSeconds(const FUnrealAgentPromptMediaLoadContext& Context)
	{
		if (Context.TimeoutSeconds < 0.0f || Context.StartSeconds <= 0.0)
		{
			return -1.0f;
		}
		return Context.TimeoutSeconds - static_cast<float>(FPlatformTime::Seconds() - Context.StartSeconds);
	}

	FString ResolveFFmpegExecutable()
	{
		TArray<FString> Candidates;
		Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/ThirdParty/FFmpeg/Win64/ffmpeg.exe")));
		Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("ThirdParty/FFmpeg/Win64/ffmpeg.exe")));

		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
		{
			Candidates.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("ThirdParty/FFmpeg/Win64/ffmpeg.exe")));
		}

		for (const FString& Candidate : Candidates)
		{
			if (FPaths::FileExists(Candidate))
			{
				return FPaths::ConvertRelativePathToFull(Candidate);
			}
		}
		return TEXT("ffmpeg.exe");
	}

	bool RunFFmpegProcess(const FString& FFmpegExecutable, const FString& Arguments, const FUnrealAgentPromptMediaLoadContext& Context, int32& OutReturnCode,
		FString& OutCombinedOutput, FString& OutError)
	{
		OutReturnCode = -1;
		OutCombinedOutput.Reset();
		if (ShouldAbortLoad(Context, OutError))
		{
			return false;
		}

		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
		{
			OutError = TEXT("无法创建 FFmpeg 输出管道。");
			return false;
		}

		uint32 ProcessId = 0;
		FProcHandle Process = FPlatformProcess::CreateProc(*FFmpegExecutable, *Arguments, false, true, true, &ProcessId, 0, nullptr, WritePipe, nullptr, WritePipe);
		if (!Process.IsValid())
		{
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
			OutError = TEXT("无法启动 FFmpeg；请安装 FFmpeg 或启用随项目提供的版本。");
			return false;
		}

		bool bAborted = false;
		while (FPlatformProcess::IsProcRunning(Process))
		{
			OutCombinedOutput += FPlatformProcess::ReadPipe(ReadPipe);
			if (ShouldAbortLoad(Context, OutError))
			{
				FPlatformProcess::TerminateProc(Process, true);
				bAborted = true;
				break;
			}
			const float RemainingSeconds = GetRemainingTimeoutSeconds(Context);
			if (RemainingSeconds >= 0.0f && RemainingSeconds <= 0.0f)
			{
				FPlatformProcess::TerminateProc(Process, true);
				OutError = TEXT("媒体准备超时。");
				bAborted = true;
				break;
			}
			FPlatformProcess::Sleep(0.05f);
		}

		OutCombinedOutput += FPlatformProcess::ReadPipe(ReadPipe);
		if (!bAborted)
		{
			FPlatformProcess::GetProcReturnCode(Process, &OutReturnCode);
		}
		FPlatformProcess::CloseProc(Process);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return !bAborted;
	}

	bool LoadImageFile(const FString& Path, const FString& SourceLabel, const int32 CurrentImageCount, int64& InOutRawBytes, int32& InOutBase64Chars,
		FUnrealAgentPreparedPromptImage& OutImage, FString& OutError)
	{
		const int64 FileSize = IFileManager::Get().FileSize(*Path);
		if (FileSize <= 0 || FileSize > FUnrealAgentPromptMediaLoader::MaximumImageBytes)
		{
			OutError = FString::Printf(TEXT("图片附件大小无效或超过 20 MiB：%s"), *FPaths::GetCleanFilename(Path));
			return false;
		}
		if (!FUnrealAgentPromptMediaLoader::CanAcceptAdditionalImage(CurrentImageCount, InOutRawBytes, InOutBase64Chars, FileSize, 0, OutError))
		{
			return false;
		}

		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			OutError = FString::Printf(TEXT("无法读取图片附件：%s"), *FPaths::GetCleanFilename(Path));
			return false;
		}

		OutImage.SourceLabel = SourceLabel;
		OutImage.MimeType = ResolveImageMimeType(Path);
		OutImage.Base64Data = FBase64::Encode(Bytes);
		if (!FUnrealAgentPromptMediaLoader::CanAcceptAdditionalImage(CurrentImageCount, InOutRawBytes, InOutBase64Chars, FileSize, OutImage.Base64Data.Len(), OutError))
		{
			OutImage = FUnrealAgentPreparedPromptImage();
			return false;
		}

		InOutRawBytes += FileSize;
		InOutBase64Chars += OutImage.Base64Data.Len();
		return true;
	}

	double ParseDurationSeconds(const FString& FFmpegOutput)
	{
		const FString Marker = TEXT("Duration: ");
		const int32 MarkerIndex = FFmpegOutput.Find(Marker);
		if (MarkerIndex == INDEX_NONE)
		{
			return 0.0;
		}

		const FString Duration = FFmpegOutput.Mid(MarkerIndex + Marker.Len(), 11);
		TArray<FString> Parts;
		Duration.ParseIntoArray(Parts, TEXT(":"), true);
		if (Parts.Num() != 3)
		{
			return 0.0;
		}
		return FCString::Atod(*Parts[0]) * 3600.0 + FCString::Atod(*Parts[1]) * 60.0 + FCString::Atod(*Parts[2]);
	}

	bool ExtractVideoFrames(const FString& VideoPath, const int32 MaximumFrames, const FUnrealAgentPromptMediaLoadContext& Context, TArray<FString>& OutFramePaths,
		FString& OutError)
	{
		if (VideoPath.Contains(TEXT("\"")))
		{
			OutError = TEXT("视频路径包含不受支持的引号字符。");
			return false;
		}

		const FString FFmpegExecutable = !Context.FFmpegExecutable.IsEmpty() ? Context.FFmpegExecutable : ResolveFFmpegExecutable();
		int32 ProbeReturnCode = 0;
		FString ProbeOutput;
		const FString ProbeArguments = FString::Printf(TEXT("-hide_banner -i \"%s\""), *VideoPath);
		if (!RunFFmpegProcess(FFmpegExecutable, ProbeArguments, Context, ProbeReturnCode, ProbeOutput, OutError))
		{
			return false;
		}

		const double DurationSeconds = ParseDurationSeconds(ProbeOutput);
		const double FramesPerSecond = DurationSeconds > 0.0 ? FMath::Clamp(static_cast<double>(MaximumFrames) / DurationSeconds, 0.001, 30.0) : 0.2;

		const FString SessionDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent/PromptMedia"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!IFileManager::Get().MakeDirectory(*SessionDirectory, true))
		{
			OutError = TEXT("无法创建视频关键帧临时目录。");
			return false;
		}

		const FString OutputPattern = FPaths::Combine(SessionDirectory, TEXT("frame_%03d.jpg"));
		const FString ExtractArguments = FString::Printf(TEXT("-hide_banner -loglevel error -y -i \"%s\" "
															  "-vf \"fps=%.8f,scale=w=1280:h=-2:force_original_aspect_ratio=decrease\" "
															  "-frames:v %d -q:v 3 \"%s\""),
			*VideoPath, FramesPerSecond, MaximumFrames, *OutputPattern);
		int32 ExtractReturnCode = 0;
		FString ExtractOutput;
		const bool bWasStarted = RunFFmpegProcess(FFmpegExecutable, ExtractArguments, Context, ExtractReturnCode, ExtractOutput, OutError);

		IFileManager::Get().FindFiles(OutFramePaths, *FPaths::Combine(SessionDirectory, TEXT("*.jpg")), true, false);
		OutFramePaths.Sort();
		for (FString& FramePath : OutFramePaths)
		{
			FramePath = FPaths::Combine(SessionDirectory, FramePath);
		}

		if (!bWasStarted || ExtractReturnCode != 0 || OutFramePaths.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*SessionDirectory, false, true);
			OutFramePaths.Reset();
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("视频关键帧提取失败：%s"), *ExtractOutput.Left(512));
			}
			return false;
		}
		return true;
	}
}

bool FUnrealAgentPromptMediaLoader::IsSupportedImagePath(const FString& Path)
{
	const FString Extension = GetLowercaseExtension(Path);
	return Extension == TEXT("png") || Extension == TEXT("jpg") || Extension == TEXT("jpeg") || Extension == TEXT("webp");
}

bool FUnrealAgentPromptMediaLoader::IsSupportedVideoPath(const FString& Path)
{
	const FString Extension = GetLowercaseExtension(Path);
	return Extension == TEXT("mp4") || Extension == TEXT("mov") || Extension == TEXT("mkv") || Extension == TEXT("avi") || Extension == TEXT("webm");
}

bool FUnrealAgentPromptMediaLoader::IsSupportedMediaPath(const FString& Path)
{
	return IsSupportedImagePath(Path) || IsSupportedVideoPath(Path);
}

int32 FUnrealAgentPromptMediaLoader::CountSupportedMediaPaths(const TArray<FString>& Paths)
{
	int32 MediaCount = 0;
	for (const FString& Path : Paths)
	{
		if (IsSupportedMediaPath(Path))
		{
			++MediaCount;
		}
	}
	return MediaCount;
}

bool FUnrealAgentPromptMediaLoader::CanAcceptAdditionalImage(const int32 CurrentImageCount, const int64 CurrentRawBytes, const int32 CurrentBase64Chars,
	const int64 AdditionalRawBytes, const int32 AdditionalBase64Chars, FString& OutError)
{
	if (CurrentImageCount >= MaximumPromptImages)
	{
		OutError = FString::Printf(TEXT("图片附件数量不能超过 %d 张。"), MaximumPromptImages);
		return false;
	}
	if (CurrentRawBytes + AdditionalRawBytes > MaximumTotalRawImageBytes)
	{
		OutError = TEXT("图片附件原始总大小超过 32 MiB 上限。");
		return false;
	}
	if (AdditionalBase64Chars > 0 && CurrentBase64Chars + AdditionalBase64Chars > MaximumTotalBase64Chars)
	{
		OutError = TEXT("图片附件 Base64 总长度超过上限。");
		return false;
	}
	return true;
}

bool FUnrealAgentPromptMediaLoader::Load(const TArray<FString>& AttachmentPaths, FUnrealAgentPreparedPromptMedia& OutMedia, FString& OutError)
{
	FUnrealAgentPromptMediaLoadContext Context;
	Context.StartSeconds = FPlatformTime::Seconds();
	return Load(AttachmentPaths, OutMedia, OutError, Context);
}

bool FUnrealAgentPromptMediaLoader::Load(const TArray<FString>& AttachmentPaths, FUnrealAgentPreparedPromptMedia& OutMedia, FString& OutError,
	const FUnrealAgentPromptMediaLoadContext& Context)
{
	OutMedia = FUnrealAgentPreparedPromptMedia();
	OutError.Reset();
	int32 VideoCount = 0;
	int32 VideoFrameCount = 0;
	int64 TotalRawBytes = 0;
	int32 TotalBase64Chars = 0;
	FUnrealAgentPromptMediaLoadContext EffectiveContext = Context;
	if (EffectiveContext.StartSeconds <= 0.0)
	{
		EffectiveContext.StartSeconds = FPlatformTime::Seconds();
	}
	if (EffectiveContext.FFmpegExecutable.IsEmpty())
	{
		EffectiveContext.FFmpegExecutable = ResolveFFmpegExecutable();
	}

	for (const FString& AttachmentPath : AttachmentPaths)
	{
		if (ShouldAbortLoad(EffectiveContext, OutError))
		{
			return false;
		}
		if (IsSupportedMediaPath(AttachmentPath) && OutMedia.Images.Num() >= MaximumPromptImages)
		{
			OutError = FString::Printf(TEXT("图片与视频附件转换后超过 %d 张上限，已阻止发送。请减少附件后重试。"), MaximumPromptImages);
			return false;
		}
		if (IsSupportedImagePath(AttachmentPath))
		{
			FUnrealAgentPreparedPromptImage Image;
			if (!LoadImageFile(AttachmentPath, FPaths::GetCleanFilename(AttachmentPath), OutMedia.Images.Num(), TotalRawBytes, TotalBase64Chars, Image, OutError))
			{
				return false;
			}
			OutMedia.Images.Add(MoveTemp(Image));
			continue;
		}
		if (!IsSupportedVideoPath(AttachmentPath))
		{
			continue;
		}

		TArray<FString> FramePaths;
		const int32 RemainingImageCount = MaximumPromptImages - OutMedia.Images.Num();
		if (!ExtractVideoFrames(AttachmentPath, FMath::Min(MaximumFramesPerVideo, RemainingImageCount), EffectiveContext, FramePaths, OutError))
		{
			return false;
		}

		const FString SessionDirectory = FPaths::GetPath(FramePaths[0]);
		for (int32 FrameIndex = 0; FrameIndex < FramePaths.Num(); ++FrameIndex)
		{
			if (ShouldAbortLoad(EffectiveContext, OutError))
			{
				IFileManager::Get().DeleteDirectory(*SessionDirectory, false, true);
				return false;
			}
			FUnrealAgentPreparedPromptImage Image;
			if (!LoadImageFile(FramePaths[FrameIndex], FString::Printf(TEXT("%s 关键帧 %d/%d"), *FPaths::GetCleanFilename(AttachmentPath), FrameIndex + 1, FramePaths.Num()),
					OutMedia.Images.Num(), TotalRawBytes, TotalBase64Chars, Image, OutError))
			{
				IFileManager::Get().DeleteDirectory(*SessionDirectory, false, true);
				return false;
			}
			OutMedia.Images.Add(MoveTemp(Image));
			++VideoFrameCount;
		}
		IFileManager::Get().DeleteDirectory(*SessionDirectory, false, true);
		++VideoCount;
	}

	if (!OutMedia.Images.IsEmpty())
	{
		OutMedia.ContextText = FString::Printf(TEXT("媒体附件已作为 %d 张图片随本条消息发送；其中 %d 个视频已转换为 %d 张按时间分布的关键帧。请结合这些视觉内容理解用户请求。"),
			OutMedia.Images.Num(), VideoCount, VideoFrameCount);
	}
	return true;
}

TSharedPtr<FUnrealAgentPromptMediaLoadCancellation, ESPMode::ThreadSafe> FUnrealAgentPromptMediaLoader::LoadAsync(TArray<FString> AttachmentPaths,
	TFunction<void(bool bSucceeded, FUnrealAgentPreparedPromptMedia Media, FString Error)> Completion, const float TimeoutSeconds)
{
	TSharedPtr<FUnrealAgentPromptMediaLoadCancellation, ESPMode::ThreadSafe> Cancellation = MakeShared<FUnrealAgentPromptMediaLoadCancellation, ESPMode::ThreadSafe>();
	FUnrealAgentPromptMediaLoadContext Context;
	Context.Cancellation = Cancellation;
	Context.TimeoutSeconds = TimeoutSeconds;
	Context.StartSeconds = FPlatformTime::Seconds();
	Context.FFmpegExecutable = ResolveFFmpegExecutable();

	Async(EAsyncExecution::ThreadPool,
		[AttachmentPaths = MoveTemp(AttachmentPaths), Context, Completion = MoveTemp(Completion)]() mutable
		{
			FUnrealAgentPreparedPromptMedia Media;
			FString Error;
			const bool bSucceeded = Load(AttachmentPaths, Media, Error, Context);
			AsyncTask(ENamedThreads::GameThread,
				[bSucceeded, Media = MoveTemp(Media), Error = MoveTemp(Error), Completion = MoveTemp(Completion)]() mutable
				{
					if (Completion)
					{
						Completion(bSucceeded, MoveTemp(Media), MoveTemp(Error));
					}
				});
		});
	return Cancellation;
}
