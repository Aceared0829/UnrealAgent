// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAnimationAdapter.Common.cpp
 * @brief 动画结果、参数、资产生命周期与反射属性公共实现。
 */

#include "Adapters/Unreal/Animation/UnrealAgentMCPUnrealAnimationAdapter.Internal.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::AnimationPrivate
{
	FString Success(const TSharedRef<FJsonObject>& Result)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("domain"), TEXT("animation"));
		return JsonObjectToString(Result);
	}

	FString Error(const FString& Message)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("animation"));
		Result->SetStringField(TEXT("error"), Message);
		return JsonObjectToString(Result);
	}

	FString StringArg(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names, const FString& Default)
	{
		FString Value;
		if (Args)
		{
			for (const TCHAR* Name : Names)
			{
				if (Args->TryGetStringField(Name, Value) && !Value.IsEmpty())
				{
					return Value;
				}
			}
		}
		return Default;
	}

	bool BoolArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const bool Default)
	{
		bool Value = Default;
		return Args && Args->TryGetBoolField(Name, Value) ? Value : Default;
	}

	double NumberArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const double Default)
	{
		double Value = Default;
		return Args && Args->TryGetNumberField(Name, Value) ? Value : Default;
	}

	UObject* LoadAsset(const FString& Path, UClass* ExpectedClass)
	{
		if (Path.IsEmpty())
			return nullptr;
		UObject* Asset = StaticLoadObject(ExpectedClass ? ExpectedClass : UObject::StaticClass(), nullptr, *Path);
		if (!Asset && !Path.Contains(TEXT(".")))
		{
			const FString Name = FPackageName::GetShortName(Path);
			Asset = StaticLoadObject(ExpectedClass ? ExpectedClass : UObject::StaticClass(), nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name));
		}
		return Asset && (!ExpectedClass || Asset->IsA(ExpectedClass)) ? Asset : nullptr;
	}

	UClass* FindClass(const FString& NameOrPath)
	{
		if (NameOrPath.IsEmpty())
			return nullptr;
		if (UClass* Class = LoadObject<UClass>(nullptr, *NameOrPath))
			return Class;
		if (UClass* Class = FindFirstObject<UClass>(*NameOrPath, EFindFirstObjectOptions::NativeFirst))
			return Class;
		return FindFirstObject<UClass>(*(NameOrPath + TEXT("_C")), EFindFirstObjectOptions::NativeFirst);
	}

	UObject* CreateAsset(UClass* Class, const TSharedPtr<FJsonObject>& Args, const FString& Prefix, FString& OutPath)
	{
		if (!Class)
			return nullptr;
		const FString Name = StringArg(Args, { TEXT("name") }, Prefix);
		const FString Directory = StringArg(Args, { TEXT("packagePath"), TEXT("directory") }, TEXT("/Game/Animation"));
		OutPath = Directory / Name + TEXT(".") + Name;
		if (UObject* Existing = LoadAsset(OutPath, Class))
			return Existing;
		UPackage* Package = CreatePackage(*FPackageName::ObjectPathToPackageName(OutPath));
		UObject* Asset = NewObject<UObject>(Package, Class, *FPackageName::ObjectPathToObjectName(OutPath), RF_Public | RF_Standalone | RF_Transactional);
		if (Asset)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			Save(Asset);
		}
		return Asset;
	}

	bool Save(UObject* Asset)
	{
		if (!Asset)
			return false;
		Asset->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
	}

	bool SetProperty(UObject* Object, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Object || !Value)
		{
			OutError = TEXT("属性写入参数无效。");
			return false;
		}
		FProperty* Property = Object->GetClass()->FindPropertyByName(*Name);
		if (!Property)
		{
			OutError = FString::Printf(TEXT("找不到属性：%s"), *Name);
			return false;
		}
		void* Address = Property->ContainerPtrToValuePtr<void>(Object);
		if (FBoolProperty* Typed = CastField<FBoolProperty>(Property))
		{
			bool Parsed = false;
			if (!Value->TryGetBool(Parsed))
				return false;
			Typed->SetPropertyValue(Address, Parsed);
			return true;
		}
		if (FNumericProperty* Typed = CastField<FNumericProperty>(Property))
		{
			double Parsed = 0.0;
			if (!Value->TryGetNumber(Parsed))
				return false;
			if (Typed->IsInteger())
				Typed->SetIntPropertyValue(Address, static_cast<int64>(Parsed));
			else
				Typed->SetFloatingPointPropertyValue(Address, Parsed);
			return true;
		}
		if (FStrProperty* Typed = CastField<FStrProperty>(Property))
		{
			FString Parsed;
			if (!Value->TryGetString(Parsed))
				return false;
			Typed->SetPropertyValue(Address, Parsed);
			return true;
		}
		if (FNameProperty* Typed = CastField<FNameProperty>(Property))
		{
			FString Parsed;
			if (!Value->TryGetString(Parsed))
				return false;
			Typed->SetPropertyValue(Address, FName(*Parsed));
			return true;
		}
		if (FEnumProperty* Typed = CastField<FEnumProperty>(Property))
		{
			FString Parsed;
			if (!Value->TryGetString(Parsed))
				return false;
			const int64 EnumValue = Typed->GetEnum()->GetValueByNameString(Parsed);
			if (EnumValue == INDEX_NONE)
				return false;
			Typed->GetUnderlyingProperty()->SetIntPropertyValue(Address, EnumValue);
			return true;
		}
		if (FObjectPropertyBase* Typed = CastField<FObjectPropertyBase>(Property))
		{
			FString Parsed;
			if (!Value->TryGetString(Parsed))
				return false;
			UObject* Referenced = LoadAsset(Parsed, Typed->PropertyClass);
			if (!Referenced)
				return false;
			Typed->SetObjectPropertyValue(Address, Referenced);
			return true;
		}
		OutError = FString::Printf(TEXT("属性类型暂不支持直接写入：%s"), *Name);
		return false;
	}

	TSharedRef<FJsonObject> DescribeObject(UObject* Object)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!Object)
			return Result;
		Result->SetStringField(TEXT("name"), Object->GetName());
		Result->SetStringField(TEXT("path"), Object->GetPathName());
		Result->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
				continue;
			Properties.Add(MakeShared<FJsonValueString>(It->GetName()));
		}
		Result->SetArrayField(TEXT("properties"), Properties);
		return Result;
	}
}
