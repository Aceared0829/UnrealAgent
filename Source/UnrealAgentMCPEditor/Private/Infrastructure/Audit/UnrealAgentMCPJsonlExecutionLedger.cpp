// Copyright ZhaoZining. All Rights Reserved.

#include "Infrastructure/Audit/UnrealAgentMCPJsonlExecutionLedger.h"

#include "Core/Protocol/UnrealAgentMCPProtocol.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Infrastructure/Environment/UnrealAgentMCPServerEnvironment.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAgentMCPExecutionLedger, Log, All);

namespace UnrealAgentMCP::Audit
{
	namespace
	{
		void AppendCanonicalJson(const TSharedPtr<FJsonValue>& Value, FString& OutJson)
		{
			if (!Value.IsValid() || Value->IsNull())
			{
				OutJson += TEXT("null");
				return;
			}
			if (Value->Type == EJson::Object)
			{
				OutJson.AppendChar(TEXT('{'));
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				struct FCanonicalField
				{
					FString Key;
					TSharedPtr<FJsonValue> FieldValue;
				};
				TArray<FCanonicalField> Fields;
				Fields.Reserve(Object->Values.Num());
				for (const auto& Pair : Object->Values)
				{
					FCanonicalField& Field = Fields.AddDefaulted_GetRef();
					Field.Key = FString(Pair.Key.ToView());
					Field.FieldValue = Pair.Value;
				}
				Fields.Sort(
					[](const FCanonicalField& Left, const FCanonicalField& Right)
					{
						return Left.Key < Right.Key;
					});
				for (int32 Index = 0; Index < Fields.Num(); ++Index)
				{
					if (Index > 0)
					{
						OutJson.AppendChar(TEXT(','));
					}
					FString EncodedKey;
					const TSharedRef<TJsonWriter<>> KeyWriter = TJsonWriterFactory<>::Create(&EncodedKey);
					FJsonSerializer::Serialize(MakeShared<FJsonValueString>(Fields[Index].Key), TEXT(""), KeyWriter);
					OutJson += EncodedKey;
					OutJson.AppendChar(TEXT(':'));
					AppendCanonicalJson(Fields[Index].FieldValue, OutJson);
				}
				OutJson.AppendChar(TEXT('}'));
				return;
			}
			if (Value->Type == EJson::Array)
			{
				OutJson.AppendChar(TEXT('['));
				const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
				for (int32 Index = 0; Index < Values.Num(); ++Index)
				{
					if (Index > 0)
					{
						OutJson.AppendChar(TEXT(','));
					}
					AppendCanonicalJson(Values[Index], OutJson);
				}
				OutJson.AppendChar(TEXT(']'));
				return;
			}

			FString ScalarJson;
			const TSharedRef<TJsonWriter<>> ScalarWriter = TJsonWriterFactory<>::Create(&ScalarJson);
			FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT(""), ScalarWriter);
			OutJson += ScalarJson;
		}

		FString BuildCanonicalJson(const TSharedRef<FJsonObject>& Object)
		{
			FString Json;
			AppendCanonicalJson(MakeShared<FJsonValueObject>(Object), Json);
			return Json;
		}

		FString SerializeJson(const TSharedRef<FJsonObject>& Object)
		{
			FString Json;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
			FJsonSerializer::Serialize(Object, Writer);
			return Json;
		}

		FString GuidToString(const FGuid& Guid)
		{
			return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
		}

		class FJsonlExecutionLedger final : public Execution::IMcpExecutionLedger
		{
		public:
			explicit FJsonlExecutionLedger(FString InLocation)
				: Location(
					  InLocation.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAgent"), TEXT("Audit"), TEXT("execution-ledger.jsonl")) : MoveTemp(InLocation))
			{
				Initialize();
			}

			virtual bool AppendAuditRecord(const Policy::FMcpAuditRecord& Record, FString& OutError) override
			{
				TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("traceId"), GuidToString(Record.TraceId));
				Payload->SetStringField(TEXT("requestId"), GuidToString(Record.RequestId));
				Payload->SetStringField(TEXT("timestamp"), Record.Timestamp.ToIso8601());
				Payload->SetStringField(TEXT("clientId"), Record.ClientId);
				Payload->SetStringField(TEXT("sessionId"), Record.SessionId);
				Payload->SetStringField(TEXT("source"), Record.Source);
				Payload->SetStringField(TEXT("provider"), Record.Provider);
				Payload->SetStringField(TEXT("tool"), Record.ToolName);
				Payload->SetStringField(TEXT("risk"), ToolDescriptor::RiskToString(Record.Risk));
				Payload->SetStringField(TEXT("outcome"), Policy::PolicyOutcomeToString(Record.Outcome));
				Payload->SetStringField(TEXT("decisionCode"), Record.DecisionCode);
				TArray<TSharedPtr<FJsonValue>> SchemaErrorPaths;
				SchemaErrorPaths.Reserve(Record.SchemaErrorPaths.Num());
				for (const FString& Path : Record.SchemaErrorPaths)
				{
					SchemaErrorPaths.Add(MakeShared<FJsonValueString>(Path));
				}
				Payload->SetArrayField(TEXT("schemaErrorPaths"), SchemaErrorPaths);
				Payload->SetBoolField(TEXT("accepted"), Record.bAccepted);
				return AppendEvent(TEXT("audit"), Record.TraceId, Payload, OutError);
			}

			virtual bool AppendTaskSnapshot(const Execution::FMcpTaskSnapshot& Snapshot, FString& OutError) override
			{
				TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
				Payload->SetStringField(TEXT("traceId"), GuidToString(Snapshot.TraceId));
				Payload->SetStringField(TEXT("taskId"), GuidToString(Snapshot.Id));
				Payload->SetStringField(TEXT("requestId"), GuidToString(Snapshot.RequestId));
				Payload->SetStringField(TEXT("protocolRequestId"), Snapshot.ProtocolRequestId);
				Payload->SetStringField(TEXT("clientId"), Snapshot.ClientId);
				Payload->SetStringField(TEXT("sessionId"), Snapshot.SessionId);
				Payload->SetStringField(TEXT("source"), Snapshot.Source);
				Payload->SetStringField(TEXT("provider"), Snapshot.Provider);
				Payload->SetStringField(TEXT("tool"), Snapshot.ToolName);
				Payload->SetStringField(TEXT("owner"), Snapshot.Owner);
				Payload->SetStringField(TEXT("state"), Execution::TaskStateToString(Snapshot.State));
				Payload->SetStringField(TEXT("executionPhase"), Execution::TaskExecutionPhaseToString(Snapshot.ExecutionPhase));
				Payload->SetBoolField(TEXT("cancelable"), Snapshot.bCancelable);
				Payload->SetBoolField(TEXT("resumable"), Snapshot.bResumable);
				Payload->SetBoolField(TEXT("cancellationRequested"), Snapshot.bCancellationRequested);
				Payload->SetBoolField(TEXT("cancellationDeferred"), Snapshot.bCancellationDeferred);
				Payload->SetStringField(TEXT("sideEffectState"), Snapshot.SideEffectState);
				Payload->SetStringField(TEXT("createdAt"), Snapshot.CreatedAt.ToIso8601());
				Payload->SetStringField(TEXT("startedAt"), Snapshot.StartedAt.ToIso8601());
				Payload->SetStringField(TEXT("completedAt"), Snapshot.CompletedAt.ToIso8601());
				Payload->SetStringField(TEXT("deadlineAt"), Snapshot.DeadlineAt.ToIso8601());
				Payload->SetNumberField(TEXT("stepCount"), Snapshot.StepCount);
				Payload->SetNumberField(TEXT("applyStepBudgetMs"), Snapshot.ApplyStepBudgetMs);
				Payload->SetNumberField(TEXT("prepareDurationMs"), Snapshot.PrepareDurationMs);
				Payload->SetNumberField(TEXT("totalApplyDurationMs"), Snapshot.TotalApplyDurationMs);
				Payload->SetNumberField(TEXT("applyStepP95DurationMs"), Snapshot.ApplyStepP95DurationMs);
				Payload->SetNumberField(TEXT("applyStepP99DurationMs"), Snapshot.ApplyStepP99DurationMs);
				Payload->SetNumberField(TEXT("budgetOverrunCount"), Snapshot.BudgetOverrunCount);
				Payload->SetNumberField(TEXT("cancellationCheckpointCount"), Snapshot.CancellationCheckpointCount);
				Payload->SetNumberField(TEXT("compensationRegisteredCount"), Snapshot.CompensationRegisteredCount);
				Payload->SetNumberField(TEXT("compensationExecutedCount"), Snapshot.CompensationExecutedCount);
				Payload->SetNumberField(TEXT("compensationFailureCount"), Snapshot.CompensationFailureCount);
				Payload->SetNumberField(TEXT("compensationDurationMs"), Snapshot.CompensationDurationMs);
				return AppendEvent(TEXT("task_state"), Snapshot.TraceId, Payload, OutError);
			}

			virtual bool IsHealthy(FString& OutError) const override
			{
				FScopeLock Lock(&Mutex);
				OutError = HealthError;
				return bIsHealthy;
			}

			virtual FString GetLocation() const override
			{
				return Location;
			}

		private:
			void Initialize()
			{
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Location), true);
				const FString CanonicalLocation = FPaths::ConvertRelativePathToFull(Location).ToLower();
				FString LockSuffix = Protocol::CalculateStableRevision(CanonicalLocation);
				LockSuffix.ReplaceInline(TEXT(":"), TEXT("_"));
				SystemWideLock = MakeUnique<FSystemWideCriticalSection>(TEXT("UnrealAgentExecutionLedger_") + LockSuffix, FTimespan::Zero());
				if (!SystemWideLock->IsValid())
				{
					SetUnhealthy(TEXT("Execution ledger is already owned by another Editor "
									  "instance."));
					return;
				}
				if (!FPaths::FileExists(Location) && !ServerEnvironment::WriteStringAtomically(FString(), Location, true))
				{
					SetUnhealthy(TEXT("Unable to create the owner-only execution ledger."));
					return;
				}
				FString AccessError;
				if (!ServerEnvironment::IsFileAccessRestrictedToCurrentUser(Location, AccessError))
				{
					SetUnhealthy(FString::Printf(TEXT("Execution ledger ACL validation failed: %s"), *AccessError));
					return;
				}

				FString Content;
				if (!FFileHelper::LoadFileToString(Content, *Location))
				{
					SetUnhealthy(TEXT("Unable to read the execution ledger."));
					return;
				}
				TArray<FString> Lines;
				Content.ParseIntoArrayLines(Lines, true);
				for (const FString& Line : Lines)
				{
					TSharedPtr<FJsonObject> Event;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
					if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
					{
						SetUnhealthy(TEXT("Execution ledger contains invalid JSONL."));
						return;
					}
					double SequenceValue = 0.0;
					FString EventPreviousHash;
					FString EventHash;
					if (!Event->TryGetNumberField(TEXT("sequence"), SequenceValue) || !Event->TryGetStringField(TEXT("previousHash"), EventPreviousHash) ||
						!Event->TryGetStringField(TEXT("eventHash"), EventHash) || static_cast<int64>(SequenceValue) != Sequence + 1 || EventPreviousHash != PreviousHash)
					{
						SetUnhealthy(TEXT("Execution ledger sequence or hash link is invalid."));
						return;
					}
					Event->RemoveField(TEXT("eventHash"));
					const FString CalculatedHash = Protocol::CalculateStableRevision(BuildCanonicalJson(Event.ToSharedRef()));
					if (CalculatedHash != EventHash)
					{
						SetUnhealthy(TEXT("Execution ledger event hash validation failed."));
						return;
					}
					Sequence = static_cast<int64>(SequenceValue);
					PreviousHash = EventHash;
				}
			}

			bool AppendEvent(const FString& EventType, const FGuid& TraceId, const TSharedRef<FJsonObject>& Payload, FString& OutError)
			{
				FScopeLock Lock(&Mutex);
				OutError.Reset();
				if (!bIsHealthy)
				{
					OutError = HealthError;
					return false;
				}

				TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
				Event->SetNumberField(TEXT("schemaVersion"), 1);
				Event->SetNumberField(TEXT("sequence"), Sequence + 1);
				Event->SetStringField(TEXT("previousHash"), PreviousHash);
				Event->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());
				Event->SetStringField(TEXT("eventType"), EventType);
				Event->SetStringField(TEXT("traceId"), GuidToString(TraceId));
				Event->SetObjectField(TEXT("payload"), Payload);
				const FString EventHash = Protocol::CalculateStableRevision(BuildCanonicalJson(Event));
				Event->SetStringField(TEXT("eventHash"), EventHash);
				const FString Line = SerializeJson(Event) + LINE_TERMINATOR;
				if (!FFileHelper::SaveStringToFile(Line, *Location, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
				{
					SetUnhealthy(TEXT("Unable to append the execution ledger."));
					OutError = HealthError;
					return false;
				}
				++Sequence;
				PreviousHash = EventHash;
				return true;
			}

			void SetUnhealthy(FString Error)
			{
				bIsHealthy = false;
				HealthError = MoveTemp(Error);
				UE_LOG(LogUnrealAgentMCPExecutionLedger, Error, TEXT("%s"), *HealthError);
			}

			mutable FCriticalSection Mutex;
			TUniquePtr<FSystemWideCriticalSection> SystemWideLock;
			FString Location;
			int64 Sequence = 0;
			FString PreviousHash = TEXT("genesis");
			bool bIsHealthy = true;
			FString HealthError;
		};
	}

	TSharedRef<Execution::IMcpExecutionLedger, ESPMode::ThreadSafe> CreateJsonlExecutionLedger(FString Location)
	{
		return MakeShared<FJsonlExecutionLedger, ESPMode::ThreadSafe>(MoveTemp(Location));
	}
}
