// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.Mutation.cpp
 * @brief 资产生命周期、目录、属性写入与协作锁实现。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "Adapters/Tooling/UnrealAgentMCPTools.h"
#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "FileHelpers.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "PackageTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		FCriticalSection AssetLockMutex;
		TMap<FString, FString> AssetLocks;

		TSharedRef<FJsonObject> MakeMutationResult(const FString& Action, const FString& Path, const bool bChanged)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("action"), Action);
			Result->SetStringField(TEXT("assetPath"), Path);
			Result->SetBoolField(TEXT("changed"), bChanged);
			return Result;
		}

		TSharedRef<FJsonObject> CopyArguments(const TSharedPtr<FJsonObject>& Args)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			if (Args.IsValid())
			{
				Result->Values = Args->Values;
			}
			return Result;
		}

		void CopyAlias(const TSharedRef<FJsonObject>& Args, const TCHAR* Target, std::initializer_list<const TCHAR*> Sources)
		{
			if (Args->HasField(Target))
			{
				return;
			}
			for (const TCHAR* Source : Sources)
			{
				const TSharedPtr<FJsonValue> Value = Args->TryGetField(Source);
				if (Value.IsValid())
				{
					Args->SetField(Target, Value);
					return;
				}
			}
		}

		FString RequirePath(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names, FString& OutError)
		{
			const FString Path = FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(Args, Names);
			if (Path.IsEmpty())
			{
				OutError = TEXT("缺少必填资产或目录路径。");
			}
			return Path;
		}
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::Mutate(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		UEditorAssetSubsystem* Subsystem = GetAssetSubsystem();
		if (!Subsystem)
		{
			return ErrorJson(TEXT("EditorAssetSubsystem 当前不可用。"));
		}

		if (Action == TEXT("create_data_asset") || Action == TEXT("create_asset_by_class"))
		{
			TSharedRef<FJsonObject> Normalized = CopyArguments(Args);
			CopyAlias(Normalized, TEXT("assetPath"), { TEXT("path"), TEXT("objectPath") });
			CopyAlias(Normalized, TEXT("class"), { TEXT("className"), TEXT("assetClass") });
			return Tools::CreateAsset(Normalized);
		}

		if (Action == TEXT("save_all_dirty"))
		{
			const bool bSaved = FEditorFileUtils::SaveDirtyPackages(false, true, true, false, false, false);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("saved"), bSaved);
			return SuccessJson(Result);
		}

		if (Action == TEXT("list_locks"))
		{
			FScopeLock Guard(&AssetLockMutex);
			TArray<FString> Paths;
			AssetLocks.GetKeys(Paths);
			Paths.Sort();
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FString& Path : Paths)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("assetPath"), Path);
				Item->SetStringField(TEXT("owner"), AssetLocks[Path]);
				Items.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Items.Num());
			Result->SetArrayField(TEXT("locks"), Items);
			return SuccessJson(Result);
		}

		if (Action == TEXT("unlock_all"))
		{
			const FString Owner = GetStringArgument(Args, { TEXT("owner"), TEXT("sessionId") });
			FScopeLock Guard(&AssetLockMutex);
			int32 Removed = 0;
			for (auto It = AssetLocks.CreateIterator(); It; ++It)
			{
				if (Owner.IsEmpty() || It.Value() == Owner)
				{
					It.RemoveCurrent();
					++Removed;
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("removedCount"), Removed);
			Result->SetStringField(TEXT("owner"), Owner);
			return SuccessJson(Result);
		}

		FString Error;
		if (Action == TEXT("create_folder"))
		{
			const FString Directory = RequirePath(Args, { TEXT("directory"), TEXT("path"), TEXT("packagePath") }, Error);
			if (!Error.IsEmpty())
			{
				return ErrorJson(Error);
			}
			const bool bChanged = Subsystem->MakeDirectory(Directory);
			return bChanged ? SuccessJson(MakeMutationResult(Action, Directory, true)) : ErrorJson(FString::Printf(TEXT("无法创建资产目录：%s"), *Directory));
		}

		if (Action == TEXT("move_folder"))
		{
			const FString Source = RequirePath(Args, { TEXT("sourcePath"), TEXT("source"), TEXT("path") }, Error);
			const FString Destination = GetStringArgument(Args, { TEXT("destinationPath"), TEXT("destination"), TEXT("newPath"), TEXT("targetPath") });
			if (!Error.IsEmpty() || Destination.IsEmpty())
			{
				return ErrorJson(Error.IsEmpty() ? TEXT("缺少必填 destinationPath。") : Error);
			}
			const bool bChanged = Subsystem->RenameDirectory(Source, Destination);
			TSharedRef<FJsonObject> Result = MakeMutationResult(Action, Source, bChanged);
			Result->SetStringField(TEXT("destinationPath"), Destination);
			return bChanged ? SuccessJson(Result) : ErrorJson(TEXT("资产目录移动失败。"));
		}

		if (Action == TEXT("delete_folder"))
		{
			const FString Directory = RequirePath(Args, { TEXT("directory"), TEXT("path"), TEXT("packagePath") }, Error);
			if (!Error.IsEmpty())
			{
				return ErrorJson(Error);
			}
			const bool bChanged = Subsystem->DeleteDirectory(Directory);
			return bChanged ? SuccessJson(MakeMutationResult(Action, Directory, true)) : ErrorJson(FString::Printf(TEXT("无法删除资产目录：%s"), *Directory));
		}

		const FString AssetPath = RequirePath(Args, { TEXT("assetPath"), TEXT("path"), TEXT("sourcePath"), TEXT("source"), TEXT("asset") }, Error);
		if (!Error.IsEmpty() && Action != TEXT("delete_batch") && Action != TEXT("bulk_rename"))
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("lock") || Action == TEXT("unlock"))
		{
			const FString OwnerValue = GetStringArgument(Args, { TEXT("owner"), TEXT("sessionId") });
			const FString Owner = OwnerValue.IsEmpty() ? TEXT("default") : OwnerValue;
			FScopeLock Guard(&AssetLockMutex);
			if (Action == TEXT("lock"))
			{
				const FString* Existing = AssetLocks.Find(AssetPath);
				if (Existing && *Existing != Owner)
				{
					return ErrorJson(FString::Printf(TEXT("资产已被 '%s' 锁定：%s"), **Existing, *AssetPath));
				}
				AssetLocks.Add(AssetPath, Owner);
			}
			else
			{
				const FString* Existing = AssetLocks.Find(AssetPath);
				if (Existing && *Existing != Owner)
				{
					return ErrorJson(TEXT("只有锁持有者可以解锁资产。"));
				}
				AssetLocks.Remove(AssetPath);
			}
			TSharedRef<FJsonObject> Result = MakeMutationResult(Action, AssetPath, true);
			Result->SetStringField(TEXT("owner"), Owner);
			return SuccessJson(Result);
		}

		if (Action == TEXT("duplicate") || Action == TEXT("rename") || Action == TEXT("move"))
		{
			const FString Destination = GetStringArgument(Args, { TEXT("destinationPath"), TEXT("destination"), TEXT("newPath"), TEXT("targetPath") });
			if (Destination.IsEmpty())
			{
				return ErrorJson(TEXT("缺少必填 destinationPath。"));
			}
			UObject* NewAsset = nullptr;
			bool bChanged = false;
			if (Action == TEXT("duplicate"))
			{
				NewAsset = Subsystem->DuplicateAsset(AssetPath, Destination);
				bChanged = NewAsset != nullptr;
			}
			else
			{
				bChanged = Subsystem->RenameAsset(AssetPath, Destination);
			}
			if (!bChanged)
			{
				return ErrorJson(FString::Printf(TEXT("资产 %s 操作失败：%s -> %s"), *Action, *AssetPath, *Destination));
			}
			TSharedRef<FJsonObject> Result = MakeMutationResult(Action, AssetPath, true);
			Result->SetStringField(TEXT("destinationPath"), Destination);
			if (NewAsset)
			{
				Result->SetStringField(TEXT("createdObjectPath"), NewAsset->GetPathName());
			}
			return SuccessJson(Result);
		}

		if (Action == TEXT("bulk_rename"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Renames = nullptr;
			if (!Args.IsValid() || (!Args->TryGetArrayField(TEXT("renames"), Renames) && !Args->TryGetArrayField(TEXT("assets"), Renames)))
			{
				return ErrorJson(TEXT("缺少必填 renames 数组。"));
			}
			TArray<TSharedPtr<FJsonValue>> Results;
			int32 Succeeded = 0;
			for (const TSharedPtr<FJsonValue>& Value : *Renames)
			{
				const TSharedPtr<FJsonObject> Item = Value.IsValid() ? Value->AsObject() : nullptr;
				const FString Source = GetStringArgument(Item, { TEXT("sourcePath"), TEXT("source"), TEXT("assetPath") });
				const FString Destination = GetStringArgument(Item, { TEXT("destinationPath"), TEXT("destination"), TEXT("newPath") });
				const bool bChanged = !Source.IsEmpty() && !Destination.IsEmpty() && Subsystem->RenameAsset(Source, Destination);
				Succeeded += bChanged ? 1 : 0;
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("sourcePath"), Source);
				Entry->SetStringField(TEXT("destinationPath"), Destination);
				Entry->SetBoolField(TEXT("success"), bChanged);
				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("requested"), Renames->Num());
			Result->SetNumberField(TEXT("succeeded"), Succeeded);
			Result->SetArrayField(TEXT("results"), Results);
			return SuccessJson(Result);
		}

		if (Action == TEXT("delete"))
		{
			const bool bChanged = Subsystem->DeleteAsset(AssetPath);
			return bChanged ? SuccessJson(MakeMutationResult(Action, AssetPath, true)) : ErrorJson(FString::Printf(TEXT("资产删除失败：%s"), *AssetPath));
		}

		if (Action == TEXT("delete_batch"))
		{
			const TArray<FString> Paths = GetStringArrayArgument(Args, { TEXT("assetPaths"), TEXT("paths"), TEXT("assets") });
			if (Paths.IsEmpty())
			{
				return ErrorJson(TEXT("缺少必填 assetPaths 数组。"));
			}
			int32 Deleted = 0;
			TArray<TSharedPtr<FJsonValue>> Failed;
			for (const FString& Path : Paths)
			{
				if (Subsystem->DeleteAsset(Path))
				{
					++Deleted;
				}
				else
				{
					Failed.Add(MakeShared<FJsonValueString>(Path));
				}
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("requested"), Paths.Num());
			Result->SetNumberField(TEXT("deleted"), Deleted);
			Result->SetArrayField(TEXT("failed"), Failed);
			return SuccessJson(Result);
		}

		if (Action == TEXT("save"))
		{
			bool bOnlyIfDirty = true;
			if (Args.IsValid())
			{
				Args->TryGetBoolField(TEXT("onlyIfDirty"), bOnlyIfDirty);
			}
			const bool bSaved = Subsystem->SaveAsset(AssetPath, bOnlyIfDirty);
			return bSaved ? SuccessJson(MakeMutationResult(Action, AssetPath, true)) : ErrorJson(FString::Printf(TEXT("资产保存失败：%s"), *AssetPath));
		}

		if (Action == TEXT("set_property"))
		{
			const FString PropertyName = GetStringArgument(Args, { TEXT("propertyName"), TEXT("property") });
			const TSharedPtr<FJsonValue> Value = Args.IsValid() ? Args->TryGetField(TEXT("value")) : nullptr;
			UObject* Asset = Subsystem->LoadAsset(AssetPath);
			FProperty* Property = Asset ? FindFProperty<FProperty>(Asset->GetClass(), *PropertyName) : nullptr;
			if (!Asset || !Property || !Value.IsValid())
			{
				return ErrorJson(TEXT("资产、propertyName 或 value 无效。"));
			}
			Asset->Modify();
			if (!PropertyWriter::SetPropertyFromJson(Asset, Property, Value, Error))
			{
				return ErrorJson(Error);
			}
			Asset->PostEditChange();
			Asset->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeMutationResult(Action, AssetPath, true);
			Result->SetStringField(TEXT("propertyName"), PropertyName);
			return SuccessJson(Result);
		}

		if (Action == TEXT("reload_package") || Action == TEXT("force_reload"))
		{
			UObject* Asset = Subsystem->LoadAsset(AssetPath);
			UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
			if (!Package)
			{
				return ErrorJson(FString::Printf(TEXT("无法加载资产包：%s"), *AssetPath));
			}
			const bool bReloaded = UPackageTools::ReloadPackages({ Package });
			return bReloaded ? SuccessJson(MakeMutationResult(Action, AssetPath, true)) : ErrorJson(FString::Printf(TEXT("资产包重载失败：%s"), *AssetPath));
		}

		return ErrorJson(FString::Printf(TEXT("资产动作未进入有效实现分支：%s"), *Action));
	}
}
