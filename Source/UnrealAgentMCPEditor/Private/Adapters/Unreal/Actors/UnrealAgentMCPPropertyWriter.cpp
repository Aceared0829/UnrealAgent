// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPropertyWriter.cpp
 * @brief MCP JSON 到 Unreal 可编辑属性的统一、原子写入入口。
 */

#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"

#include "Core/Reflection/UnrealAgentMCPPropertyTypeAdapter.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::PropertyWriter
{
	namespace
	{
		bool TryReadFiniteNumber(const TSharedPtr<FJsonValue>& Value, double& OutNumber)
		{
			return Value.IsValid() && Value->Type == EJson::Number && Value->TryGetNumber(OutNumber) && FMath::IsFinite(OutNumber);
		}

		bool TryReadComponents(const TSharedPtr<FJsonValue>& Value, TConstArrayView<const TCHAR*> LowerNames, TConstArrayView<const TCHAR*> UpperNames,
			TArrayView<double> InOutComponents, const int32 MinimumArraySize, const int32 MaximumArraySize, FString& OutError)
		{
			if (!Value.IsValid() || LowerNames.Num() != UpperNames.Num() || LowerNames.Num() != InOutComponents.Num())
			{
				OutError = TEXT("Invalid structured property conversion input.");
				return false;
			}

			if (Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
				if (Values.Num() < MinimumArraySize || Values.Num() > MaximumArraySize)
				{
					OutError = FString::Printf(TEXT("Expected an array with %d to %d numeric components."), MinimumArraySize, MaximumArraySize);
					return false;
				}
				for (int32 Index = 0; Index < Values.Num(); ++Index)
				{
					if (!TryReadFiniteNumber(Values[Index], InOutComponents[Index]))
					{
						OutError = FString::Printf(TEXT("Component %d must be a finite JSON number."), Index);
						return false;
					}
				}
				return true;
			}

			if (Value->Type != EJson::Object)
			{
				OutError = TEXT("Expected a JSON object or component array.");
				return false;
			}

			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (!Object.IsValid())
			{
				OutError = TEXT("Structured property JSON object is invalid.");
				return false;
			}
			for (int32 Index = 0; Index < LowerNames.Num(); ++Index)
			{
				TSharedPtr<FJsonValue> Component = Object->TryGetField(LowerNames[Index]);
				if (!Component.IsValid())
				{
					Component = Object->TryGetField(UpperNames[Index]);
				}
				if (Component.IsValid() && !TryReadFiniteNumber(Component, InOutComponents[Index]))
				{
					OutError = FString::Printf(TEXT("Component '%s' must be a finite JSON number."), UpperNames[Index]);
					return false;
				}
			}
			return true;
		}

		TSharedPtr<FJsonValue> MakeComponentObject(TConstArrayView<const TCHAR*> Names, TConstArrayView<double> Components)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < Names.Num(); ++Index)
			{
				Object->SetNumberField(Names[Index], Components[Index]);
			}
			return MakeShared<FJsonValueObject>(Object);
		}

		bool NormalizeLegacyStructValue(const FProperty* Property, const void* ValueAddress, const TSharedPtr<FJsonValue>& Value, TSharedPtr<FJsonValue>& OutValue,
			FString& OutError)
		{
			OutValue = Value;
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty || !ValueAddress || !Value.IsValid())
			{
				return true;
			}

			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				const FVector& Current = *static_cast<const FVector*>(ValueAddress);
				double Components[] = { Current.X, Current.Y, Current.Z };
				const TCHAR* LowerNames[] = { TEXT("x"), TEXT("y"), TEXT("z") };
				const TCHAR* UpperNames[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
				if (!TryReadComponents(Value, LowerNames, UpperNames, Components, 3, 3, OutError))
				{
					return false;
				}
				OutValue = MakeComponentObject(UpperNames, Components);
				return true;
			}

			if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				const FRotator& Current = *static_cast<const FRotator*>(ValueAddress);
				double Components[] = { Current.Pitch, Current.Yaw, Current.Roll };
				const TCHAR* LowerNames[] = { TEXT("pitch"), TEXT("yaw"), TEXT("roll") };
				const TCHAR* UpperNames[] = { TEXT("Pitch"), TEXT("Yaw"), TEXT("Roll") };
				if (!TryReadComponents(Value, LowerNames, UpperNames, Components, 3, 3, OutError))
				{
					return false;
				}
				OutValue = MakeComponentObject(UpperNames, Components);
				return true;
			}

			if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
			{
				const FLinearColor& Current = *static_cast<const FLinearColor*>(ValueAddress);
				double Components[] = { Current.R, Current.G, Current.B, Current.A };
				if (Value->Type == EJson::Array && Value->AsArray().Num() == 3)
				{
					Components[3] = 1.0;
				}
				const TCHAR* LowerNames[] = { TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a") };
				const TCHAR* UpperNames[] = { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") };
				if (!TryReadComponents(Value, LowerNames, UpperNames, Components, 3, 4, OutError))
				{
					return false;
				}
				OutValue = MakeComponentObject(UpperNames, Components);
			}
			return true;
		}
	}

	bool SetPropertyFromJson(UObject* Target, FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		OutError.Reset();
		if (!Target || !Property)
		{
			OutError = TEXT("Target object or property is invalid.");
			return false;
		}

		if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient))
		{
			OutError = FString::Printf(TEXT("Property '%s' is not editable through MCP."), *Property->GetName());
			return false;
		}

		void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Target);
		TSharedPtr<FJsonValue> NormalizedValue;
		if (!NormalizeLegacyStructValue(Property, ValueAddress, Value, NormalizedValue, OutError))
		{
			OutError = FString::Printf(TEXT("Property '%s' rejected the supplied value: %s"), *Property->GetName(), *OutError);
			return false;
		}

		FString ConversionError;
		if (!Reflection::FPropertyTypeAdapterRegistry::GetDefault().Read(NormalizedValue, Property, ValueAddress, ConversionError))
		{
			OutError = FString::Printf(TEXT("Property '%s' rejected the supplied value: %s"), *Property->GetName(), *ConversionError);
			return false;
		}
		return true;
	}
}
