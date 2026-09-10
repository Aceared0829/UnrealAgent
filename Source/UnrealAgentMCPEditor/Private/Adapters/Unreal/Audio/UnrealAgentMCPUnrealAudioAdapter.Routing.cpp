// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.Routing.cpp
 * @brief Submix、SoundClass、SoundMix、Concurrency、Attenuation 与声音路由实现。
 */

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Factories/SoundAttenuationFactory.h"
#include "Factories/SoundClassFactory.h"
#include "Factories/SoundConcurrencyFactory.h"
#include "Factories/SoundMixFactory.h"
#include "Factories/SoundSubmixFactory.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundEffectSubmix.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundSubmixSend.h"

namespace UnrealAgentMCP
{
	using namespace AudioPrivate;

	namespace
	{
		FString CreateStandardAsset(const TSharedPtr<FJsonObject>& Args, UClass* AssetClass, UFactory* Factory)
		{
			FString Name;
			if (const FString Required = RequireString(Args, TEXT("name"), Name); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			bool bCreated = false;
			FString Error;
			UObject* Asset = CreateAsset(Name, OptionalString(Args, TEXT("packagePath"), TEXT("/Game/Audio")), AssetClass, Factory,
				OptionalString(Args, TEXT("onConflict"), TEXT("reuse")), bCreated, Error);
			if (!Asset)
			{
				return Failure(Error);
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
			Result->SetBoolField(TEXT("created"), bCreated);
			return Serialize(Result);
		}
	}

	FString FUnrealAgentMCPUnrealAudioAdapter::ExecuteRoutingAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_submix"))
		{
			return CreateStandardAsset(Args, USoundSubmix::StaticClass(), NewObject<USoundSubmixFactory>());
		}
		if (Action == TEXT("create_sound_class"))
		{
			return CreateStandardAsset(Args, USoundClass::StaticClass(), NewObject<USoundClassFactory>());
		}
		if (Action == TEXT("create_sound_mix"))
		{
			return CreateStandardAsset(Args, USoundMix::StaticClass(), NewObject<USoundMixFactory>());
		}
		if (Action == TEXT("create_concurrency"))
		{
			return CreateStandardAsset(Args, USoundConcurrency::StaticClass(), NewObject<USoundConcurrencyFactory>());
		}
		if (Action == TEXT("create_attenuation"))
		{
			return CreateStandardAsset(Args, USoundAttenuation::StaticClass(), NewObject<USoundAttenuationFactory>());
		}

		if (Action == TEXT("set_submix_parent") || Action == TEXT("add_submix_effect"))
		{
			FString AssetPath;
			FString OtherPath;
			if (const FString Required = RequireString(Args, TEXT("assetPath"), AssetPath); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			USoundSubmix* Submix = Cast<USoundSubmix>(LoadAsset(AssetPath, USoundSubmix::StaticClass()));
			if (!Submix)
			{
				return Failure(FString::Printf(TEXT("找不到 SoundSubmix：%s"), *AssetPath));
			}
			Submix->Modify();
			if (Action == TEXT("set_submix_parent"))
			{
				if (const FString Required = RequireString(Args, TEXT("parentPath"), OtherPath); !Required.IsEmpty())
				{
					return Failure(Required);
				}
				USoundSubmixBase* Parent = Cast<USoundSubmixBase>(LoadAsset(OtherPath, USoundSubmixBase::StaticClass()));
				if (!Parent || Parent == Submix)
				{
					return Failure(TEXT("父 Submix 无效或形成自引用。"));
				}
				Submix->SetParentSubmix(Parent, true);
			}
			else
			{
				if (const FString Required = RequireString(Args, TEXT("effectPath"), OtherPath); !Required.IsEmpty())
				{
					return Failure(Required);
				}
				USoundEffectSubmixPreset* Effect = Cast<USoundEffectSubmixPreset>(LoadAsset(OtherPath, USoundEffectSubmixPreset::StaticClass()));
				if (!Effect)
				{
					return Failure(FString::Printf(TEXT("找不到 Submix Effect：%s"), *OtherPath));
				}
				Submix->SubmixEffectChain.AddUnique(Effect);
			}
			SaveAsset(Submix);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Submix->GetPathName());
			return Serialize(Result);
		}

		FString Error;
		USoundBase* Sound = RequireSound(Args, Error);
		if (!Sound)
		{
			return Failure(Error);
		}
		Sound->Modify();
		FString TargetPath;
		if (Action == TEXT("set_sound_submix"))
		{
			if (const FString Required = RequireString(Args, TEXT("submixPath"), TargetPath); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			USoundSubmixBase* Submix = Cast<USoundSubmixBase>(LoadAsset(TargetPath, USoundSubmixBase::StaticClass()));
			if (!Submix)
			{
				return Failure(TEXT("找不到目标 SoundSubmix。"));
			}
			Sound->bEnableBaseSubmix = true;
			Sound->SoundSubmixObject = Submix;
		}
		else if (Action == TEXT("add_sound_submix_send"))
		{
			if (const FString Required = RequireString(Args, TEXT("submixPath"), TargetPath); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			USoundSubmixBase* Submix = Cast<USoundSubmixBase>(LoadAsset(TargetPath, USoundSubmixBase::StaticClass()));
			if (!Submix)
			{
				return Failure(TEXT("找不到目标 SoundSubmix。"));
			}
			FSoundSubmixSendInfo Send;
			Send.SoundSubmix = Submix;
			Send.SendLevelControlMethod = ESendLevelControlMethod::Manual;
			Send.SendLevel = static_cast<float>(OptionalNumber(Args, TEXT("sendLevel"), 1.0));
			Sound->bEnableSubmixSends = true;
			Sound->SoundSubmixSends.Add(Send);
		}
		else if (Action == TEXT("set_sound_class"))
		{
			if (const FString Required = RequireString(Args, TEXT("soundClassPath"), TargetPath); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			Sound->SoundClassObject = Cast<USoundClass>(LoadAsset(TargetPath, USoundClass::StaticClass()));
			if (!Sound->SoundClassObject)
			{
				return Failure(TEXT("找不到目标 SoundClass。"));
			}
		}
		else if (Action == TEXT("set_sound_attenuation"))
		{
			if (const FString Required = RequireString(Args, TEXT("attenuationPath"), TargetPath); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			Sound->AttenuationSettings = Cast<USoundAttenuation>(LoadAsset(TargetPath, USoundAttenuation::StaticClass()));
			if (!Sound->AttenuationSettings)
			{
				return Failure(TEXT("找不到目标 SoundAttenuation。"));
			}
		}
		else
		{
			if (const FString Required = RequireString(Args, TEXT("concurrencyPath"), TargetPath); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			USoundConcurrency* Concurrency = Cast<USoundConcurrency>(LoadAsset(TargetPath, USoundConcurrency::StaticClass()));
			if (!Concurrency)
			{
				return Failure(TEXT("找不到目标 SoundConcurrency。"));
			}
			Sound->ConcurrencySet.Reset();
			Sound->ConcurrencySet.Add(Concurrency);
		}
		SaveAsset(Sound);
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("assetPath"), Sound->GetPathName());
		Result->SetStringField(TEXT("targetPath"), TargetPath);
		return Serialize(Result);
	}
}
