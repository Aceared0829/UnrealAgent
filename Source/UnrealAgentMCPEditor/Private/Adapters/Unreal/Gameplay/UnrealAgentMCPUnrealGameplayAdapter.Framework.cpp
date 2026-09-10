// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.Framework.cpp
 * @brief GameMode、GameState、Controller、PlayerState、HUD 与世界框架实现。
 */

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.h"

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"
#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"

namespace UnrealAgentMCP
{
	using namespace GameplayPrivate;

	FString FUnrealAgentMCPUnrealGameplayAdapter::Framework(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action.StartsWith(TEXT("create_")))
		{
			const FString Parent = Action == TEXT("create_game_mode") ? TEXT("GameModeBase")
				: Action == TEXT("create_game_state")                 ? TEXT("GameStateBase")
				: Action == TEXT("create_player_controller")          ? TEXT("PlayerController")
				: Action == TEXT("create_player_state")               ? TEXT("PlayerState")
																	  : TEXT("HUD");
			TSharedRef<FJsonObject> Forward = MakeShared<FJsonObject>(*Args);
			Forward->SetStringField(TEXT("assetPath"), MakeObjectPath(Args, TEXT("BP_Gameplay")));
			Forward->SetStringField(TEXT("parentClass"), StringArg(Args, { TEXT("parentClass") }, Parent));
			FUnrealAgentMCPUnrealBlueprintAdapter Adapter;
			return Adapter.ExecuteAction(TEXT("create"), Forward);
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		if (!Settings)
			return Error(TEXT("当前编辑器世界没有 WorldSettings。"));
		if (Action == TEXT("set_world_game_mode"))
		{
			FString Path = StringArg(Args, { TEXT("gameModePath") });
			UClass* Class = LoadObject<UClass>(nullptr, *Path);
			if (!Class)
			{
				const FString Package = Path.Contains(TEXT(".")) ? Path : Path + TEXT(".") + FPackageName::GetShortName(Path);
				Class = LoadObject<UClass>(nullptr, *(Package + TEXT("_C")));
			}
			if (!Class || !Class->IsChildOf(AGameModeBase::StaticClass()))
				return Error(TEXT("gameModePath 不是有效的 GameMode 类。"));
			Settings->Modify();
			Settings->DefaultGameMode = Class;
			Settings->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("gameMode"), Class->GetPathName());
			return Success(Result);
		}
		if (Action == TEXT("get_framework_info"))
		{
			UClass* GameMode = Settings->DefaultGameMode;
			const AGameModeBase* Default = GameMode ? Cast<AGameModeBase>(GameMode->GetDefaultObject()) : nullptr;
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("gameMode"), GameMode ? GameMode->GetPathName() : TEXT("None"));
			Result->SetStringField(TEXT("gameState"), Default && Default->GameStateClass ? Default->GameStateClass->GetPathName() : TEXT("None"));
			Result->SetStringField(TEXT("playerController"), Default && Default->PlayerControllerClass ? Default->PlayerControllerClass->GetPathName() : TEXT("None"));
			Result->SetStringField(TEXT("playerState"), Default && Default->PlayerStateClass ? Default->PlayerStateClass->GetPathName() : TEXT("None"));
			Result->SetStringField(TEXT("hud"), Default && Default->HUDClass ? Default->HUDClass->GetPathName() : TEXT("None"));
			return Success(Result);
		}
		return Error(FString::Printf(TEXT("未知 Gameplay 框架操作：%s"), *Action));
	}
}
