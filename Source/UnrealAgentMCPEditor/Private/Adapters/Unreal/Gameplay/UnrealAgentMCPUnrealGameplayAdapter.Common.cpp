// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealGameplayAdapter.Common.cpp
 * @brief Gameplay 结果、参数、场景对象和资产生命周期公共实现。
 */

#include "Adapters/Unreal/Gameplay/UnrealAgentMCPUnrealGameplayAdapter.Internal.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace UnrealAgentMCP::GameplayPrivate
{
	FString Success(const TSharedRef<FJsonObject>& Result)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("domain"), TEXT("gameplay"));
		return JsonObjectToString(Result);
	}

	FString Error(const FString& Message)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("gameplay"));
		Result->SetStringField(TEXT("error"), Message);
		return JsonObjectToString(Result);
	}

	FString StringArg(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names, const FString& Default)
	{
		FString Value;
		if (Args)
			for (const TCHAR* Name : Names)
				if (Args->TryGetStringField(Name, Value) && !Value.IsEmpty())
					return Value;
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

	FVector VectorArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Name, const FVector& Default)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Args || !Args->TryGetObjectField(Name, Object) || !Object || !*Object)
			return Default;
		return FVector(NumberArg(*Object, TEXT("x"), Default.X), NumberArg(*Object, TEXT("y"), Default.Y), NumberArg(*Object, TEXT("z"), Default.Z));
	}

	AActor* FindActor(const FString& Label)
	{
		if (!GEditor || !GEditor->GetEditorWorldContext().World())
			return nullptr;
		for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
		{
			if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase) || It->GetName().Equals(Label, ESearchCase::IgnoreCase))
				return *It;
		}
		return nullptr;
	}

	FString MakeObjectPath(const TSharedPtr<FJsonObject>& Args, const FString& Prefix)
	{
		const FString Name = StringArg(Args, { TEXT("name") }, Prefix);
		const FString Directory = StringArg(Args, { TEXT("packagePath"), TEXT("directory") }, TEXT("/Game"));
		return Directory / Name + TEXT(".") + Name;
	}

	UObject* CreateAsset(UClass* Class, const TSharedPtr<FJsonObject>& Args, const FString& Prefix, FString& OutPath)
	{
		OutPath = MakeObjectPath(Args, Prefix);
		if (UObject* Existing = LoadObject<UObject>(nullptr, *OutPath))
			return Existing;
		const FString PackageName = FPackageName::ObjectPathToPackageName(OutPath);
		UPackage* Package = CreatePackage(*PackageName);
		UObject* Asset = NewObject<UObject>(Package, Class, *FPackageName::ObjectPathToObjectName(OutPath), RF_Public | RF_Standalone | RF_Transactional);
		if (Asset)
			FAssetRegistryModule::AssetCreated(Asset);
		Save(Asset);
		return Asset;
	}

	bool Save(UObject* Asset)
	{
		if (!Asset)
			return false;
		Asset->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
	}
}
