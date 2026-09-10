// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPMarkdown.h
 * @brief 对话 Markdown 的纯解析模型，不依赖 Slate，可供自动化测试复用。
 */

#include "CoreMinimal.h"

enum class EWorldDataMarkdownBlockType : uint8
{
	Paragraph,
	Heading,
	UnorderedListItem,
	OrderedListItem,
	Quote,
	CodeBlock,
	HorizontalRule,
	Table
};

struct FWorldDataMarkdownBlock
{
	EWorldDataMarkdownBlockType Type = EWorldDataMarkdownBlockType::Paragraph;
	FString Text;
	FString Info;
	int32 Level = 0;
	TArray<TArray<FString>> Rows;
};

namespace UnrealAgentMCPMarkdown
{
	/** Slate 原生文本选区使用 Z=-10；块背景必须更低，避免遮住选区高亮。 */
	inline constexpr int32 BackgroundHighlightZOrder = -20;

	/** 将 Markdown 文档解析为稳定的块结构；未闭合代码围栏也按代码块处理。 */
	TArray<FWorldDataMarkdownBlock> Parse(const FString& Markdown);

	/** 将行内 Markdown 转为 Slate 富文本标记，并转义原始 HTML。 */
	FString InlineToRichText(const FString& Markdown);

	/**
	 * 将完整 Markdown 文档转换为单一 Slate 富文本布局。
	 * 生成结果可交给只读 SMultiLineEditableText，从而支持跨块框选与复制。
	 */
	FString ToSelectableRichText(const FString& Markdown);
}
