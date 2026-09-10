// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPReflection.cpp
 * @brief 独立的反射工具发现、JSON Schema 构建与调用实现。
 */

#include "Core/Reflection/UnrealAgentMCPReflection.h"

#include "Core/Reflection/UnrealAgentMCPPropertyTypeAdapter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ScopeExit.h"
#include "UObject/EnumProperty.h"
#include "UObject/StrProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP::Reflection
{
	namespace
	{
		bool TryParseRisk(const FString& Value, EMcpToolRisk& OutRisk)
		{
			static const TMap<FString, EMcpToolRisk> Risks = { { TEXT("ReadOnly"), EMcpToolRisk::ReadOnly }, { TEXT("EditorState"), EMcpToolRisk::EditorState },
				{ TEXT("ContentMutation"), EMcpToolRisk::ContentMutation }, { TEXT("FileMutation"), EMcpToolRisk::FileMutation },
				{ TEXT("Destructive"), EMcpToolRisk::Destructive }, { TEXT("CodeExecution"), EMcpToolRisk::CodeExecution },
				{ TEXT("ExternalProcess"), EMcpToolRisk::ExternalProcess } };
			if (const EMcpToolRisk* Risk = Risks.Find(Value))
			{
				OutRisk = *Risk;
				return true;
			}
			return false;
		}

		bool TryParseTransactionPolicy(const FString& Value, EMcpToolTransactionPolicy& OutPolicy)
		{
			static const TMap<FString, EMcpToolTransactionPolicy> Policies = { { TEXT("None"), EMcpToolTransactionPolicy::None },
				{ TEXT("ReadOnly"), EMcpToolTransactionPolicy::ReadOnly }, { TEXT("ScopedTransaction"), EMcpToolTransactionPolicy::ScopedTransaction },
				{ TEXT("Atomic"), EMcpToolTransactionPolicy::Atomic }, { TEXT("Compensating"), EMcpToolTransactionPolicy::Compensating } };
			if (const EMcpToolTransactionPolicy* Policy = Policies.Find(Value))
			{
				OutPolicy = *Policy;
				return true;
			}
			return false;
		}

		bool TryParseBooleanMetadata(const FString& Value, bool& bOutValue)
		{
			if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bOutValue = true;
				return true;
			}
			if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bOutValue = false;
				return true;
			}
			return false;
		}

		bool IsInputParameter(const FProperty* Property)
		{
			if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				return false;
			}
			const bool bPureOutput = Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm);
			return !bPureOutput;
		}

		bool IsOutputParameter(const FProperty* Property)
		{
			return Property->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ConstParm);
		}

		FString GetParameterName(const FProperty* Property)
		{
			const FString AuthoredName = Property->GetAuthoredName();
			return AuthoredName.IsEmpty() ? Property->GetName() : AuthoredName;
		}

		bool TryGetParameterDefault(const UFunction* Function, const FString& Name, FString& OutDefault)
		{
#if WITH_METADATA
			const FString NativeDefaultKey = FString::Printf(TEXT("CPP_Default_%s"), *Name);
			if (Function->HasMetaData(*NativeDefaultKey))
			{
				OutDefault = Function->GetMetaData(*NativeDefaultKey);
				return true;
			}

			const FString MCPDefaultKey = FString(DefaultValueMetadataPrefix) + Name;
			if (Function->HasMetaData(*MCPDefaultKey))
			{
				OutDefault = Function->GetMetaData(*MCPDefaultKey);
				return true;
			}
#endif
			return false;
		}

		bool IsOptionalParameter(const UFunction* Function, const FString& Name)
		{
			FString UnusedDefault;
			if (TryGetParameterDefault(Function, Name, UnusedDefault))
			{
				return true;
			}

			TArray<FString> OptionalNames;
#if WITH_METADATA
			Function->GetMetaData(OptionalParametersMetadata).ParseIntoArray(OptionalNames, TEXT(","), true);
#endif
			return OptionalNames.ContainsByPredicate(
				[&Name](const FString& Candidate)
				{
					return Candidate.TrimStartAndEnd().Equals(Name, ESearchCase::IgnoreCase);
				});
		}

		bool TryBuildParameterDefaultJson(const UFunction* Function, const FProperty* Property, const FString& Name, TSharedPtr<FJsonValue>& OutDefault)
		{
			FString DefaultText;
			if (!TryGetParameterDefault(Function, Name, DefaultText))
			{
				return false;
			}
			void* DefaultValue = Property->AllocateAndInitializeValue();
			if (!DefaultValue)
			{
				return false;
			}
			ON_SCOPE_EXIT
			{
				Property->DestroyAndFreeValue(DefaultValue);
			};
			if (!Property->ImportText_Direct(*DefaultText, DefaultValue, nullptr, PPF_None))
			{
				return false;
			}
			FString Error;
			return FPropertyTypeAdapterRegistry::GetDefault().Write(Property, DefaultValue, OutDefault, Error);
		}

		TSharedRef<FJsonObject> PropertySchema(const FProperty* Property);

		TSharedRef<FJsonObject> StructSchema(const UStruct* Struct)
		{
			TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Required;
			for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				const FProperty* Field = *It;
				if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
				{
					continue;
				}
				const FString Name = GetParameterName(Field);
				Properties->SetObjectField(Name, PropertySchema(Field));
				Required.Add(MakeShared<FJsonValueString>(Name));
			}
			Schema->SetObjectField(TEXT("properties"), Properties);
			if (!Required.IsEmpty())
			{
				Schema->SetArrayField(TEXT("required"), MoveTemp(Required));
			}
			Schema->SetBoolField(TEXT("additionalProperties"), false);
			return Schema;
		}

		void AddDescription(const FProperty* Property, const TSharedRef<FJsonObject>& Schema)
		{
#if WITH_METADATA
			FString Description = Property->GetMetaData(TEXT("ToolTip"));
			if (Description.IsEmpty())
			{
				Description = Property->GetMetaData(TEXT("DisplayName"));
			}
			if (!Description.IsEmpty())
			{
				Schema->SetStringField(TEXT("description"), Description);
			}
#endif
		}

		TSharedRef<FJsonObject> PropertySchema(const FProperty* Property)
		{
			TSharedPtr<FJsonObject> AdapterSchema;
			FString AdapterError;
			if (FPropertyTypeAdapterRegistry::GetDefault().BuildSchema(Property, AdapterSchema, AdapterError))
			{
				return AdapterSchema.ToSharedRef();
			}

			TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();

			if (CastField<FBoolProperty>(Property))
			{
				Schema->SetStringField(TEXT("type"), TEXT("boolean"));
			}
			else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
			{
				Schema->SetStringField(TEXT("type"), Numeric->IsInteger() ? TEXT("integer") : TEXT("number"));
			}
			else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
			{
				Schema->SetStringField(TEXT("type"), TEXT("string"));
				TArray<TSharedPtr<FJsonValue>> Values;
				if (const UEnum* Enum = EnumProperty->GetEnum())
				{
					for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
					{
						bool bHidden = false;
#if WITH_METADATA
						bHidden = Enum->HasMetaData(TEXT("Hidden"), Index);
#endif
						if (!bHidden)
						{
							Values.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
						}
					}
				}
				Schema->SetArrayField(TEXT("enum"), MoveTemp(Values));
			}
			else if (const FByteProperty* Byte = CastField<FByteProperty>(Property); Byte && Byte->Enum)
			{
				Schema->SetStringField(TEXT("type"), TEXT("string"));
				TArray<TSharedPtr<FJsonValue>> Values;
				for (int32 Index = 0; Index < Byte->Enum->NumEnums() - 1; ++Index)
				{
					Values.Add(MakeShared<FJsonValueString>(Byte->Enum->GetNameStringByIndex(Index)));
				}
				Schema->SetArrayField(TEXT("enum"), MoveTemp(Values));
			}
			else if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property) || CastField<FObjectPropertyBase>(Property) ||
				CastField<FClassProperty>(Property) || CastField<FSoftObjectProperty>(Property) || CastField<FSoftClassProperty>(Property))
			{
				Schema->SetStringField(TEXT("type"), TEXT("string"));
			}
			else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				Schema = StructSchema(Struct->Struct);
			}
			else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				Schema->SetStringField(TEXT("type"), TEXT("array"));
				Schema->SetObjectField(TEXT("items"), PropertySchema(Array->Inner));
			}
			else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
			{
				Schema->SetStringField(TEXT("type"), TEXT("array"));
				Schema->SetObjectField(TEXT("items"), PropertySchema(Set->ElementProp));
				Schema->SetBoolField(TEXT("uniqueItems"), true);
			}
			else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
			{
				Schema->SetStringField(TEXT("type"), TEXT("object"));
				Schema->SetObjectField(TEXT("additionalProperties"), PropertySchema(Map->ValueProp));
			}
			else
			{
				Schema->SetStringField(TEXT("type"), TEXT("string"));
				Schema->SetStringField(TEXT("x-unreal-type"), Property->GetCPPType());
			}

			AddDescription(Property, Schema);
			return Schema;
		}

		TSharedRef<FJsonObject> BuildFunctionSchema(const UFunction* Function, const bool bInputs)
		{
			TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Required;

			if (Function)
			{
				for (TFieldIterator<FProperty> It(Function); It; ++It)
				{
					const FProperty* Property = *It;
					if (bInputs ? !IsInputParameter(Property) : !IsOutputParameter(Property))
					{
						continue;
					}

					const FString Name = GetParameterName(Property);
					TSharedRef<FJsonObject> ParameterSchema = PropertySchema(Property);
					TSharedPtr<FJsonValue> DefaultValue;
					if (bInputs && TryBuildParameterDefaultJson(Function, Property, Name, DefaultValue))
					{
						ParameterSchema->SetField(TEXT("default"), DefaultValue);
					}
					Properties->SetObjectField(Name, ParameterSchema);
					if (bInputs && !IsOptionalParameter(Function, Name))
					{
						Required.Add(MakeShared<FJsonValueString>(Name));
					}
				}
			}

			Schema->SetObjectField(TEXT("properties"), Properties);
			if (!Required.IsEmpty())
			{
				Schema->SetArrayField(TEXT("required"), MoveTemp(Required));
			}
			Schema->SetBoolField(TEXT("additionalProperties"), false);
			return Schema;
		}
	}

	TSharedRef<FJsonObject> BuildInputSchema(const UFunction* Function)
	{
		return BuildFunctionSchema(Function, true);
	}

	TSharedRef<FJsonObject> BuildOutputSchema(const UFunction* Function)
	{
		return BuildFunctionSchema(Function, false);
	}

	FDiscoveryResult DiscoverClass(UClass* ToolsetClass)
	{
		FDiscoveryResult Result;
		if (!ToolsetClass)
		{
			Result.Errors.Add(TEXT("Toolset class is null."));
			return Result;
		}

#if !WITH_METADATA
		Result.Errors.Add(TEXT("Reflected tool discovery requires a metadata-enabled target."));
		return Result;
#else
		FString ToolsetName = ToolsetClass->GetMetaData(ToolsetMetadata);
		if (ToolsetName.IsEmpty())
		{
			Result.Errors.Add(FString::Printf(TEXT("Class %s does not declare %s metadata."), *ToolsetClass->GetPathName(), ToolsetMetadata));
			return Result;
		}

		TSet<FString> SeenNames;
		for (TFieldIterator<UFunction> It(ToolsetClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			if (!Function->HasMetaData(ToolMetadata))
			{
				continue;
			}

			FString ToolName = Function->GetMetaData(ToolNameMetadata);
			if (ToolName.IsEmpty())
			{
				ToolName = Function->GetName();
			}
			if (SeenNames.Contains(ToolName))
			{
				Result.Errors.Add(FString::Printf(TEXT("Duplicate tool name %s on class %s."), *ToolName, *ToolsetClass->GetPathName()));
				continue;
			}
			SeenNames.Add(ToolName);

			EMcpToolRisk Risk = EMcpToolRisk::ContentMutation;
			EMcpToolTransactionPolicy TransactionPolicy = EMcpToolTransactionPolicy::None;
			bool bIsIdempotent = false;
			const FString RiskText = Function->GetMetaData(ToolRiskMetadata);
			const FString TransactionText = Function->GetMetaData(ToolTransactionMetadata);
			const FString IdempotentText = Function->GetMetaData(ToolIdempotentMetadata);
			if (!TryParseRisk(RiskText, Risk) || !TryParseTransactionPolicy(TransactionText, TransactionPolicy) || !TryParseBooleanMetadata(IdempotentText, bIsIdempotent))
			{
				Result.Errors.Add(FString::Printf(TEXT("Reflected tool %s.%s must declare valid %s, %s, and %s metadata."), *ToolsetName, *ToolName, ToolRiskMetadata,
					ToolTransactionMetadata, ToolIdempotentMetadata));
				continue;
			}
			if ((Risk == EMcpToolRisk::ReadOnly) != (TransactionPolicy == EMcpToolTransactionPolicy::ReadOnly))
			{
				Result.Errors.Add(FString::Printf(TEXT("Reflected tool %s.%s has inconsistent ReadOnly risk and transaction metadata."), *ToolsetName, *ToolName));
				continue;
			}

			bool bHasUnsupportedParameter = false;
			for (TFieldIterator<FProperty> ParameterIt(Function); ParameterIt; ++ParameterIt)
			{
				const FProperty* Parameter = *ParameterIt;
				if (!IsInputParameter(Parameter) && !IsOutputParameter(Parameter))
				{
					continue;
				}
				TSharedPtr<FJsonObject> ParameterSchema;
				FString AdapterError;
				if (!FPropertyTypeAdapterRegistry::GetDefault().BuildSchema(Parameter, ParameterSchema, AdapterError))
				{
					Result.Errors.Add(FString::Printf(TEXT("工具 %s.%s 的参数 %s 类型不受支持：%s；%s"), *ToolsetName, *ToolName, *GetParameterName(Parameter),
						*Parameter->GetCPPType(), *AdapterError));
					bHasUnsupportedParameter = true;
				}
			}
			if (bHasUnsupportedParameter)
			{
				continue;
			}

			FToolDescriptor Descriptor;
			Descriptor.ToolsetName = ToolsetName;
			Descriptor.ToolName = ToolName;
			Descriptor.QualifiedName = ToolsetName + TEXT(".") + ToolName;
			Descriptor.Description = Function->GetMetaData(ToolDescriptionMetadata);
			if (Descriptor.Description.IsEmpty())
			{
				Descriptor.Description = Function->GetMetaData(TEXT("ToolTip"));
			}
			Descriptor.InputSchema = BuildInputSchema(Function);
			Descriptor.OutputSchema = BuildOutputSchema(Function);
			Descriptor.Risk = Risk;
			Descriptor.TransactionPolicy = TransactionPolicy;
			Descriptor.bReadOnly = Risk == EMcpToolRisk::ReadOnly;
			Descriptor.bIdempotent = bIsIdempotent;
			Descriptor.OwnerClass = ToolsetClass;
			Descriptor.Function = Function;
			Result.Tools.Add(MoveTemp(Descriptor));
		}

		Result.Tools.Sort(
			[](const FToolDescriptor& Left, const FToolDescriptor& Right)
			{
				return Left.QualifiedName < Right.QualifiedName;
			});
		return Result;
#endif
	}

	FDiscoveryResult DiscoverLoadedClasses()
	{
		FDiscoveryResult Result;
#if !WITH_METADATA
		Result.Errors.Add(TEXT("Reflected tool discovery requires a metadata-enabled target."));
		return Result;
#else
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || !Class->HasMetaData(ToolsetMetadata) ||
				Class->HasMetaData(InternalToolsetMetadata))
			{
				continue;
			}
			FDiscoveryResult ClassResult = DiscoverClass(Class);
			Result.Tools.Append(MoveTemp(ClassResult.Tools));
			Result.Errors.Append(MoveTemp(ClassResult.Errors));
		}
		Result.Tools.Sort(
			[](const FToolDescriptor& Left, const FToolDescriptor& Right)
			{
				return Left.QualifiedName < Right.QualifiedName;
			});
		return Result;
#endif
	}

	bool Invoke(const FToolDescriptor& Descriptor, const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FString& OutError)
	{
		OutResult.Reset();
		OutError.Reset();
		if (!Descriptor.OwnerClass || !Descriptor.Function)
		{
			OutError = TEXT("Reflected tool descriptor is incomplete.");
			return false;
		}

		UObject* Target = Descriptor.OwnerClass->GetDefaultObject();
		if (!Target)
		{
			OutError = FString::Printf(TEXT("Unable to get class default object for %s."), *Descriptor.OwnerClass->GetPathName());
			return false;
		}

		UFunction* Function = Descriptor.Function;
		uint8* Parameters = static_cast<uint8*>(FMemory::Malloc(Function->ParmsSize, Function->GetMinAlignment()));
		Function->InitializeStruct(Parameters);
		ON_SCOPE_EXIT
		{
			Function->DestroyStruct(Parameters);
			FMemory::Free(Parameters);
		};

		const TSharedPtr<FJsonObject> SafeArguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!IsInputParameter(Property))
			{
				continue;
			}

			const FString Name = GetParameterName(Property);
			const TSharedPtr<FJsonValue> Value = SafeArguments->TryGetField(Name);
			if (!Value.IsValid())
			{
				FString DefaultValue;
				if (TryGetParameterDefault(Function, Name, DefaultValue))
				{
					if (!Property->ImportText_Direct(*DefaultValue, Property->ContainerPtrToValuePtr<void>(Parameters), Target, PPF_None))
					{
						OutError = FString::Printf(TEXT("Default argument '%s' is invalid."), *Name);
						return false;
					}
					continue;
				}
				OutError = FString::Printf(TEXT("Missing required argument '%s'."), *Name);
				return false;
			}

			FString ConversionError;
			if (!FPropertyTypeAdapterRegistry::GetDefault().Read(Value, Property, Property->ContainerPtrToValuePtr<void>(Parameters), ConversionError))
			{
				OutError = FString::Printf(TEXT("Argument '%s' is invalid: %s"), *Name, *ConversionError);
				return false;
			}
		}

		Target->ProcessEvent(Function, Parameters);

		OutResult = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Property = *It;
			if (!IsOutputParameter(Property))
			{
				continue;
			}
			const FString Name = GetParameterName(Property);
			TSharedPtr<FJsonValue> Value;
			FString ConversionError;
			if (!FPropertyTypeAdapterRegistry::GetDefault().Write(Property, Property->ContainerPtrToValuePtr<void>(Parameters), Value, ConversionError))
			{
				OutError = FString::Printf(TEXT("Unable to serialize output '%s': %s"), *Name, *ConversionError);
				OutResult.Reset();
				return false;
			}
			OutResult->SetField(Name, Value);
		}
		return true;
	}
}
