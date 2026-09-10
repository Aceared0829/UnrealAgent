// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPSessionRegistry.cpp
 * @brief MCP Session 状态机的有界并发实现。
 */

#include "Core/Protocol/UnrealAgentMCPSessionRegistry.h"

#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"

namespace UnrealAgentMCP::Protocol
{
	FMcpSessionRegistry::FMcpSessionRegistry(const int32 InMaximumSessions) : MaximumSessions(FMath::Max(1, InMaximumSessions))
	{
	}

	FString FMcpSessionRegistry::Open(const FString& ProtocolVersion, FString* OutEvictedSessionId)
	{
		FScopeLock Lock(&Mutex);
		if (OutEvictedSessionId)
		{
			OutEvictedSessionId->Empty();
		}
		while (Sessions.Num() >= MaximumSessions)
		{
			const FString Evicted = RemoveLeastValuableLocked();
			if (OutEvictedSessionId && !Evicted.IsEmpty())
			{
				*OutEvictedSessionId = Evicted;
			}
		}

		FString SessionId;
		do
		{
			SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		} while (Sessions.Contains(SessionId));

		FSessionState State;
		State.ProtocolVersion = ProtocolVersion;
		State.Ordinal = NextOrdinal++;
		TouchLocked(State);
		Sessions.Add(SessionId, MoveTemp(State));
		InsertionOrder.Add(SessionId);
		return SessionId;
	}

	bool FMcpSessionRegistry::Close(const FString& SessionId)
	{
		FScopeLock Lock(&Mutex);
		const bool bRemoved = Sessions.Remove(SessionId) > 0;
		if (bRemoved)
		{
			InsertionOrder.RemoveSingle(SessionId);
		}
		return bRemoved;
	}

	bool FMcpSessionRegistry::MarkInitialized(const FString& SessionId)
	{
		FScopeLock Lock(&Mutex);
		FSessionState* State = Sessions.Find(SessionId);
		if (!State)
		{
			return false;
		}
		State->bInitialized = true;
		TouchLocked(*State);
		return true;
	}

	EMcpSessionValidation FMcpSessionRegistry::Validate(const FString& SessionId, const FString& ProtocolVersion, const bool bRequireInitialized,
		FMcpSessionSnapshot* OutSnapshot) const
	{
		if (SessionId.IsEmpty())
		{
			return EMcpSessionValidation::MissingSessionId;
		}

		FScopeLock Lock(&Mutex);
		FSessionState* State = Sessions.Find(SessionId);
		if (!State)
		{
			return EMcpSessionValidation::UnknownSession;
		}
		if (State->ProtocolVersion != ProtocolVersion)
		{
			return EMcpSessionValidation::ProtocolMismatch;
		}
		TouchLocked(*State);
		if (bRequireInitialized && !State->bInitialized)
		{
			return EMcpSessionValidation::NotInitialized;
		}
		if (OutSnapshot)
		{
			*OutSnapshot = MakeSnapshot(SessionId, *State);
		}
		return EMcpSessionValidation::Ready;
	}

	bool FMcpSessionRegistry::TryGet(const FString& SessionId, FMcpSessionSnapshot& OutSnapshot) const
	{
		FScopeLock Lock(&Mutex);
		const FSessionState* State = Sessions.Find(SessionId);
		if (!State)
		{
			return false;
		}
		OutSnapshot = MakeSnapshot(SessionId, *State);
		return true;
	}

	bool FMcpSessionRegistry::BindRequestToTask(const FString& SessionId, const FString& RequestKey, const FString& TaskId)
	{
		if (RequestKey.IsEmpty() || TaskId.IsEmpty())
		{
			return false;
		}
		FScopeLock Lock(&Mutex);
		FSessionState* State = Sessions.Find(SessionId);
		if (!State)
		{
			return false;
		}
		if (!State->RequestTasks.Contains(RequestKey))
		{
			while (State->RequestTasks.Num() >= MaximumRequestBindingsPerSession)
			{
				if (State->RequestBindingOrder.IsEmpty())
				{
					State->RequestTasks.Reset();
					break;
				}
				const FString OldestRequestKey = State->RequestBindingOrder[0];
				State->RequestBindingOrder.RemoveAt(0, 1, EAllowShrinking::No);
				State->RequestTasks.Remove(OldestRequestKey);
			}
			State->RequestBindingOrder.Add(RequestKey);
		}
		State->RequestTasks.Add(RequestKey, TaskId);
		TouchLocked(*State);
		return true;
	}

	bool FMcpSessionRegistry::ResolveTaskForRequest(const FString& SessionId, const FString& RequestKey, FString& OutTaskId) const
	{
		OutTaskId.Empty();
		FScopeLock Lock(&Mutex);
		FSessionState* State = Sessions.Find(SessionId);
		const FString* TaskId = State ? State->RequestTasks.Find(RequestKey) : nullptr;
		if (!TaskId)
		{
			return false;
		}
		OutTaskId = *TaskId;
		TouchLocked(*State);
		return true;
	}

	void FMcpSessionRegistry::Reset()
	{
		FScopeLock Lock(&Mutex);
		Sessions.Reset();
		InsertionOrder.Reset();
		NextOrdinal = 1;
		NextActivityOrdinal = 1;
	}

	int32 FMcpSessionRegistry::Num() const
	{
		FScopeLock Lock(&Mutex);
		return Sessions.Num();
	}

	int32 FMcpSessionRegistry::GetMaximumSessions() const
	{
		return MaximumSessions;
	}

	FString FMcpSessionRegistry::RemoveLeastValuableLocked()
	{
		FString CandidateId;
		const FSessionState* Candidate = nullptr;
		for (const TPair<FString, FSessionState>& Pair : Sessions)
		{
			if (!Candidate || (Candidate->bInitialized && !Pair.Value.bInitialized) ||
				(Candidate->bInitialized == Pair.Value.bInitialized && Pair.Value.ActivityOrdinal < Candidate->ActivityOrdinal))
			{
				CandidateId = Pair.Key;
				Candidate = &Pair.Value;
			}
		}
		if (CandidateId.IsEmpty())
		{
			return FString();
		}
		Sessions.Remove(CandidateId);
		InsertionOrder.RemoveSingle(CandidateId);
		return CandidateId;
	}

	void FMcpSessionRegistry::TouchLocked(FSessionState& State) const
	{
		State.ActivityOrdinal = NextActivityOrdinal++;
	}

	FMcpSessionSnapshot FMcpSessionRegistry::MakeSnapshot(const FString& SessionId, const FSessionState& State)
	{
		FMcpSessionSnapshot Snapshot;
		Snapshot.Id = SessionId;
		Snapshot.ProtocolVersion = State.ProtocolVersion;
		Snapshot.bInitialized = State.bInitialized;
		Snapshot.Ordinal = State.Ordinal;
		Snapshot.ActivityOrdinal = State.ActivityOrdinal;
		Snapshot.RequestBindingCount = State.RequestTasks.Num();
		return Snapshot;
	}
}
