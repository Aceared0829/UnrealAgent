// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPropertyTypeAdapter.h
 * @brief FProperty 的 Schema、读取与写入共用的类型适配注册中心。
 */

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class FJsonObject;
class FJsonValue;
class FProperty;

namespace UnrealAgentMCP::Reflection
{
	class FPropertyTypeAdapterRegistry;

	/** 单一属性类型适配器，同时定义契约与真实转换行为。 */
	class UNREALAGENTMCPCORE_API IPropertyTypeAdapter
	{
	public:
		virtual ~IPropertyTypeAdapter() = default;

		virtual FName GetName() const = 0;
		virtual bool CanAdapt(const FProperty* Property) const = 0;
		virtual bool BuildSchema(const FProperty* Property, const FPropertyTypeAdapterRegistry& Registry, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const = 0;
		virtual bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const = 0;
		virtual bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const = 0;
	};

	/**
	 * 可扩展的类型适配注册中心。
	 * 同一属性只能由第一个匹配适配器处理，注册顺序因此是稳定契约。
	 */
	class UNREALAGENTMCPCORE_API FPropertyTypeAdapterRegistry
	{
	public:
		bool RegisterAdapter(const TSharedRef<IPropertyTypeAdapter>& Adapter, FString& OutError);

		bool CanAdapt(const FProperty* Property, FString* OutAdapterName = nullptr) const;

		bool BuildSchema(const FProperty* Property, TSharedPtr<FJsonObject>& OutSchema, FString& OutError) const;

		bool Read(const TSharedPtr<FJsonValue>& JsonValue, const FProperty* Property, void* ValueAddress, FString& OutError) const;

		bool Write(const FProperty* Property, const void* ValueAddress, TSharedPtr<FJsonValue>& OutJsonValue, FString& OutError) const;

		TArray<FName> GetAdapterNames() const;

		/** 取得 Unreal Agent 内置的共享类型注册中心。 */
		static FPropertyTypeAdapterRegistry& GetDefault();

	private:
		TSharedPtr<IPropertyTypeAdapter> FindAdapter(const FProperty* Property) const;

		mutable FRWLock RegistryLock;
		TArray<TSharedRef<IPropertyTypeAdapter>> Adapters;
	};
}
