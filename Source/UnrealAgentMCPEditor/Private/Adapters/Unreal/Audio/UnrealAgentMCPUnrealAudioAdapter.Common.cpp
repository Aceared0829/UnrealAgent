// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.Common.cpp
 * @brief Audio 适配器的 JSON、资产、反射与 MetaSound Builder 公共实现。
 */

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.Internal.h"

#include "AssetToolsModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditorSubsystem.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::AudioPrivate
{
	TSharedRef<FJsonObject> SuccessObject()
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("domain"), TEXT("audio"));
		return Result;
	}

	FString Serialize(const TSharedRef<FJsonObject>& Object)
	{
		return JsonObjectToString(Object);
	}

	FString Failure(const FString& Error)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("audio"));
		Result->SetStringField(TEXT("error"), Error);
		return Serialize(Result);
	}

	FString RequireString(const TSharedPtr<FJsonObject>& Args, const FString& Name, FString& OutValue)
	{
		if (!Args.IsValid() || !Args->TryGetStringField(Name, OutValue) || OutValue.TrimStartAndEnd().IsEmpty())
		{
			return FString::Printf(TEXT("缺少必填参数 '%s'。"), *Name);
		}
		return FString();
	}

	FString OptionalString(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FString& DefaultValue)
	{
		FString Value;
		return Args.IsValid() && Args->TryGetStringField(Name, Value) ? Value : DefaultValue;
	}

	int32 OptionalInt(const TSharedPtr<FJsonObject>& Args, const FString& Name, const int32 DefaultValue)
	{
		double Value = DefaultValue;
		return Args.IsValid() && Args->TryGetNumberField(Name, Value) ? static_cast<int32>(Value) : DefaultValue;
	}

	double OptionalNumber(const TSharedPtr<FJsonObject>& Args, const FString& Name, const double DefaultValue)
	{
		double Value = DefaultValue;
		return Args.IsValid() && Args->TryGetNumberField(Name, Value) ? Value : DefaultValue;
	}

	bool OptionalBool(const TSharedPtr<FJsonObject>& Args, const FString& Name, const bool DefaultValue)
	{
		bool Value = DefaultValue;
		return Args.IsValid() && Args->TryGetBoolField(Name, Value) ? Value : DefaultValue;
	}

	FVector OptionalVector(const TSharedPtr<FJsonObject>& Args, const FString& Name, const FVector& DefaultValue)
	{
		if (!Args.IsValid())
		{
			return DefaultValue;
		}
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Args->TryGetObjectField(Name, Object) && Object && Object->IsValid())
		{
			return FVector(OptionalNumber(*Object, TEXT("x"), DefaultValue.X), OptionalNumber(*Object, TEXT("y"), DefaultValue.Y),
				OptionalNumber(*Object, TEXT("z"), DefaultValue.Z));
		}
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Args->TryGetArrayField(Name, Values) && Values && Values->Num() >= 3)
		{
			return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		}
		return DefaultValue;
	}

	UObject* LoadAsset(const FString& Path, UClass* ExpectedClass)
	{
		if (Path.IsEmpty() || !ExpectedClass)
		{
			return nullptr;
		}
		UObject* Asset = StaticLoadObject(ExpectedClass, nullptr, *Path);
		if (!Asset && !Path.Contains(TEXT(".")))
		{
			const FString Name = FPackageName::GetShortName(Path);
			Asset = StaticLoadObject(ExpectedClass, nullptr, *FString::Printf(TEXT("%s.%s"), *Path, *Name));
		}
		return Asset && Asset->IsA(ExpectedClass) ? Asset : nullptr;
	}

	UObject* CreateAsset(const FString& Name, const FString& PackagePath, UClass* AssetClass, UFactory* Factory, const FString& OnConflict, bool& bOutCreated, FString& OutError)
	{
		bOutCreated = false;
		const FString CleanPath = PackagePath.IsEmpty() ? TEXT("/Game/Audio") : PackagePath;
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *CleanPath, *Name, *Name);
		if (UObject* Existing = LoadAsset(ObjectPath, AssetClass))
		{
			if (OnConflict.Equals(TEXT("error"), ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(TEXT("资产已存在：%s"), *ObjectPath);
				return nullptr;
			}
			return Existing;
		}
		if (!Factory)
		{
			OutError = TEXT("未提供有效的音频资产 Factory。");
			return nullptr;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* Asset = AssetTools.CreateAsset(Name, CleanPath, AssetClass, Factory);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("创建资产失败：%s"), *ObjectPath);
			return nullptr;
		}
		bOutCreated = true;
		SaveAsset(Asset);
		return Asset;
	}

	bool SaveAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}
		Asset->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
	}

	bool SetReflectedProperty(void* Container, UStruct* Struct, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Container || !Struct || !Value.IsValid())
		{
			OutError = TEXT("属性写入参数无效。");
			return false;
		}
		FProperty* Property = Struct->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			OutError = FString::Printf(TEXT("找不到属性：%s"), *PropertyName);
			return false;
		}
		void* Address = Property->ContainerPtrToValuePtr<void>(Container);
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			bool BoolValue = false;
			if (!Value->TryGetBool(BoolValue))
			{
				OutError = TEXT("目标属性需要布尔值。");
				return false;
			}
			BoolProperty->SetPropertyValue(Address, BoolValue);
			return true;
		}
		if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			double NumberValue = 0.0;
			if (!Value->TryGetNumber(NumberValue))
			{
				OutError = TEXT("目标属性需要数值。");
				return false;
			}
			if (Numeric->IsInteger())
			{
				Numeric->SetIntPropertyValue(Address, static_cast<int64>(NumberValue));
			}
			else
			{
				Numeric->SetFloatingPointPropertyValue(Address, NumberValue);
			}
			return true;
		}
		FString Text;
		if (!Value->TryGetString(Text) || !Property->ImportText_Direct(*Text, Address, nullptr, PPF_None))
		{
			OutError = FString::Printf(TEXT("无法把输入值导入属性 %s。"), *PropertyName);
			return false;
		}
		return true;
	}

	USoundBase* RequireSound(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		FString Path;
		OutError = RequireString(Args, TEXT("assetPath"), Path);
		if (!OutError.IsEmpty())
		{
			return nullptr;
		}
		USoundBase* Sound = Cast<USoundBase>(LoadAsset(Path, USoundBase::StaticClass()));
		if (!Sound)
		{
			OutError = FString::Printf(TEXT("找不到声音资产：%s"), *Path);
		}
		return Sound;
	}

	UMetaSoundBuilderBase* RequireMetaSoundBuilder(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		FString Path;
		OutError = RequireString(Args, TEXT("assetPath"), Path);
		if (!OutError.IsEmpty())
		{
			return nullptr;
		}
		UObject* Asset = LoadAsset(Path, UObject::StaticClass());
		IMetaSoundDocumentInterface* Interface = Asset ? Cast<IMetaSoundDocumentInterface>(Asset) : nullptr;
		if (!Asset || !Interface || !GEditor)
		{
			OutError = FString::Printf(TEXT("找不到 MetaSound 资产：%s"), *Path);
			return nullptr;
		}
		TScriptInterface<IMetaSoundDocumentInterface> ScriptInterface;
		ScriptInterface.SetObject(Asset);
		ScriptInterface.SetInterface(Interface);
		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		UMetaSoundEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>();
		UMetaSoundBuilderBase* Builder = Subsystem ? Subsystem->FindOrBeginBuilding(ScriptInterface, Result) : nullptr;
		if (!Builder || Result != EMetaSoundBuilderResult::Succeeded)
		{
			OutError = FString::Printf(TEXT("无法打开 MetaSound Builder：%s"), *Path);
			return nullptr;
		}
		return Builder;
	}
}
