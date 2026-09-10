// Copyright ZhaoZining. All Rights Reserved.

#include "Provider/UnrealAgentMCPToolExtensions.h"

#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAgentProviderSDK, Log, All);

namespace UnrealAgentMCP::Extensions
{
	namespace
	{
		FCriticalSection GHostMutex;
		TSharedPtr<IMcpToolProviderHost, ESPMode::ThreadSafe> GHost;
		TMap<FName, TSharedRef<IMcpToolProvider>> GKnownProviders;

		TSharedPtr<IMcpToolProviderHost, ESPMode::ThreadSafe> GetHost()
		{
			FScopeLock Lock(&GHostMutex);
			return GHost;
		}
	}

	bool RegisterToolProvider(const TSharedRef<IMcpToolProvider>& Provider, TArray<FString>& OutErrors)
	{
		TSharedPtr<IMcpToolProviderHost, ESPMode::ThreadSafe> Host;
		const FName ProviderName = Provider->GetProviderName();
		{
			FScopeLock Lock(&GHostMutex);
			if (ProviderName.IsNone())
			{
				OutErrors.Add(TEXT("Provider name cannot be empty."));
				return false;
			}
			if (GKnownProviders.Contains(ProviderName))
			{
				OutErrors.Add(FString::Printf(TEXT("Provider '%s' is already registered or queued."), *ProviderName.ToString()));
				return false;
			}
			GKnownProviders.Add(ProviderName, Provider);
			Host = GHost;
		}
		if (!Host.IsValid())
		{
			return true;
		}
		if (Host->RegisterProvider(Provider, OutErrors))
		{
			return true;
		}
		FScopeLock Lock(&GHostMutex);
		GKnownProviders.Remove(ProviderName);
		return false;
	}

	int32 UnregisterToolProvider(const FName ProviderName)
	{
		TSharedPtr<IMcpToolProviderHost, ESPMode::ThreadSafe> Host;
		int32 RemovedCount = 0;
		{
			FScopeLock Lock(&GHostMutex);
			RemovedCount = GKnownProviders.Remove(ProviderName);
			Host = GHost;
		}
		if (Host.IsValid())
		{
			RemovedCount = FMath::Max(RemovedCount, Host->UnregisterProvider(ProviderName));
		}
		return RemovedCount;
	}

	FMcpToolCatalogState GetToolCatalogState()
	{
		const TSharedPtr<IMcpToolProviderHost, ESPMode::ThreadSafe> Host = GetHost();
		return Host.IsValid() ? Host->GetCatalogState() : FMcpToolCatalogState();
	}

	void BindToolProviderHost(TSharedRef<IMcpToolProviderHost, ESPMode::ThreadSafe> Host)
	{
		TArray<TSharedRef<IMcpToolProvider>> Providers;
		{
			FScopeLock Lock(&GHostMutex);
			if (GHost.Get() == &Host.Get())
			{
				return;
			}
			GHost = Host;
			GKnownProviders.GenerateValueArray(Providers);
		}
		for (const TSharedRef<IMcpToolProvider>& Provider : Providers)
		{
			TArray<FString> Errors;
			if (!Host->RegisterProvider(Provider, Errors))
			{
				UE_LOG(LogUnrealAgentProviderSDK, Error, TEXT("Deferred Provider '%s' registration failed: %s"), *Provider->GetProviderName().ToString(),
					*FString::Join(Errors, TEXT(" | ")));
			}
		}
	}

	void UnbindToolProviderHost(const IMcpToolProviderHost* ExpectedHost)
	{
		FScopeLock Lock(&GHostMutex);
		if (!ExpectedHost || GHost.Get() == ExpectedHost)
		{
			GHost.Reset();
		}
	}
}

IMPLEMENT_MODULE(FDefaultModuleImpl, UnrealAgentProviderSDK)
