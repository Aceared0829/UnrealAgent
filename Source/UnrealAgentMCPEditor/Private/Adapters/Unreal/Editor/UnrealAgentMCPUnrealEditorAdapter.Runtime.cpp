// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Runtime.cpp
 * @brief PIE 生命周期、运行时对象读取与时间缩放操作。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Reflection/UnrealAgentMCPPropertyTypeAdapter.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedRef<FJsonObject> VectorJson(const FVector& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("x"), Value.X);
			Result->SetNumberField(TEXT("y"), Value.Y);
			Result->SetNumberField(TEXT("z"), Value.Z);
			return Result;
		}

		TSharedRef<FJsonObject> RotatorJson(const FRotator& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("pitch"), Value.Pitch);
			Result->SetNumberField(TEXT("yaw"), Value.Yaw);
			Result->SetNumberField(TEXT("roll"), Value.Roll);
			return Result;
		}

		TSharedRef<FJsonObject> TransformJson(const FTransform& Value)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("location"), VectorJson(Value.GetLocation()));
			Result->SetObjectField(TEXT("rotation"), RotatorJson(Value.Rotator()));
			Result->SetObjectField(TEXT("scale"), VectorJson(Value.GetScale3D()));
			return Result;
		}

		bool ReadRuntimeVector(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FVector& OutValue)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args.IsValid() || !Args->TryGetObjectField(Field, Object) || !Object)
			{
				return false;
			}
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			(*Object)->TryGetNumberField(TEXT("x"), X);
			(*Object)->TryGetNumberField(TEXT("y"), Y);
			(*Object)->TryGetNumberField(TEXT("z"), Z);
			OutValue = FVector(X, Y, Z);
			return true;
		}

		bool ReadRuntimeRotator(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FRotator& OutValue)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Args.IsValid() || !Args->TryGetObjectField(Field, Object) || !Object)
			{
				return false;
			}
			double Pitch = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			(*Object)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*Object)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*Object)->TryGetNumberField(TEXT("roll"), Roll);
			OutValue = FRotator(Pitch, Yaw, Roll);
			return true;
		}

		TArray<FString> ReadStringArray(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names)
		{
			TArray<FString> Result;
			if (!Args.IsValid())
			{
				return Result;
			}
			for (const TCHAR* Name : Names)
			{
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (!Args->TryGetArrayField(Name, Values) || !Values)
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					FString Text;
					if (Value.IsValid() && Value->TryGetString(Text) && !Text.IsEmpty())
					{
						Result.Add(Text);
					}
				}
				break;
			}
			return Result;
		}

		FString ResolvePieSettingName(const FString& Input)
		{
			static const TMap<FString, FString> Aliases{ { TEXT("numPlayers"), TEXT("PlayNumberOfClients") }, { TEXT("numberOfClients"), TEXT("PlayNumberOfClients") },
				{ TEXT("dedicatedServer"), TEXT("bLaunchSeparateServer") }, { TEXT("runUnderOneProcess"), TEXT("RunUnderOneProcess") },
				{ TEXT("clientWindowWidth"), TEXT("ClientWindowWidth") }, { TEXT("clientWindowHeight"), TEXT("ClientWindowHeight") } };
			if (const FString* Mapped = Aliases.Find(Input))
			{
				return *Mapped;
			}
			return Input;
		}

		bool IsActorClassMatch(const AActor* Actor, const FString& ClassFilter)
		{
			if (!Actor || ClassFilter.IsEmpty())
			{
				return Actor != nullptr;
			}
			for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
			{
				if (Class->GetName().Equals(ClassFilter, ESearchCase::IgnoreCase) || Class->GetPathName().Equals(ClassFilter, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		TSharedPtr<FJsonValue> ReadActorValue(AActor* Actor, const FString& PropertyPath, FString& OutError)
		{
			if (PropertyPath.Equals(TEXT("location"), ESearchCase::IgnoreCase))
			{
				return MakeShared<FJsonValueObject>(VectorJson(Actor->GetActorLocation()));
			}
			if (PropertyPath.Equals(TEXT("rotation"), ESearchCase::IgnoreCase))
			{
				return MakeShared<FJsonValueObject>(RotatorJson(Actor->GetActorRotation()));
			}
			if (PropertyPath.Equals(TEXT("scale"), ESearchCase::IgnoreCase))
			{
				return MakeShared<FJsonValueObject>(VectorJson(Actor->GetActorScale3D()));
			}
			if (PropertyPath.Equals(TEXT("name"), ESearchCase::IgnoreCase))
			{
				return MakeShared<FJsonValueString>(Actor->GetName());
			}
			if (PropertyPath.Equals(TEXT("actorLabel"), ESearchCase::IgnoreCase))
			{
				return MakeShared<FJsonValueString>(Actor->GetActorLabel());
			}
			if (PropertyPath.Equals(TEXT("classPath"), ESearchCase::IgnoreCase))
			{
				return MakeShared<FJsonValueString>(Actor->GetClass()->GetPathName());
			}

			FProperty* Property = FindFProperty<FProperty>(Actor->GetClass(), *PropertyPath);
			if (!Property)
			{
				OutError = FString::Printf(TEXT("未找到属性：%s"), *PropertyPath);
				return nullptr;
			}
			TSharedPtr<FJsonValue> Value;
			const void* Address = Property->ContainerPtrToValuePtr<void>(Actor);
			if (!Reflection::FPropertyTypeAdapterRegistry::GetDefault().Write(Property, Address, Value, OutError))
			{
				return nullptr;
			}
			return Value;
		}
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Runtime(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (!GEditor)
		{
			return ErrorJson(TEXT("Unreal Editor 当前不可用。"));
		}

		if (Action == TEXT("play_in_editor"))
		{
			FString Mode = GetString(Args, { TEXT("mode"), TEXT("operation") });
			if (Mode.IsEmpty())
			{
				Mode = GEditor->PlayWorld ? TEXT("status") : TEXT("start");
			}
			Mode.ToLowerInline();
			if (Mode == TEXT("start"))
			{
				if (GEditor->PlayWorld)
				{
					return ErrorJson(TEXT("PIE 已经在运行。"));
				}
				FRequestPlaySessionParams Parameters;
				GEditor->RequestPlaySession(Parameters);
			}
			else if (Mode == TEXT("stop"))
			{
				if (!GEditor->PlayWorld)
				{
					return ErrorJson(TEXT("PIE 当前没有运行。"));
				}
				GEditor->RequestEndPlayMap();
			}
			else if (Mode == TEXT("pause"))
			{
				GEditor->SetPIEWorldsPaused(true);
			}
			else if (Mode == TEXT("resume"))
			{
				GEditor->SetPIEWorldsPaused(false);
			}
			else if (Mode != TEXT("status"))
			{
				return ErrorJson(TEXT("不支持的 PIE mode。"));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("mode"), Mode);
			Result->SetBoolField(TEXT("running"), GEditor->PlayWorld != nullptr || Mode == TEXT("start"));
			return SuccessJson(Result);
		}

		if (Action == TEXT("list_pie_instances"))
		{
			TArray<TSharedPtr<FJsonValue>> Instances;
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::PIE)
					{
						continue;
					}
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetNumberField(TEXT("pieInstance"), Context.PIEInstance);
					Item->SetStringField(TEXT("contextHandle"), Context.ContextHandle.ToString());
					Item->SetStringField(TEXT("worldPath"), Context.World() ? Context.World()->GetPathName() : FString());
					Instances.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Instances.Num());
			Result->SetArrayField(TEXT("instances"), Instances);
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_pie_config") || Action == TEXT("configure_pie"))
		{
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			if (!Settings)
			{
				return ErrorJson(TEXT("PIE 配置对象当前不可用。"));
			}

			TArray<TSharedPtr<FJsonValue>> Changed;
			if (Action == TEXT("configure_pie"))
			{
				const TSharedPtr<FJsonObject>* Requested = nullptr;
				const TSharedPtr<FJsonObject>& Values = Args->TryGetObjectField(TEXT("settings"), Requested) && Requested ? *Requested : Args;
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values->Values)
				{
					if (Pair.Key == TEXT("action") || Pair.Key == TEXT("settings"))
					{
						continue;
					}
					const FString PropertyName = ResolvePieSettingName(Pair.Key);
					FProperty* Property = FindFProperty<FProperty>(Settings->GetClass(), *PropertyName);
					if (!Property || !Property->HasAnyPropertyFlags(CPF_Config))
					{
						return ErrorJson(FString::Printf(TEXT("未找到可配置的 PIE 属性：%s"), *Pair.Key));
					}
					FString Error;
					void* Address = Property->ContainerPtrToValuePtr<void>(Settings);
					if (!Reflection::FPropertyTypeAdapterRegistry::GetDefault().Read(Pair.Value, Property, Address, Error))
					{
						return ErrorJson(FString::Printf(TEXT("PIE 属性 %s 写入失败：%s"), *Pair.Key, *Error));
					}
					Changed.Add(MakeShared<FJsonValueString>(PropertyName));
				}
				Settings->PostEditChange();
				Settings->SaveConfig();
			}

			TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
			{
				FProperty* Property = *It;
				if (!Property->HasAnyPropertyFlags(CPF_Config))
				{
					continue;
				}
				TSharedPtr<FJsonValue> Value;
				FString Error;
				const void* Address = Property->ContainerPtrToValuePtr<void>(Settings);
				if (Reflection::FPropertyTypeAdapterRegistry::GetDefault().Write(Property, Address, Value, Error) && Value.IsValid())
				{
					Config->SetField(Property->GetName(), Value);
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("settings"), Config);
			Result->SetArrayField(TEXT("changed"), Changed);
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_runtime_values"))
		{
			UWorld* World = GEditor->PlayWorld ? GEditor->PlayWorld.Get() : GEditor->GetEditorWorldContext().World();
			if (!World)
			{
				return ErrorJson(TEXT("没有可读取的编辑器或 PIE 世界。"));
			}
			TArray<FString> Properties = ReadStringArray(Args, { TEXT("properties"), TEXT("propertyPaths"), TEXT("paths") });
			if (Properties.IsEmpty())
			{
				const FString Single = GetString(Args, { TEXT("propertyName"), TEXT("property") });
				if (!Single.IsEmpty())
				{
					Properties.Add(Single);
				}
			}
			if (Properties.IsEmpty())
			{
				return ErrorJson(TEXT("缺少 properties 或 propertyName。"));
			}

			const FString ClassFilter = GetString(Args, { TEXT("classFilter"), TEXT("className") });
			double LimitNumber = 100.0;
			Args->TryGetNumberField(TEXT("limit"), LimitNumber);
			const int32 Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 5000);
			TArray<TSharedPtr<FJsonValue>> Rows;
			for (TActorIterator<AActor> It(World); It && Rows.Num() < Limit; ++It)
			{
				AActor* Actor = *It;
				if (!IsActorClassMatch(Actor, ClassFilter))
				{
					continue;
				}
				TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("objectPath"), Actor->GetPathName());
				Row->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
				TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
				TArray<TSharedPtr<FJsonValue>> Errors;
				for (const FString& Property : Properties)
				{
					FString Error;
					TSharedPtr<FJsonValue> Value = ReadActorValue(Actor, Property, Error);
					if (Value.IsValid())
					{
						Values->SetField(Property, Value);
					}
					else
					{
						Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s：%s"), *Property, *Error)));
					}
				}
				Row->SetObjectField(TEXT("values"), Values);
				Row->SetArrayField(TEXT("errors"), Errors);
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("world"), World->GetPathName());
			Result->SetBoolField(TEXT("playInEditor"), World->WorldType == EWorldType::PIE);
			Result->SetNumberField(TEXT("count"), Rows.Num());
			Result->SetArrayField(TEXT("rows"), Rows);
			return SuccessJson(Result);
		}

		UWorld* PlayWorld = GEditor->PlayWorld.Get();
		if (!PlayWorld)
		{
			return ErrorJson(TEXT("需要先启动 PIE。"));
		}

		if (Action == TEXT("set_pie_time_scale"))
		{
			double Factor = 1.0;
			Args->TryGetNumberField(TEXT("factor"), Factor);
			if (Factor <= 0.0 || Factor > 10000.0)
			{
				return ErrorJson(TEXT("factor 必须位于 (0, 10000]。"));
			}
			UGameplayStatics::SetGlobalTimeDilation(PlayWorld, static_cast<float>(Factor));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("factor"), Factor);
			return SuccessJson(Result);
		}

		if (Action == TEXT("read_bone_transforms"))
		{
			FString Error;
			UObject* Target = ResolveObject(Args, true, Error);
			AActor* Actor = Cast<AActor>(Target);
			USkeletalMeshComponent* Mesh = nullptr;
			if (ASkeletalMeshActor* SkeletalActor = Cast<ASkeletalMeshActor>(Actor))
			{
				Mesh = SkeletalActor->GetSkeletalMeshComponent();
			}
			if (!Mesh && Actor)
			{
				Mesh = Actor->FindComponentByClass<USkeletalMeshComponent>();
			}
			if (!Mesh)
			{
				return ErrorJson(Actor ? TEXT("目标 Actor 没有 SkeletalMeshComponent。") : Error);
			}

			TArray<FName> BoneNames;
			Mesh->GetBoneNames(BoneNames);
			const TArray<FString> Requested = ReadStringArray(Args, { TEXT("bones"), TEXT("boneNames") });
			const TSet<FString> RequestedSet(Requested);
			TArray<TSharedPtr<FJsonValue>> Bones;
			for (const FName BoneName : BoneNames)
			{
				if (!RequestedSet.IsEmpty() && !RequestedSet.Contains(BoneName.ToString()))
				{
					continue;
				}
				const int32 BoneIndex = Mesh->GetBoneIndex(BoneName);
				if (BoneIndex == INDEX_NONE)
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), BoneName.ToString());
				Item->SetNumberField(TEXT("index"), BoneIndex);
				Item->SetObjectField(TEXT("worldTransform"), TransformJson(Mesh->GetBoneTransform(BoneIndex)));
				Bones.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("componentPath"), Mesh->GetPathName());
			Result->SetNumberField(TEXT("count"), Bones.Num());
			Result->SetArrayField(TEXT("bones"), Bones);
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_movement_mode"))
		{
			FString Error;
			ACharacter* Character = Cast<ACharacter>(ResolveObject(Args, true, Error));
			UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
			if (!Movement)
			{
				return ErrorJson(Character ? TEXT("目标 Character 没有移动组件。") : Error);
			}
			FString ModeText = GetString(Args, { TEXT("movementMode"), TEXT("mode") });
			ModeText.ToLowerInline();
			static const TMap<FString, EMovementMode> Modes{ { TEXT("none"), MOVE_None }, { TEXT("walking"), MOVE_Walking }, { TEXT("navwalking"), MOVE_NavWalking },
				{ TEXT("falling"), MOVE_Falling }, { TEXT("swimming"), MOVE_Swimming }, { TEXT("flying"), MOVE_Flying }, { TEXT("custom"), MOVE_Custom } };
			const EMovementMode* Mode = Modes.Find(ModeText);
			if (!Mode)
			{
				return ErrorJson(TEXT("不支持的 movementMode。"));
			}
			double CustomMode = 0.0;
			Args->TryGetNumberField(TEXT("customMode"), CustomMode);
			Movement->SetMovementMode(*Mode, static_cast<uint8>(FMath::Clamp(static_cast<int32>(CustomMode), 0, 255)));
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("movementMode"), ModeText);
			Result->SetNumberField(TEXT("customMode"), Movement->CustomMovementMode);
			return SuccessJson(Result);
		}

		if (Action == TEXT("teleport_runtime_actor"))
		{
			FString Error;
			AActor* Actor = Cast<AActor>(ResolveObject(Args, true, Error));
			if (!Actor)
			{
				return ErrorJson(Error);
			}
			FVector Location = Actor->GetActorLocation();
			FRotator Rotation = Actor->GetActorRotation();
			const bool bHasLocation = ReadRuntimeVector(Args, TEXT("location"), Location);
			const bool bHasRotation = ReadRuntimeRotator(Args, TEXT("rotation"), Rotation);
			if (!bHasLocation && !bHasRotation)
			{
				return ErrorJson(TEXT("缺少 location 或 rotation。"));
			}
			const bool bTeleported = Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("teleported"), bTeleported);
			Result->SetObjectField(TEXT("location"), VectorJson(Location));
			Result->SetObjectField(TEXT("rotation"), RotatorJson(Rotation));
			return bTeleported ? SuccessJson(Result) : ErrorJson(TEXT("运行时 Actor 传送失败。"));
		}

		if (Action == TEXT("pie_set_player_view"))
		{
			FString Error;
			AActor* Target = Cast<AActor>(ResolveObject(Args, true, Error));
			double PlayerIndexNumber = 0.0;
			Args->TryGetNumberField(TEXT("playerIndex"), PlayerIndexNumber);
			APlayerController* Controller = UGameplayStatics::GetPlayerController(PlayWorld, FMath::Max(0, static_cast<int32>(PlayerIndexNumber)));
			if (!Target || !Controller)
			{
				return ErrorJson(!Target ? Error : TEXT("未找到指定 PIE 玩家控制器。"));
			}
			Controller->SetViewTarget(Target);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("target"), Target->GetPathName());
			Result->SetNumberField(TEXT("playerIndex"), PlayerIndexNumber);
			return SuccessJson(Result);
		}

		if (Action == TEXT("stage_game_input"))
		{
			double PlayerIndexNumber = 0.0;
			double AmountNumber = 1.0;
			Args->TryGetNumberField(TEXT("playerIndex"), PlayerIndexNumber);
			Args->TryGetNumberField(TEXT("amount"), AmountNumber);
			APlayerController* Controller = UGameplayStatics::GetPlayerController(PlayWorld, FMath::Max(0, static_cast<int32>(PlayerIndexNumber)));
			const FKey Key(FName(*GetString(Args, { TEXT("key"), TEXT("keyName") })));
			if (!Controller || !Key.IsValid())
			{
				return ErrorJson(!Controller ? TEXT("未找到指定 PIE 玩家控制器。") : TEXT("无效的输入按键。"));
			}
			FString EventText = GetString(Args, { TEXT("event"), TEXT("inputEvent") });
			EventText.ToLowerInline();
			EInputEvent Event = IE_Pressed;
			if (EventText == TEXT("released") || EventText == TEXT("release"))
			{
				Event = IE_Released;
			}
			else if (EventText == TEXT("repeat"))
			{
				Event = IE_Repeat;
			}
			else if (EventText == TEXT("doubleclick"))
			{
				Event = IE_DoubleClick;
			}
			else if (!EventText.IsEmpty() && EventText != TEXT("pressed") && EventText != TEXT("press"))
			{
				return ErrorJson(TEXT("不支持的 inputEvent。"));
			}
			FInputKeyEventArgs Input = FInputKeyEventArgs::CreateSimulated(Key, Event, static_cast<float>(AmountNumber), -1,
				FInputDeviceId::CreateFromInternalId(FMath::Max(0, static_cast<int32>(PlayerIndexNumber))));
			const bool bHandled = Controller->InputKey(Input);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("key"), Key.GetFName().ToString());
			Result->SetStringField(TEXT("event"), EventText);
			Result->SetBoolField(TEXT("handled"), bHandled);
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_pie_pawn"))
		{
			APlayerController* Controller = PlayWorld->GetFirstPlayerController();
			APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (!Pawn)
			{
				return ErrorJson(TEXT("当前 PIE 没有可用 Pawn。"));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("objectPath"), Pawn->GetPathName());
			Result->SetStringField(TEXT("name"), Pawn->GetName());
			Result->SetStringField(TEXT("classPath"), Pawn->GetClass()->GetPathName());
			Result->SetObjectField(TEXT("location"), VectorJson(Pawn->GetActorLocation()));
			Result->SetObjectField(TEXT("rotation"), RotatorJson(Pawn->GetActorRotation()));
			return SuccessJson(Result);
		}

		FString Error;
		UObject* Target = ResolveObject(Args, true, Error);
		if (!Target)
		{
			return ErrorJson(Error);
		}
		const FString PropertyName = GetString(Args, { TEXT("propertyName"), TEXT("property") });
		FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
		if (!Property)
		{
			return ErrorJson(TEXT("未找到运行时属性。"));
		}
		TSharedPtr<FJsonValue> Value;
		const void* Address = Property->ContainerPtrToValuePtr<void>(Target);
		if (!Reflection::FPropertyTypeAdapterRegistry::GetDefault().Write(Property, Address, Value, Error) || !Value.IsValid())
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("objectPath"), Target->GetPathName());
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		Result->SetField(TEXT("value"), Value);
		return SuccessJson(Result);
	}
}
