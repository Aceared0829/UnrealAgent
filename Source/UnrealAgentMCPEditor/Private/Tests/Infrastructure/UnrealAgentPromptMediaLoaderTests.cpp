// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentPromptMediaLoaderTests.cpp
 * @brief 用户图片与视频附件到模型图片内容块的自动化测试。
 */

#include "Application/Media/UnrealAgentPromptMedia.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentPromptImageAttachmentTest, "UnrealAgent.Media.ImageAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentPromptImageAttachmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent/Tests/PromptMedia"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	const FString ImagePath = FPaths::Combine(TestDirectory, TEXT("input.png"));
	const TArray<uint8> Bytes = { 0x89, 0x50, 0x4e, 0x47 };
	TestTrue(TEXT("写入图片测试附件"), FFileHelper::SaveArrayToFile(Bytes, *ImagePath));

	FUnrealAgentPreparedPromptMedia Media;
	FString Error;
	const bool bWasLoaded = FUnrealAgentPromptMediaLoader::Load({ ImagePath }, Media, Error);
	TestTrue(TEXT("图片附件可准备"), bWasLoaded);
	TestEqual(TEXT("生成一个图片块"), Media.Images.Num(), 1);
	if (Media.Images.Num() == 1)
	{
		TestEqual(TEXT("保留 PNG MIME"), Media.Images[0].MimeType, FString(TEXT("image/png")));
		TestFalse(TEXT("图片正文已编码"), Media.Images[0].Base64Data.IsEmpty());
	}
	TestTrue(TEXT("上下文声明真实图片数量"), Media.ContextText.Contains(TEXT("1 张图片")));

	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);

	FString LimitError;
	TestFalse(TEXT("张数达到上限后拒绝继续加入"),
		FUnrealAgentPromptMediaLoader::CanAcceptAdditionalImage(FUnrealAgentPromptMediaLoader::MaximumPromptImages, 0, 0, 1, 0, LimitError));
	TestFalse(TEXT("原始总字节超过上限后拒绝继续加入"),
		FUnrealAgentPromptMediaLoader::CanAcceptAdditionalImage(0, FUnrealAgentPromptMediaLoader::MaximumTotalRawImageBytes, 0, 1, 0, LimitError));
	TestFalse(TEXT("Base64 总长度超过上限后拒绝继续加入"),
		FUnrealAgentPromptMediaLoader::CanAcceptAdditionalImage(0, 0, FUnrealAgentPromptMediaLoader::MaximumTotalBase64Chars, 1, 1, LimitError));
	TestTrue(TEXT("未超限的单张图片可以接受"), FUnrealAgentPromptMediaLoader::CanAcceptAdditionalImage(0, 0, 0, 1024, 1368, LimitError));

	FUnrealAgentPromptMediaLoadContext CancelledContext;
	CancelledContext.Cancellation = MakeShared<FUnrealAgentPromptMediaLoadCancellation, ESPMode::ThreadSafe>();
	CancelledContext.Cancellation->Request();
	FUnrealAgentPreparedPromptMedia CancelledMedia;
	FString CancelledError;
	TestFalse(TEXT("已取消的同步准备立即失败"), FUnrealAgentPromptMediaLoader::Load({ ImagePath }, CancelledMedia, CancelledError, CancelledContext));
	TestTrue(TEXT("取消失败原因可识别"), CancelledError.Contains(TEXT("取消")));

	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	TArray<FString> OverflowPaths;
	for (int32 Index = 0; Index < FUnrealAgentPromptMediaLoader::MaximumPromptImages + 1; ++Index)
	{
		const FString OverflowPath = FPaths::Combine(TestDirectory, FString::Printf(TEXT("overflow-%02d.png"), Index));
		TestTrue(TEXT("写入超量图片测试附件"), FFileHelper::SaveArrayToFile(Bytes, *OverflowPath));
		OverflowPaths.Add(OverflowPath);
	}
	FUnrealAgentPreparedPromptMedia OverflowMedia;
	FString OverflowError;
	TestFalse(TEXT("超过 12 张必须拒绝而不是静默截断"), FUnrealAgentPromptMediaLoader::Load(OverflowPaths, OverflowMedia, OverflowError));
	TestTrue(TEXT("超量拒绝原因可识别"), OverflowError.Contains(TEXT("12")));
	TestEqual(TEXT("13 条路径中有 13 个受支持媒体"), FUnrealAgentPromptMediaLoader::CountSupportedMediaPaths(OverflowPaths),
		FUnrealAgentPromptMediaLoader::MaximumPromptImages + 1);
	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentPromptVideoAttachmentTest, "UnrealAgent.Media.VideoAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAgentPromptVideoAttachmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<IPlugin> VideoPlugin = IPluginManager::Get().FindPlugin(TEXT("WorldDataVideoAssembler"));
	if (!VideoPlugin.IsValid())
	{
		AddWarning(TEXT("未安装 WorldDataVideoAssembler，跳过真实视频适配测试。"));
		return true;
	}
	const FString FFmpegPath = FPaths::Combine(VideoPlugin->GetBaseDir(), TEXT("ThirdParty/FFmpeg/Win64/ffmpeg.exe"));
	if (!FPaths::FileExists(FFmpegPath))
	{
		AddWarning(TEXT("未找到随项目提供的 FFmpeg，跳过真实视频适配测试。"));
		return true;
	}

	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent/Tests/PromptMedia"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	const FString VideoPath = FPaths::Combine(TestDirectory, TEXT("input.mp4"));
	const FString Arguments = FString::Printf(TEXT("-hide_banner -loglevel error -y -f lavfi -i "
												   "\"color=c=blue:s=320x180:d=2:r=4\" -pix_fmt yuv420p \"%s\""),
		*VideoPath);
	int32 ReturnCode = 0;
	FString StandardOutput;
	FString StandardError;
	const bool bWasStarted = FPlatformProcess::ExecProcess(*FFmpegPath, *Arguments, &ReturnCode, &StandardOutput, &StandardError);
	TestTrue(TEXT("启动 FFmpeg 生成测试视频"), bWasStarted);
	TestEqual(TEXT("测试视频生成成功"), ReturnCode, 0);

	FUnrealAgentPreparedPromptMedia Media;
	FString Error;
	const bool bWasLoaded = FUnrealAgentPromptMediaLoader::Load({ VideoPath }, Media, Error);
	TestTrue(TEXT("视频附件可转换"), bWasLoaded);
	TestTrue(TEXT("视频至少生成一个关键帧"), Media.Images.Num() > 0);
	TestTrue(TEXT("关键帧数量有硬上限"), Media.Images.Num() <= 8);
	TestTrue(TEXT("上下文声明视频关键帧"), Media.ContextText.Contains(TEXT("1 个视频")) && Media.ContextText.Contains(TEXT("关键帧")));

	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
	return true;
}

#endif
