// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPJsonIntegrity.cpp
 * @brief 使用 UE PlatformCrypto 计算规范化 JSON 的 SHA-256。
 */

#include "Core/Serialization/UnrealAgentMCPJsonIntegrity.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPlatformCrypto.h"

namespace UnrealAgentMCP::JsonIntegrity
{
	namespace
	{
		void AppendEscapedString(const FString& Value, FString& OutJson)
		{
			OutJson.AppendChar(TEXT('"'));
			for (const TCHAR Character : Value)
			{
				switch (Character)
				{
				case TEXT('"'):
					OutJson += TEXT("\\\"");
					break;
				case TEXT('\\'):
					OutJson += TEXT("\\\\");
					break;
				case TEXT('\b'):
					OutJson += TEXT("\\b");
					break;
				case TEXT('\f'):
					OutJson += TEXT("\\f");
					break;
				case TEXT('\n'):
					OutJson += TEXT("\\n");
					break;
				case TEXT('\r'):
					OutJson += TEXT("\\r");
					break;
				case TEXT('\t'):
					OutJson += TEXT("\\t");
					break;
				default:
					if (Character < 0x20)
					{
						OutJson += FString::Printf(TEXT("\\u%04x"), Character);
					}
					else
					{
						OutJson.AppendChar(Character);
					}
					break;
				}
			}
			OutJson.AppendChar(TEXT('"'));
		}

		bool AppendCanonicalValue(const TSharedPtr<FJsonValue>& Value, FString& OutJson)
		{
			if (!Value.IsValid())
			{
				return false;
			}

			switch (Value->Type)
			{
			case EJson::Array:
			{
				OutJson.AppendChar(TEXT('['));
				const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
				for (int32 Index = 0; Index < Values.Num(); ++Index)
				{
					if (Index > 0)
					{
						OutJson.AppendChar(TEXT(','));
					}
					if (!AppendCanonicalValue(Values[Index], OutJson))
					{
						return false;
					}
				}
				OutJson.AppendChar(TEXT(']'));
				return true;
			}
			case EJson::Object:
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				if (!Object.IsValid())
				{
					return false;
				}
				TArray<FJsonObject::FStringType> Keys;
				Object->Values.GetKeys(Keys);
				Keys.Sort(
					[](const FJsonObject::FStringType& Left, const FJsonObject::FStringType& Right)
					{
						return Left.ToView().Compare(Right.ToView(), ESearchCase::CaseSensitive) < 0;
					});
				OutJson.AppendChar(TEXT('{'));
				for (int32 Index = 0; Index < Keys.Num(); ++Index)
				{
					if (Index > 0)
					{
						OutJson.AppendChar(TEXT(','));
					}
					AppendEscapedString(FString(Keys[Index].ToView()), OutJson);
					OutJson.AppendChar(TEXT(':'));
					if (!AppendCanonicalValue(Object->Values[Keys[Index]], OutJson))
					{
						return false;
					}
				}
				OutJson.AppendChar(TEXT('}'));
				return true;
			}
			case EJson::String:
				AppendEscapedString(Value->AsString(), OutJson);
				return true;
			case EJson::Number:
			{
				FString Number;
				if (!Value->TryGetString(Number))
				{
					return false;
				}
				OutJson += Number;
				return true;
			}
			case EJson::Boolean:
				OutJson += Value->AsBool() ? TEXT("true") : TEXT("false");
				return true;
			case EJson::Null:
				OutJson += TEXT("null");
				return true;
			default:
				return false;
			}
		}
	}

	bool TryComputeCanonicalSha256(const TSharedPtr<FJsonValue>& Value, FString& OutHash, FString& OutError)
	{
		OutHash.Reset();
		OutError.Reset();
		FString CanonicalJson;
		if (!AppendCanonicalValue(Value, CanonicalJson))
		{
			OutError = TEXT("JSON 包含无法规范化的值。");
			return false;
		}

		TUniquePtr<FEncryptionContext> CryptoContext = IPlatformCrypto::Get().CreateContext();
		if (!CryptoContext.IsValid())
		{
			OutError = TEXT("当前平台无法创建 UE 加密上下文。");
			return false;
		}

		FTCHARToUTF8 Utf8(*CanonicalJson);
		const TArrayView<const uint8> Source(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		TArray<uint8> Digest;
		if (!CryptoContext->CalcSHA256(Source, Digest) || Digest.Num() != 32)
		{
			OutError = TEXT("UE PlatformCrypto 无法计算 SHA-256。");
			return false;
		}

		OutHash = TEXT("sha256:") + BytesToHex(Digest.GetData(), Digest.Num());
		OutHash.ToLowerInline();
		return true;
	}
}
