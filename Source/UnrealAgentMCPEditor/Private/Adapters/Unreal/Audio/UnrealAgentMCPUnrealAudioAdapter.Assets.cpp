// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.Assets.cpp
 * @brief 音频资产查询、导入、PCM 提取与通用属性写入。
 */

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.Internal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IAssetTools.h"
#include "Infrastructure/Transactions/UnrealAgentMCPAssetImportCompensation.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "UObject/SavePackage.h"

namespace UnrealAgentMCP
{
	using namespace AudioPrivate;

	FString FUnrealAgentMCPUnrealAudioAdapter::ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list"))
		{
			FARFilter Filter;
			Filter.bRecursiveClasses = true;
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(*OptionalString(Args, TEXT("path"), TEXT("/Game")));
			Filter.ClassPaths.Add(USoundBase::StaticClass()->GetClassPathName());
			TArray<FAssetData> Assets;
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
			TArray<TSharedPtr<FJsonValue>> Items;
			const int32 Limit = FMath::Max(1, OptionalInt(Args, TEXT("limit"), 200));
			for (int32 Index = 0; Index < Assets.Num() && Index < Limit; ++Index)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Assets[Index].AssetName.ToString());
				Item->SetStringField(TEXT("assetPath"), Assets[Index].GetObjectPathString());
				Item->SetStringField(TEXT("class"), Assets[Index].AssetClassPath.ToString());
				Items.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("count"), Items.Num());
			Result->SetArrayField(TEXT("assets"), Items);
			return Serialize(Result);
		}

		if (Action == TEXT("import_audio"))
		{
			FString FilePath;
			if (const FString Error = RequireString(Args, TEXT("filePath"), FilePath); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			if (!FPaths::FileExists(FilePath))
			{
				return Failure(FString::Printf(TEXT("音频源文件不存在：%s"), *FilePath));
			}
			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->Filename = FilePath;
			Task->DestinationPath = OptionalString(Args, TEXT("destinationPath"), TEXT("/Game/Audio"));
			FUnrealAgentMCPAssetImportCompensation Compensation(Task->DestinationPath);
			Task->bAutomated = true;
			Task->bSave = true;
			Task->bReplaceExisting = OptionalString(Args, TEXT("onConflict"), TEXT("reuse")) == TEXT("replace");
			FString CompensationError;
			if (!Compensation.ValidateReplacePolicy(Task->bReplaceExisting, CompensationError))
			{
				return Failure(CompensationError);
			}
			TArray<UAssetImportTask*> Tasks{ Task };
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks(Tasks);
			if (Task->ImportedObjectPaths.IsEmpty())
			{
				return Failure(TEXT("Unreal 未能导入该音频文件。"));
			}
			TArray<UObject*> ImportedAssets;
			ImportedAssets.Reserve(Task->ImportedObjectPaths.Num());
			for (const FString& Path : Task->ImportedObjectPaths)
			{
				if (UObject* Asset = LoadObject<UObject>(nullptr, *Path))
				{
					ImportedAssets.Add(Asset);
				}
			}
			if (!Compensation.RegisterCreatedAssets(ImportedAssets, CompensationError))
			{
				return Failure(CompensationError);
			}
			TArray<TSharedPtr<FJsonValue>> Paths;
			for (const FString& Path : Task->ImportedObjectPaths)
			{
				Paths.Add(MakeShared<FJsonValueString>(Path));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("importedObjectPaths"), Paths);
			return Serialize(Result);
		}

		FString Error;
		USoundBase* Sound = RequireSound(Args, Error);
		if (!Sound)
		{
			return Failure(Error);
		}
		if (Action == TEXT("extract_pcm"))
		{
			USoundWave* Wave = Cast<USoundWave>(Sound);
			if (!Wave)
			{
				return Failure(TEXT("extract_pcm 仅支持 SoundWave。"));
			}
			TArray<uint8> Bytes;
			uint32 SampleRate = 0;
			uint16 Channels = 0;
			if (!Wave->GetImportedSoundWaveData(Bytes, SampleRate, Channels))
			{
				return Failure(TEXT("该 SoundWave 没有可读取的导入 PCM 数据。"));
			}
			const int32 MaxBytes = FMath::Clamp(OptionalInt(Args, TEXT("maxBytes"), 65536), 1, 4 * 1024 * 1024);
			const int32 ReturnedBytes = FMath::Min(Bytes.Num(), MaxBytes);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("sampleRate"), SampleRate);
			Result->SetNumberField(TEXT("channels"), Channels);
			Result->SetNumberField(TEXT("byteCount"), Bytes.Num());
			Result->SetBoolField(TEXT("truncated"), ReturnedBytes < Bytes.Num());
			Result->SetStringField(TEXT("pcmBase64"), FBase64::Encode(Bytes.GetData(), ReturnedBytes));
			return Serialize(Result);
		}

		FString PropertyName;
		if (const FString Required = RequireString(Args, TEXT("propertyName"), PropertyName); !Required.IsEmpty())
		{
			return Failure(Required);
		}
		const TSharedPtr<FJsonValue> Value = Args->TryGetField(TEXT("value"));
		if (!Value.IsValid())
		{
			return Failure(TEXT("缺少必填参数 'value'。"));
		}
		Sound->Modify();
		if (!SetReflectedProperty(Sound, Sound->GetClass(), PropertyName, Value, Error))
		{
			return Failure(Error);
		}
		SaveAsset(Sound);
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetStringField(TEXT("assetPath"), Sound->GetPathName());
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		return Serialize(Result);
	}
}
