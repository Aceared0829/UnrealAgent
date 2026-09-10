// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.Cue.cpp
 * @brief SoundCue 创建、节点编排、连线、查询与批量创作。
 */

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Factories/SoundCueFactoryNew.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"

namespace UnrealAgentMCP
{
	using namespace AudioPrivate;

	namespace
	{
		USoundCue* RequireCue(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			FString Path;
			OutError = RequireString(Args, TEXT("assetPath"), Path);
			USoundCue* Cue = OutError.IsEmpty() ? Cast<USoundCue>(LoadAsset(Path, USoundCue::StaticClass())) : nullptr;
			if (!Cue && OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("找不到 SoundCue：%s"), *Path);
			}
			return Cue;
		}

		UClass* ResolveSoundNodeClass(const FString& ClassName)
		{
			if (ClassName.IsEmpty())
			{
				return USoundNodeWavePlayer::StaticClass();
			}
			if (UClass* Exact = LoadObject<UClass>(nullptr, *ClassName))
			{
				return Exact->IsChildOf(USoundNode::StaticClass()) ? Exact : nullptr;
			}
			const FString FullName = ClassName.StartsWith(TEXT("SoundNode")) ? ClassName : TEXT("SoundNode") + ClassName;
			UClass* Class = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *FullName));
			return Class && Class->IsChildOf(USoundNode::StaticClass()) ? Class : nullptr;
		}
	}

	FString FUnrealAgentMCPUnrealAudioAdapter::ExecuteCueAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_cue"))
		{
			FString Name;
			if (const FString Error = RequireString(Args, TEXT("name"), Name); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			USoundCueFactoryNew* Factory = NewObject<USoundCueFactoryNew>();
			const FString WavePath = OptionalString(Args, TEXT("soundWavePath"));
			if (!WavePath.IsEmpty())
			{
				if (USoundWave* Wave = Cast<USoundWave>(LoadAsset(WavePath, USoundWave::StaticClass())))
				{
					Factory->InitialSoundWaves.Add(Wave);
				}
			}
			bool bCreated = false;
			FString Error;
			USoundCue* Cue = Cast<USoundCue>(CreateAsset(Name, OptionalString(Args, TEXT("packagePath"), TEXT("/Game/Audio")), USoundCue::StaticClass(), Factory,
				OptionalString(Args, TEXT("onConflict"), TEXT("reuse")), bCreated, Error));
			if (!Cue)
			{
				return Failure(Error);
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Cue->GetPathName());
			Result->SetBoolField(TEXT("created"), bCreated);
			return Serialize(Result);
		}

		if (Action == TEXT("cue_author"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
			if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("operations"), Operations) || !Operations)
			{
				return Failure(TEXT("缺少 operations 数组。"));
			}
			TArray<TSharedPtr<FJsonValue>> Results;
			for (const TSharedPtr<FJsonValue>& Value : *Operations)
			{
				const TSharedPtr<FJsonObject> Operation = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Operation.IsValid())
				{
					continue;
				}
				const TSharedRef<FJsonObject> Routed = MakeShared<FJsonObject>(*Operation);
				if (!Routed->HasField(TEXT("assetPath")) && Args->HasField(TEXT("assetPath")))
				{
					Routed->SetStringField(TEXT("assetPath"), Args->GetStringField(TEXT("assetPath")));
				}
				FString OperationAction;
				Routed->TryGetStringField(TEXT("action"), OperationAction);
				Results.Add(MakeShared<FJsonValueString>(ExecuteCueAction(OperationAction, Routed)));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("results"), Results);
			return Serialize(Result);
		}

		FString Error;
		USoundCue* Cue = RequireCue(Args, Error);
		if (!Cue)
		{
			return Failure(Error);
		}
		if (Action == TEXT("cue_get_graph"))
		{
			TArray<TSharedPtr<FJsonValue>> Nodes;
			for (int32 Index = 0; Index < Cue->AllNodes.Num(); ++Index)
			{
				USoundNode* Node = Cue->AllNodes[Index];
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetNumberField(TEXT("index"), Index);
				Item->SetStringField(TEXT("class"), Node ? Node->GetClass()->GetName() : TEXT("None"));
				TArray<TSharedPtr<FJsonValue>> Children;
				if (Node)
				{
					for (USoundNode* Child : Node->ChildNodes)
					{
						Children.Add(MakeShared<FJsonValueNumber>(Cue->AllNodes.IndexOfByKey(Child)));
					}
				}
				Item->SetArrayField(TEXT("children"), Children);
				Nodes.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("firstNodeIndex"), Cue->AllNodes.IndexOfByKey(Cue->FirstNode));
			Result->SetArrayField(TEXT("nodes"), Nodes);
			return Serialize(Result);
		}

		Cue->Modify();
		if (Action == TEXT("cue_add_node"))
		{
			const FString ClassName = OptionalString(Args, TEXT("nodeClass"), TEXT("SoundNodeWavePlayer"));
			UClass* Class = ResolveSoundNodeClass(ClassName);
			if (!Class)
			{
				return Failure(FString::Printf(TEXT("找不到 SoundNode 类：%s"), *ClassName));
			}
			USoundNode* Node = Cue->ConstructSoundNode<USoundNode>(Class, false);
			if (!Node)
			{
				return Failure(TEXT("创建 SoundNode 失败。"));
			}
			Node->CreateStartingConnectors();
			if (USoundNodeWavePlayer* Player = Cast<USoundNodeWavePlayer>(Node))
			{
				const FString WavePath = OptionalString(Args, TEXT("soundWavePath"));
				Player->SetSoundWave(Cast<USoundWave>(LoadAsset(WavePath, USoundWave::StaticClass())));
			}
			if (!Cue->FirstNode || OptionalBool(Args, TEXT("setAsRoot"), false))
			{
				Cue->FirstNode = Node;
			}
			Cue->LinkGraphNodesFromSoundNodes();
			SaveAsset(Cue);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("nodeIndex"), Cue->AllNodes.IndexOfByKey(Node));
			Result->SetStringField(TEXT("nodeClass"), Class->GetName());
			return Serialize(Result);
		}

		const int32 ParentIndex = OptionalInt(Args, TEXT("fromNodeIndex"), INDEX_NONE);
		const int32 ChildIndex = OptionalInt(Args, TEXT("toNodeIndex"), INDEX_NONE);
		if (!Cue->AllNodes.IsValidIndex(ParentIndex) || !Cue->AllNodes.IsValidIndex(ChildIndex))
		{
			return Failure(TEXT("Cue 连线节点索引无效。"));
		}
		USoundNode* Parent = Cue->AllNodes[ParentIndex];
		const int32 InputIndex = FMath::Max(0, OptionalInt(Args, TEXT("inputIndex"), 0));
		while (Parent->ChildNodes.Num() <= InputIndex)
		{
			Parent->InsertChildNode(Parent->ChildNodes.Num());
		}
		Parent->ChildNodes[InputIndex] = Cue->AllNodes[ChildIndex];
		Cue->LinkGraphNodesFromSoundNodes();
		SaveAsset(Cue);
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetNumberField(TEXT("fromNodeIndex"), ParentIndex);
		Result->SetNumberField(TEXT("toNodeIndex"), ChildIndex);
		return Serialize(Result);
	}
}
