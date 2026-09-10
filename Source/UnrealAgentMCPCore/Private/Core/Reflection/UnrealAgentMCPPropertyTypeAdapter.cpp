// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPropertyTypeAdapter.cpp
 * @brief UE 基础、容器、结构和引用属性的统一类型适配实现。
 */

#include "Core/Reflection/UnrealAgentMCPPropertyTypeAdapter.h"

#include "Core/Schema/UnrealAgentMCPJsonSchema.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP::Reflection
{
	namespace
	{
		constexpr int32 MaxSchemaDepth = 32;
		thread_local int32 SchemaDepth = 0;

		FString PropertyDisplayName(const FProperty* Property)
		{
			const FString AuthoredName = Property ? Property->GetAuthoredName() : FString();
			return AuthoredName.IsEmpty() && Property ? Property->GetName() : AuthoredName;
		}

		bool TryReadNumberMetadata(const FProperty* Property, const TCHAR* PrimaryName, const TCHAR* FallbackName, double& OutValue, FString& OutError)
		{
#if WITH_METADATA
			FString Text = Property->GetMetaData(PrimaryName);
			if (Text.IsEmpty() && FallbackName)
			{
				Text = Property->GetMetaData(FallbackName);
			}
			if (Text.IsEmpty())
			{
				return false;
			}
			if (!LexTryParseString(OutValue, *Text) || !FMath::IsFinite(OutValue))
			{
				OutError = FString::Printf(TEXT("属性 %s 的 %s metadata 不是有限数值：%s"), *PropertyDisplayName(Property), PrimaryName, *Text);
				return false;
			}
			return true;
#else
			return false;
#endif
		}

		bool ApplyPropertyConstraints(const FProperty* Property, const TSharedPtr<FJsonObject>& Schema, FString& OutError)
		{
			if (!Property || !Schema.IsValid())
			{
				OutError = TEXT("属性或 Schema 为空。");
				return false;
			}

			if (CastField<FNumericProperty>(Property))
			{
				double Minimum = 0.0;
				if (TryReadNumberMetadata(Property, TEXT("MCPMinimum"), TEXT("ClampMin"), Minimum, OutError))
				{
					Schema->SetNumberField(TEXT("minimum"), Minimum);
				}
				else if (!OutError.IsEmpty())
				{
					return false;
				}

				double Maximum = 0.0;
				if (TryReadNumberMetadata(Property, TEXT("MCPMaximum"), TEXT("ClampMax"), Maximum, OutError))
				{
					Schema->SetNumberField(TEXT("maximum"), Maximum);
				}
				else if (!OutError.IsEmpty())
				{
					return false;
				}
				if (Schema->HasField(TEXT("minimum")) && Schema->HasField(TEXT("maximum")) && Minimum > Maximum)
				{
					OutError = FString::Printf(TEXT("属性 %s 的最小值大于最大值。"), *PropertyDisplayName(Property));
					return false;
				}
			}

#if WITH_METADATA
			if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property))
			{
				auto ApplyLength = [&OutError, Property, &Schema](const TCHAR* MetadataName, const TCHAR* SchemaName) -> bool
				{
					double Value = 0.0;
					if (!TryReadNumberMetadata(Property, MetadataName, nullptr, Value, OutError))
					{
						return OutError.IsEmpty();
					}
					if (Value < 0.0 || Value != FMath::FloorToDouble(Value))
					{
						OutError = FString::Printf(TEXT("属性 %s 的 %s 必须是非负整数。"), *PropertyDisplayName(Property), MetadataName);
						return false;
					}
					Schema->SetNumberField(SchemaName, Value);
					return true;
				};
				if (!ApplyLength(TEXT("MCPMinLength"), TEXT("minLength")) || !ApplyLength(TEXT("MCPMaxLength"), TEXT("maxLength")))
				{
					return false;
				}
				const FString Pattern = Property->GetMetaData(TEXT("MCPPattern"));
				if (!Pattern.IsEmpty())
				{
					Schema->SetStringField(TEXT("pattern"), Pattern);
				}
			}
			if (CastField<FArrayProperty>(Property) || CastField<FSetProperty>(Property))
			{
				for (const TPair<const TCHAR*, const TCHAR*>& Constraint :
					{ TPair<const TCHAR*, const TCHAR*>(TEXT("MCPMinItems"), TEXT("minItems")), TPair<const TCHAR*, const TCHAR*>(TEXT("MCPMaxItems"), TEXT("maxItems")) })
				{
					double Value = 0.0;
					if (TryReadNumberMetadata(Property, Constraint.Key, nullptr, Value, OutError))
					{
						if (Value < 0.0 || Value != FMath::FloorToDouble(Value))
						{
							OutError = FString::Printf(TEXT("属性 %s 的 %s 必须是非负整数。"), *PropertyDisplayName(Property), Constraint.Key);
							return false;
						}
						Schema->SetNumberField(Constraint.Value, Value);
					}
					else if (!OutError.IsEmpty())
					{
						return false;
					}
				}
			}
#endif
			return true;
		}

		bool IsUnsignedIntegerProperty(const FProperty* Property)
		{
			return CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property) || CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property);
		}

		void AddPropertyDescription(const FProperty* Property, const TSharedPtr<FJsonObject>& Schema)
		{
#if WITH_METADATA
			if (!Property || !Schema.IsValid())
			{
				return;
			}
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

		class FJsonObjectConverterAdapter : public IPropertyTypeAdapter
		{
		public:
			virtual bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const override
			{
				// UE 5.8 的转换接口仍保留非 const 形参，但不会修改属性描述对象。
				FProperty* MutableProperty = const_cast<FProperty*>(Property);
				FText FailureReason;
				if (!JsonValue.IsValid() || !FJsonObjectConverter::JsonValueToUProperty(JsonValue, MutableProperty, ValueAddress, 0, 0, true, &FailureReason))
				{
					OutError = FailureReason.IsEmpty() ? TEXT("JSON 值无法写入属性。") : FailureReason.ToString();
					return false;
				}
				return true;
			}

			virtual bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const override
			{
				// UE 5.8 的转换接口仍保留非 const 形参，但不会修改属性描述对象。
				FProperty* MutableProperty = const_cast<FProperty*>(Property);
				OutJsonValue = FJsonObjectConverter::UPropertyToJsonValue(MutableProperty, ValueAddress);
				if (!OutJsonValue.IsValid())
				{
					OutError = TEXT("属性无法序列化为 JSON。");
					return false;
				}
				return true;
			}
		};

		class FScalarTypeAdapter final : public FJsonObjectConverterAdapter
		{
		public:
			virtual FName GetName() const override
			{
				return TEXT("Scalar");
			}

			virtual bool CanAdapt(const FProperty* Property) const override
			{
				return CastField<FBoolProperty>(Property) || CastField<FNumericProperty>(Property);
			}

			virtual bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const override
			{
				if (!JsonValue.IsValid() || !Property || !ValueAddress)
				{
					OutError = TEXT("标量转换参数为空。");
					return false;
				}
				if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
				{
					if (JsonValue->Type != EJson::Boolean)
					{
						OutError = TEXT("布尔属性只接受 JSON boolean。");
						return false;
					}
					Bool->SetPropertyValue(ValueAddress, JsonValue->AsBool());
					return true;
				}

				const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
				if (!Numeric || JsonValue->Type != EJson::Number)
				{
					OutError = TEXT("数值属性只接受 JSON number。");
					return false;
				}
				if (Numeric->IsFloatingPoint())
				{
					double Value = 0.0;
					const bool bIsFloatProperty = CastField<FFloatProperty>(Property) != nullptr;
					if (!JsonValue->TryGetNumber(Value) || !FMath::IsFinite(Value) ||
						(bIsFloatProperty && (Value < TNumericLimits<float>::Lowest() || Value > TNumericLimits<float>::Max())))
					{
						OutError = TEXT("浮点数超出属性范围或不是有限值。");
						return false;
					}
					Numeric->SetFloatingPointPropertyValue(ValueAddress, Value);
					return true;
				}

				if (IsUnsignedIntegerProperty(Property))
				{
					uint64 Value = 0;
					if (!JsonValue->TryGetNumber(Value) || !Numeric->CanHoldValue(Value))
					{
						OutError = TEXT("无符号整数不是精确整数或超出属性范围。");
						return false;
					}
					Numeric->SetIntPropertyValue(ValueAddress, Value);
					return true;
				}

				int64 Value = 0;
				if (!JsonValue->TryGetNumber(Value) || !Numeric->CanHoldValue(Value))
				{
					OutError = TEXT("有符号整数不是精确整数或超出属性范围。");
					return false;
				}
				Numeric->SetIntPropertyValue(ValueAddress, Value);
				return true;
			}

			virtual bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const override
			{
				if (!Property || !ValueAddress)
				{
					OutError = TEXT("标量序列化参数为空。");
					return false;
				}
				if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
				{
					OutJsonValue = MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(ValueAddress));
					return true;
				}

				const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
				if (!Numeric)
				{
					OutError = TEXT("Scalar 适配器收到不兼容属性。");
					return false;
				}
				if (Numeric->IsFloatingPoint())
				{
					const double Value = Numeric->GetFloatingPointPropertyValue(ValueAddress);
					if (!FMath::IsFinite(Value))
					{
						OutError = TEXT("非有限浮点数不能序列化为 JSON。");
						return false;
					}
					OutJsonValue = MakeShared<FJsonValueNumber>(Value);
					return true;
				}

				if (IsUnsignedIntegerProperty(Property))
				{
					OutJsonValue = MakeShared<FJsonValueNumberString>(LexToString(Numeric->GetUnsignedIntPropertyValue(ValueAddress)));
				}
				else
				{
					OutJsonValue = MakeShared<FJsonValueNumberString>(LexToString(Numeric->GetSignedIntPropertyValue(ValueAddress)));
				}
				return true;
			}

			virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const override
			{
				OutSchema = MakeShared<FJsonObject>();
				if (CastField<FBoolProperty>(Property))
				{
					OutSchema->SetStringField(TEXT("type"), TEXT("boolean"));
				}
				else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
				{
					OutSchema->SetStringField(TEXT("type"), Numeric->IsInteger() ? TEXT("integer") : TEXT("number"));
				}
				else
				{
					OutError = TEXT("Scalar 适配器收到不兼容属性。");
					return false;
				}
				return true;
			}
		};

		class FEnumTypeAdapter final : public FJsonObjectConverterAdapter
		{
		public:
			virtual FName GetName() const override
			{
				return TEXT("Enum");
			}

			virtual bool CanAdapt(const FProperty* Property) const override
			{
				if (CastField<FEnumProperty>(Property))
				{
					return true;
				}
				const FByteProperty* Byte = CastField<FByteProperty>(Property);
				return Byte && Byte->Enum;
			}

			virtual bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const override
			{
				if (!JsonValue.IsValid() || JsonValue->Type != EJson::String || !Property || !ValueAddress)
				{
					OutError = TEXT("枚举属性只接受 JSON string。");
					return false;
				}

				const FString Name = JsonValue->AsString();
				if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
				{
					const int64 Value = EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetValueByNameString(Name) : INDEX_NONE;
					if (Value == INDEX_NONE)
					{
						OutError = FString::Printf(TEXT("未知枚举值：%s"), *Name);
						return false;
					}
					EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValueAddress, Value);
					return true;
				}

				if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
				{
					const int64 Value = Byte->Enum ? Byte->Enum->GetValueByNameString(Name) : INDEX_NONE;
					if (Value == INDEX_NONE || !Byte->CanHoldValue(Value))
					{
						OutError = FString::Printf(TEXT("未知枚举值：%s"), *Name);
						return false;
					}
					Byte->SetPropertyValue(ValueAddress, static_cast<uint8>(Value));
					return true;
				}

				OutError = TEXT("Enum 适配器收到不兼容属性。");
				return false;
			}

			virtual bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const override
			{
				if (!Property || !ValueAddress)
				{
					OutError = TEXT("枚举序列化参数无效。");
					return false;
				}
				const UEnum* Enum = nullptr;
				int64 Value = INDEX_NONE;
				if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
				{
					Enum = EnumProperty->GetEnum();
					Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValueAddress);
				}
				else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
				{
					Enum = Byte->Enum;
					Value = Byte->GetPropertyValue(ValueAddress);
				}
				if (!Enum)
				{
					OutError = TEXT("枚举序列化参数无效。");
					return false;
				}
				const FString Name = Enum->GetNameStringByValue(Value);
				if (Name.IsEmpty())
				{
					OutError = FString::Printf(TEXT("枚举数值没有稳定名称：%lld"), Value);
					return false;
				}
				OutJsonValue = MakeShared<FJsonValueString>(Name);
				return true;
			}

			virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const override
			{
				const UEnum* Enum = nullptr;
				if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
				{
					Enum = EnumProperty->GetEnum();
				}
				else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
				{
					Enum = Byte->Enum;
				}
				if (!Enum)
				{
					OutError = TEXT("枚举属性缺少 UEnum。");
					return false;
				}

				OutSchema = MakeShared<FJsonObject>();
				OutSchema->SetStringField(TEXT("type"), TEXT("string"));
				TArray<TSharedPtr<FJsonValue>> Values;
				for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
				{
#if WITH_METADATA
					if (Enum->HasMetaData(TEXT("Hidden"), Index))
					{
						continue;
					}
#endif
					Values.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
				}
				OutSchema->SetArrayField(TEXT("enum"), Values);
				return true;
			}
		};

		class FStringTypeAdapter final : public FJsonObjectConverterAdapter
		{
		public:
			virtual FName GetName() const override
			{
				return TEXT("String");
			}

			virtual bool CanAdapt(const FProperty* Property) const override
			{
				return CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property) || CastField<FTextProperty>(Property);
			}

			virtual bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const override
			{
				if (!JsonValue.IsValid() || JsonValue->Type != EJson::String || !Property || !ValueAddress)
				{
					OutError = TEXT("字符串属性只接受 JSON string。");
					return false;
				}
				const FString Value = JsonValue->AsString();
				if (const FStrProperty* String = CastField<FStrProperty>(Property))
				{
					String->SetPropertyValue(ValueAddress, Value);
					return true;
				}
				if (const FNameProperty* Name = CastField<FNameProperty>(Property))
				{
					Name->SetPropertyValue(ValueAddress, FName(*Value));
					return true;
				}
				if (const FTextProperty* Text = CastField<FTextProperty>(Property))
				{
					Text->SetPropertyValue(ValueAddress, FText::FromString(Value));
					return true;
				}
				OutError = TEXT("String 适配器收到不兼容属性。");
				return false;
			}

			virtual bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const override
			{
				if (!Property || !ValueAddress)
				{
					OutError = TEXT("字符串序列化参数无效。");
					return false;
				}
				FString Value;
				if (const FStrProperty* String = CastField<FStrProperty>(Property))
				{
					Value = String->GetPropertyValue(ValueAddress);
				}
				else if (const FNameProperty* Name = CastField<FNameProperty>(Property))
				{
					Value = Name->GetPropertyValue(ValueAddress).ToString();
				}
				else if (const FTextProperty* Text = CastField<FTextProperty>(Property))
				{
					Value = Text->GetPropertyValue(ValueAddress).ToString();
				}
				else
				{
					OutError = TEXT("String 适配器收到不兼容属性。");
					return false;
				}
				OutJsonValue = MakeShared<FJsonValueString>(Value);
				return true;
			}

			virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const override
			{
				OutSchema = MakeShared<FJsonObject>();
				OutSchema->SetStringField(TEXT("type"), TEXT("string"));
				OutSchema->SetStringField(TEXT("x-unreal-type"), Property->GetCPPType());
				return true;
			}
		};

		class FReferenceTypeAdapter final : public IPropertyTypeAdapter
		{
		public:
			virtual FName GetName() const override
			{
				return TEXT("Reference");
			}

			virtual bool CanAdapt(const FProperty* Property) const override
			{
				return CastField<FObjectPropertyBase>(Property) || CastField<FClassProperty>(Property) || CastField<FSoftObjectProperty>(Property) ||
					CastField<FSoftClassProperty>(Property) || CastField<FWeakObjectProperty>(Property) || CastField<FLazyObjectProperty>(Property) ||
					CastField<FInterfaceProperty>(Property);
			}

			virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const override
			{
				OutSchema = MakeShared<FJsonObject>();
				TArray<TSharedPtr<FJsonValue>> AllowedTypes;
				TSharedRef<FJsonObject> PathSchema = MakeShared<FJsonObject>();
				PathSchema->SetStringField(TEXT("type"), TEXT("string"));
				AllowedTypes.Add(MakeShared<FJsonValueObject>(PathSchema));
				TSharedRef<FJsonObject> NullSchema = MakeShared<FJsonObject>();
				NullSchema->SetStringField(TEXT("type"), TEXT("null"));
				AllowedTypes.Add(MakeShared<FJsonValueObject>(NullSchema));
				OutSchema->SetArrayField(TEXT("anyOf"), AllowedTypes);
				OutSchema->SetStringField(TEXT("x-unreal-type"), Property->GetCPPType());
				OutSchema->SetStringField(TEXT("x-unreal-format"), TEXT("object-path"));
				if (const UClass* ExpectedClass = GetExpectedClass(Property))
				{
					OutSchema->SetStringField(TEXT("x-unreal-class"), ExpectedClass->GetPathName());
				}
				return true;
			}

			virtual bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const override
			{
				if (!JsonValue.IsValid() || !Property || !ValueAddress)
				{
					OutError = TEXT("引用转换参数包含 null 指针。");
					return false;
				}
				if (JsonValue->Type == EJson::Null)
				{
					Property->ClearValue(ValueAddress);
					return true;
				}
				if (JsonValue->Type != EJson::String)
				{
					OutError = TEXT("引用属性只接受对象路径字符串或 null。");
					return false;
				}

				const FString PathText = JsonValue->AsString();
				const FSoftObjectPath ObjectPath(PathText);
				if (PathText.IsEmpty() || !ObjectPath.IsValid())
				{
					OutError = FString::Printf(TEXT("无效对象路径：%s"), *PathText);
					return false;
				}

				if (const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
				{
					const UClass* ResolvedClass = Cast<UClass>(ObjectPath.ResolveObject());
					if (ResolvedClass && SoftClass->MetaClass && !ResolvedClass->IsChildOf(SoftClass->MetaClass))
					{
						OutError = MakeTypeMismatchError(PathText, SoftClass->MetaClass);
						return false;
					}
					SoftClass->SetPropertyValue(ValueAddress, FSoftObjectPtr(ObjectPath));
					return true;
				}
				if (const FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
				{
					const UObject* Resolved = ObjectPath.ResolveObject();
					if (Resolved && SoftObject->PropertyClass && !Resolved->IsA(SoftObject->PropertyClass))
					{
						OutError = MakeTypeMismatchError(PathText, SoftObject->PropertyClass);
						return false;
					}
					SoftObject->SetPropertyValue(ValueAddress, FSoftObjectPtr(ObjectPath));
					return true;
				}
				if (const FClassProperty* Class = CastField<FClassProperty>(Property))
				{
					UClass* LoadedClass = TryLoadClass(PathText);
					if (!LoadedClass || (Class->MetaClass && !LoadedClass->IsChildOf(Class->MetaClass)))
					{
						OutError = MakeTypeMismatchError(PathText, Class->MetaClass);
						return false;
					}
					Class->SetObjectPropertyValue(ValueAddress, LoadedClass);
					return true;
				}
				if (CastField<FInterfaceProperty>(Property))
				{
					if (!Property->ImportText_Direct(*PathText, ValueAddress, nullptr, PPF_None))
					{
						OutError = FString::Printf(TEXT("对象未实现目标接口：%s"), *PathText);
						return false;
					}
					return true;
				}

				const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
				if (!ObjectProperty)
				{
					OutError = TEXT("Reference 适配器收到不兼容属性。");
					return false;
				}
				UObject* Object = ObjectPath.ResolveObject();
				const bool bWeakReference = CastField<FWeakObjectProperty>(Property) || CastField<FLazyObjectProperty>(Property);
				if (!Object && !bWeakReference)
				{
					Object = ObjectPath.TryLoad();
				}
				if (!Object || (ObjectProperty->PropertyClass && !Object->IsA(ObjectProperty->PropertyClass)))
				{
					OutError =
						bWeakReference ? FString::Printf(TEXT("弱引用目标尚未加载或类型不匹配：%s"), *PathText) : MakeTypeMismatchError(PathText, ObjectProperty->PropertyClass);
					return false;
				}
				ObjectProperty->SetObjectPropertyValue(ValueAddress, Object);
				return true;
			}

			virtual bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const override
			{
				if (!Property || !ValueAddress)
				{
					OutError = TEXT("引用序列化参数无效。");
					return false;
				}

				FString PathText;
				if (const FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
				{
					PathText = SoftObject->GetPropertyValue(ValueAddress).ToSoftObjectPath().ToString();
				}
				else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					const UObject* Object = ObjectProperty->GetObjectPropertyValue(ValueAddress);
					PathText = Object ? Object->GetPathName() : FString();
				}
				else if (const FInterfaceProperty* Interface = CastField<FInterfaceProperty>(Property))
				{
					const FScriptInterface Value = Interface->GetPropertyValue(ValueAddress);
					PathText = Value.GetObject() ? Value.GetObject()->GetPathName() : FString();
				}
				else
				{
					OutError = TEXT("Reference 适配器收到不兼容属性。");
					return false;
				}

				if (PathText.IsEmpty())
				{
					OutJsonValue = MakeShared<FJsonValueNull>();
				}
				else
				{
					OutJsonValue = MakeShared<FJsonValueString>(PathText);
				}
				return true;
			}

		private:
			static const UClass* GetExpectedClass(const FProperty* Property)
			{
				if (const FClassProperty* Class = CastField<FClassProperty>(Property))
				{
					return Class->MetaClass;
				}
				if (const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property))
				{
					return SoftClass->MetaClass;
				}
				if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
				{
					return Object->PropertyClass;
				}
				return nullptr;
			}

			static FString MakeTypeMismatchError(const FString& Path, const UClass* ExpectedClass)
			{
				return FString::Printf(TEXT("对象 %s 不是有效的 %s。"), *Path, ExpectedClass ? *ExpectedClass->GetPathName() : TEXT("class"));
			}

			static UClass* TryLoadClass(const FString& Path)
			{
				UClass* Class = FSoftClassPath(Path).TryLoadClass<UObject>();
				if (!Class && Path.StartsWith(TEXT("/Game/")) && !Path.EndsWith(TEXT("_C")))
				{
					Class = FSoftClassPath(Path + TEXT("_C")).TryLoadClass<UObject>();
				}
				return Class;
			}
		};

		class FGuidTypeAdapter final : public IPropertyTypeAdapter
		{
		public:
			virtual FName GetName() const override
			{
				return TEXT("Guid");
			}

			virtual bool CanAdapt(const FProperty* Property) const override
			{
				const FStructProperty* Struct = CastField<FStructProperty>(Property);
				return Struct && Struct->Struct == TBaseStructure<FGuid>::Get();
			}

			virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const override
			{
				OutSchema = MakeShared<FJsonObject>();
				OutSchema->SetStringField(TEXT("type"), TEXT("string"));
				OutSchema->SetStringField(TEXT("format"), TEXT("uuid"));
				OutSchema->SetStringField(TEXT("pattern"), TEXT("^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"));
				return true;
			}

			virtual bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const override
			{
				FGuid Value;
				if (!JsonValue.IsValid() || JsonValue->Type != EJson::String || !ValueAddress || !FGuid::ParseExact(JsonValue->AsString(), EGuidFormats::DigitsWithHyphens, Value))
				{
					OutError = TEXT("GUID 必须使用 8-4-4-4-12 连字符格式。");
					return false;
				}
				*static_cast<FGuid*>(ValueAddress) = Value;
				return true;
			}

			virtual bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const override
			{
				if (!ValueAddress)
				{
					OutError = TEXT("GUID 序列化地址为空。");
					return false;
				}
				OutJsonValue = MakeShared<FJsonValueString>(static_cast<const FGuid*>(ValueAddress)->ToString(EGuidFormats::DigitsWithHyphens));
				return true;
			}
		};

		class FStructTypeAdapter final : public FJsonObjectConverterAdapter
		{
		public:
			virtual FName GetName() const override
			{
				return TEXT("Struct");
			}

			virtual bool CanAdapt(const FProperty* Property) const override
			{
				return CastField<FStructProperty>(Property) != nullptr;
			}

			virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const override
			{
				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				if (!StructProperty || !StructProperty->Struct)
				{
					OutError = TEXT("结构属性缺少 UStruct。");
					return false;
				}
				OutSchema = MakeShared<FJsonObject>();
				OutSchema->SetStringField(TEXT("type"), TEXT("object"));
				OutSchema->SetStringField(TEXT("x-unreal-type"), StructProperty->Struct->GetPathName());
				TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
				TArray<TSharedPtr<FJsonValue>> Required;
				for (TFieldIterator<FProperty> It(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
				{
					const FProperty* Field = *It;
					if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
					{
						continue;
					}
					TSharedPtr<FJsonObject> FieldSchema;
					if (!Registry.BuildSchema(Field, FieldSchema, OutError))
					{
						OutError = FString::Printf(TEXT("结构 %s 的字段 %s 不受支持：%s"), *StructProperty->Struct->GetName(), *Field->GetName(), *OutError);
						return false;
					}
					const FString Name = PropertyDisplayName(Field);
					Properties->SetObjectField(Name, FieldSchema);
					Required.Add(MakeShared<FJsonValueString>(Name));
				}
				OutSchema->SetObjectField(TEXT("properties"), Properties);
				OutSchema->SetArrayField(TEXT("required"), Required);
				OutSchema->SetBoolField(TEXT("additionalProperties"), false);
				return true;
			}
		};

		class FContainerTypeAdapter final : public FJsonObjectConverterAdapter
		{
		public:
			virtual FName GetName() const override
			{
				return TEXT("Container");
			}

			virtual bool CanAdapt(const FProperty* Property) const override
			{
				return CastField<FArrayProperty>(Property) || CastField<FSetProperty>(Property) || CastField<FMapProperty>(Property);
			}

			virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const override
			{
				OutSchema = MakeShared<FJsonObject>();
				if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
				{
					TSharedPtr<FJsonObject> ItemSchema;
					if (!Registry.BuildSchema(Array->Inner, ItemSchema, OutError))
					{
						return false;
					}
					OutSchema->SetStringField(TEXT("type"), TEXT("array"));
					OutSchema->SetObjectField(TEXT("items"), ItemSchema);
					return true;
				}
				if (const FSetProperty* Set = CastField<FSetProperty>(Property))
				{
					TSharedPtr<FJsonObject> ItemSchema;
					if (!Registry.BuildSchema(Set->ElementProp, ItemSchema, OutError))
					{
						return false;
					}
					OutSchema->SetStringField(TEXT("type"), TEXT("array"));
					OutSchema->SetObjectField(TEXT("items"), ItemSchema);
					OutSchema->SetBoolField(TEXT("uniqueItems"), true);
					return true;
				}
				if (const FMapProperty* Map = CastField<FMapProperty>(Property))
				{
					if (!CastField<FStrProperty>(Map->KeyProp) && !CastField<FNameProperty>(Map->KeyProp))
					{
						OutError = TEXT("Map 仅支持 FString 或 FName Key。");
						return false;
					}
					TSharedPtr<FJsonObject> ValueSchema;
					if (!Registry.BuildSchema(Map->ValueProp, ValueSchema, OutError))
					{
						return false;
					}
					OutSchema->SetStringField(TEXT("type"), TEXT("object"));
					OutSchema->SetObjectField(TEXT("additionalProperties"), ValueSchema);
					return true;
				}
				OutError = TEXT("Container 适配器收到不兼容属性。");
				return false;
			}
		};

		void RegisterDefaultAdapters(FPropertyTypeAdapterRegistry& Registry)
		{
			FString Error;
			Registry.RegisterAdapter(MakeShared<FEnumTypeAdapter>(), Error);
			Registry.RegisterAdapter(MakeShared<FScalarTypeAdapter>(), Error);
			Registry.RegisterAdapter(MakeShared<FStringTypeAdapter>(), Error);
			Registry.RegisterAdapter(MakeShared<FReferenceTypeAdapter>(), Error);
			Registry.RegisterAdapter(MakeShared<FGuidTypeAdapter>(), Error);
			Registry.RegisterAdapter(MakeShared<FStructTypeAdapter>(), Error);
			Registry.RegisterAdapter(MakeShared<FContainerTypeAdapter>(), Error);
		}

		/**
		 * 默认注册表持有者。
		 *
		 * 用显式构造阶段完成内建适配器注册，避免依赖静态局部变量
		 * 在 Lambda 捕获上的编译器差异。
		 */
		struct FDefaultTypeAdapterRegistryHolder
		{
			FPropertyTypeAdapterRegistry Registry;

			FDefaultTypeAdapterRegistryHolder()
			{
				RegisterDefaultAdapters(Registry);
			}
		};
	}

	bool FPropertyTypeAdapterRegistry::RegisterAdapter(const TSharedRef<IPropertyTypeAdapter>& Adapter, FString& OutError)
	{
		const FName Name = Adapter->GetName();
		if (Name.IsNone())
		{
			OutError = TEXT("类型适配器名称不能为空。");
			return false;
		}
		FWriteScopeLock Lock(RegistryLock);
		for (const TSharedRef<IPropertyTypeAdapter>& Existing : Adapters)
		{
			if (Existing->GetName() == Name)
			{
				OutError = FString::Printf(TEXT("类型适配器名称已注册：%s"), *Name.ToString());
				return false;
			}
		}
		Adapters.Add(Adapter);
		return true;
	}

	TSharedPtr<IPropertyTypeAdapter> FPropertyTypeAdapterRegistry::FindAdapter(const FProperty* Property) const
	{
		FReadScopeLock Lock(RegistryLock);
		for (const TSharedRef<IPropertyTypeAdapter>& Adapter : Adapters)
		{
			if (Adapter->CanAdapt(Property))
			{
				return Adapter;
			}
		}
		return nullptr;
	}

	bool FPropertyTypeAdapterRegistry::CanAdapt(const FProperty* Property, FString* OutAdapterName) const
	{
		const TSharedPtr<IPropertyTypeAdapter> Adapter = FindAdapter(Property);
		if (Adapter.IsValid() && OutAdapterName)
		{
			*OutAdapterName = Adapter->GetName().ToString();
		}
		return Adapter.IsValid();
	}

	bool FPropertyTypeAdapterRegistry::BuildSchema(const FProperty* Property, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const
	{
		OutSchema.Reset();
		OutError.Reset();
		if (!Property)
		{
			OutError = TEXT("不能为 null FProperty 构建 Schema。");
			return false;
		}
		if (SchemaDepth >= MaxSchemaDepth)
		{
			OutError = FString::Printf(TEXT("属性 %s 的 Schema 嵌套深度超过 %d。"), *PropertyDisplayName(Property), MaxSchemaDepth);
			return false;
		}
		++SchemaDepth;
		ON_SCOPE_EXIT
		{
			--SchemaDepth;
		};
		const TSharedPtr<IPropertyTypeAdapter> Adapter = FindAdapter(Property);
		if (!Adapter.IsValid())
		{
			OutError = FString::Printf(TEXT("不支持属性类型：%s"), Property ? *Property->GetCPPType() : TEXT("<null>"));
			return false;
		}
		if (!Adapter->BuildSchema(Property, *this, OutSchema, OutError) || !OutSchema.IsValid())
		{
			return false;
		}
		AddPropertyDescription(Property, OutSchema);
		return ApplyPropertyConstraints(Property, OutSchema, OutError);
	}

	bool FPropertyTypeAdapterRegistry::Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const
	{
		OutError.Reset();
		if (!JsonValue.IsValid() || !Property || !ValueAddress)
		{
			OutError = TEXT("属性读取参数包含 null。");
			return false;
		}
		const TSharedPtr<IPropertyTypeAdapter> Adapter = FindAdapter(Property);
		if (!Adapter.IsValid())
		{
			OutError = FString::Printf(TEXT("没有可读取属性 %s 的类型适配器。"), Property ? *Property->GetName() : TEXT("<null>"));
			return false;
		}
		TSharedPtr<FJsonObject> Schema;
		if (!BuildSchema(Property, Schema, OutError) || !Schema.IsValid())
		{
			return false;
		}
		const JsonSchema::FValidationResult Validation = JsonSchema::ValidateValue(Schema.ToSharedRef(), JsonValue);
		if (!Validation.IsValid())
		{
			const JsonSchema::FValidationError& First = Validation.Errors[0];
			OutError = FString::Printf(TEXT("属性值不符合 Schema（%s）：%s"), *First.Path, *First.Message);
			return false;
		}
		void* CandidateValue = Property->AllocateAndInitializeValue();
		if (!CandidateValue)
		{
			OutError = FString::Printf(TEXT("无法为属性 %s 分配候选值。"), *PropertyDisplayName(Property));
			return false;
		}
		ON_SCOPE_EXIT
		{
			Property->DestroyAndFreeValue(CandidateValue);
		};
		Property->CopyCompleteValue(CandidateValue, ValueAddress);
		if (!Adapter->Read(JsonValue, Property, CandidateValue, OutError))
		{
			return false;
		}
		Property->CopyCompleteValue(ValueAddress, CandidateValue);
		return true;
	}

	bool FPropertyTypeAdapterRegistry::Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const
	{
		OutJsonValue.Reset();
		OutError.Reset();
		if (!Property || !ValueAddress)
		{
			OutError = TEXT("属性写出参数包含 null。");
			return false;
		}
		const TSharedPtr<IPropertyTypeAdapter> Adapter = FindAdapter(Property);
		if (!Adapter.IsValid())
		{
			OutError = FString::Printf(TEXT("没有可写出属性 %s 的类型适配器。"), Property ? *Property->GetName() : TEXT("<null>"));
			return false;
		}
		return Adapter->Write(Property, ValueAddress, OutJsonValue, OutError);
	}

	TArray<FName> FPropertyTypeAdapterRegistry::GetAdapterNames() const
	{
		TArray<FName> Names;
		FReadScopeLock Lock(RegistryLock);
		Names.Reserve(Adapters.Num());
		for (const TSharedRef<IPropertyTypeAdapter>& Adapter : Adapters)
		{
			Names.Add(Adapter->GetName());
		}
		return Names;
	}

	FPropertyTypeAdapterRegistry& FPropertyTypeAdapterRegistry::GetDefault()
	{
		static FDefaultTypeAdapterRegistryHolder Holder;
		return Holder.Registry;
	}
}
