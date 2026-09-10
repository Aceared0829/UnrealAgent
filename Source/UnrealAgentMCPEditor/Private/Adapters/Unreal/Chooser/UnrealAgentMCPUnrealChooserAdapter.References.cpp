// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealChooserAdapter.References.cpp
 * @brief Chooser 嵌套结构中的对象引用枚举与安全重映射。
 */

#include "Adapters/Unreal/Chooser/UnrealAgentMCPUnrealChooserAdapter.h"

#include "Chooser.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		using FReferenceVisitor = TFunction<void(UChooserTable*, const FString&, const UStruct*, FProperty*, void*)>;

		class FChooserReferenceWalker
		{
		public:
			explicit FChooserReferenceWalker(FReferenceVisitor InVisitor) : Visitor(MoveTemp(InVisitor))
			{
			}

			void Walk(UChooserTable* Table)
			{
				WalkTable(Table);
			}

		private:
			void WalkTable(UChooserTable* Table)
			{
				if (!Table || Visited.Contains(Table))
				{
					return;
				}
				Visited.Add(Table);
#if WITH_EDITORONLY_DATA
				for (int32 Index = 0; Index < Table->ResultsStructs.Num(); ++Index)
				{
					WalkInstancedStruct(Table, Table->ResultsStructs[Index], FString::Printf(TEXT("ResultsStructs[%d]"), Index));
				}
#endif
				WalkInstancedStruct(Table, Table->FallbackResult, TEXT("FallbackResult"));
				for (int32 Index = 0; Index < Table->ColumnsStructs.Num(); ++Index)
				{
					WalkInstancedStruct(Table, Table->ColumnsStructs[Index], FString::Printf(TEXT("ColumnsStructs[%d]"), Index));
				}
			}

			void WalkInstancedStruct(UChooserTable* Owner, FInstancedStruct& Value, const FString& Location)
			{
				if (!Value.IsValid())
				{
					return;
				}
				WalkStruct(Owner, Value.GetScriptStruct(), Value.GetMutableMemory(), Location);
			}

			void WalkStruct(UChooserTable* Owner, const UStruct* Struct, void* Data, const FString& Location)
			{
				for (TFieldIterator<FProperty> It(Struct); It; ++It)
				{
					FProperty* Property = *It;
					void* Value = Property->ContainerPtrToValuePtr<void>(Data);
					WalkProperty(Owner, Struct, Property, Value, Location + TEXT(".") + Property->GetName());
				}
			}

			void WalkProperty(UChooserTable* Owner, const UStruct* ContainerStruct, FProperty* Property, void* Value, const FString& Location)
			{
				if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
					if (UChooserTable* Nested = Cast<UChooserTable>(Object))
					{
						WalkTable(Nested);
						return;
					}
					Visitor(Owner, Location, ContainerStruct, Property, Value);
					return;
				}
				if (FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
				{
					const FSoftObjectPtr Soft = SoftProperty->GetPropertyValue(Value);
					if (UChooserTable* Nested = Cast<UChooserTable>(Soft.Get()))
					{
						WalkTable(Nested);
						return;
					}
					Visitor(Owner, Location, ContainerStruct, Property, Value);
					return;
				}
				if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
				{
					FScriptArrayHelper Array(ArrayProperty, Value);
					for (int32 Index = 0; Index < Array.Num(); ++Index)
					{
						WalkProperty(Owner, ContainerStruct, ArrayProperty->Inner, Array.GetRawPtr(Index), FString::Printf(TEXT("%s[%d]"), *Location, Index));
					}
					return;
				}
				if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					if (StructProperty->Struct == FInstancedStruct::StaticStruct())
					{
						WalkInstancedStruct(Owner, *static_cast<FInstancedStruct*>(Value), Location);
					}
					else
					{
						WalkStruct(Owner, StructProperty->Struct, Value, Location);
					}
				}
			}

			FReferenceVisitor Visitor;
			TSet<TObjectPtr<UChooserTable>> Visited;
		};

		FString ObjectPathForProperty(FProperty* Property, void* Value, UObject*& OutObject)
		{
			OutObject = nullptr;
			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				OutObject = ObjectProperty->GetObjectPropertyValue(Value);
				return OutObject ? OutObject->GetPathName() : FString();
			}
			if (FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
			{
				const FSoftObjectPtr Soft = SoftProperty->GetPropertyValue(Value);
				OutObject = Soft.Get();
				return Soft.ToSoftObjectPath().ToString();
			}
			return FString();
		}

		bool MatchesClass(UObject* Object, const FString& Filter)
		{
			if (Filter.IsEmpty())
			{
				return true;
			}
			return Object &&
				(Object->GetClass()->GetName().Contains(Filter, ESearchCase::IgnoreCase) || Object->GetClass()->GetPathName().Contains(Filter, ESearchCase::IgnoreCase));
		}

		bool ExactPathMatch(const FString& Current, const FString& Expected)
		{
			if (Current.Equals(Expected, ESearchCase::IgnoreCase))
			{
				return true;
			}
			const FSoftObjectPath CurrentPath(Current);
			return CurrentPath.GetLongPackageName().Equals(Expected, ESearchCase::IgnoreCase);
		}

		bool ComputeReplacement(const FString& Current, const FString& From, const FString& To, const FString& FromPrefix, const FString& ToPrefix, FString& OutReplacement)
		{
			if (!From.IsEmpty())
			{
				if (!ExactPathMatch(Current, From))
				{
					return false;
				}
				OutReplacement = To;
				return true;
			}
			if (!FromPrefix.IsEmpty() && Current.StartsWith(FromPrefix, ESearchCase::IgnoreCase))
			{
				OutReplacement = ToPrefix + Current.Mid(FromPrefix.Len());
				return true;
			}
			return false;
		}

		UObject* LoadReplacementObject(const FString& Path)
		{
			UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
			if (!Object && Path.StartsWith(TEXT("/Game/")))
			{
				const FString Name = FPackageName::GetLongPackageAssetName(Path);
				Object = StaticLoadObject(UObject::StaticClass(), nullptr, *(Path + TEXT(".") + Name));
			}
			return Object;
		}
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::ListObjectReferences(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("assetPath"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
		FString ClassFilter;
		FString PathFilter;
		Args->TryGetStringField(TEXT("classFilter"), ClassFilter);
		Args->TryGetStringField(TEXT("pathFilter"), PathFilter);
		TArray<TSharedPtr<FJsonValue>> References;
		FChooserReferenceWalker Walker(
			[&References, &ClassFilter, &PathFilter](UChooserTable* Owner, const FString& Location, const UStruct* ContainerStruct, FProperty* Property, void* Value)
			{
				UObject* Object = nullptr;
				const FString Path = ObjectPathForProperty(Property, Value, Object);
				if (Path.IsEmpty() || (!PathFilter.IsEmpty() && !Path.Contains(PathFilter, ESearchCase::IgnoreCase)) || !MatchesClass(Object, ClassFilter))
				{
					return;
				}
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("ownerTable"), Owner->GetPathName());
				Entry->SetStringField(TEXT("location"), Location);
				Entry->SetStringField(TEXT("structType"), ContainerStruct->GetName());
				Entry->SetStringField(TEXT("propertyType"), Property->GetCPPType());
				Entry->SetStringField(TEXT("objectPath"), Path);
				Entry->SetStringField(TEXT("objectClass"), Object ? Object->GetClass()->GetPathName() : TEXT(""));
				References.Add(MakeShared<FJsonValueObject>(Entry));
			});
		Walker.Walk(Table);
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("table"), Table->GetPathName());
		Result->SetNumberField(TEXT("referenceCount"), References.Num());
		Result->SetArrayField(TEXT("references"), MoveTemp(References));
		return JsonObjectToString(Result);
	}

	FString FUnrealAgentMCPUnrealChooserAdapter::RemapObjectReferences(const TSharedPtr<FJsonObject>& Args)
	{
		FString Error;
		UChooserTable* Table = LoadTable(Args, TEXT("assetPath"), Error);
		if (!Table)
		{
			return ErrorJson(Error);
		}
		FString From;
		FString To;
		FString FromPrefix;
		FString ToPrefix;
		Args->TryGetStringField(TEXT("from"), From);
		Args->TryGetStringField(TEXT("to"), To);
		Args->TryGetStringField(TEXT("fromPrefix"), FromPrefix);
		Args->TryGetStringField(TEXT("toPrefix"), ToPrefix);
		const bool bExact = !From.IsEmpty() && !To.IsEmpty();
		const bool bPrefix = !FromPrefix.IsEmpty() && !ToPrefix.IsEmpty();
		if (bExact == bPrefix)
		{
			return ErrorJson(TEXT("必须且只能提供 from+to 或 fromPrefix+toPrefix。"));
		}
		bool bDryRun = true;
		bool bAllowMissing = false;
		Args->TryGetBoolField(TEXT("dryRun"), bDryRun);
		Args->TryGetBoolField(TEXT("allowMissing"), bAllowMissing);

		TArray<TSharedPtr<FJsonValue>> Changes;
		TArray<FString> Failures;
		TSet<TObjectPtr<UChooserTable>> TouchedTables;
		FChooserReferenceWalker Walker(
			[&](UChooserTable* Owner, const FString& Location, const UStruct* ContainerStruct, FProperty* Property, void* Value)
			{
				UObject* CurrentObject = nullptr;
				const FString Current = ObjectPathForProperty(Property, Value, CurrentObject);
				FString Replacement;
				if (Current.IsEmpty() || !ComputeReplacement(Current, From, To, FromPrefix, ToPrefix, Replacement))
				{
					return;
				}

				bool bCanApply = true;
				FString Failure;
				if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					UObject* ReplacementObject = LoadReplacementObject(Replacement);
					if (!ReplacementObject)
					{
						bCanApply = false;
						Failure = bAllowMissing ? TEXT("硬对象属性不能写入尚不存在的目标。") : TEXT("目标对象不存在。");
					}
					else if (!ReplacementObject->IsA(ObjectProperty->PropertyClass))
					{
						bCanApply = false;
						Failure = TEXT("目标对象类型不兼容。");
					}
					else if (!bDryRun)
					{
						Owner->Modify();
						ObjectProperty->SetObjectPropertyValue(Value, ReplacementObject);
						TouchedTables.Add(Owner);
					}
				}
				else if (FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
				{
					UObject* ReplacementObject = LoadReplacementObject(Replacement);
					if (ReplacementObject && !ReplacementObject->IsA(SoftProperty->PropertyClass))
					{
						bCanApply = false;
						Failure = TEXT("目标软对象类型不兼容。");
					}
					else if (!ReplacementObject && !bAllowMissing)
					{
						bCanApply = false;
						Failure = TEXT("目标软对象不存在。");
					}
					else if (!bDryRun)
					{
						Owner->Modify();
						SoftProperty->SetPropertyValue(Value, FSoftObjectPtr(FSoftObjectPath(Replacement)));
						TouchedTables.Add(Owner);
					}
				}
				if (!bCanApply)
				{
					Failures.Add(FString::Printf(TEXT("%s：%s"), *Location, *Failure));
				}
				TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
				Change->SetStringField(TEXT("ownerTable"), Owner->GetPathName());
				Change->SetStringField(TEXT("location"), Location);
				Change->SetStringField(TEXT("structType"), ContainerStruct->GetName());
				Change->SetStringField(TEXT("from"), Current);
				Change->SetStringField(TEXT("to"), Replacement);
				Change->SetBoolField(TEXT("compatible"), bCanApply);
				Change->SetBoolField(TEXT("applied"), bCanApply && !bDryRun);
				if (!Failure.IsEmpty())
				{
					Change->SetStringField(TEXT("reason"), Failure);
				}
				Changes.Add(MakeShared<FJsonValueObject>(Change));
			});
		Walker.Walk(Table);
		for (UChooserTable* Touched : TouchedTables)
		{
			Touched->Compile(true);
			Touched->MarkPackageDirty();
		}
		if (!Failures.IsEmpty())
		{
			return ErrorJson(FString::Join(Failures, TEXT(" | ")));
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("table"), Table->GetPathName());
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		Result->SetNumberField(TEXT("matchedCount"), Changes.Num());
		Result->SetNumberField(TEXT("appliedCount"), bDryRun ? 0 : Changes.Num());
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetArrayField(TEXT("changes"), MoveTemp(Changes));
		return JsonObjectToString(Result);
	}
}
