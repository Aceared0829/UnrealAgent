// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealPluginsAdapter.cpp
 * @brief UE 原生插件、模块与依赖清单的 JSON 投影实现。
 */

#include "Adapters/Unreal/Plugins/UnrealAgentMCPUnrealPluginsAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "ModuleDescriptor.h"
#include "PluginDescriptor.h"
#include "PluginReferenceDescriptor.h"

namespace UnrealAgentMCP
{
	TArray<FString> FUnrealAgentMCPUnrealPluginsAdapter::GetImplementedActions()
	{
		return { TEXT("list"), TEXT("describe") };
	}

	FString FUnrealAgentMCPUnrealPluginsAdapter::Execute(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("action"), Action);
		}
		const TSharedRef<FJsonObject> SafeArgs = Args.IsValid() ? MakeShared<FJsonObject>(*Args) : MakeShared<FJsonObject>();
		if (Action == TEXT("list"))
		{
			return List(SafeArgs);
		}
		if (Action == TEXT("describe"))
		{
			return Describe(SafeArgs);
		}

		TArray<TSharedPtr<FJsonValue>> Actions;
		for (const FString& Name : GetImplementedActions())
		{
			Actions.Add(MakeShared<FJsonValueString>(Name));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("domain"), TEXT("plugins"));
		Result->SetStringField(TEXT("action"), Action);
		Result->SetStringField(TEXT("error"), Action.IsEmpty() ? TEXT("缺少必填 action。") : FString::Printf(TEXT("Plugins action '%s' 尚未迁移。"), *Action));
		Result->SetArrayField(TEXT("implementedActions"), Actions);
		return JsonObjectToString(Result);
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealPluginsAdapter::MakeSummaryJson(const TSharedRef<IPlugin>& Plugin)
	{
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Plugin->GetName());
		Json->SetStringField(TEXT("friendlyName"), Plugin->GetFriendlyName());
		Json->SetStringField(TEXT("version"), Descriptor.VersionName);
		Json->SetStringField(TEXT("description"), Descriptor.Description);
		Json->SetStringField(TEXT("category"), Descriptor.Category);
		Json->SetBoolField(TEXT("enabled"), Plugin->IsEnabled());
		Json->SetBoolField(TEXT("mounted"), Plugin->IsMounted());
		Json->SetBoolField(TEXT("canContainContent"), Plugin->CanContainContent());
		Json->SetNumberField(TEXT("moduleCount"), Descriptor.Modules.Num());
		Json->SetNumberField(TEXT("dependencyCount"), Descriptor.Plugins.Num());
		return Json;
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealPluginsAdapter::MakeDetailJson(const TSharedRef<IPlugin>& Plugin)
	{
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
		TSharedRef<FJsonObject> Json = MakeSummaryJson(Plugin);
		Json->SetStringField(TEXT("descriptorFile"), Plugin->GetDescriptorFileName());
		Json->SetStringField(TEXT("baseDir"), Plugin->GetBaseDir());
		Json->SetStringField(TEXT("contentDir"), Plugin->GetContentDir());
		Json->SetStringField(TEXT("mountedAssetPath"), Plugin->GetMountedAssetPath());
		Json->SetStringField(TEXT("createdBy"), Descriptor.CreatedBy);
		Json->SetStringField(TEXT("docsUrl"), Descriptor.DocsURL);
		Json->SetStringField(TEXT("engineVersion"), Descriptor.EngineVersion);

		TArray<TSharedPtr<FJsonValue>> Modules;
		for (const FModuleDescriptor& Module : Descriptor.Modules)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Module.Name.ToString());
			Item->SetStringField(TEXT("type"), EHostType::ToString(Module.Type));
			Item->SetStringField(TEXT("loadingPhase"), ELoadingPhase::ToString(Module.LoadingPhase));
			Item->SetBoolField(TEXT("compiledForCurrentTarget"), Module.IsCompiledInCurrentConfiguration());
			Item->SetBoolField(TEXT("loadedForCurrentTarget"), Module.IsLoadedInCurrentConfiguration());
			Modules.Add(MakeShared<FJsonValueObject>(Item));
		}
		Json->SetArrayField(TEXT("modules"), Modules);

		TArray<TSharedPtr<FJsonValue>> Dependencies;
		for (const FPluginReferenceDescriptor& Dependency : Descriptor.Plugins)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Dependency.Name);
			Item->SetBoolField(TEXT("enabled"), Dependency.bEnabled);
			Item->SetBoolField(TEXT("optional"), Dependency.bOptional);
			Item->SetStringField(TEXT("description"), Dependency.Description);
			Dependencies.Add(MakeShared<FJsonValueObject>(Item));
		}
		Json->SetArrayField(TEXT("dependencies"), Dependencies);
		return Json;
	}

	FString FUnrealAgentMCPUnrealPluginsAdapter::List(const TSharedPtr<FJsonObject>& Args)
	{
		bool bEnabledOnly = false;
		Args->TryGetBoolField(TEXT("enabledOnly"), bEnabledOnly);
		FString NameFilter;
		Args->TryGetStringField(TEXT("nameFilter"), NameFilter);
		double LimitNumber = 500.0;
		Args->TryGetNumberField(TEXT("limit"), LimitNumber);
		const int32 Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, 2000);

		TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetDiscoveredPlugins();
		Plugins.Sort(
			[](const TSharedRef<IPlugin>& Left, const TSharedRef<IPlugin>& Right)
			{
				return Left->GetName() < Right->GetName();
			});

		TArray<TSharedPtr<FJsonValue>> Items;
		int32 MatchedCount = 0;
		for (const TSharedRef<IPlugin>& Plugin : Plugins)
		{
			if (bEnabledOnly && !Plugin->IsEnabled())
				continue;
			if (!NameFilter.IsEmpty() && !Plugin->GetName().Contains(NameFilter, ESearchCase::IgnoreCase) &&
				!Plugin->GetFriendlyName().Contains(NameFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			++MatchedCount;
			if (Items.Num() < Limit)
			{
				Items.Add(MakeShared<FJsonValueObject>(MakeSummaryJson(Plugin)));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Items.Num());
		Result->SetNumberField(TEXT("matchedCount"), MatchedCount);
		Result->SetBoolField(TEXT("truncated"), MatchedCount > Items.Num());
		Result->SetStringField(TEXT("provider"), TEXT("Unreal IPluginManager"));
		Result->SetArrayField(TEXT("plugins"), Items);
		return SuccessJson(Result);
	}

	FString FUnrealAgentMCPUnrealPluginsAdapter::Describe(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
		{
			return ErrorJson(TEXT("缺少必填 name。"));
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name);
		if (!Plugin.IsValid())
		{
			return ErrorJson(FString::Printf(TEXT("未找到 UE 插件：%s"), *Name));
		}
		return SuccessJson(MakeDetailJson(Plugin.ToSharedRef()));
	}
}
