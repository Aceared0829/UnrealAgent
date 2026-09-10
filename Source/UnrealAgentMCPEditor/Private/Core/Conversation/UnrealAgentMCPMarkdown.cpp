// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPMarkdown.cpp
 * @brief 对话 Markdown 的块级与行内安全解析实现。
 */

#include "Core/Conversation/UnrealAgentMCPMarkdown.h"

namespace
{
	FString EscapeRichText(const FString& Text, const bool bAttribute = false)
	{
		FString Escaped = Text;
		Escaped.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		Escaped.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		Escaped.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		if (bAttribute)
		{
			Escaped.ReplaceInline(TEXT("\""), TEXT("&quot;"));
		}
		return Escaped;
	}

	bool IsHorizontalRule(const FString& Line)
	{
		FString Compact = Line.TrimStartAndEnd();
		Compact.ReplaceInline(TEXT(" "), TEXT(""));
		if (Compact.Len() < 3)
		{
			return false;
		}

		const TCHAR RuleCharacter = Compact[0];
		if (RuleCharacter != TCHAR('-') && RuleCharacter != TCHAR('*') && RuleCharacter != TCHAR('_'))
		{
			return false;
		}
		for (const TCHAR Character : Compact)
		{
			if (Character != RuleCharacter)
			{
				return false;
			}
		}
		return true;
	}

	TArray<FString> SplitTableRow(FString Line)
	{
		Line.TrimStartAndEndInline();
		if (Line.StartsWith(TEXT("|")))
		{
			Line.RightChopInline(1);
		}
		if (Line.EndsWith(TEXT("|")))
		{
			Line.LeftChopInline(1);
		}

		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		for (FString& Cell : Cells)
		{
			Cell.TrimStartAndEndInline();
		}
		return Cells;
	}

	bool IsTableDelimiter(const FString& Line)
	{
		const TArray<FString> Cells = SplitTableRow(Line);
		if (Cells.IsEmpty())
		{
			return false;
		}

		for (FString Cell : Cells)
		{
			Cell.TrimStartAndEndInline();
			if (Cell.StartsWith(TEXT(":")))
			{
				Cell.RightChopInline(1);
			}
			if (Cell.EndsWith(TEXT(":")))
			{
				Cell.LeftChopInline(1);
			}
			if (Cell.Len() < 3)
			{
				return false;
			}
			for (const TCHAR Character : Cell)
			{
				if (Character != TCHAR('-'))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool TryParseHeading(const FString& Line, int32& OutLevel, FString& OutText)
	{
		const FString Trimmed = Line.TrimStart();
		int32 HashCount = 0;
		while (HashCount < Trimmed.Len() && HashCount < 6 && Trimmed[HashCount] == TCHAR('#'))
		{
			++HashCount;
		}
		if (HashCount == 0 || HashCount >= Trimmed.Len() || !FChar::IsWhitespace(Trimmed[HashCount]))
		{
			return false;
		}

		OutLevel = HashCount;
		OutText = Trimmed.Mid(HashCount).TrimStartAndEnd();
		while (OutText.EndsWith(TEXT("#")))
		{
			OutText.LeftChopInline(1);
			OutText.TrimEndInline();
		}
		return true;
	}

	bool TryParseListItem(const FString& Line, bool& bOutOrdered, int32& OutLevel, FString& OutMarker, FString& OutText)
	{
		int32 LeadingSpaces = 0;
		while (LeadingSpaces < Line.Len() && (Line[LeadingSpaces] == TCHAR(' ') || Line[LeadingSpaces] == TCHAR('\t')))
		{
			LeadingSpaces += Line[LeadingSpaces] == TCHAR('\t') ? 4 : 1;
		}
		const FString Trimmed = Line.TrimStart();
		OutLevel = LeadingSpaces / 2;

		if (Trimmed.Len() >= 2 && (Trimmed[0] == TCHAR('-') || Trimmed[0] == TCHAR('*') || Trimmed[0] == TCHAR('+')) && FChar::IsWhitespace(Trimmed[1]))
		{
			bOutOrdered = false;
			OutMarker = TEXT("•");
			OutText = Trimmed.Mid(2).TrimStartAndEnd();
			return true;
		}

		int32 DigitCount = 0;
		while (DigitCount < Trimmed.Len() && FChar::IsDigit(Trimmed[DigitCount]))
		{
			++DigitCount;
		}
		if (DigitCount > 0 && DigitCount + 1 < Trimmed.Len() && Trimmed[DigitCount] == TCHAR('.') && FChar::IsWhitespace(Trimmed[DigitCount + 1]))
		{
			bOutOrdered = true;
			OutMarker = Trimmed.Left(DigitCount + 1);
			OutText = Trimmed.Mid(DigitCount + 2).TrimStartAndEnd();
			return true;
		}
		return false;
	}

	bool StartsBlock(const TArray<FString>& Lines, const int32 Index)
	{
		if (!Lines.IsValidIndex(Index))
		{
			return false;
		}
		const FString Trimmed = Lines[Index].TrimStartAndEnd();
		if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("```")) || Trimmed.StartsWith(TEXT("~~~")) || Trimmed.StartsWith(TEXT(">")) || IsHorizontalRule(Trimmed))
		{
			return true;
		}

		int32 HeadingLevel = 0;
		FString Text;
		if (TryParseHeading(Lines[Index], HeadingLevel, Text))
		{
			return true;
		}
		bool bOrdered = false;
		FString Marker;
		if (TryParseListItem(Lines[Index], bOrdered, HeadingLevel, Marker, Text))
		{
			return true;
		}
		return Lines.IsValidIndex(Index + 1) && Lines[Index].Contains(TEXT("|")) && IsTableDelimiter(Lines[Index + 1]);
	}

	FString ConvertInline(const FString& Markdown, const int32 Depth)
	{
		if (Depth > 8)
		{
			return EscapeRichText(Markdown);
		}

		FString Result;
		for (int32 Index = 0; Index < Markdown.Len();)
		{
			if (Markdown[Index] == TCHAR('\\') && Index + 1 < Markdown.Len())
			{
				Result += EscapeRichText(Markdown.Mid(Index + 1, 1));
				Index += 2;
				continue;
			}

			if (Markdown[Index] == TCHAR('`'))
			{
				const int32 CloseIndex = Markdown.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 1);
				if (CloseIndex != INDEX_NONE)
				{
					Result += TEXT("<WorldDataMarkdown.Code>");
					Result += EscapeRichText(Markdown.Mid(Index + 1, CloseIndex - Index - 1));
					Result += TEXT("</>");
					Index = CloseIndex + 1;
					continue;
				}
			}

			const bool bImage = Markdown.Mid(Index).StartsWith(TEXT("!["));
			const bool bLink = Markdown[Index] == TCHAR('[') || bImage;
			if (bLink)
			{
				const int32 LabelStart = Index + (bImage ? 2 : 1);
				const int32 LabelEnd = Markdown.Find(TEXT("]("), ESearchCase::CaseSensitive, ESearchDir::FromStart, LabelStart);
				const int32 UrlEnd = LabelEnd == INDEX_NONE ? INDEX_NONE : Markdown.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, LabelEnd + 2);
				if (LabelEnd != INDEX_NONE && UrlEnd != INDEX_NONE)
				{
					const FString Label = Markdown.Mid(LabelStart, LabelEnd - LabelStart);
					const FString Url = Markdown.Mid(LabelEnd + 2, UrlEnd - LabelEnd - 2).TrimStartAndEnd();
					Result += TEXT("<a id=\"browser\" href=\"");
					Result += EscapeRichText(Url, true);
					Result += TEXT("\" style=\"WorldDataMarkdown.Link\">");
					Result += bImage ? TEXT("图像：") : FString();
					Result += ConvertInline(Label, Depth + 1);
					Result += TEXT("</>");
					Index = UrlEnd + 1;
					continue;
				}
			}

			struct FDelimiter
			{
				const TCHAR* Token;
				const TCHAR* Style;
			};
			static const FDelimiter Delimiters[] = { { TEXT("**"), TEXT("WorldDataMarkdown.Bold") }, { TEXT("__"), TEXT("WorldDataMarkdown.Bold") },
				{ TEXT("~~"), TEXT("WorldDataMarkdown.Muted") }, { TEXT("*"), TEXT("WorldDataMarkdown.Italic") }, { TEXT("_"), TEXT("WorldDataMarkdown.Italic") } };
			bool bMatchedDelimiter = false;
			for (const FDelimiter& Delimiter : Delimiters)
			{
				const FString Token(Delimiter.Token);
				if (!Markdown.Mid(Index).StartsWith(Token))
				{
					continue;
				}
				const int32 CloseIndex = Markdown.Find(Token, ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + Token.Len());
				if (CloseIndex == INDEX_NONE)
				{
					continue;
				}
				Result += FString::Printf(TEXT("<%s>"), Delimiter.Style);
				Result += ConvertInline(Markdown.Mid(Index + Token.Len(), CloseIndex - Index - Token.Len()), Depth + 1);
				Result += TEXT("</>");
				Index = CloseIndex + Token.Len();
				bMatchedDelimiter = true;
				break;
			}
			if (bMatchedDelimiter)
			{
				continue;
			}

			Result += EscapeRichText(Markdown.Mid(Index, 1));
			++Index;
		}
		return Result;
	}

	FString RichTextMarkupToPlainText(FString RichText)
	{
		for (int32 TagStart = RichText.Find(TEXT("<")); TagStart != INDEX_NONE;)
		{
			const int32 TagEnd = RichText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TagStart + 1);
			if (TagEnd == INDEX_NONE)
			{
				break;
			}
			RichText.RemoveAt(TagStart, TagEnd - TagStart + 1);
			TagStart = RichText.Find(TEXT("<"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TagStart);
		}
		RichText.ReplaceInline(TEXT("&lt;"), TEXT("<"));
		RichText.ReplaceInline(TEXT("&gt;"), TEXT(">"));
		RichText.ReplaceInline(TEXT("&quot;"), TEXT("\""));
		RichText.ReplaceInline(TEXT("&amp;"), TEXT("&"));
		return RichText;
	}
}

namespace UnrealAgentMCPMarkdown
{
	TArray<FWorldDataMarkdownBlock> Parse(const FString& Markdown)
	{
		FString Normalized = Markdown;
		Normalized.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Normalized.ReplaceInline(TEXT("\r"), TEXT("\n"));
		TArray<FString> Lines;
		Normalized.ParseIntoArray(Lines, TEXT("\n"), false);

		TArray<FWorldDataMarkdownBlock> Blocks;
		for (int32 Index = 0; Index < Lines.Num();)
		{
			const FString Trimmed = Lines[Index].TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				++Index;
				continue;
			}

			if (Trimmed.StartsWith(TEXT("```")) || Trimmed.StartsWith(TEXT("~~~")))
			{
				const FString Fence = Trimmed.Left(3);
				FWorldDataMarkdownBlock Block;
				Block.Type = EWorldDataMarkdownBlockType::CodeBlock;
				Block.Info = Trimmed.Mid(3).TrimStartAndEnd();
				++Index;
				TArray<FString> CodeLines;
				while (Index < Lines.Num() && !Lines[Index].TrimStart().StartsWith(Fence))
				{
					CodeLines.Add(Lines[Index]);
					++Index;
				}
				if (Index < Lines.Num())
				{
					++Index;
				}
				Block.Text = FString::Join(CodeLines, TEXT("\n"));
				Blocks.Add(MoveTemp(Block));
				continue;
			}

			int32 HeadingLevel = 0;
			FString BlockText;
			if (TryParseHeading(Lines[Index], HeadingLevel, BlockText))
			{
				FWorldDataMarkdownBlock Block;
				Block.Type = EWorldDataMarkdownBlockType::Heading;
				Block.Level = HeadingLevel;
				Block.Text = MoveTemp(BlockText);
				Blocks.Add(MoveTemp(Block));
				++Index;
				continue;
			}

			if (IsHorizontalRule(Trimmed))
			{
				FWorldDataMarkdownBlock Block;
				Block.Type = EWorldDataMarkdownBlockType::HorizontalRule;
				Blocks.Add(MoveTemp(Block));
				++Index;
				continue;
			}

			if (Lines.IsValidIndex(Index + 1) && Lines[Index].Contains(TEXT("|")) && IsTableDelimiter(Lines[Index + 1]))
			{
				FWorldDataMarkdownBlock Block;
				Block.Type = EWorldDataMarkdownBlockType::Table;
				Block.Rows.Add(SplitTableRow(Lines[Index]));
				Index += 2;
				while (Index < Lines.Num() && !Lines[Index].TrimStartAndEnd().IsEmpty() && Lines[Index].Contains(TEXT("|")))
				{
					Block.Rows.Add(SplitTableRow(Lines[Index]));
					++Index;
				}
				Blocks.Add(MoveTemp(Block));
				continue;
			}

			bool bOrdered = false;
			int32 ListLevel = 0;
			FString Marker;
			if (TryParseListItem(Lines[Index], bOrdered, ListLevel, Marker, BlockText))
			{
				FWorldDataMarkdownBlock Block;
				Block.Type = bOrdered ? EWorldDataMarkdownBlockType::OrderedListItem : EWorldDataMarkdownBlockType::UnorderedListItem;
				Block.Level = ListLevel;
				Block.Info = MoveTemp(Marker);
				Block.Text = MoveTemp(BlockText);
				Blocks.Add(MoveTemp(Block));
				++Index;
				continue;
			}

			if (Trimmed.StartsWith(TEXT(">")))
			{
				FWorldDataMarkdownBlock Block;
				Block.Type = EWorldDataMarkdownBlockType::Quote;
				Block.Text = Trimmed.Mid(1).TrimStartAndEnd();
				Blocks.Add(MoveTemp(Block));
				++Index;
				continue;
			}

			TArray<FString> ParagraphLines;
			while (Index < Lines.Num() && !StartsBlock(Lines, Index))
			{
				ParagraphLines.Add(Lines[Index].TrimStartAndEnd());
				++Index;
			}
			if (!ParagraphLines.IsEmpty())
			{
				FWorldDataMarkdownBlock Block;
				Block.Type = EWorldDataMarkdownBlockType::Paragraph;
				Block.Text = FString::Join(ParagraphLines, TEXT("\n"));
				Blocks.Add(MoveTemp(Block));
			}
			else
			{
				// 对任何未识别输入保证前进，避免流式半成品造成解析停滞。
				FWorldDataMarkdownBlock Block;
				Block.Type = EWorldDataMarkdownBlockType::Paragraph;
				Block.Text = Lines[Index];
				Blocks.Add(MoveTemp(Block));
				++Index;
			}
		}
		return Blocks;
	}

	FString InlineToRichText(const FString& Markdown)
	{
		return ConvertInline(Markdown, 0);
	}

	FString ToSelectableRichText(const FString& Markdown)
	{
		const TArray<FWorldDataMarkdownBlock> Blocks = Parse(Markdown);
		TArray<FString> OutputLines;

		auto AppendStyledLine = [&OutputLines](const TCHAR* Style, const FString& RichText)
		{
			OutputLines.Add(FString::Printf(TEXT("<%s>%s</>"), Style, *RichText));
		};

		for (int32 BlockIndex = 0; BlockIndex < Blocks.Num(); ++BlockIndex)
		{
			if (BlockIndex > 0)
			{
				AppendStyledLine(TEXT("WorldDataMarkdown.Spacer"), TEXT(" "));
			}

			const FWorldDataMarkdownBlock& Block = Blocks[BlockIndex];
			switch (Block.Type)
			{
			case EWorldDataMarkdownBlockType::Heading:
				AppendStyledLine(*FString::Printf(TEXT("WorldDataMarkdown.Heading%d"), FMath::Clamp(Block.Level, 1, 6)),
					EscapeRichText(RichTextMarkupToPlainText(InlineToRichText(Block.Text))));
				break;

			case EWorldDataMarkdownBlockType::UnorderedListItem:
			case EWorldDataMarkdownBlockType::OrderedListItem:
				OutputLines.Add(FString::Printf(TEXT("%s<WorldDataMarkdown.ListMarker>%s</> %s"), *FString::ChrN(FMath::Max(0, Block.Level) * 4, TCHAR(' ')),
					*EscapeRichText(Block.Info), *InlineToRichText(Block.Text)));
				break;

			case EWorldDataMarkdownBlockType::Quote:
				OutputLines.Add(FString::Printf(TEXT("<WorldDataMarkdown.QuoteMark>▎</> %s"), *InlineToRichText(Block.Text)));
				break;

			case EWorldDataMarkdownBlockType::CodeBlock:
				AppendStyledLine(TEXT("WorldDataMarkdown.CodeBlockTop"), TEXT(" "));
				if (!Block.Info.IsEmpty())
				{
					OutputLines.Add(FString::Printf(TEXT("<WorldDataMarkdown.CodeBlockMiddle> </>") TEXT("<WorldDataMarkdown.CodeLabel>  %s</>"), *EscapeRichText(Block.Info)));
				}
				{
					TArray<FString> CodeLines;
					Block.Text.ParseIntoArray(CodeLines, TEXT("\n"), false);
					if (CodeLines.IsEmpty())
					{
						CodeLines.Add(FString());
					}
					for (const FString& CodeLine : CodeLines)
					{
						OutputLines.Add(
							FString::Printf(TEXT("<WorldDataMarkdown.CodeBlockMiddle> </>") TEXT("<WorldDataMarkdown.CodeBlockText>  %s</>"), *EscapeRichText(CodeLine)));
					}
				}
				AppendStyledLine(TEXT("WorldDataMarkdown.CodeBlockBottom"), TEXT(" "));
				break;

			case EWorldDataMarkdownBlockType::HorizontalRule:
				AppendStyledLine(TEXT("WorldDataMarkdown.HorizontalRule"), TEXT("────────────────────────────────"));
				break;

			case EWorldDataMarkdownBlockType::Table:
				for (int32 RowIndex = 0; RowIndex < Block.Rows.Num(); ++RowIndex)
				{
					TArray<FString> Cells;
					for (const FString& Cell : Block.Rows[RowIndex])
					{
						const FString RichCell = InlineToRichText(Cell);
						Cells.Add(RowIndex == 0 ? FString::Printf(TEXT("<WorldDataMarkdown.Bold>%s</>"), *EscapeRichText(RichTextMarkupToPlainText(RichCell))) : RichCell);
					}

					const bool bFirstRow = RowIndex == 0;
					const bool bLastRow = RowIndex == Block.Rows.Num() - 1;
					const TCHAR* RowStyle = bFirstRow && bLastRow ? TEXT("WorldDataMarkdown.TableSingle")
						: bFirstRow                               ? TEXT("WorldDataMarkdown.TableTop")
						: bLastRow                                ? TEXT("WorldDataMarkdown.TableBottom")
																  : TEXT("WorldDataMarkdown.TableMiddle");
					OutputLines.Add(FString::Printf(TEXT("<%s> </>  %s"), RowStyle, *FString::Join(Cells, TEXT("  │  "))));
				}
				break;

			case EWorldDataMarkdownBlockType::Paragraph:
			default:
			{
				TArray<FString> ParagraphLines;
				Block.Text.ParseIntoArray(ParagraphLines, TEXT("\n"), false);
				for (const FString& ParagraphLine : ParagraphLines)
				{
					OutputLines.Add(InlineToRichText(ParagraphLine));
				}
			}
			break;
			}
		}

		return FString::Join(OutputLines, TEXT("\n"));
	}
}
