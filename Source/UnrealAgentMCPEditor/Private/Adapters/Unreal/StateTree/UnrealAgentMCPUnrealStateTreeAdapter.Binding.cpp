// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealStateTreeAdapter.Binding.cpp
 * @brief StateTree Property Binding 与可绑定数据源操作。
 */

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"

#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "PropertyBindingPath.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreePropertyBindings.h"

namespace UnrealAgentMCP
{
	using namespace StateTreePrivate;

	static bool MakePath(const FString& IdText, const FString& PathText, FPropertyBindingPath& OutPath)
	{
		const FGuid Id = ParseGuid(IdText);
		if (!Id.IsValid())
		{
			return false;
		}
		OutPath.SetStructID(Id);
		return PathText.IsEmpty() || OutPath.FromString(PathText);
	}

	FString FUnrealAgentMCPUnrealStateTreeAdapter::ExecuteBindingAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString AssetPath;
		if (const FString Error = RequireString(Args, TEXT("assetPath"), AssetPath); !Error.IsEmpty())
		{
			return Failure(Error);
		}
		UStateTree* StateTree = LoadStateTree(AssetPath);
		UStateTreeEditorData* EditorData = GetEditorData(StateTree);
		if (!StateTree || !EditorData)
		{
			return Failure(TEXT("找不到 StateTree 或编辑数据。"));
		}

		if (Action == TEXT("list_bindings"))
		{
			TArray<TSharedPtr<FJsonValue>> Bindings;
			const FGuid Filter = ParseGuid(OptionalString(Args, TEXT("structId")));
			if (const FStateTreeEditorPropertyBindings* EditorBindings = EditorData->GetPropertyEditorBindings())
			{
				for (const FStateTreePropertyPathBinding& Binding : EditorBindings->GetBindings())
				{
					if (Filter.IsValid() && Binding.GetSourcePath().GetStructID() != Filter && Binding.GetTargetPath().GetStructID() != Filter)
					{
						continue;
					}
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("sourceStructId"), GuidString(Binding.GetSourcePath().GetStructID()));
					Entry->SetStringField(TEXT("sourcePath"), Binding.GetSourcePath().ToString());
					Entry->SetStringField(TEXT("targetStructId"), GuidString(Binding.GetTargetPath().GetStructID()));
					Entry->SetStringField(TEXT("targetPath"), Binding.GetTargetPath().ToString());
					Bindings.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("bindings"), Bindings);
			return Serialize(Result);
		}

		if (Action == TEXT("list_bindable_sources"))
		{
			TArray<TSharedPtr<FJsonValue>> Sources;
			EditorData->VisitAllNodes(
				[&](const UStateTreeState*, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView)
				{
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("id"), GuidString(Desc.ID));
					Entry->SetStringField(TEXT("name"), Desc.Name.ToString());
					Entry->SetStringField(TEXT("section"), Desc.GetSection());
					Entry->SetStringField(TEXT("structType"), Desc.Struct ? Desc.Struct->GetPathName() : FString());
					Sources.Add(MakeShared<FJsonValueObject>(Entry));
					return EStateTreeVisitor::Continue;
				});
			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetStringField(TEXT("id"), GuidString(EditorData->GetRootParametersGuid()));
			Root->SetStringField(TEXT("name"), TEXT("RootParameters"));
			Sources.Insert(MakeShared<FJsonValueObject>(Root), 0);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("sources"), Sources);
			return Serialize(Result);
		}

		FString TargetId;
		FString TargetPathText;
		if (const FString Error = RequireString(Args, TEXT("targetStructId"), TargetId); !Error.IsEmpty())
			return Failure(Error);
		if (const FString Error = RequireString(Args, TEXT("targetPath"), TargetPathText); !Error.IsEmpty())
			return Failure(Error);
		FPropertyBindingPath TargetPath;
		if (!MakePath(TargetId, TargetPathText, TargetPath))
		{
			return Failure(TEXT("目标 Binding 路径无效。"));
		}
		MarkModified(StateTree, EditorData);
		if (Action == TEXT("remove_binding"))
		{
			EditorData->RemovePropertyBinding(TargetPath);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetBoolField(TEXT("removed"), true);
			return Serialize(Result);
		}

		FString SourceId;
		FString SourcePathText;
		if (const FString Error = RequireString(Args, TEXT("sourceStructId"), SourceId); !Error.IsEmpty())
			return Failure(Error);
		if (const FString Error = RequireString(Args, TEXT("sourcePath"), SourcePathText); !Error.IsEmpty())
			return Failure(Error);
		FPropertyBindingPath SourcePath;
		if (!MakePath(SourceId, SourcePathText, SourcePath))
		{
			return Failure(TEXT("来源 Binding 路径无效。"));
		}
		EditorData->AddPropertyBinding(SourcePath, TargetPath);
		EditorData->UpdateBindings();
		TSharedRef<FJsonObject> Result = SuccessObject();
		Result->SetBoolField(TEXT("created"), true);
		return Serialize(Result);
	}
}
