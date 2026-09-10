// Copyright ZhaoZining. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"
#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"
#include "Adapters/Unreal/Niagara/UnrealAgentMCPUnrealNiagaraAdapter.h"
#include "Adapters/Unreal/StateTree/UnrealAgentMCPUnrealStateTreeAdapter.h"
#include "Adapters/Unreal/Widget/UnrealAgentMCPUnrealWidgetAdapter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP::Tests
{

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealAgentMCPThinDomainValidationTest, "WorldData.UnrealAgent.Adapters.ThinDomainValidation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FUnrealAgentMCPThinDomainValidationTest::RunTest(const FString& Parameters)
	{
		auto VerifyRejectedActions = [this](auto& Adapter, const FString& Domain, int32 ExpectedActionCount)
		{
			for (const FString& Action : { FString(), FString(TEXT("unknown_cleanup_action")) })
			{
				TSharedPtr<FJsonObject> Args;
				if (!Action.IsEmpty())
				{
					Args = MakeShared<FJsonObject>();
					Args->SetStringField(TEXT("action"), Action);
				}
				const FString Response = Adapter.Execute(Args);
				TSharedPtr<FJsonObject> Result;
				if (!TestTrue(Domain + TEXT(" returns JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response), Result) && Result.IsValid()))
				{
					continue;
				}
				TestFalse(Domain + TEXT(" rejects invalid action"), Result->GetBoolField(TEXT("success")));
				TestEqual(Domain + TEXT(" preserves domain"), Result->GetStringField(TEXT("domain")), Domain);
				TestEqual(Domain + TEXT(" preserves action"), Result->GetStringField(TEXT("action")), Action);
				TestEqual(Domain + TEXT(" retains action catalog"), Result->GetArrayField(TEXT("implementedActions")).Num(), ExpectedActionCount);
				TestFalse(Domain + TEXT(" explains rejection"), Result->GetStringField(TEXT("error")).IsEmpty());
				if (Args.IsValid())
				{
					TestEqual(Domain + TEXT(" does not mutate arguments"), Args->Values.Num(), 1);
					TestEqual(Domain + TEXT(" retains caller action"), Args->GetStringField(TEXT("action")), Action);
				}
			}
		};

		FUnrealAgentMCPUnrealMaterialAdapter MaterialAdapter;
		FUnrealAgentMCPUnrealWidgetAdapter WidgetAdapter;
		FUnrealAgentMCPUnrealNiagaraAdapter NiagaraAdapter;
		FUnrealAgentMCPUnrealStateTreeAdapter StateTreeAdapter;
		FUnrealAgentMCPUnrealAudioAdapter AudioAdapter;
		VerifyRejectedActions(MaterialAdapter, TEXT("material"), 41);
		VerifyRejectedActions(WidgetAdapter, TEXT("widget"), 27);
		VerifyRejectedActions(NiagaraAdapter, TEXT("niagara"), 31);
		VerifyRejectedActions(StateTreeAdapter, TEXT("statetree"), 36);
		VerifyRejectedActions(AudioAdapter, TEXT("audio"), 36);
		return true;
	}

}

#endif
