// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAssetAdapter.cpp
 * @brief 资产 Adapter 的参数归一化、动作分派与共享 JSON 投影。
 */

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"

#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UnrealAgentMCP
{
	UEditorAssetSubsystem* FUnrealAgentMCPUnrealAssetAdapter::GetAssetSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::GetStringArgument(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names)
	{
		if (!Args.IsValid())
		{
			return FString();
		}
		for (const TCHAR* Name : Names)
		{
			FString Value;
			if (Args->TryGetStringField(Name, Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}

	TArray<FString> FUnrealAgentMCPUnrealAssetAdapter::GetStringArrayArgument(const TSharedPtr<FJsonObject>& Args, std::initializer_list<const TCHAR*> Names)
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
				if (Value.IsValid() && Value->TryGetString(Text))
				{
					Result.Add(Text);
				}
			}
			break;
		}
		return Result;
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::NormalizeAssetPath(const FString& Path)
	{
		FString Result = Path;
		Result.TrimStartAndEndInline();
		if (Result.StartsWith(TEXT("SoftObjectPath:")))
		{
			Result.RightChopInline(15);
			Result.TrimStartAndEndInline();
		}
		return Result;
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::ToPackageName(const FString& Path)
	{
		const FString Normalized = NormalizeAssetPath(Path);
		if (Normalized.Contains(TEXT(".")))
		{
			return FPackageName::ObjectPathToPackageName(Normalized);
		}
		return Normalized;
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealAssetAdapter::MakeAssetJson(const FAssetData& Asset)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Result->SetStringField(TEXT("packageName"), Asset.PackageName.ToString());
		Result->SetStringField(TEXT("packagePath"), Asset.PackagePath.ToString());
		Result->SetStringField(TEXT("objectPath"), Asset.GetSoftObjectPath().ToString());
		Result->SetStringField(TEXT("classPath"), Asset.AssetClassPath.ToString());
		return Result;
	}

	FString FUnrealAgentMCPUnrealAssetAdapter::ExecuteAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list") || Action == TEXT("search"))
		{
			return Discover(Action, Args);
		}
		if (Action == TEXT("read") || Action == TEXT("read_properties") || Action == TEXT("list_properties") || Action == TEXT("get_properties"))
		{
			return Inspect(Action, Args);
		}
		if (Action == TEXT("health_check") || Action == TEXT("diagnose_registry") || Action == TEXT("get_referencers") || Action == TEXT("get_dependencies") ||
			Action == TEXT("get_primary_asset_ids"))
		{
			return QueryRegistry(Action, Args);
		}
		if (Action == TEXT("recenter_pivot") || Action == TEXT("import_static_mesh") || Action == TEXT("import_skeletal_mesh") || Action == TEXT("import_animation") ||
			Action == TEXT("import_texture") || Action == TEXT("import_texture_batch") || Action == TEXT("reimport") || Action.StartsWith(TEXT("export")) ||
			Action == TEXT("compare_textures") || Action.Contains(TEXT("cloth")) || Action == TEXT("create_interchange_pipeline") || Action.EndsWith(TEXT("_fts")) ||
			Action == TEXT("migrate") || Action == TEXT("diff"))
		{
			return Advanced(Action, Args);
		}
		if (Action.Contains(TEXT("datatable")) || Action.Contains(TEXT("curvetable")) || Action.Contains(TEXT("stringtable")))
		{
			return Tables(Action, Args);
		}
		if (Action.Contains(TEXT("texture")))
		{
			return Textures(Action, Args);
		}
		if (Action.Contains(TEXT("mesh")) || Action.Contains(TEXT("socket")) || Action == TEXT("list_skeleton_bones") || Action == TEXT("read_import_sources"))
		{
			return Meshes(Action, Args);
		}
		if (Action.Contains(TEXT("input_mapping")) || Action.Contains(TEXT("user_defined")) || Action == TEXT("list_enum_values") || Action == TEXT("list_struct_fields") ||
			Action == TEXT("rename_struct_field"))
		{
			return Definitions(Action, Args);
		}
		return Mutate(Action, Args);
	}
}
