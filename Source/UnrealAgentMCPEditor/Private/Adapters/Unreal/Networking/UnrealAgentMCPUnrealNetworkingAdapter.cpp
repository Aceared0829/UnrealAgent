// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealNetworkingAdapter.cpp
 * @brief Actor Blueprint 网络默认值写入、读取和持久化实现。
 */

#include "Adapters/Unreal/Networking/UnrealAgentMCPUnrealNetworkingAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Serialization/UnrealAgentMCPJsonConversion.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UnrealAgentMCP
{
	namespace
	{
		bool TryGetBoolWithDefault(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, const bool DefaultValue, bool& OutValue)
		{
			OutValue = DefaultValue;
			if (!Args->HasField(Field))
				return true;
			return Args->TryGetBoolField(Field, OutValue);
		}

		bool TryParseDormancy(FString Value, ENetDormancy& OutDormancy)
		{
			Value.TrimStartAndEndInline();
			Value.ToLowerInline();
			Value.RemoveFromStart(TEXT("dorm_"));
			if (Value == TEXT("never"))
				OutDormancy = DORM_Never;
			else if (Value == TEXT("awake"))
				OutDormancy = DORM_Awake;
			else if (Value == TEXT("dormantall") || Value == TEXT("dormant_all"))
			{
				OutDormancy = DORM_DormantAll;
			}
			else if (Value == TEXT("dormantpartial") || Value == TEXT("dormant_partial"))
			{
				OutDormancy = DORM_DormantPartial;
			}
			else if (Value == TEXT("initial"))
				OutDormancy = DORM_Initial;
			else
				return false;
			return true;
		}
	}

	UBlueprint* FUnrealAgentMCPUnrealNetworkingAdapter::ResolveBlueprint(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		FString BlueprintPath;
		if (!Args->TryGetStringField(TEXT("blueprintPath"), BlueprintPath) || BlueprintPath.IsEmpty())
		{
			OutError = TEXT("缺少必填 blueprintPath。");
			return nullptr;
		}
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *JsonConversion::NormalizeAssetObjectPath(BlueprintPath));
		if (!Blueprint)
		{
			OutError = FString::Printf(TEXT("未找到 Blueprint：%s"), *BlueprintPath);
			return nullptr;
		}
		return Blueprint;
	}

	AActor* FUnrealAgentMCPUnrealNetworkingAdapter::ResolveActorDefaults(UBlueprint* Blueprint, FString& OutError)
	{
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			OutError = TEXT("Blueprint 尚未生成有效类。");
			return nullptr;
		}
		AActor* Defaults = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!Defaults)
		{
			OutError = TEXT("Blueprint 生成类不是 Actor 子类。");
		}
		return Defaults;
	}

	bool FUnrealAgentMCPUnrealNetworkingAdapter::SaveBlueprint(UBlueprint* Blueprint, FString& OutError)
	{
		UPackage* Package = Blueprint ? Blueprint->GetOutermost() : nullptr;
		if (!Package)
		{
			OutError = TEXT("Blueprint Package 无效。");
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
		{
			OutError = TEXT("Blueprint 资产保存失败。");
			return false;
		}
		return true;
	}

	TSharedRef<FJsonObject> FUnrealAgentMCPUnrealNetworkingAdapter::MakeInfoJson(UBlueprint* Blueprint, AActor* Defaults)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Result->SetStringField(TEXT("generatedClass"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : FString());
		Result->SetBoolField(TEXT("replicates"), Defaults->GetIsReplicated());
		Result->SetBoolField(TEXT("replicateMovement"), Defaults->IsReplicatingMovement());
		Result->SetNumberField(TEXT("netUpdateFrequency"), Defaults->GetNetUpdateFrequency());
		Result->SetNumberField(TEXT("minNetUpdateFrequency"), Defaults->GetMinNetUpdateFrequency());
		Result->SetStringField(TEXT("dormancy"), UEnum::GetValueAsString(static_cast<ENetDormancy>(Defaults->NetDormancy)));
		Result->SetBoolField(TEXT("netLoadOnClient"), Defaults->bNetLoadOnClient);
		Result->SetBoolField(TEXT("alwaysRelevant"), Defaults->bAlwaysRelevant);
		Result->SetBoolField(TEXT("onlyRelevantToOwner"), Defaults->bOnlyRelevantToOwner);
		Result->SetNumberField(TEXT("netCullDistanceSquared"), Defaults->GetNetCullDistanceSquared());
		Result->SetNumberField(TEXT("netPriority"), Defaults->NetPriority);

		TArray<TSharedPtr<FJsonValue>> Variables;
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Variable.VarName.ToString());
			const bool bRepNotify = (Variable.PropertyFlags & CPF_RepNotify) != 0;
			const bool bReplicated = (Variable.PropertyFlags & CPF_Net) != 0;
			Item->SetStringField(TEXT("replicationType"), bRepNotify ? TEXT("RepNotify") : bReplicated ? TEXT("Replicated") : TEXT("None"));
			Item->SetStringField(TEXT("repNotifyFunction"), Variable.RepNotifyFunc.ToString());
			Variables.Add(MakeShared<FJsonValueObject>(Item));
		}
		Result->SetArrayField(TEXT("variables"), Variables);
		return Result;
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::ApplyActorDefaults(const TSharedPtr<FJsonObject>& Args, TFunctionRef<bool(AActor*, FString&)> Mutator)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, Error);
		AActor* Defaults = Blueprint ? ResolveActorDefaults(Blueprint, Error) : nullptr;
		if (!Defaults)
			return ErrorJson(Error);

		Blueprint->Modify();
		Defaults->Modify();
		if (!Mutator(Defaults, Error))
			return ErrorJson(Error);
		Defaults->PostEditChange();
		Blueprint->MarkPackageDirty();
		if (!SaveBlueprint(Blueprint, Error))
			return ErrorJson(Error);
		return SuccessJson(MakeInfoJson(Blueprint, Defaults));
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetReplicates(const TSharedPtr<FJsonObject>& Args)
	{
		bool bValue = true;
		if (!TryGetBoolWithDefault(Args, TEXT("replicates"), true, bValue))
		{
			return ErrorJson(TEXT("replicates 必须是布尔值。"));
		}
		return ApplyActorDefaults(Args,
			[bValue](AActor* Defaults, FString&)
			{
				Defaults->SetReplicates(bValue);
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::ConfigureNetFrequency(const TSharedPtr<FJsonObject>& Args)
	{
		double Update = 0.0;
		double Minimum = 0.0;
		const bool bHasUpdate = Args->TryGetNumberField(TEXT("netUpdateFrequency"), Update);
		const bool bHasMinimum = Args->TryGetNumberField(TEXT("minNetUpdateFrequency"), Minimum);
		if (!bHasUpdate && !bHasMinimum)
		{
			return ErrorJson(TEXT("至少提供一个网络频率字段。"));
		}
		if ((bHasUpdate && Update < 0.0) || (bHasMinimum && Minimum < 0.0))
		{
			return ErrorJson(TEXT("网络频率不能为负数。"));
		}
		return ApplyActorDefaults(Args,
			[bHasUpdate, bHasMinimum, Update, Minimum](AActor* Defaults, FString&)
			{
				if (bHasUpdate)
					Defaults->SetNetUpdateFrequency(Update);
				if (bHasMinimum)
					Defaults->SetMinNetUpdateFrequency(Minimum);
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetDormancy(const TSharedPtr<FJsonObject>& Args)
	{
		FString Value;
		ENetDormancy Dormancy = DORM_Awake;
		if (!Args->TryGetStringField(TEXT("dormancy"), Value) || !TryParseDormancy(Value, Dormancy))
		{
			return ErrorJson(TEXT("dormancy 必须是 Never、Awake、DormantAll、DormantPartial 或 Initial。"));
		}
		return ApplyActorDefaults(Args,
			[Dormancy](AActor* Defaults, FString&)
			{
				Defaults->NetDormancy = Dormancy;
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetNetLoadOnClient(const TSharedPtr<FJsonObject>& Args)
	{
		bool bValue = true;
		if (!TryGetBoolWithDefault(Args, TEXT("loadOnClient"), true, bValue))
		{
			return ErrorJson(TEXT("loadOnClient 必须是布尔值。"));
		}
		return ApplyActorDefaults(Args,
			[bValue](AActor* Defaults, FString&)
			{
				Defaults->bNetLoadOnClient = bValue;
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetAlwaysRelevant(const TSharedPtr<FJsonObject>& Args)
	{
		bool bValue = true;
		if (!TryGetBoolWithDefault(Args, TEXT("alwaysRelevant"), true, bValue))
		{
			return ErrorJson(TEXT("alwaysRelevant 必须是布尔值。"));
		}
		return ApplyActorDefaults(Args,
			[bValue](AActor* Defaults, FString&)
			{
				Defaults->bAlwaysRelevant = bValue;
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetOnlyRelevantToOwner(const TSharedPtr<FJsonObject>& Args)
	{
		bool bValue = true;
		if (!TryGetBoolWithDefault(Args, TEXT("onlyRelevantToOwner"), true, bValue))
		{
			return ErrorJson(TEXT("onlyRelevantToOwner 必须是布尔值。"));
		}
		return ApplyActorDefaults(Args,
			[bValue](AActor* Defaults, FString&)
			{
				Defaults->bOnlyRelevantToOwner = bValue;
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::ConfigureCullDistance(const TSharedPtr<FJsonObject>& Args)
	{
		double Value = 0.0;
		if (!Args->TryGetNumberField(TEXT("netCullDistanceSquared"), Value) || Value < 0.0)
		{
			return ErrorJson(TEXT("netCullDistanceSquared 必须是非负数。"));
		}
		return ApplyActorDefaults(Args,
			[Value](AActor* Defaults, FString&)
			{
				Defaults->SetNetCullDistanceSquared(Value);
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetPriority(const TSharedPtr<FJsonObject>& Args)
	{
		double Value = 0.0;
		if (!Args->TryGetNumberField(TEXT("netPriority"), Value) || Value < 0.0)
		{
			return ErrorJson(TEXT("netPriority 必须是非负数。"));
		}
		return ApplyActorDefaults(Args,
			[Value](AActor* Defaults, FString&)
			{
				Defaults->NetPriority = Value;
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::SetReplicateMovement(const TSharedPtr<FJsonObject>& Args)
	{
		bool bValue = true;
		if (!TryGetBoolWithDefault(Args, TEXT("replicateMovement"), true, bValue))
		{
			return ErrorJson(TEXT("replicateMovement 必须是布尔值。"));
		}
		return ApplyActorDefaults(Args,
			[bValue](AActor* Defaults, FString&)
			{
				Defaults->SetReplicateMovement(bValue);
				return true;
			});
	}

	FString FUnrealAgentMCPUnrealNetworkingAdapter::GetInfo(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UBlueprint* Blueprint = ResolveBlueprint(Args, Error);
		AActor* Defaults = Blueprint ? ResolveActorDefaults(Blueprint, Error) : nullptr;
		return Defaults ? SuccessJson(MakeInfoJson(Blueprint, Defaults)) : ErrorJson(Error);
	}
}
