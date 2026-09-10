// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.Environment.cpp
 * @brief Level 的关卡、体积、灯光、雾、虚拟纹理与世界设置实现。
 */

#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "Builders/CubeBuilder.h"
#include "Components/ActorComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorBuildUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Volume.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "UObject/Package.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureVolume.h"

namespace UnrealAgentMCP
{
	namespace
	{
		AActor* FindEnvironmentActor(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			FString Label;
			if (!Args.IsValid() || !Args->TryGetStringField(TEXT("name"), Label) || Label.IsEmpty())
			{
				OutError = TEXT("缺少必填 actorLabel。");
				return nullptr;
			}
			AActor* Actor = ActorSupport::FindActorByNameOrLabel(ActorSupport::GetEditorWorld(), Label);
			if (!Actor)
			{
				OutError = FString::Printf(TEXT("未找到 Actor：%s"), *Label);
			}
			return Actor;
		}

		FString ToMapFilename(FString LevelPath)
		{
			LevelPath.RemoveFromEnd(TEXT(".umap"));
			if (LevelPath.StartsWith(TEXT("/")))
			{
				return FPackageName::LongPackageNameToFilename(LevelPath, FPackageName::GetMapPackageExtension());
			}
			return LevelPath;
		}

		FLinearColor ReadEditorColor(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, const FLinearColor& Default)
		{
			FLinearColor Color = Default;
			if (Args.IsValid())
			{
				JsonConversion::TryValueToLinearColor(Args->TryGetField(Field), Color);
			}
			if (Color.R > 1.0f || Color.G > 1.0f || Color.B > 1.0f || Color.A > 1.0f)
			{
				Color /= 255.0f;
			}
			return Color;
		}

		EComponentMobility::Type ReadMobility(const TSharedPtr<FJsonObject>& Args, const EComponentMobility::Type Default)
		{
			FString Text;
			if (!Args.IsValid() || !Args->TryGetStringField(TEXT("mobility"), Text))
			{
				return Default;
			}
			if (Text.Equals(TEXT("static"), ESearchCase::IgnoreCase))
			{
				return EComponentMobility::Static;
			}
			if (Text.Equals(TEXT("stationary"), ESearchCase::IgnoreCase))
			{
				return EComponentMobility::Stationary;
			}
			return EComponentMobility::Movable;
		}

		TSharedRef<FJsonObject> MakeEnvironmentActor(AActor* Actor)
		{
			return ActorSupport::MakeActorObject(Actor).ToSharedRef();
		}

		bool ApplyPropertyMap(UObject* Target, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
		{
			if (!Target || !Properties.IsValid())
			{
				OutError = TEXT("属性目标或 properties 无效。");
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
			{
				FProperty* Property = Target->GetClass()->FindPropertyByName(*Pair.Key);
				if (!Property)
				{
					OutError = FString::Printf(TEXT("未找到属性：%s"), *Pair.Key);
					return false;
				}
				if (!PropertyWriter::SetPropertyFromJson(Target, Property, Pair.Value, OutError))
				{
					return false;
				}
			}
			return true;
		}

		ULightComponentBase* FindLightComponent(AActor* Actor)
		{
			return Actor ? Actor->FindComponentByClass<ULightComponentBase>() : nullptr;
		}

		void SetLightColor(ULightComponentBase* Component, const FLinearColor& Color)
		{
			if (ULightComponent* Light = Cast<ULightComponent>(Component))
			{
				Light->SetLightColor(Color, false);
			}
			else if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
			{
				Sky->SetLightColor(Color);
			}
		}

		void SetLightIntensity(ULightComponentBase* Component, const float Intensity)
		{
			if (ULightComponent* Light = Cast<ULightComponent>(Component))
			{
				Light->SetIntensity(Intensity);
			}
			else if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
			{
				Sky->SetIntensity(Intensity);
			}
		}
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::LoadLevel(const TSharedPtr<FJsonObject>& Args)
	{
		FString LevelPath;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("levelPath"), LevelPath) || LevelPath.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 levelPath。"));
		}
		UWorld* LoadedWorld = UEditorLoadingAndSavingUtils::LoadMap(ToMapFilename(LevelPath));
		if (!LoadedWorld)
		{
			return ErrorJson(FString::Printf(TEXT("关卡加载失败：%s"), *LevelPath));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("levelPath"), LoadedWorld->GetPathName());
		Result->SetStringField(TEXT("levelName"), LoadedWorld->GetMapName());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::CreateLevel(const TSharedPtr<FJsonObject>& Args)
	{
		FString LevelPath;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("levelPath"), LevelPath);
		}
		if (LevelPath.IsEmpty())
		{
			LevelPath = FString::Printf(TEXT("/Game/Maps/MCP_Level_%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
		}
		LevelPath.RemoveFromEnd(TEXT(".umap"));

		FString TemplateLevel;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("templateLevel"), TemplateLevel);
		}
		UWorld* World = TemplateLevel.IsEmpty() ? GEditor->NewMap(false) : UEditorLoadingAndSavingUtils::NewMapFromTemplate(ToMapFilename(TemplateLevel), false);
		if (!World || !UEditorLoadingAndSavingUtils::SaveMap(World, LevelPath))
		{
			return ErrorJson(FString::Printf(TEXT("关卡创建或保存失败：%s"), *LevelPath));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("levelPath"), World->GetPathName());
		Result->SetStringField(TEXT("levelName"), World->GetMapName());
		Result->SetBoolField(TEXT("fromTemplate"), !TemplateLevel.IsEmpty());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SpawnVolume(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString VolumeType;
		if (!World || !Args.IsValid() || !Args->TryGetStringField(TEXT("volumeType"), VolumeType) || VolumeType.IsEmpty())
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 volumeType。"));
		}
		if (!VolumeType.EndsWith(TEXT("Volume")))
		{
			VolumeType += TEXT("Volume");
		}
		UClass* VolumeClass = ActorSupport::ResolveActorClass(VolumeType);
		if (!VolumeClass || !VolumeClass->IsChildOf(AVolume::StaticClass()))
		{
			return ErrorJson(FString::Printf(TEXT("不是有效的 Volume 类：%s"), *VolumeType));
		}

		FVector Location = FVector::ZeroVector;
		FVector Extent(100.0);
		JsonConversion::TryGetVectorField(Args, TEXT("location"), Location);
		JsonConversion::TryGetVectorField(Args, TEXT("extent"), Extent);
		Extent.X = FMath::Max(FMath::Abs(Extent.X), 1.0);
		Extent.Y = FMath::Max(FMath::Abs(Extent.Y), 1.0);
		Extent.Z = FMath::Max(FMath::Abs(Extent.Z), 1.0);

		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SpawnVolume", "MCP 创建体积"));
		AVolume* Volume = World->SpawnActor<AVolume>(VolumeClass, Location, FRotator::ZeroRotator);
		if (!Volume)
		{
			return ErrorJson(TEXT("Volume 创建失败。"));
		}
		UCubeBuilder* Builder = NewObject<UCubeBuilder>();
		Builder->X = Extent.X * 2.0f;
		Builder->Y = Extent.Y * 2.0f;
		Builder->Z = Extent.Z * 2.0f;
		Builder->Build(World, Volume);
		FString Label;
		if (Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		{
			Volume->SetActorLabel(Label);
		}
		Volume->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeEnvironmentActor(Volume));
		Result->SetObjectField(TEXT("extent"), JsonConversion::MakeVectorObject(Extent));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::ListVolumes(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("编辑器世界不可用。"));
		}
		FString VolumeType;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("volumeType"), VolumeType);
		}
		TArray<TSharedPtr<FJsonValue>> Volumes;
		for (TActorIterator<AVolume> It(World); It; ++It)
		{
			if (!VolumeType.IsEmpty() && !It->GetClass()->GetName().Contains(VolumeType, ESearchCase::IgnoreCase))
			{
				continue;
			}
			Volumes.Add(MakeShared<FJsonValueObject>(ActorSupport::MakeActorObject(*It)));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Volumes.Num());
		Result->SetArrayField(TEXT("volumes"), Volumes);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetVolumeProperties(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AVolume* Volume = Cast<AVolume>(FindEnvironmentActor(Args, Error));
		if (!Volume)
		{
			return ErrorJson(Error.IsEmpty() ? TEXT("目标不是 Volume。") : Error);
		}
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!Args->TryGetObjectField(TEXT("properties"), Properties) || !Properties || !Properties->IsValid())
		{
			return ErrorJson(TEXT("缺少 properties 对象。"));
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetVolumeProperties", "MCP 修改体积"));
		Volume->Modify();
		if (!ApplyPropertyMap(Volume, *Properties, Error))
		{
			return ErrorJson(Error);
		}
		Volume->PostEditChange();
		Volume->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeEnvironmentActor(Volume));
		Result->SetNumberField(TEXT("propertyCount"), (*Properties)->Values.Num());
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SpawnLight(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		FString LightType;
		if (!World || !Args.IsValid() || !Args->TryGetStringField(TEXT("lightType"), LightType))
		{
			return ErrorJson(TEXT("编辑器世界不可用，或缺少 lightType。"));
		}
		UClass* LightClass = nullptr;
		if (LightType.Equals(TEXT("point"), ESearchCase::IgnoreCase))
			LightClass = APointLight::StaticClass();
		else if (LightType.Equals(TEXT("spot"), ESearchCase::IgnoreCase))
			LightClass = ASpotLight::StaticClass();
		else if (LightType.Equals(TEXT("directional"), ESearchCase::IgnoreCase))
			LightClass = ADirectionalLight::StaticClass();
		else if (LightType.Equals(TEXT("rect"), ESearchCase::IgnoreCase))
			LightClass = ARectLight::StaticClass();
		else if (LightType.Equals(TEXT("sky"), ESearchCase::IgnoreCase))
			LightClass = ASkyLight::StaticClass();
		if (!LightClass)
		{
			return ErrorJson(TEXT("lightType 必须是 point、spot、directional、rect 或 sky。"));
		}

		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		JsonConversion::TryGetVectorField(Args, TEXT("location"), Location);
		JsonConversion::TryGetRotatorField(Args, TEXT("rotation"), Rotation);
		AActor* Actor = World->SpawnActor<AActor>(LightClass, Location, Rotation);
		if (!Actor)
		{
			return ErrorJson(TEXT("灯光 Actor 创建失败。"));
		}
		FString Label;
		if (Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}
		ULightComponentBase* Component = FindLightComponent(Actor);
		if (!Component)
		{
			World->DestroyActor(Actor);
			return ErrorJson(TEXT("灯光组件创建失败。"));
		}
		Component->SetMobility(ReadMobility(Args, EComponentMobility::Movable));
		double Number = 0.0;
		if (Args->TryGetNumberField(TEXT("intensity"), Number))
			SetLightIntensity(Component, Number);
		if (Args->HasField(TEXT("color")))
			SetLightColor(Component, ReadEditorColor(Args, TEXT("color"), FLinearColor::White));
		if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(Component))
		{
			if (Args->TryGetNumberField(TEXT("attenuationRadius"), Number))
				Local->SetAttenuationRadius(Number);
		}
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("lightType"), LightType);
		Result->SetObjectField(TEXT("actor"), MakeEnvironmentActor(Actor));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetLightProperties(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindEnvironmentActor(Args, Error);
		ULightComponentBase* Component = FindLightComponent(Actor);
		if (!Actor || !Component)
		{
			return ErrorJson(Error.IsEmpty() ? TEXT("目标 Actor 没有灯光组件。") : Error);
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetLightProperties", "MCP 修改灯光"));
		Actor->Modify();
		Component->Modify();
		double Number = 0.0;
		if (Args->TryGetNumberField(TEXT("intensity"), Number))
			SetLightIntensity(Component, Number);
		if (Args->HasField(TEXT("color")))
			SetLightColor(Component, ReadEditorColor(Args, TEXT("color"), FLinearColor::White));
		if (Args->HasField(TEXT("mobility")))
			Component->SetMobility(ReadMobility(Args, Component->Mobility));
		FRotator Rotation;
		if (JsonConversion::TryGetRotatorField(Args, TEXT("rotation"), Rotation))
		{
			Actor->SetActorRotation(Rotation);
		}
		if (ULightComponent* Light = Cast<ULightComponent>(Component))
		{
			if (Args->TryGetNumberField(TEXT("volumetricScatteringIntensity"), Number))
			{
				Light->SetVolumetricScatteringIntensity(Number);
			}
		}
		if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(Component))
		{
			if (Args->TryGetNumberField(TEXT("attenuationRadius"), Number))
				Local->SetAttenuationRadius(Number);
		}
		if (UPointLightComponent* Point = Cast<UPointLightComponent>(Component))
		{
			if (Args->TryGetNumberField(TEXT("sourceRadius"), Number))
				Point->SetSourceRadius(Number);
		}
		if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Component))
		{
			if (Args->TryGetNumberField(TEXT("innerConeAngle"), Number))
				Spot->SetInnerConeAngle(Number);
			if (Args->TryGetNumberField(TEXT("outerConeAngle"), Number))
				Spot->SetOuterConeAngle(Number);
		}
		bool bRecapture = false;
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component); Sky && Args->TryGetBoolField(TEXT("recaptureSky"), bRecapture) && bRecapture)
		{
			Sky->RecaptureSky();
		}
		Actor->PostEditChange();
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeEnvironmentActor(Actor));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetFogProperties(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("编辑器世界不可用。"));
		}
		AExponentialHeightFog* Fog = nullptr;
		FString Label;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("name"), Label);
		}
		if (!Label.IsEmpty())
		{
			Fog = Cast<AExponentialHeightFog>(ActorSupport::FindActorByNameOrLabel(World, Label));
		}
		if (!Fog)
		{
			for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
			{
				Fog = *It;
				break;
			}
		}
		if (!Fog || !Fog->GetComponent())
		{
			return ErrorJson(TEXT("未找到 ExponentialHeightFog。"));
		}
		UExponentialHeightFogComponent* Component = Fog->GetComponent();
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetFogProperties", "MCP 修改雾"));
		Fog->Modify();
		Component->Modify();
		double Number = 0.0;
		bool bValue = false;
		if (Args->TryGetNumberField(TEXT("fogDensity"), Number))
			Component->SetFogDensity(Number);
		if (Args->TryGetNumberField(TEXT("fogHeightFalloff"), Number))
			Component->SetFogHeightFalloff(Number);
		if (Args->TryGetNumberField(TEXT("startDistance"), Number))
			Component->SetStartDistance(Number);
		if (Args->HasField(TEXT("fogInscatteringColor")))
			Component->SetFogInscatteringColor(ReadEditorColor(Args, TEXT("fogInscatteringColor"), FLinearColor::White));
		if (Args->TryGetBoolField(TEXT("enableVolumetricFog"), bValue))
			Component->SetVolumetricFog(bValue);
		if (Args->TryGetNumberField(TEXT("volumetricFogScatteringDistribution"), Number))
			Component->SetVolumetricFogScatteringDistribution(Number);
		if (Args->TryGetNumberField(TEXT("volumetricFogExtinctionScale"), Number))
			Component->SetVolumetricFogExtinctionScale(Number);
		if (Args->TryGetNumberField(TEXT("volumetricFogDistance"), Number))
			Component->SetVolumetricFogDistance(Number);
		if (Args->HasField(TEXT("volumetricFogAlbedo")))
		{
			const FLinearColor Albedo = ReadEditorColor(Args, TEXT("volumetricFogAlbedo"), FLinearColor::White);
			Component->SetVolumetricFogAlbedo(Albedo.ToFColor(false));
		}
		Fog->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("actor"), MakeEnvironmentActor(Fog));
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetRuntimeVirtualTextureSummary(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("编辑器世界不可用。"));
		}
		TArray<TSharedPtr<FJsonValue>> Volumes;
		for (TActorIterator<ARuntimeVirtualTextureVolume> It(World); It; ++It)
		{
			ARuntimeVirtualTextureVolume* Volume = *It;
			URuntimeVirtualTextureComponent* Component = Volume->VirtualTextureComponent;
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetObjectField(TEXT("actor"), MakeEnvironmentActor(Volume));
			Item->SetStringField(TEXT("virtualTexture"), Component && Component->GetVirtualTexture() ? Component->GetVirtualTexture()->GetPathName() : FString());
			Volumes.Add(MakeShared<FJsonValueObject>(Item));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Volumes.Num());
		Result->SetArrayField(TEXT("volumes"), Volumes);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetWaterBodyProperty(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		AActor* Actor = FindEnvironmentActor(Args, Error);
		if (!Actor)
		{
			return ErrorJson(Error);
		}
		UActorComponent* WaterComponent = nullptr;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->GetClass()->GetName().Contains(TEXT("WaterBodyComponent")))
			{
				WaterComponent = Component;
				break;
			}
		}
		if (!WaterComponent)
		{
			return ErrorJson(TEXT("目标 Actor 没有 WaterBodyComponent；请确认 Water 插件已启用。"));
		}
		FString PropertyName;
		if (!Args->TryGetStringField(TEXT("propertyName"), PropertyName))
		{
			return ErrorJson(TEXT("缺少 propertyName。"));
		}
		FProperty* Property = WaterComponent->GetClass()->FindPropertyByName(*PropertyName);
		if (!Property)
		{
			return ErrorJson(FString::Printf(TEXT("未找到 WaterBodyComponent 属性：%s"), *PropertyName));
		}
		WaterComponent->Modify();
		if (!PropertyWriter::SetPropertyFromJson(WaterComponent, Property, Args->TryGetField(TEXT("value")), Error))
		{
			return ErrorJson(Error);
		}
		WaterComponent->PostEditChange();
		Actor->MarkPackageDirty();
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("component"), WaterComponent->GetName());
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::BuildLighting(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		if (!World)
		{
			return ErrorJson(TEXT("编辑器世界不可用。"));
		}
		const bool bBuilt = FEditorBuildUtils::EditorBuild(World, FBuildOptions::BuildLighting, false);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("built"), bBuilt);
		FString Quality;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("quality"), Quality);
		}
		Result->SetStringField(TEXT("quality"), Quality);
		return bBuilt ? SuccessJson(Result) : ErrorJson(TEXT("灯光构建未成功完成。"));
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::GetWorldSettings(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		if (!Settings)
		{
			return ErrorJson(TEXT("WorldSettings 不可用。"));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("defaultGameMode"), Settings->DefaultGameMode ? Settings->DefaultGameMode->GetPathName() : FString());
		Result->SetNumberField(TEXT("killZ"), Settings->KillZ);
		Result->SetNumberField(TEXT("globalGravityZ"), Settings->GlobalGravityZ);
		Result->SetBoolField(TEXT("enableWorldBoundsChecks"), Settings->bEnableWorldBoundsChecks);
		Result->SetBoolField(TEXT("worldGravitySet"), Settings->bWorldGravitySet);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealLevelAdapter::SetWorldSettings(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = ActorSupport::GetEditorWorld();
		AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		if (!Settings || !Args.IsValid())
		{
			return ErrorJson(TEXT("WorldSettings 或参数不可用。"));
		}
		Transactions::FUnrealAgentMCPScopedEditorTransaction Transaction(NSLOCTEXT("UnrealAgent", "SetWorldSettings", "MCP 修改世界设置"));
		Settings->Modify();
		FString GameModePath;
		if (Args->TryGetStringField(TEXT("defaultGameMode"), GameModePath))
		{
			UClass* GameMode = LoadObject<UClass>(nullptr, *GameModePath);
			if (!GameMode)
			{
				GameMode = ActorSupport::ResolveActorClass(GameModePath);
			}
			if (!GameMode || !GameMode->IsChildOf(AGameModeBase::StaticClass()))
			{
				return ErrorJson(FString::Printf(TEXT("无效的 GameMode 类：%s"), *GameModePath));
			}
			Settings->DefaultGameMode = GameMode;
		}
		double Number = 0.0;
		if (Args->TryGetNumberField(TEXT("killZ"), Number))
			Settings->KillZ = Number;
		if (Args->TryGetNumberField(TEXT("globalGravityZ"), Number))
		{
			Settings->GlobalGravityZ = Number;
			Settings->bWorldGravitySet = true;
		}
		bool bValue = false;
		if (Args->TryGetBoolField(TEXT("enableWorldBoundsChecks"), bValue))
			Settings->bEnableWorldBoundsChecks = bValue;
		Settings->PostEditChange();
		Settings->MarkPackageDirty();
		return GetWorldSettings(Args);
	}
}
