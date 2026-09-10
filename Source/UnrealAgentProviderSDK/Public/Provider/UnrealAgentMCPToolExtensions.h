// Copyright ZhaoZining. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"

namespace UnrealAgentMCP::Extensions
{
	struct UNREALAGENTPROVIDERSDK_API FMcpToolCatalogState
	{
		uint64 Generation = 0;
		FString CatalogHash;
		int32 ToolCount = 0;
	};

	class UNREALAGENTPROVIDERSDK_API IMcpToolProviderHost
	{
	public:
		virtual ~IMcpToolProviderHost() = default;

		virtual bool RegisterProvider(const TSharedRef<IMcpToolProvider>& Provider, TArray<FString>& OutErrors) = 0;
		virtual int32 UnregisterProvider(FName ProviderName) = 0;
		virtual FMcpToolCatalogState GetCatalogState() const = 0;
	};

	UNREALAGENTPROVIDERSDK_API bool RegisterToolProvider(const TSharedRef<IMcpToolProvider>& Provider, TArray<FString>& OutErrors);

	UNREALAGENTPROVIDERSDK_API int32 UnregisterToolProvider(FName ProviderName);

	UNREALAGENTPROVIDERSDK_API FMcpToolCatalogState GetToolCatalogState();

	UNREALAGENTPROVIDERSDK_API void BindToolProviderHost(TSharedRef<IMcpToolProviderHost, ESPMode::ThreadSafe> Host);
	UNREALAGENTPROVIDERSDK_API void UnbindToolProviderHost(const IMcpToolProviderHost* ExpectedHost);
}
