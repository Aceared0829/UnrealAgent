// Copyright ZhaoZining. All Rights Reserved.

#include "Tests/Core/UnrealAgentMCPReflectionTestTypes.h"

int32 UUnrealAgentMCPReflectionTestToolset::Add(const int32 A, const int32 B) const
{
	return A + B;
}

FUnrealAgentMCPReflectionPayload UUnrealAgentMCPReflectionTestToolset::EchoPayload(FUnrealAgentMCPReflectionPayload Payload) const
{
	return Payload;
}
