// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Objects.cpp
 * @brief 编辑器对象属性、反射说明与函数调用操作。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Reflection/UnrealAgentMCPPropertyTypeAdapter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedPtr<FJsonObject> ReadInvocationArguments(const TSharedPtr<FJsonObject>& Args)
		{
			if (!Args.IsValid())
			{
				return MakeShared<FJsonObject>();
			}
			const TSharedPtr<FJsonObject>* Arguments = nullptr;
			if (Args->TryGetObjectField(TEXT("arguments"), Arguments) && Arguments)
			{
				return *Arguments;
			}
			if (Args->TryGetObjectField(TEXT("params"), Arguments) && Arguments)
			{
				return *Arguments;
			}
			return MakeShared<FJsonObject>();
		}

		UClass* ResolveFunctionLibraryClass(const FString& ClassText)
		{
			if (ClassText.IsEmpty())
			{
				return nullptr;
			}
			if (UClass* Loaded = LoadObject<UClass>(nullptr, *ClassText))
			{
				return Loaded;
			}
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName().Equals(ClassText, ESearchCase::IgnoreCase) || It->GetPathName().Equals(ClassText, ESearchCase::IgnoreCase))
				{
					return *It;
				}
			}
			return nullptr;
		}

		TSharedRef<FJsonObject> PropertyDescription(FProperty* Property)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Property->GetName());
			Item->SetStringField(TEXT("cppType"), Property->GetCPPType());
			Item->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
			Item->SetBoolField(TEXT("readOnly"), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_EditConst));
			TSharedPtr<FJsonObject> Schema;
			FString Error;
			if (Reflection::FPropertyTypeAdapterRegistry::GetDefault().BuildSchema(Property, Schema, Error) && Schema.IsValid())
			{
				Item->SetObjectField(TEXT("schema"), Schema);
			}
			return Item;
		}
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Objects(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list_function_libraries"))
		{
			TArray<TSharedPtr<FJsonValue>> Libraries;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;
				if (!Class || !Class->IsChildOf(UBlueprintFunctionLibrary::StaticClass()) || Class == UBlueprintFunctionLibrary::StaticClass() ||
					Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Class->GetName());
				Item->SetStringField(TEXT("classPath"), Class->GetPathName());
				Libraries.Add(MakeShared<FJsonValueObject>(Item));
			}
			Libraries.Sort(
				[](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
				{
					return Left->AsObject()->GetStringField(TEXT("name")) < Right->AsObject()->GetStringField(TEXT("name"));
				});
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Libraries.Num());
			Result->SetArrayField(TEXT("libraries"), Libraries);
			return SuccessJson(Result);
		}

		if (Action == TEXT("invoke_static_function"))
		{
			const FString ClassText = GetString(Args, { TEXT("classPath"), TEXT("className"), TEXT("library") });
			UClass* Class = ResolveFunctionLibraryClass(ClassText);
			if (!Class || !Class->IsChildOf(UBlueprintFunctionLibrary::StaticClass()))
			{
				return ErrorJson(TEXT("未找到指定的 BlueprintFunctionLibrary。"));
			}
			return InvokeReflectedFunction(Class->GetDefaultObject(), GetString(Args, { TEXT("functionName"), TEXT("function") }), ReadInvocationArguments(Args));
		}

		FString Error;
		const bool bRuntime = Action == TEXT("invoke_function") || Action == TEXT("invoke_object_function") || Action == TEXT("get_object_properties");
		UObject* Target = ResolveObject(Args, bRuntime, Error);
		if (!Target)
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("invoke_function") || Action == TEXT("invoke_object_function"))
		{
			return InvokeReflectedFunction(Target, GetString(Args, { TEXT("functionName"), TEXT("function") }), ReadInvocationArguments(Args));
		}

		if (Action == TEXT("set_property"))
		{
			const FString PropertyName = GetString(Args, { TEXT("propertyName"), TEXT("property") });
			FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
			const TSharedPtr<FJsonValue> Value = Args.IsValid() ? Args->TryGetField(TEXT("value")) : nullptr;
			if (!Property || !Value.IsValid())
			{
				return ErrorJson(TEXT("属性不存在或缺少 value。"));
			}
			if (!PropertyWriter::SetPropertyFromJson(Target, Property, Value, Error))
			{
				return ErrorJson(Error);
			}
			Target->PostEditChange();
			Target->MarkPackageDirty();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("objectPath"), Target->GetPathName());
			Result->SetStringField(TEXT("propertyName"), PropertyName);
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_property"))
		{
			const FString PropertyName = GetString(Args, { TEXT("propertyName"), TEXT("property") });
			FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), *PropertyName);
			if (!Property)
			{
				return ErrorJson(TEXT("未找到指定属性。"));
			}
			TSharedPtr<FJsonValue> Value;
			const void* Address = Property->ContainerPtrToValuePtr<void>(Target);
			if (!Reflection::FPropertyTypeAdapterRegistry::GetDefault().Write(Property, Address, Value, Error) || !Value.IsValid())
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("objectPath"), Target->GetPathName());
			Result->SetStringField(TEXT("propertyName"), PropertyName);
			Result->SetField(TEXT("value"), Value);
			return SuccessJson(Result);
		}

		TArray<TSharedPtr<FJsonValue>> Properties;
		TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Target->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			Properties.Add(MakeShared<FJsonValueObject>(PropertyDescription(Property)));
			if (Action == TEXT("get_object_properties"))
			{
				TSharedPtr<FJsonValue> Value;
				const void* Address = Property->ContainerPtrToValuePtr<void>(Target);
				if (Reflection::FPropertyTypeAdapterRegistry::GetDefault().Write(Property, Address, Value, Error) && Value.IsValid())
				{
					Values->SetField(Property->GetName(), Value);
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("objectPath"), Target->GetPathName());
		Result->SetStringField(TEXT("classPath"), Target->GetClass()->GetPathName());
		Result->SetNumberField(TEXT("propertyCount"), Properties.Num());
		Result->SetArrayField(TEXT("properties"), Properties);
		if (Action == TEXT("get_object_properties"))
		{
			Result->SetObjectField(TEXT("values"), Values);
		}
		if (Action == TEXT("describe_object"))
		{
			TArray<TSharedPtr<FJsonValue>> Functions;
			for (TFieldIterator<UFunction> It(Target->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
			{
				if (It->HasAnyFunctionFlags(FUNC_Public))
				{
					Functions.Add(MakeShared<FJsonValueString>(It->GetName()));
				}
			}
			Result->SetNumberField(TEXT("functionCount"), Functions.Num());
			Result->SetArrayField(TEXT("functions"), Functions);
		}
		return SuccessJson(Result);
	}
}
