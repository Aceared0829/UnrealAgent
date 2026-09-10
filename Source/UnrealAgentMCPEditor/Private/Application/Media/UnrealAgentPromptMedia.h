// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentPromptMedia.h
 * @brief 用户媒体附件准备的应用层数据契约。
 */

#include "CoreMinimal.h"
#include "Templates/Function.h"

#include <atomic>

/** 单张已准备的模型输入图片；Base64 正文不得写入日志或持久化文件。 */
struct FUnrealAgentPreparedPromptImage
{
	FString SourceLabel;
	FString MimeType;
	FString Base64Data;
};

/** 一次附件准备结果。 */
struct FUnrealAgentPreparedPromptMedia
{
	TArray<FUnrealAgentPreparedPromptImage> Images;
	FString ContextText;
};

/**
 * 跨线程取消令牌。
 * Request 可在任意线程调用；Load 在工作线程轮询。
 */
struct FUnrealAgentPromptMediaLoadCancellation
{
	void Request()
	{
		bRequested.store(true, std::memory_order_relaxed);
	}

	bool IsRequested() const
	{
		return bRequested.load(std::memory_order_relaxed);
	}

private:
	std::atomic<bool> bRequested{ false };
};

/**
 * 同步 Load 的可选约束。
 * FFmpeg 可执行文件必须在调用线程（通常是 GameThread）解析后再传入，
 * 工作线程不得查询 IPluginManager。
 */
struct FUnrealAgentPromptMediaLoadContext
{
	TSharedPtr<FUnrealAgentPromptMediaLoadCancellation, ESPMode::ThreadSafe> Cancellation;
	/** 小于 0 表示不启用超时。 */
	float TimeoutSeconds = 45.0f;
	double StartSeconds = 0.0;
	FString FFmpegExecutable;
};

/** 图片直读与视频关键帧提取的应用入口。 */
class FUnrealAgentPromptMediaLoader
{
public:
	static constexpr int32 MaximumPromptImages = 12;
	static constexpr int32 MaximumFramesPerVideo = 8;
	static constexpr int64 MaximumImageBytes = 20LL * 1024LL * 1024LL;
	static constexpr int64 MaximumTotalRawImageBytes = 32LL * 1024LL * 1024LL;
	static constexpr int32 MaximumTotalBase64Chars = 44 * 1024 * 1024;

	static bool IsSupportedImagePath(const FString& Path);
	static bool IsSupportedVideoPath(const FString& Path);
	static bool IsSupportedMediaPath(const FString& Path);
	/** 统计图片与视频路径数量，文件夹不计入。 */
	static int32 CountSupportedMediaPaths(const TArray<FString>& Paths);

	/**
	 * 判断再加入一张图片是否会超过张数、原始字节或 Base64 上限。
	 *
	 * @param CurrentImageCount 已经接受的图片数。
	 * @param CurrentRawBytes 已经接受的原始字节合计。
	 * @param CurrentBase64Chars 已经接受的 Base64 字符合计。
	 * @param AdditionalRawBytes 候选图片的原始字节。
	 * @param AdditionalBase64Chars 候选图片编码后的字符数；尚未编码时传 0 只检查原始上限。
	 * @param OutError 拒绝原因。
	 */
	static bool CanAcceptAdditionalImage(int32 CurrentImageCount, int64 CurrentRawBytes, int32 CurrentBase64Chars, int64 AdditionalRawBytes, int32 AdditionalBase64Chars,
		FString& OutError);

	/**
	 * 准备模型可见的媒体内容。
	 *
	 * 不访问 UObject。视频通过基础设施实现均匀抽取有限数量的 JPEG 关键帧。
	 * 可在工作线程调用；调用方必须先在 GameThread 填好 FFmpeg 路径。
	 */
	static bool Load(const TArray<FString>& AttachmentPaths, FUnrealAgentPreparedPromptMedia& OutMedia, FString& OutError);
	static bool Load(const TArray<FString>& AttachmentPaths, FUnrealAgentPreparedPromptMedia& OutMedia, FString& OutError, const FUnrealAgentPromptMediaLoadContext& Context);

	/**
	 * 在线程池准备附件，并在 GameThread 投递唯一一次完成回调。
	 *
	 * @return 可用于取消的令牌；超时或取消后仍会投递失败结果。
	 */
	static TSharedPtr<FUnrealAgentPromptMediaLoadCancellation, ESPMode::ThreadSafe> LoadAsync(TArray<FString> AttachmentPaths,
		TFunction<void(bool bSucceeded, FUnrealAgentPreparedPromptMedia Media, FString Error)> Completion, float TimeoutSeconds = 45.0f);
};
