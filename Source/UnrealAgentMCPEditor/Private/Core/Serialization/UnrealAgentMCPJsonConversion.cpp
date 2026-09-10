// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPJsonConversion.cpp
 * @brief MCP JSON 基础值与 Transform 分量转换实现。
 */

#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"

namespace UnrealAgentMCP::JsonConversion
{
	FString NormalizeAssetObjectPath(FString Path)
	{
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}

		if (Path.Contains(TEXT(".")))
		{
			return Path;
		}

		// 缺对象名时补全 Package.Asset 形式。
		const FString AssetName = FPaths::GetBaseFilename(Path);
		return FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
	}

	void SetVectorFields(const TSharedRef<FJsonObject>& Json, const FVector& Vector)
	{
		Json->SetNumberField(TEXT("x"), Vector.X);
		Json->SetNumberField(TEXT("y"), Vector.Y);
		Json->SetNumberField(TEXT("z"), Vector.Z);
	}

	TSharedPtr<FJsonObject> MakeVectorObject(const FVector& Vector)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		SetVectorFields(Json, Vector);
		return Json;
	}

	TSharedPtr<FJsonObject> MakeRotatorObject(const FRotator& Rotator)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("pitch"), Rotator.Pitch);
		Json->SetNumberField(TEXT("yaw"), Rotator.Yaw);
		Json->SetNumberField(TEXT("roll"), Rotator.Roll);
		return Json;
	}

	bool TryGetNumberFieldCaseInsensitive(const TSharedPtr<FJsonObject>& Json, const TCHAR* LowerName, const TCHAR* UpperName, double& OutValue)
	{
		return Json->TryGetNumberField(LowerName, OutValue) || Json->TryGetNumberField(UpperName, OutValue);
	}

	// -----------------------------------------------------------------------------
	// JSON 字段解析（向量 / 旋转 / 颜色）
	// -----------------------------------------------------------------------------

	bool TryGetVectorField(const TSharedPtr<FJsonObject>& Args, const TCHAR* FieldName, FVector& OutVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Args->TryGetArrayField(FieldName, Array) && Array && Array->Num() >= 3)
		{
			OutVector.X = (*Array)[0]->AsNumber();
			OutVector.Y = (*Array)[1]->AsNumber();
			OutVector.Z = (*Array)[2]->AsNumber();
			return true;
		}

		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Args->TryGetObjectField(FieldName, Object) && Object && Object->IsValid())
		{
			double X = OutVector.X;
			double Y = OutVector.Y;
			double Z = OutVector.Z;
			TryGetNumberFieldCaseInsensitive(*Object, TEXT("x"), TEXT("X"), X);
			TryGetNumberFieldCaseInsensitive(*Object, TEXT("y"), TEXT("Y"), Y);
			TryGetNumberFieldCaseInsensitive(*Object, TEXT("z"), TEXT("Z"), Z);
			OutVector = FVector(X, Y, Z);
			return true;
		}

		return false;
	}

	bool TryGetRotatorField(const TSharedPtr<FJsonObject>& Args, const TCHAR* FieldName, FRotator& OutRotator)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Args->TryGetArrayField(FieldName, Array) && Array && Array->Num() >= 3)
		{
			OutRotator = FRotator((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
			return true;
		}

		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Args->TryGetObjectField(FieldName, Object) && Object && Object->IsValid())
		{
			double Pitch = OutRotator.Pitch;
			double Yaw = OutRotator.Yaw;
			double Roll = OutRotator.Roll;
			TryGetNumberFieldCaseInsensitive(*Object, TEXT("pitch"), TEXT("Pitch"), Pitch);
			TryGetNumberFieldCaseInsensitive(*Object, TEXT("yaw"), TEXT("Yaw"), Yaw);
			TryGetNumberFieldCaseInsensitive(*Object, TEXT("roll"), TEXT("Roll"), Roll);
			OutRotator = FRotator(Pitch, Yaw, Roll);
			return true;
		}

		return false;
	}

	bool TryValueToNumber(const TSharedPtr<FJsonValue>& Value, double& OutNumber)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Number)
		{
			OutNumber = Value->AsNumber();
			return true;
		}
		if (Value->Type == EJson::String)
		{
			OutNumber = FCString::Atod(*Value->AsString());
			return true;
		}
		return false;
	}

	bool TryValueToBool(const TSharedPtr<FJsonValue>& Value, bool& bOutValue)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Boolean)
		{
			bOutValue = Value->AsBool();
			return true;
		}
		if (Value->Type == EJson::String)
		{
			const FString Text = Value->AsString().ToLower();
			if (Text == TEXT("true") || Text == TEXT("1") || Text == TEXT("yes"))
			{
				bOutValue = true;
				return true;
			}
			if (Text == TEXT("false") || Text == TEXT("0") || Text == TEXT("no"))
			{
				bOutValue = false;
				return true;
			}
		}
		return false;
	}

	bool TryValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array) && Array && Array->Num() >= 3)
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if (TryValueToNumber((*Array)[0], X) && TryValueToNumber((*Array)[1], Y) && TryValueToNumber((*Array)[2], Z))
			{
				OutVector = FVector(X, Y, Z);
				return true;
			}
		}

		TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid())
		{
			double X = OutVector.X;
			double Y = OutVector.Y;
			double Z = OutVector.Z;
			TryGetNumberFieldCaseInsensitive(Object, TEXT("x"), TEXT("X"), X);
			TryGetNumberFieldCaseInsensitive(Object, TEXT("y"), TEXT("Y"), Y);
			TryGetNumberFieldCaseInsensitive(Object, TEXT("z"), TEXT("Z"), Z);
			OutVector = FVector(X, Y, Z);
			return true;
		}
		return false;
	}

	bool TryValueToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array) && Array && Array->Num() >= 3)
		{
			double Pitch = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			if (TryValueToNumber((*Array)[0], Pitch) && TryValueToNumber((*Array)[1], Yaw) && TryValueToNumber((*Array)[2], Roll))
			{
				OutRotator = FRotator(Pitch, Yaw, Roll);
				return true;
			}
		}

		TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid())
		{
			double Pitch = OutRotator.Pitch;
			double Yaw = OutRotator.Yaw;
			double Roll = OutRotator.Roll;
			TryGetNumberFieldCaseInsensitive(Object, TEXT("pitch"), TEXT("Pitch"), Pitch);
			TryGetNumberFieldCaseInsensitive(Object, TEXT("yaw"), TEXT("Yaw"), Yaw);
			TryGetNumberFieldCaseInsensitive(Object, TEXT("roll"), TEXT("Roll"), Roll);
			OutRotator = FRotator(Pitch, Yaw, Roll);
			return true;
		}
		return false;
	}

	bool TryValueToLinearColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array) && Array && Array->Num() >= 3)
		{
			double R = 0.0;
			double G = 0.0;
			double B = 0.0;
			double A = 1.0;
			if (TryValueToNumber((*Array)[0], R) && TryValueToNumber((*Array)[1], G) && TryValueToNumber((*Array)[2], B))
			{
				if (Array->Num() >= 4)
				{
					TryValueToNumber((*Array)[3], A);
				}
				OutColor = FLinearColor(R, G, B, A);
				return true;
			}
		}

		TSharedPtr<FJsonObject> Object = Value->AsObject();
		if (Object.IsValid())
		{
			double R = OutColor.R;
			double G = OutColor.G;
			double B = OutColor.B;
			double A = OutColor.A;
			TryGetNumberFieldCaseInsensitive(Object, TEXT("r"), TEXT("R"), R);
			TryGetNumberFieldCaseInsensitive(Object, TEXT("g"), TEXT("G"), G);
			TryGetNumberFieldCaseInsensitive(Object, TEXT("b"), TEXT("B"), B);
			TryGetNumberFieldCaseInsensitive(Object, TEXT("a"), TEXT("A"), A);
			OutColor = FLinearColor(R, G, B, A);
			return true;
		}
		return false;
	}
}
