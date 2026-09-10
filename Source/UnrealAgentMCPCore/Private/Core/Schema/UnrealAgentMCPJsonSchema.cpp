// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPJsonSchema.cpp
 * @brief JSON Schema 基础类型、对象、容器与枚举校验实现。
 */

#include "Core/Schema/UnrealAgentMCPJsonSchema.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/Regex.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAgentMCP::JsonSchema
{
	namespace
	{
		constexpr int32 MaxDepth = 64;
		constexpr int32 MaxErrors = 32;

		bool HasErrorCapacity(const FValidationResult& Result)
		{
			return Result.Errors.Num() < MaxErrors;
		}

		void AddError(FValidationResult& Result, const FString& Path, const FString& Message)
		{
			if (HasErrorCapacity(Result))
			{
				Result.Errors.Add({ Path, Message });
			}
		}

		FString JoinPath(const FString& Parent, const FString& Child)
		{
			return Parent == TEXT("$") ? Parent + TEXT(".") + Child : Parent + TEXT(".") + Child;
		}

		bool JsonValuesEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
		{
			if (!Left.IsValid() || !Right.IsValid() || Left->Type != Right->Type)
			{
				return false;
			}
			switch (Left->Type)
			{
			case EJson::Null:
				return true;
			case EJson::String:
				return Left->AsString() == Right->AsString();
			case EJson::Number:
				return Left->AsNumber() == Right->AsNumber();
			case EJson::Boolean:
				return Left->AsBool() == Right->AsBool();
			case EJson::Array:
			{
				const TArray<TSharedPtr<FJsonValue>>& LeftValues = Left->AsArray();
				const TArray<TSharedPtr<FJsonValue>>& RightValues = Right->AsArray();
				if (LeftValues.Num() != RightValues.Num())
				{
					return false;
				}
				for (int32 Index = 0; Index < LeftValues.Num(); ++Index)
				{
					if (!JsonValuesEqual(LeftValues[Index], RightValues[Index]))
					{
						return false;
					}
				}
				return true;
			}
			case EJson::Object:
			{
				const TSharedPtr<FJsonObject> LeftObject = Left->AsObject();
				const TSharedPtr<FJsonObject> RightObject = Right->AsObject();
				if (!LeftObject.IsValid() || !RightObject.IsValid() || LeftObject->Values.Num() != RightObject->Values.Num())
				{
					return false;
				}
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : LeftObject->Values)
				{
					const TSharedPtr<FJsonValue> Other = RightObject->TryGetField(Pair.Key);
					if (!Other.IsValid() || !JsonValuesEqual(Pair.Value, Other))
					{
						return false;
					}
				}
				return true;
			}
			default:
				return false;
			}
		}

		bool MatchesType(const FString& Type, const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return Type == TEXT("null");
			}
			if (Type == TEXT("object"))
				return Value->Type == EJson::Object;
			if (Type == TEXT("array"))
				return Value->Type == EJson::Array;
			if (Type == TEXT("string"))
				return Value->Type == EJson::String;
			if (Type == TEXT("number"))
				return Value->Type == EJson::Number && FMath::IsFinite(Value->AsNumber());
			if (Type == TEXT("integer"))
			{
				return Value->Type == EJson::Number && FMath::IsFinite(Value->AsNumber()) && FMath::IsNearlyEqual(Value->AsNumber(), FMath::RoundToDouble(Value->AsNumber()));
			}
			if (Type == TEXT("boolean"))
				return Value->Type == EJson::Boolean;
			if (Type == TEXT("null"))
				return Value->Type == EJson::Null;
			return true;
		}

		void ValidateInternal(const TSharedRef<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value, const FString& Path, const int32 Depth, FValidationResult& Result)
		{
			if (!HasErrorCapacity(Result))
			{
				return;
			}
			if (Depth > MaxDepth)
			{
				AddError(Result, Path, TEXT("Schema 校验递归深度超过 64。"));
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* AnyOf = nullptr;
			if (Schema->TryGetArrayField(TEXT("anyOf"), AnyOf) && AnyOf && !AnyOf->IsEmpty())
			{
				bool bMatchedAnyBranch = false;
				FString RequestedAction;
				if (Value.IsValid() && Value->Type == EJson::Object)
				{
					Value->AsObject()->TryGetStringField(TEXT("action"), RequestedAction);
				}
				TArray<FString> AllowedActions;
				FValidationResult MatchingActionResult;
				bool bMatchedActionBranch = false;
				for (const TSharedPtr<FJsonValue>& Candidate : *AnyOf)
				{
					if (!Candidate.IsValid() || Candidate->Type != EJson::Object)
					{
						continue;
					}
					bool bCandidateMatchesAction = false;
					const TSharedPtr<FJsonObject>* Properties = nullptr;
					const TSharedPtr<FJsonObject>* ActionSchema = nullptr;
					const TArray<TSharedPtr<FJsonValue>>* ActionEnum = nullptr;
					if (Candidate->AsObject()->TryGetObjectField(TEXT("properties"), Properties) && Properties && Properties->IsValid() &&
						(*Properties)->TryGetObjectField(TEXT("action"), ActionSchema) && ActionSchema && ActionSchema->IsValid() &&
						(*ActionSchema)->TryGetArrayField(TEXT("enum"), ActionEnum) && ActionEnum)
					{
						for (const TSharedPtr<FJsonValue>& ActionValue : *ActionEnum)
						{
							if (ActionValue.IsValid() && ActionValue->Type == EJson::String)
							{
								const FString Action = ActionValue->AsString();
								AllowedActions.AddUnique(Action);
								bCandidateMatchesAction |= Action == RequestedAction;
							}
						}
					}
					FValidationResult CandidateResult;
					ValidateInternal(Candidate->AsObject().ToSharedRef(), Value, Path, Depth + 1, CandidateResult);
					if (CandidateResult.IsValid())
					{
						bMatchedAnyBranch = true;
						break;
					}
					if (bCandidateMatchesAction)
					{
						MatchingActionResult = MoveTemp(CandidateResult);
						bMatchedActionBranch = true;
					}
				}
				if (!bMatchedAnyBranch && bMatchedActionBranch)
				{
					for (const FValidationError& Error : MatchingActionResult.Errors)
					{
						AddError(Result, Error.Path, Error.Message);
					}
					return;
				}
				if (!bMatchedAnyBranch && !RequestedAction.IsEmpty() && !AllowedActions.IsEmpty())
				{
					AddError(Result, JoinPath(Path, TEXT("action")),
						FString::Printf(TEXT("未知 action：%s。可用 action：%s。"), *RequestedAction, *FString::Join(AllowedActions, TEXT(", "))));
					return;
				}
				if (!bMatchedAnyBranch)
				{
					AddError(Result, Path, TEXT("值不符合 anyOf 中的任何分支。"));
					return;
				}
			}

			FString Type;
			if (Schema->TryGetStringField(TEXT("type"), Type) && !MatchesType(Type, Value))
			{
				AddError(Result, Path, FString::Printf(TEXT("类型不匹配，期望 %s。"), *Type));
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* EnumValues = nullptr;
			if (Schema->TryGetArrayField(TEXT("enum"), EnumValues) && EnumValues)
			{
				bool bMatched = false;
				for (const TSharedPtr<FJsonValue>& Candidate : *EnumValues)
				{
					if (JsonValuesEqual(Value, Candidate))
					{
						bMatched = true;
						break;
					}
				}
				if (!bMatched)
				{
					AddError(Result, Path, TEXT("值不在允许的 enum 集合中。"));
					return;
				}
			}

			if (Value.IsValid() && Value->Type == EJson::Number)
			{
				double Limit = 0.0;
				if (Schema->TryGetNumberField(TEXT("minimum"), Limit) && Value->AsNumber() < Limit)
				{
					AddError(Result, Path, FString::Printf(TEXT("数值小于最小值 %g。"), Limit));
				}
				if (Schema->TryGetNumberField(TEXT("maximum"), Limit) && Value->AsNumber() > Limit)
				{
					AddError(Result, Path, FString::Printf(TEXT("数值大于最大值 %g。"), Limit));
				}
			}

			if (Value.IsValid() && Value->Type == EJson::String)
			{
				const FString& StringValue = Value->AsString();
				double LengthLimit = 0.0;
				if (Schema->TryGetNumberField(TEXT("minLength"), LengthLimit) && StringValue.Len() < LengthLimit)
				{
					AddError(Result, Path, FString::Printf(TEXT("字符串长度小于最小值 %g。"), LengthLimit));
				}
				if (Schema->TryGetNumberField(TEXT("maxLength"), LengthLimit) && StringValue.Len() > LengthLimit)
				{
					AddError(Result, Path, FString::Printf(TEXT("字符串长度大于最大值 %g。"), LengthLimit));
				}
				FString Pattern;
				if (Schema->TryGetStringField(TEXT("pattern"), Pattern) && !Pattern.IsEmpty())
				{
					const FRegexPattern RegexPattern(Pattern);
					FRegexMatcher Matcher(RegexPattern, StringValue);
					if (!Matcher.FindNext())
					{
						AddError(Result, Path, FString::Printf(TEXT("字符串不符合 pattern：%s。"), *Pattern));
					}
				}
			}

			if (Value.IsValid() && Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
				if (Schema->TryGetArrayField(TEXT("required"), Required) && Required)
				{
					for (const TSharedPtr<FJsonValue>& RequiredValue : *Required)
					{
						FString Name;
						if (RequiredValue.IsValid() && RequiredValue->TryGetString(Name) && !Object->HasField(Name))
						{
							AddError(Result, JoinPath(Path, Name), TEXT("缺少必填字段。"));
						}
					}
				}

				const TSharedPtr<FJsonObject>* Properties = nullptr;
				const bool bHasProperties = Schema->TryGetObjectField(TEXT("properties"), Properties) && Properties && Properties->IsValid();
				bool bAllowAdditional = true;
				const bool bHasAdditionalFlag = Schema->TryGetBoolField(TEXT("additionalProperties"), bAllowAdditional);
				const TSharedPtr<FJsonObject>* AdditionalSchema = nullptr;
				const bool bHasAdditionalSchema = Schema->TryGetObjectField(TEXT("additionalProperties"), AdditionalSchema) && AdditionalSchema && AdditionalSchema->IsValid();
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
				{
					const TSharedPtr<FJsonValue> PropertySchema = bHasProperties ? (*Properties)->TryGetField(Pair.Key) : nullptr;
					if (PropertySchema.IsValid() && PropertySchema->Type == EJson::Object)
					{
						ValidateInternal(PropertySchema->AsObject().ToSharedRef(), Pair.Value, JoinPath(Path, Pair.Key), Depth + 1, Result);
					}
					else if (bHasAdditionalSchema)
					{
						ValidateInternal(AdditionalSchema->ToSharedRef(), Pair.Value, JoinPath(Path, Pair.Key), Depth + 1, Result);
					}
					else if (bHasAdditionalFlag && !bAllowAdditional)
					{
						AddError(Result, JoinPath(Path, Pair.Key), TEXT("不允许额外字段。"));
					}
				}
			}

			if (Value.IsValid() && Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
				double ItemLimit = 0.0;
				if (Schema->TryGetNumberField(TEXT("minItems"), ItemLimit) && Values.Num() < ItemLimit)
				{
					AddError(Result, Path, FString::Printf(TEXT("数组元素数量小于最小值 %g。"), ItemLimit));
				}
				if (Schema->TryGetNumberField(TEXT("maxItems"), ItemLimit) && Values.Num() > ItemLimit)
				{
					AddError(Result, Path, FString::Printf(TEXT("数组元素数量大于最大值 %g。"), ItemLimit));
				}
				bool bUniqueItems = false;
				if (Schema->TryGetBoolField(TEXT("uniqueItems"), bUniqueItems) && bUniqueItems)
				{
					bool bDuplicateFound = false;
					for (int32 LeftIndex = 0; LeftIndex < Values.Num() && !bDuplicateFound; ++LeftIndex)
					{
						for (int32 RightIndex = LeftIndex + 1; RightIndex < Values.Num(); ++RightIndex)
						{
							if (JsonValuesEqual(Values[LeftIndex], Values[RightIndex]))
							{
								AddError(Result, Path, FString::Printf(TEXT("数组元素 %d 与 %d 重复。"), LeftIndex, RightIndex));
								bDuplicateFound = true;
								break;
							}
						}
					}
				}
				const TSharedPtr<FJsonObject>* Items = nullptr;
				if (Schema->TryGetObjectField(TEXT("items"), Items) && Items && Items->IsValid())
				{
					for (int32 Index = 0; Index < Values.Num(); ++Index)
					{
						ValidateInternal(Items->ToSharedRef(), Values[Index], FString::Printf(TEXT("%s[%d]"), *Path, Index), Depth + 1, Result);
					}
				}
			}
		}
	}

	FValidationResult ValidateValue(const TSharedRef<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value)
	{
		FValidationResult Result;
		ValidateInternal(Schema, Value, TEXT("$"), 0, Result);
		return Result;
	}

	FValidationResult ValidateObject(const TSharedRef<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Value)
	{
		return ValidateValue(Schema, MakeShared<FJsonValueObject>(Value.IsValid() ? Value : MakeShared<FJsonObject>()));
	}

	FString MakeValidationErrorJson(const FString& ToolName, const FValidationResult& Result)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("code"), TEXT("invalid_arguments"));
		Root->SetStringField(TEXT("tool"), ToolName);
		Root->SetStringField(TEXT("error"), TEXT("工具参数不符合 inputSchema。"));
		TArray<TSharedPtr<FJsonValue>> Errors;
		for (const FValidationError& Error : Result.Errors)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("path"), Error.Path);
			Item->SetStringField(TEXT("message"), Error.Message);
			Errors.Add(MakeShared<FJsonValueObject>(Item));
		}
		Root->SetArrayField(TEXT("schemaErrors"), Errors);

		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		return Json;
	}

	FString MakeOutputValidationErrorJson(const FString& ToolName, const FValidationResult& Result)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("code"), TEXT("output_schema_invalid"));
		Root->SetStringField(TEXT("tool"), ToolName);
		Root->SetStringField(TEXT("error"), TEXT("工具成功载荷不符合 outputSchema。"));
		Root->SetBoolField(TEXT("retryable"), false);
		TArray<TSharedPtr<FJsonValue>> Errors;
		for (const FValidationError& Error : Result.Errors)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("path"), Error.Path);
			Item->SetStringField(TEXT("message"), Error.Message);
			Errors.Add(MakeShared<FJsonValueObject>(Item));
		}
		Root->SetArrayField(TEXT("schemaErrors"), Errors);

		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		return Json;
	}
}
