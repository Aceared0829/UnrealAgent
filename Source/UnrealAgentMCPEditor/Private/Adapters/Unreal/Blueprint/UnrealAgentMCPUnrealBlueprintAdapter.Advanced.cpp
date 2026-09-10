// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealBlueprintAdapter.Advanced.cpp
 * @brief Blueprint 依赖、差异、复制、CDO、批量编译与复合编排实现。
 */

#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.h"

#include "Adapters/Unreal/Asset/UnrealAgentMCPUnrealAssetAdapter.h"
#include "Adapters/Unreal/Blueprint/UnrealAgentMCPUnrealBlueprintAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	using namespace BlueprintPrivate;

	FString FUnrealAgentMCPUnrealBlueprintAdapter::ExecuteAdvanced(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("get_dependencies") || Action == TEXT("diff") || Action == TEXT("duplicate"))
		{
			FUnrealAgentMCPUnrealAssetAdapter AssetAdapter;
			return AssetAdapter.ExecuteAction(Action, Args);
		}

		if (Action == TEXT("compile_all"))
		{
			TArray<UBlueprint*> Blueprints;
			const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
			if (Args.IsValid() && Args->TryGetArrayField(TEXT("assetPaths"), Paths))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Paths)
				{
					FString Path;
					if (Value.IsValid() && Value->TryGetString(Path))
					{
						if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path))
							Blueprints.AddUnique(Blueprint);
					}
				}
			}
			else
			{
				for (TObjectIterator<UBlueprint> It; It; ++It)
				{
					if (!It->HasAnyFlags(RF_ClassDefaultObject) && !It->GetPackage()->HasAnyPackageFlags(PKG_CompiledIn))
						Blueprints.Add(*It);
				}
			}
			int32 Failed = 0;
			for (UBlueprint* Blueprint : Blueprints)
			{
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				if (Blueprint->Status == BS_Error)
					++Failed;
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("compiled"), Blueprints.Num());
			Result->SetNumberField(TEXT("failed"), Failed);
			Result->SetBoolField(TEXT("success"), Failed == 0);
			return Serialize(Result);
		}

		if (Action == TEXT("author"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
			if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("operations"), Operations))
				return Failure(TEXT("缺少 operations。"));
			TArray<TSharedPtr<FJsonValue>> Results;
			for (const TSharedPtr<FJsonValue>& Value : *Operations)
			{
				TSharedPtr<FJsonObject> Operation = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Operation)
					continue;
				TSharedRef<FJsonObject> Merged = MakeShared<FJsonObject>(*Args);
				Merged->RemoveField(TEXT("operations"));
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Operation->Values)
					Merged->SetField(Pair.Key, Pair.Value);
				FString ChildAction;
				Merged->TryGetStringField(TEXT("action"), ChildAction);
				if (ChildAction == TEXT("author"))
					return Failure(TEXT("author 不允许递归调用自身。"));
				const FString ChildJson = ExecuteAction(ChildAction, Merged);
				TSharedPtr<FJsonObject> Child;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ChildJson);
				if (!FJsonSerializer::Deserialize(Reader, Child) || !Child)
					return Failure(TEXT("子操作返回了无效 JSON。"));
				Results.Add(MakeShared<FJsonValueObject>(Child));
				bool bSuccess = false;
				if (!Child->TryGetBoolField(TEXT("success"), bSuccess) || !bSuccess)
				{
					TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
					Result->SetBoolField(TEXT("success"), false);
					Result->SetStringField(TEXT("domain"), TEXT("blueprint"));
					Result->SetStringField(TEXT("error"), TEXT("author 子操作失败。"));
					Result->SetArrayField(TEXT("results"), Results);
					return Serialize(Result);
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("results"), Results);
			return Serialize(Result);
		}

		FString Error;
		UBlueprint* Blueprint = LoadBlueprint(Args, Error);
		if (!Blueprint)
			return Failure(Error);
		UObject* Cdo = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
		if (!Cdo)
			return Failure(TEXT("Blueprint 尚未生成 CDO。"));
		if (Action == TEXT("set_cdo_property"))
		{
			if (!SetProperty(Cdo, StringArg(Args, { TEXT("propertyName") }), Args->TryGetField(TEXT("value")), Error))
				return Failure(Error);
			SaveBlueprint(Blueprint, false);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
			return Serialize(Result);
		}
		if (Action == TEXT("get_cdo_properties"))
		{
			TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			const FString Only = StringArg(Args, { TEXT("propertyName") });
			for (TFieldIterator<FProperty> It(Cdo->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (!Only.IsEmpty() && !It->GetName().Equals(Only, ESearchCase::IgnoreCase))
					continue;
				if (!It->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
					continue;
				if (TSharedPtr<FJsonValue> Value = ReadProperty(Cdo, It->GetName()))
					Properties->SetField(It->GetName(), Value);
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetObjectField(TEXT("properties"), Properties);
			return Serialize(Result);
		}
		return Failure(FString::Printf(TEXT("未知 Blueprint 高级操作：%s"), *Action));
	}
}
