// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPAsyncResultStore.cpp
 * @brief 线程安全的异步结果生命周期实现。
 */

#include "Core/Results/UnrealAgentMCPAsyncResultStore.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAgentMCP
{
	namespace
	{
		TSharedPtr<FJsonValue> ParseJson(const FString& Json)
		{
			if (Json.IsEmpty())
			{
				return nullptr;
			}
			TSharedPtr<FJsonValue> Value;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Reader, Value))
			{
				Value.Reset();
			}
			return Value;
		}
	}

	TSharedRef<FJsonObject> FAsyncResultSnapshot::ToJsonObject() const
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("id"), Id.ToString(EGuidFormats::DigitsWithHyphensLower));
		Result->SetStringField(TEXT("state"), AsyncResultStateToString(State));
		Result->SetBoolField(TEXT("complete"), IsComplete());
		Result->SetStringField(TEXT("createdAt"), CreatedAt.ToIso8601());
		if (CompletedAt != FDateTime())
		{
			Result->SetStringField(TEXT("completedAt"), CompletedAt.ToIso8601());
		}
		if (!ValueJson.IsEmpty())
		{
			const TSharedPtr<FJsonValue> Value = ParseJson(ValueJson);
			if (Value.IsValid())
			{
				Result->SetField(TEXT("value"), Value);
			}
		}
		if (!Error.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), Error);
		}
		return Result;
	}

	FAsyncResultStore::FAsyncResultStore(const FTimespan InRetention) : Retention(InRetention)
	{
	}

	FGuid FAsyncResultStore::Create()
	{
		FScopeLock Lock(&Mutex);
		FAsyncResultSnapshot Snapshot;
		do
		{
			Snapshot.Id = FGuid::NewGuid();
		} while (!Snapshot.Id.IsValid() || Results.Contains(Snapshot.Id));
		Snapshot.CreatedAt = FDateTime::UtcNow();
		Results.Add(Snapshot.Id, Snapshot);
		return Snapshot.Id;
	}

	bool FAsyncResultStore::Complete(const FGuid& Id, FString ValueJson)
	{
		if (!ParseJson(ValueJson).IsValid())
		{
			return false;
		}
		return Finish(Id, EAsyncResultState::Succeeded, MoveTemp(ValueJson), FString());
	}

	bool FAsyncResultStore::Fail(const FGuid& Id, FString Error)
	{
		Error.TrimStartAndEndInline();
		if (Error.IsEmpty())
		{
			return false;
		}
		return Finish(Id, EAsyncResultState::Failed, FString(), MoveTemp(Error));
	}

	bool FAsyncResultStore::Cancel(const FGuid& Id, FString Reason)
	{
		Reason.TrimStartAndEndInline();
		if (Reason.IsEmpty())
		{
			Reason = TEXT("Cancelled.");
		}
		return Finish(Id, EAsyncResultState::Cancelled, FString(), MoveTemp(Reason));
	}

	bool FAsyncResultStore::TryRead(const FGuid& Id, FAsyncResultSnapshot& OutSnapshot, const bool bConsumeCompleted)
	{
		FScopeLock Lock(&Mutex);
		const FAsyncResultSnapshot* Snapshot = Results.Find(Id);
		if (!Snapshot)
		{
			return false;
		}
		OutSnapshot = *Snapshot;
		if (bConsumeCompleted && Snapshot->IsComplete())
		{
			Results.Remove(Id);
		}
		return true;
	}

	int32 FAsyncResultStore::PurgeExpired(const FDateTime Now)
	{
		int32 Affected = 0;
		TArray<FAsyncResultSnapshot> ExpiredPendingResults;
		{
			FScopeLock Lock(&Mutex);
			for (auto It = Results.CreateIterator(); It; ++It)
			{
				FAsyncResultSnapshot& Snapshot = It.Value();
				const FDateTime AgeBase = Snapshot.IsComplete() ? Snapshot.CompletedAt : Snapshot.CreatedAt;
				if (Now - AgeBase < Retention)
				{
					continue;
				}
				if (Snapshot.IsComplete())
				{
					It.RemoveCurrent();
					++Affected;
					continue;
				}

				Snapshot.State = EAsyncResultState::Cancelled;
				Snapshot.Error = TEXT("Result expired before completion.");
				Snapshot.CompletedAt = Now;
				ExpiredPendingResults.Add(Snapshot);
				++Affected;
			}
		}
		for (const FAsyncResultSnapshot& Snapshot : ExpiredPendingResults)
		{
			ResultCompleted.Broadcast(Snapshot);
		}
		return Affected;
	}

	int32 FAsyncResultStore::Num() const
	{
		FScopeLock Lock(&Mutex);
		return Results.Num();
	}

	bool FAsyncResultStore::Finish(const FGuid& Id, const EAsyncResultState State, FString ValueJson, FString Error)
	{
		FAsyncResultSnapshot CompletedSnapshot;
		{
			FScopeLock Lock(&Mutex);
			FAsyncResultSnapshot* Snapshot = Results.Find(Id);
			if (!Snapshot || Snapshot->IsComplete())
			{
				return false;
			}
			Snapshot->State = State;
			Snapshot->ValueJson = MoveTemp(ValueJson);
			Snapshot->Error = MoveTemp(Error);
			Snapshot->CompletedAt = FDateTime::UtcNow();
			CompletedSnapshot = *Snapshot;
		}
		ResultCompleted.Broadcast(MoveTemp(CompletedSnapshot));
		return true;
	}

	FString AsyncResultStateToString(const EAsyncResultState State)
	{
		switch (State)
		{
		case EAsyncResultState::Pending:
			return TEXT("Pending");
		case EAsyncResultState::Succeeded:
			return TEXT("Succeeded");
		case EAsyncResultState::Failed:
			return TEXT("Failed");
		case EAsyncResultState::Cancelled:
			return TEXT("Cancelled");
		default:
			return TEXT("Unknown");
		}
	}
}
