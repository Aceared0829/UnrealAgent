// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.Playback.cpp
 * @brief 编辑器世界中的一次性试听与 AmbientSound 放置。
 */

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.Internal.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AmbientSound.h"
#include "Sound/SoundBase.h"

namespace UnrealAgentMCP
{
	using namespace AudioPrivate;

	FString FUnrealAgentMCPUnrealAudioAdapter::ExecutePlaybackAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		USoundBase* Sound = RequireSound(Args, Error);
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!Sound || !World)
		{
			return Failure(!Error.IsEmpty() ? Error : TEXT("当前没有可用的编辑器世界。"));
		}
		const FVector Location = OptionalVector(Args, TEXT("location"));
		const float Volume = static_cast<float>(OptionalNumber(Args, TEXT("volume"), 1.0));
		const float Pitch = static_cast<float>(OptionalNumber(Args, TEXT("pitch"), 1.0));
		if (Action == TEXT("play_at_location"))
		{
			UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, Volume, Pitch);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Sound->GetPathName());
			Result->SetStringField(TEXT("mode"), TEXT("oneShot"));
			return Serialize(Result);
		}

		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transactional;
		AAmbientSound* Actor = World->SpawnActor<AAmbientSound>(AAmbientSound::StaticClass(), Location, FRotator::ZeroRotator, Params);
		if (!Actor || !Actor->GetAudioComponent())
		{
			return Failure(TEXT("无法在编辑器世界放置 AmbientSound。"));
		}
		Actor->Modify();
		Actor->SetActorLabel(OptionalString(Args, TEXT("label"), FString::Printf(TEXT("MCP_%s"), *Sound->GetName())));
		Actor->GetAudioComponent()->SetSound(Sound);
		Actor->GetAudioComponent()->SetVolumeMultiplier(Volume);
		Actor->GetAudioComponent()->SetPitchMultiplier(Pitch);
		Actor->GetAudioComponent()->bAutoActivate = OptionalBool(Args, TEXT("autoActivate"), false);
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		return Serialize(Result);
	}
}
