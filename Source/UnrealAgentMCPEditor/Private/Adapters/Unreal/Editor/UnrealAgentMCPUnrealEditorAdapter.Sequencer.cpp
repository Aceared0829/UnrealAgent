// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Sequencer.cpp
 * @brief Level Sequence 资产、轨道、区段、关键帧与播放控制。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealAgentMCP
{
	namespace
	{
		ULevelSequence* LoadLevelSequence(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			const FString AssetPath = FUnrealAgentMCPUnrealEditorAdapter::GetString(Args, { TEXT("assetPath"), TEXT("sequencePath"), TEXT("path") });
			ULevelSequence* Sequence = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *AssetPath));
			if (!Sequence)
			{
				OutError = FString::Printf(TEXT("未找到 Level Sequence：%s"), *AssetPath);
			}
			return Sequence;
		}

		UClass* ResolveSequenceTrackClass(FString TrackType)
		{
			TrackType.ReplaceInline(TEXT("MovieScene"), TEXT(""));
			TrackType.ReplaceInline(TEXT("Track"), TEXT(""));
			if (TrackType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase) || TrackType.Equals(TEXT("3DTransform"), ESearchCase::IgnoreCase))
			{
				return UMovieScene3DTransformTrack::StaticClass();
			}
			if (TrackType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
			{
				return UMovieSceneFloatTrack::StaticClass();
			}
			if (TrackType.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
			{
				return UMovieSceneDoubleTrack::StaticClass();
			}
			if (TrackType.Equals(TEXT("SkeletalAnimation"), ESearchCase::IgnoreCase))
			{
				return UMovieSceneSkeletalAnimationTrack::StaticClass();
			}
			if (TrackType.Equals(TEXT("CameraCut"), ESearchCase::IgnoreCase))
			{
				return UMovieSceneCameraCutTrack::StaticClass();
			}
			if (TrackType.Equals(TEXT("Audio"), ESearchCase::IgnoreCase))
			{
				return UMovieSceneAudioTrack::StaticClass();
			}
			if (TrackType.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
			{
				return UMovieSceneEventTrack::StaticClass();
			}
			if (TrackType.Equals(TEXT("Fade"), ESearchCase::IgnoreCase))
			{
				return UMovieSceneFadeTrack::StaticClass();
			}
			return nullptr;
		}

		AActor* FindSequenceActor(UWorld* World, const FString& ActorLabel)
		{
			if (!World)
			{
				return nullptr;
			}
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) || It->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
				{
					return *It;
				}
			}
			return nullptr;
		}

		TArray<UMovieSceneTrack*> CollectSequenceTracks(UMovieScene* MovieScene)
		{
			TArray<UMovieSceneTrack*> Tracks;
			if (!MovieScene)
			{
				return Tracks;
			}
			Tracks.Append(MovieScene->GetTracks());
			for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
			{
				const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
				if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(Possessable.GetGuid()))
				{
					Tracks.Append(Binding->GetTracks());
				}
			}
			for (int32 Index = 0; Index < MovieScene->GetSpawnableCount(); ++Index)
			{
				const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Index);
				if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(Spawnable.GetGuid()))
				{
					Tracks.Append(Binding->GetTracks());
				}
			}
			return Tracks;
		}

		UMovieSceneTrack* ResolveSequenceTrack(UMovieScene* MovieScene, const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			const FString TrackPath = FUnrealAgentMCPUnrealEditorAdapter::GetString(Args, { TEXT("trackPath") });
			if (!TrackPath.IsEmpty())
			{
				if (UMovieSceneTrack* Track = FindObject<UMovieSceneTrack>(nullptr, *TrackPath))
				{
					return Track;
				}
			}
			const TArray<UMovieSceneTrack*> Tracks = CollectSequenceTracks(MovieScene);
			double TrackIndexNumber = -1.0;
			Args->TryGetNumberField(TEXT("trackIndex"), TrackIndexNumber);
			const int32 TrackIndex = static_cast<int32>(TrackIndexNumber);
			if (Tracks.IsValidIndex(TrackIndex))
			{
				return Tracks[TrackIndex];
			}
			const FString TrackType = FUnrealAgentMCPUnrealEditorAdapter::GetString(Args, { TEXT("trackType"), TEXT("trackClass") });
			UClass* TrackClass = ResolveSequenceTrackClass(TrackType);
			const FString TrackName = FUnrealAgentMCPUnrealEditorAdapter::GetString(Args, { TEXT("trackName") });
			for (UMovieSceneTrack* Track : Tracks)
			{
				if (Track && (!TrackClass || Track->IsA(TrackClass)) && (TrackName.IsEmpty() || Track->GetTrackName().ToString().Equals(TrackName, ESearchCase::IgnoreCase)))
				{
					return Track;
				}
			}
			OutError = TEXT("未找到轨道；请提供有效的 trackPath、trackIndex 或 trackType。");
			return nullptr;
		}

		UMovieSceneSection* ResolveSequenceSection(UMovieSceneTrack* Track, const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			const FString SectionPath = FUnrealAgentMCPUnrealEditorAdapter::GetString(Args, { TEXT("sectionPath") });
			if (!SectionPath.IsEmpty())
			{
				if (UMovieSceneSection* Section = FindObject<UMovieSceneSection>(nullptr, *SectionPath))
				{
					return Section;
				}
			}
			double SectionIndexNumber = 0.0;
			Args->TryGetNumberField(TEXT("sectionIndex"), SectionIndexNumber);
			const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
			const int32 SectionIndex = static_cast<int32>(SectionIndexNumber);
			if (Sections.IsValidIndex(SectionIndex))
			{
				return Sections[SectionIndex];
			}
			OutError = TEXT("未找到指定的 Sequence 区段。");
			return nullptr;
		}

		TSharedRef<FJsonObject> MakeSectionInfo(UMovieSceneSection* Section, const int32 Index)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetNumberField(TEXT("index"), Index);
			Item->SetStringField(TEXT("path"), Section->GetPathName());
			Item->SetStringField(TEXT("classPath"), Section->GetClass()->GetPathName());
			const TRange<FFrameNumber> Range = Section->GetRange();
			if (Range.HasLowerBound())
			{
				Item->SetNumberField(TEXT("startFrame"), Range.GetLowerBoundValue().Value);
			}
			if (Range.HasUpperBound())
			{
				Item->SetNumberField(TEXT("endFrame"), Range.GetUpperBoundValue().Value);
			}
			return Item;
		}

		TSharedRef<FJsonObject> MakeTrackInfo(UMovieSceneTrack* Track, const int32 Index)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetNumberField(TEXT("index"), Index);
			Item->SetStringField(TEXT("path"), Track->GetPathName());
			Item->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
			Item->SetStringField(TEXT("classPath"), Track->GetClass()->GetPathName());
			TArray<TSharedPtr<FJsonValue>> Sections;
			const TArray<UMovieSceneSection*>& Source = Track->GetAllSections();
			for (int32 SectionIndex = 0; SectionIndex < Source.Num(); ++SectionIndex)
			{
				if (Source[SectionIndex])
				{
					Sections.Add(MakeShared<FJsonValueObject>(MakeSectionInfo(Source[SectionIndex], SectionIndex)));
				}
			}
			Item->SetNumberField(TEXT("sectionCount"), Sections.Num());
			Item->SetArrayField(TEXT("sections"), Sections);
			return Item;
		}
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Sequencer(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_sequence"))
		{
			const FString Name = GetString(Args, { TEXT("name") });
			FString PackagePath = GetString(Args, { TEXT("packagePath"), TEXT("directory") });
			if (PackagePath.IsEmpty())
			{
				PackagePath = TEXT("/Game/Cinematics");
			}
			if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || !PackagePath.StartsWith(TEXT("/Game")))
			{
				return ErrorJson(TEXT("Sequence 名称或 /Game 包路径无效。"));
			}
			const FString PackageName = PackagePath / Name;
			if (!FPackageName::IsValidLongPackageName(PackageName))
			{
				return ErrorJson(TEXT("Level Sequence 包名无效。"));
			}
			const FString ObjectPath = PackageName + TEXT(".") + Name;
			if (ULevelSequence* Existing = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *ObjectPath)))
			{
				const FString Conflict = GetString(Args, { TEXT("onConflict") });
				if (Conflict.Equals(TEXT("error"), ESearchCase::IgnoreCase))
				{
					return ErrorJson(TEXT("Level Sequence 已存在。"));
				}
				TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("existed"), true);
				Result->SetStringField(TEXT("assetPath"), Existing->GetPathName());
				return SuccessJson(Result);
			}
			UPackage* Package = CreatePackage(*PackageName);
			ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			if (!Sequence)
			{
				return ErrorJson(TEXT("Level Sequence 创建失败。"));
			}
			Sequence->Initialize();
			FAssetRegistryModule::AssetCreated(Sequence);
			Package->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("created"), true);
			Result->SetStringField(TEXT("assetPath"), Sequence->GetPathName());
			Result->SetStringField(TEXT("packageName"), PackageName);
			return SuccessJson(Result);
		}

		FString Error;
		ULevelSequence* Sequence = LoadLevelSequence(Args, Error);
		if (!Sequence || !Sequence->GetMovieScene())
		{
			return ErrorJson(!Error.IsEmpty() ? Error : TEXT("Level Sequence 没有 MovieScene。"));
		}
		UMovieScene* MovieScene = Sequence->GetMovieScene();

		if (Action == TEXT("get_sequence_info"))
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Sequence->GetPathName());
			const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
			TSharedRef<FJsonObject> Rate = MakeShared<FJsonObject>();
			Rate->SetNumberField(TEXT("numerator"), DisplayRate.Numerator);
			Rate->SetNumberField(TEXT("denominator"), DisplayRate.Denominator);
			Result->SetObjectField(TEXT("displayRate"), Rate);
			const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
			TSharedRef<FJsonObject> Range = MakeShared<FJsonObject>();
			if (PlaybackRange.HasLowerBound())
			{
				Range->SetNumberField(TEXT("startFrame"), PlaybackRange.GetLowerBoundValue().Value);
			}
			if (PlaybackRange.HasUpperBound())
			{
				Range->SetNumberField(TEXT("endFrame"), PlaybackRange.GetUpperBoundValue().Value);
			}
			Result->SetObjectField(TEXT("playbackRange"), Range);

			TArray<TSharedPtr<FJsonValue>> Tracks;
			const TArray<UMovieSceneTrack*> SourceTracks = CollectSequenceTracks(MovieScene);
			for (int32 Index = 0; Index < SourceTracks.Num(); ++Index)
			{
				if (SourceTracks[Index])
				{
					Tracks.Add(MakeShared<FJsonValueObject>(MakeTrackInfo(SourceTracks[Index], Index)));
				}
			}
			Result->SetNumberField(TEXT("trackCount"), Tracks.Num());
			Result->SetArrayField(TEXT("tracks"), Tracks);

			TArray<TSharedPtr<FJsonValue>> Bindings;
			for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
			{
				const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
				TSharedRef<FJsonObject> Binding = MakeShared<FJsonObject>();
				Binding->SetStringField(TEXT("name"), Possessable.GetName());
				Binding->SetStringField(TEXT("guid"), Possessable.GetGuid().ToString());
				Binding->SetStringField(TEXT("type"), TEXT("possessable"));
				Bindings.Add(MakeShared<FJsonValueObject>(Binding));
			}
			Result->SetNumberField(TEXT("bindingCount"), Bindings.Num());
			Result->SetArrayField(TEXT("bindings"), Bindings);
			return SuccessJson(Result);
		}

		if (Action == TEXT("add_sequence_track"))
		{
			const FString TrackType = GetString(Args, { TEXT("trackType"), TEXT("trackClass") });
			UClass* TrackClass = ResolveSequenceTrackClass(TrackType);
			if (!TrackClass)
			{
				return ErrorJson(TEXT("不支持的轨道类型；支持 Transform、Float、Double、"
									  "SkeletalAnimation、CameraCut、Audio、Event、Fade。"));
			}
			MovieScene->Modify();
			UMovieSceneTrack* Track = nullptr;
			FGuid BindingGuid;
			const FString ActorLabel = GetString(Args, { TEXT("actorLabel") });
			if (!ActorLabel.IsEmpty())
			{
				UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				AActor* Actor = FindSequenceActor(World, ActorLabel);
				if (!Actor)
				{
					return ErrorJson(TEXT("未找到待绑定 Actor。"));
				}
				for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
				{
					const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
					if (Possessable.GetName().Equals(ActorLabel, ESearchCase::IgnoreCase) || Possessable.GetName().Equals(Actor->GetName(), ESearchCase::IgnoreCase))
					{
						BindingGuid = Possessable.GetGuid();
						break;
					}
				}
				if (!BindingGuid.IsValid())
				{
					BindingGuid = MovieScene->AddPossessable(ActorLabel, Actor->GetClass());
					Sequence->BindPossessableObject(BindingGuid, *Actor, World);
				}
				Track = MovieScene->FindTrack(TrackClass, BindingGuid);
				if (!Track)
				{
					Track = MovieScene->AddTrack(TrackClass, BindingGuid);
				}
			}
			else
			{
				for (UMovieSceneTrack* Existing : MovieScene->GetTracks())
				{
					if (Existing && Existing->IsA(TrackClass))
					{
						Track = Existing;
						break;
					}
				}
				if (!Track)
				{
					Track = MovieScene->AddTrack(TrackClass);
				}
			}
			if (!Track)
			{
				return ErrorJson(TEXT("MovieScene 拒绝创建该轨道。"));
			}
			if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track))
			{
				const FString PropertyName = GetString(Args, { TEXT("propertyName") });
				if (!PropertyName.IsEmpty())
				{
					FString PropertyPath = GetString(Args, { TEXT("propertyPath") });
					if (PropertyPath.IsEmpty())
					{
						PropertyPath = PropertyName;
					}
					PropertyTrack->SetPropertyNameAndPath(FName(*PropertyName), PropertyPath);
				}
			}
			Sequence->GetOutermost()->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("trackPath"), Track->GetPathName());
			Result->SetStringField(TEXT("trackClass"), Track->GetClass()->GetPathName());
			Result->SetStringField(TEXT("bindingGuid"), BindingGuid.ToString());
			return SuccessJson(Result);
		}

		if (Action == TEXT("add_sequence_section"))
		{
			UMovieSceneTrack* Track = ResolveSequenceTrack(MovieScene, Args, Error);
			if (!Track)
			{
				return ErrorJson(Error);
			}
			Track->Modify();
			UMovieSceneSection* Section = Track->CreateNewSection();
			if (!Section)
			{
				return ErrorJson(TEXT("轨道无法创建新区段。"));
			}
			double StartNumber = 0.0;
			double EndNumber = 120.0;
			Args->TryGetNumberField(TEXT("startFrame"), StartNumber);
			Args->TryGetNumberField(TEXT("endFrame"), EndNumber);
			const int32 Start = static_cast<int32>(StartNumber);
			const int32 End = static_cast<int32>(EndNumber);
			if (End <= Start)
			{
				return ErrorJson(TEXT("endFrame 必须大于 startFrame。"));
			}
			Section->SetRange(TRange<FFrameNumber>(FFrameNumber(Start), FFrameNumber(End)));
			Track->AddSection(*Section);
			Sequence->GetOutermost()->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeSectionInfo(Section, Track->GetAllSections().IndexOfByKey(Section));
			Result->SetStringField(TEXT("trackPath"), Track->GetPathName());
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_sequence_keyframes"))
		{
			UMovieSceneTrack* Track = ResolveSequenceTrack(MovieScene, Args, Error);
			UMovieSceneSection* Section = Track ? ResolveSequenceSection(Track, Args, Error) : nullptr;
			if (!Track || !Section)
			{
				return ErrorJson(Error);
			}
			const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
			if (!Args->TryGetArrayField(TEXT("keys"), Keys) || !Keys)
			{
				return ErrorJson(TEXT("缺少 keys 数组。"));
			}
			const FString ChannelName = GetString(Args, { TEXT("channel"), TEXT("channelName") });
			double ChannelIndexNumber = 0.0;
			Args->TryGetNumberField(TEXT("channelIndex"), ChannelIndexNumber);
			FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
			FMovieSceneDoubleChannel* DoubleChannel = !ChannelName.IsEmpty() ? Proxy.GetChannelByName<FMovieSceneDoubleChannel>(FName(*ChannelName)).Get()
																			 : Proxy.GetChannel<FMovieSceneDoubleChannel>(static_cast<int32>(ChannelIndexNumber));
			FMovieSceneFloatChannel* FloatChannel = !ChannelName.IsEmpty() ? Proxy.GetChannelByName<FMovieSceneFloatChannel>(FName(*ChannelName)).Get()
																		   : Proxy.GetChannel<FMovieSceneFloatChannel>(static_cast<int32>(ChannelIndexNumber));
			if (!DoubleChannel && !FloatChannel)
			{
				return ErrorJson(TEXT("未找到指定的 Float/Double 通道。"));
			}
			Section->Modify();
			int32 Added = 0;
			for (const TSharedPtr<FJsonValue>& KeyValue : *Keys)
			{
				const TSharedPtr<FJsonObject>* Key = nullptr;
				if (!KeyValue.IsValid() || !KeyValue->TryGetObject(Key) || !Key)
				{
					continue;
				}
				double FrameNumber = 0.0;
				double ValueNumber = 0.0;
				if (!(*Key)->TryGetNumberField(TEXT("frame"), FrameNumber) || !(*Key)->TryGetNumberField(TEXT("value"), ValueNumber))
				{
					continue;
				}
				if (DoubleChannel)
				{
					DoubleChannel->GetData().AddKey(FFrameNumber(static_cast<int32>(FrameNumber)), FMovieSceneDoubleValue(ValueNumber));
				}
				else
				{
					FloatChannel->GetData().AddKey(FFrameNumber(static_cast<int32>(FrameNumber)), FMovieSceneFloatValue(static_cast<float>(ValueNumber)));
				}
				++Added;
			}
			if (Added == 0)
			{
				return ErrorJson(TEXT("keys 中没有有效关键帧。"));
			}
			Sequence->GetOutermost()->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("sectionPath"), Section->GetPathName());
			Result->SetStringField(TEXT("channel"), ChannelName);
			Result->SetNumberField(TEXT("added"), Added);
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_sequence_playback_range"))
		{
			double StartNumber = 0.0;
			double EndNumber = 0.0;
			if (!Args->TryGetNumberField(TEXT("startFrame"), StartNumber) || !Args->TryGetNumberField(TEXT("endFrame"), EndNumber))
			{
				return ErrorJson(TEXT("缺少 startFrame 或 endFrame。"));
			}
			const int32 Start = static_cast<int32>(StartNumber);
			const int32 End = static_cast<int32>(EndNumber);
			if (End <= Start)
			{
				return ErrorJson(TEXT("endFrame 必须大于 startFrame。"));
			}
			MovieScene->Modify();
			MovieScene->SetPlaybackRange(FFrameNumber(Start), End - Start);
			Sequence->GetOutermost()->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("startFrame"), Start);
			Result->SetNumberField(TEXT("endFrame"), End);
			return SuccessJson(Result);
		}

		if (Action == TEXT("play_sequence"))
		{
			UWorld* World = GEditor ? (GEditor->PlayWorld ? GEditor->PlayWorld.Get() : GEditor->GetEditorWorldContext().World()) : nullptr;
			if (!World)
			{
				return ErrorJson(TEXT("没有可播放 Sequence 的世界。"));
			}
			FString Operation = GetString(Args, { TEXT("sequenceAction"), TEXT("operation"), TEXT("command") });
			if (Operation.IsEmpty())
			{
				Operation = TEXT("play");
			}
			Operation.ToLowerInline();
			ALevelSequenceActor* SequenceActor = nullptr;
			for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
			{
				if (It->GetSequence() == Sequence)
				{
					SequenceActor = *It;
					break;
				}
			}
			ULevelSequencePlayer* Player = SequenceActor ? SequenceActor->GetSequencePlayer() : nullptr;
			if (!Player && Operation == TEXT("play"))
			{
				FMovieSceneSequencePlaybackSettings Settings;
				bool bLoop = false;
				Args->TryGetBoolField(TEXT("loop"), bLoop);
				Settings.LoopCount.Value = bLoop ? -1 : 0;
				Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
			}
			if (!Player)
			{
				return ErrorJson(TEXT("未找到或无法创建 LevelSequencePlayer。"));
			}
			if (Operation == TEXT("play"))
			{
				Player->Play();
			}
			else if (Operation == TEXT("pause"))
			{
				Player->Pause();
			}
			else if (Operation == TEXT("stop"))
			{
				Player->Stop();
			}
			else
			{
				return ErrorJson(TEXT("sequenceAction 必须是 play、pause 或 stop。"));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("operation"), Operation);
			Result->SetStringField(TEXT("actorPath"), SequenceActor ? SequenceActor->GetPathName() : FString());
			Result->SetBoolField(TEXT("playing"), Player->IsPlaying());
			return SuccessJson(Result);
		}

		return ErrorJson(TEXT("Sequencer 动作没有有效实现分支。"));
	}
}
