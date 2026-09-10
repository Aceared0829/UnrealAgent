// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealWidgetAdapter.Asset.cpp
 * @brief Widget Blueprint 与 Editor Utility 资产创建、保存及执行实现。
 */

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"

#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.Internal.h"
#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorUtilityBlueprint.h"
#include "EditorUtilityBlueprintFactory.h"
#include "EditorUtilityObject.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "EditorUtilityWidgetBlueprintFactory.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace UnrealAgentMCP
{
	using namespace WidgetInternal;

	namespace
	{
		bool ReadAssetLocation(const TSharedPtr<FJsonObject>& Args, FString& OutName, FString& OutPackagePath, FString& OutError)
		{
			if (!RequireString(Args, TEXT("name"), OutName, OutError))
			{
				return false;
			}
			if (!Args->TryGetStringField(TEXT("packagePath"), OutPackagePath))
			{
				Args->TryGetStringField(TEXT("directory"), OutPackagePath);
			}
			if (OutPackagePath.IsEmpty())
			{
				OutPackagePath = TEXT("/Game");
			}
			if (!OutPackagePath.StartsWith(TEXT("/Game")))
			{
				OutError = TEXT("packagePath 必须位于 /Game。");
				return false;
			}
			return true;
		}

		bool SaveCreatedAsset(UObject* Asset, FString& OutError)
		{
			UEditorAssetSubsystem* Assets = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
			if (!Asset || !Assets || !Assets->SaveLoadedAsset(Asset, false))
			{
				OutError = TEXT("新建资产保存失败。");
				return false;
			}
			return true;
		}

		TSharedRef<FJsonObject> AssetResult(UObject* Asset)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Asset ? Asset->GetPathName() : FString());
			Result->SetStringField(TEXT("class"), Asset ? Asset->GetClass()->GetPathName() : FString());
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealWidgetAdapter::ExecuteAssetAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		if (Action == TEXT("run_utility_widget") || Action == TEXT("run_utility_blueprint"))
		{
			FString AssetPath;
			if (!RequireString(Args, TEXT("assetPath"), AssetPath, Error))
			{
				Args->TryGetStringField(TEXT("path"), AssetPath);
				if (AssetPath.IsEmpty())
				{
					return ErrorJson(Error);
				}
			}
			UEditorAssetSubsystem* Assets = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
			UEditorUtilitySubsystem* Utilities = GEditor ? GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>() : nullptr;
			if (!Assets || !Assets->DoesAssetExist(AssetPath))
			{
				return ErrorJson(FString::Printf(TEXT("未找到 Editor Utility 资产：%s"), *AssetPath));
			}
			UObject* Asset = Assets->LoadAsset(AssetPath);
			if (!Asset || !Utilities)
			{
				return ErrorJson(FString::Printf(TEXT("未找到 Editor Utility 资产或子系统不可用：%s"), *AssetPath));
			}
			TSharedRef<FJsonObject> Result = AssetResult(Asset);
			if (Action == TEXT("run_utility_widget"))
			{
				UEditorUtilityWidgetBlueprint* Blueprint = Cast<UEditorUtilityWidgetBlueprint>(Asset);
				if (!Blueprint)
				{
					return ErrorJson(TEXT("资产不是 EditorUtilityWidgetBlueprint。"));
				}
				FName TabId;
				UEditorUtilityWidget* Instance = Utilities->SpawnAndRegisterTabAndGetID(Blueprint, TabId);
				if (!Instance)
				{
					return ErrorJson(TEXT("Editor Utility Widget 启动失败。"));
				}
				Result->SetStringField(TEXT("tabId"), TabId.ToString());
				Result->SetStringField(TEXT("instance"), Instance->GetPathName());
				return SuccessJson(Result);
			}
			const bool bRan = Utilities->TryRun(Asset);
			Result->SetBoolField(TEXT("ran"), bRan);
			return bRan ? SuccessJson(Result) : ErrorJson(TEXT("Editor Utility Blueprint 执行失败。"));
		}

		FString Name;
		FString PackagePath;
		if (!ReadAssetLocation(Args, Name, PackagePath, Error))
		{
			return ErrorJson(Error);
		}
		IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
		UObject* Created = nullptr;
		if (Action == TEXT("create"))
		{
			UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
			FString ParentClassPath;
			if (Args->TryGetStringField(TEXT("parentClass"), ParentClassPath) && !ParentClassPath.IsEmpty())
			{
				UClass* Parent = StaticLoadClass(UUserWidget::StaticClass(), nullptr, *ParentClassPath);
				if (!Parent)
				{
					return ErrorJson(FString::Printf(TEXT("未找到 UserWidget 父类：%s"), *ParentClassPath));
				}
				Factory->ParentClass = Parent;
			}
			else
			{
				Factory->ParentClass = UUserWidget::StaticClass();
			}
			Created = AssetTools.CreateAsset(Name, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
			UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Created);
			FString RootClass;
			if (Blueprint && Args->TryGetStringField(TEXT("className"), RootClass) && !RootClass.IsEmpty())
			{
				UClass* Class = ResolveWidgetClass(RootClass, Error);
				if (!Class)
				{
					return ErrorJson(Error);
				}
				Blueprint->WidgetTree->RootWidget = Blueprint->WidgetTree->ConstructWidget<UWidget>(Class, TEXT("Root"));
				if (Blueprint->WidgetTree->RootWidget)
				{
					Blueprint->OnVariableAdded(Blueprint->WidgetTree->RootWidget->GetFName());
				}
				MarkBlueprintChanged(Blueprint);
			}
		}
		else if (Action == TEXT("create_utility_widget"))
		{
			UEditorUtilityWidgetBlueprintFactory* Factory = NewObject<UEditorUtilityWidgetBlueprintFactory>();
			Factory->ParentClass = UEditorUtilityWidget::StaticClass();
			Created = AssetTools.CreateAsset(Name, PackagePath, UEditorUtilityWidgetBlueprint::StaticClass(), Factory);
		}
		else if (Action == TEXT("create_utility_blueprint"))
		{
			UEditorUtilityBlueprintFactory* Factory = NewObject<UEditorUtilityBlueprintFactory>();
			Factory->ParentClass = UEditorUtilityObject::StaticClass();
			Created = AssetTools.CreateAsset(Name, PackagePath, UEditorUtilityBlueprint::StaticClass(), Factory);
		}
		if (!Created)
		{
			return ErrorJson(FString::Printf(TEXT("创建 Widget 资产失败：%s/%s"), *PackagePath, *Name));
		}
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Created))
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		if (!SaveCreatedAsset(Created, Error))
		{
			return ErrorJson(Error);
		}
		return SuccessJson(AssetResult(Created));
	}
}
