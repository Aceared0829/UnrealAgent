// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.cpp
 * @brief 将 Audio 操作分派到资产、试听、MetaSound、SoundCue 与路由实现。
 */

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonValue.h"

namespace UnrealAgentMCP
{
	TArray<FString> FUnrealAgentMCPUnrealAudioAdapter::GetImplementedActions()
	{
		return { TEXT("list"), TEXT("extract_pcm"), TEXT("import_audio"), TEXT("play_at_location"), TEXT("spawn_ambient"), TEXT("metasound_author"), TEXT("create_metasound"),
			TEXT("metasound_list_node_classes"), TEXT("metasound_get_graph"), TEXT("metasound_add_node"), TEXT("metasound_add_input"), TEXT("metasound_add_output"),
			TEXT("metasound_connect"), TEXT("metasound_connect_input"), TEXT("metasound_connect_output"), TEXT("metasound_connect_audio_out"), TEXT("metasound_set_default"),
			TEXT("metasound_build"), TEXT("cue_author"), TEXT("create_cue"), TEXT("cue_add_node"), TEXT("cue_connect"), TEXT("cue_get_graph"), TEXT("create_submix"),
			TEXT("set_submix_parent"), TEXT("add_submix_effect"), TEXT("create_sound_class"), TEXT("create_sound_mix"), TEXT("create_concurrency"), TEXT("create_attenuation"),
			TEXT("set_sound_submix"), TEXT("add_sound_submix_send"), TEXT("set_sound_class"), TEXT("set_sound_attenuation"), TEXT("set_sound_concurrency"), TEXT("set_property") };
	}

	FString FUnrealAgentMCPUnrealAudioAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TArray<FString> Actions = GetImplementedActions();
		if (!Actions.Contains(Action))
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Name : Actions)
			{
				Values.Add(MakeShared<FJsonValueString>(Name));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("domain"), TEXT("audio"));
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Audio action '%s' 尚未迁移。"), *Action));
			Result->SetArrayField(TEXT("implementedActions"), Values);
			return JsonObjectToString(Result);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		return ExecuteAction(Action, SafeArgs);
	}

	FString FUnrealAgentMCPUnrealAudioAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		static const TSet<FString> AssetActions{ TEXT("list"), TEXT("extract_pcm"), TEXT("import_audio"), TEXT("set_property") };
		if (AssetActions.Contains(Action))
		{
			return ExecuteAssetAction(Action, Args);
		}
		if (Action == TEXT("play_at_location") || Action == TEXT("spawn_ambient"))
		{
			return ExecutePlaybackAction(Action, Args);
		}
		if (Action.StartsWith(TEXT("metasound")) || Action == TEXT("create_metasound"))
		{
			return ExecuteMetaSoundAction(Action, Args);
		}
		if (Action.StartsWith(TEXT("cue_")) || Action == TEXT("create_cue"))
		{
			return ExecuteCueAction(Action, Args);
		}
		return ExecuteRoutingAction(Action, Args);
	}
}
