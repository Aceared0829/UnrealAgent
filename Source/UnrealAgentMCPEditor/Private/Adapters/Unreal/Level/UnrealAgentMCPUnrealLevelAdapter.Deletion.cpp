// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealLevelAdapter.Deletion.cpp
 * @brief Actor 批量删除与按文件夹清理的可恢复任务实现。
 */

#include "Adapters/Unreal/Level/UnrealAgentMCPUnrealLevelAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPActorSupport.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Core/Execution/UnrealAgentMCPTaskManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

namespace UnrealAgentMCP
{
	namespace
	{
		enum class ELevelDeletionPhase : uint8
		{
			Resolve,
			Discover,
			Delete,
			CountRemaining,
			Complete
		};

		struct FLevelDeletionTarget
		{
			TWeakObjectPtr<AActor> Actor;
			TSharedPtr<FJsonObject> Snapshot;
			FString Label;
		};

		FString NormalizeFolderPath(FString Path)
		{
			Path.TrimStartAndEndInline();
			Path.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (Path.StartsWith(TEXT("/")))
			{
				Path.RightChopInline(1);
			}
			while (Path.EndsWith(TEXT("/")))
			{
				Path.LeftChopInline(1);
			}
			while (Path.ReplaceInline(TEXT("//"), TEXT("/")) > 0)
			{
			}
			return Path;
		}
	}

	class FLevelActorDeletionStepper final : public Execution::IMcpTaskStepper
	{
	public:
		explicit FLevelActorDeletionStepper(const TSharedPtr<FJsonObject>& InArguments)
			: Arguments(InArguments.IsValid() ? MakeShared<FJsonObject>(*InArguments) : MakeShared<FJsonObject>())
		{
		}

		virtual bool HasWorkerPreparation() const override
		{
			return true;
		}

		virtual Execution::FMcpTaskPrepareResult PrepareOnWorker(Execution::FMcpTaskExecutionContext& Context) override
		{
			if (IsInGameThread())
			{
				return Execution::FMcpTaskPrepareResult::Failed(TEXT("Actor deletion preparation must run off the GameThread."));
			}
			if (Context.ShouldStop())
			{
				return Execution::FMcpTaskPrepareResult::Failed(
					Context.IsDeadlineExceeded() ? TEXT("Actor deletion preparation deadline exceeded.") : Context.GetCancellationReason());
			}
			if (!Arguments.IsValid())
			{
				return Execution::FMcpTaskPrepareResult::Failed(TEXT("Actor deletion arguments are unavailable."));
			}

			Arguments->TryGetStringField(TEXT("action"), Action);
			Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);
			double MaxActorsNumber = 10000.0;
			double MaxReturnedLabelsNumber = 1000.0;
			Arguments->TryGetNumberField(TEXT("maxActors"), MaxActorsNumber);
			Arguments->TryGetNumberField(TEXT("maxReturnedLabels"), MaxReturnedLabelsNumber);
			MaxActors = FMath::Clamp(static_cast<int32>(MaxActorsNumber), 1, 50000);
			MaxReturnedLabels = FMath::Clamp(static_cast<int32>(MaxReturnedLabelsNumber), 0, 5000);

			if (Action == TEXT("delete_by_folder"))
			{
				FString Confirmation;
				Arguments->TryGetStringField(TEXT("folderPath"), FolderPath);
				Arguments->TryGetStringField(TEXT("confirmation"), Confirmation);
				FolderPath = NormalizeFolderPath(MoveTemp(FolderPath));
				if (FolderPath.IsEmpty())
				{
					return Execution::FMcpTaskPrepareResult::Failed(TEXT("folderPath 不能为空，根文件夹不允许批量删除。"));
				}
				if (Confirmation != TEXT("delete_folder_contents"))
				{
					return Execution::FMcpTaskPrepareResult::Failed(TEXT("confirmation 必须为 delete_folder_contents。"));
				}
				bRecursive = true;
				Arguments->TryGetBoolField(TEXT("recursive"), bRecursive);
			}
			else if (Action == TEXT("delete_actors"))
			{
				Arguments->TryGetStringArrayField(TEXT("names"), Names);
				Arguments->TryGetStringField(TEXT("labelPrefix"), LabelPrefix);
				Arguments->TryGetStringField(TEXT("className"), ClassName);
				Arguments->TryGetStringField(TEXT("tag"), Tag);
				if (Names.IsEmpty() && LabelPrefix.IsEmpty() && ClassName.IsEmpty() && Tag.IsEmpty())
				{
					return Execution::FMcpTaskPrepareResult::Failed(TEXT("delete_actors 至少需要 names、labelPrefix、className 或 tag 之一。"));
				}
			}
			else
			{
				return Execution::FMcpTaskPrepareResult::Failed(FString::Printf(TEXT("不支持的删除 action：%s"), *Action));
			}

			Arguments.Reset();
			bPrepared = true;
			return Execution::FMcpTaskPrepareResult::Succeeded();
		}

		virtual Execution::FMcpTaskStepResult Step(Execution::FMcpTaskExecutionContext& Context, const FTimespan FrameBudget) override
		{
			if (!IsInGameThread())
			{
				return Fail(TEXT("Actor 批量删除必须在 GameThread 上执行。"));
			}
			if (Phase != ELevelDeletionPhase::Resolve && !IsValid(World))
			{
				return Fail(TEXT("Actor 批量删除期间编辑器世界已失效。"));
			}

			const double StopTime = FPlatformTime::Seconds() + FMath::Max(FrameBudget.GetTotalSeconds(), 0.00025);
			bool bAdvanced = false;
			do
			{
				if (Context.ShouldStop())
				{
					const FString Reason = Context.IsDeadlineExceeded() ? TEXT("Level deletion task deadline exceeded.") : Context.GetCancellationReason();
					return Fail(Reason);
				}

				switch (Phase)
				{
				case ELevelDeletionPhase::Resolve:
					if (!ResolveRequest(Context))
					{
						return Fail(Error);
					}
					break;
				case ELevelDeletionPhase::Discover:
					if (!DiscoverNextChunk(Context))
					{
						return Fail(Error);
					}
					break;
				case ELevelDeletionPhase::Delete:
					DeleteNextChunk(Context);
					break;
				case ELevelDeletionPhase::CountRemaining:
					CountRemainingNextChunk(Context);
					break;
				case ELevelDeletionPhase::Complete:
					return Succeed();
				default:
					return Fail(TEXT("Actor 批量删除进入未知阶段。"));
				}
				bAdvanced = true;
			} while (FPlatformTime::Seconds() < StopTime || !bAdvanced);

			return Phase == ELevelDeletionPhase::Complete ? Succeed() : Execution::FMcpTaskStepResult::Continue();
		}

		virtual Execution::FMcpTaskStepResult Abort(Execution::FMcpTaskExecutionContext& Context, FString Reason) override
		{
			(void)Context;
			return Fail(Reason.IsEmpty() ? TEXT("Actor 批量删除已取消。") : MoveTemp(Reason));
		}

	private:
		bool ResolveRequest(Execution::FMcpTaskExecutionContext& Context)
		{
			if (!bPrepared)
			{
				Error = TEXT("Actor 批量删除尚未完成 Worker Prepare。");
				return false;
			}
			World = ActorSupport::GetEditorWorld();
			if (!World)
			{
				Error = TEXT("编辑器世界不可用。");
				return false;
			}
			if (Action == TEXT("delete_actors"))
			{
				for (const FString& Name : Names)
				{
					RequestedNames.Add(FName(*Name));
				}
				if (!ClassName.IsEmpty())
				{
					RequestedClass = ActorSupport::ResolveActorClass(ClassName);
				}
			}

			DiscoveryIterator = MakeUnique<TActorIterator<AActor>>(World);
			Phase = ELevelDeletionPhase::Discover;
			Context.ReportProgress(0.02, TEXT("Discovering actors for deletion"));
			return true;
		}

		bool DiscoverNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			int32 Processed = 0;
			while (DiscoveryIterator.IsValid() && static_cast<bool>(*DiscoveryIterator) && Processed < ActorsPerDiscoveryChunk)
			{
				AActor* Actor = **DiscoveryIterator;
				++(*DiscoveryIterator);
				++Processed;
				++DiscoveredActorCount;
				TrackRequestedName(Actor);
				if (!MatchesActor(Actor))
				{
					continue;
				}
				FLevelDeletionTarget& Target = Targets.AddDefaulted_GetRef();
				Target.Actor = Actor;
				Target.Snapshot = ActorSupport::MakeActorObject(Actor);
				Target.Label = Actor->GetActorLabel();
				if (Targets.Num() > MaxActors)
				{
					Error = FString::Printf(TEXT("删除匹配数超过 maxActors=%d；请缩小选择范围。"), MaxActors);
					return false;
				}
			}

			Context.ReportProgress(0.02, FString::Printf(TEXT("Discovered %d actors; matched %d"), DiscoveredActorCount, Targets.Num()));
			if (DiscoveryIterator.IsValid() && static_cast<bool>(*DiscoveryIterator))
			{
				return true;
			}

			DiscoveryIterator.Reset();
			BuildMissingNames();
			if (bDryRun)
			{
				RemainingCount = Targets.Num();
				Phase = ELevelDeletionPhase::Complete;
			}
			else
			{
				Phase = ELevelDeletionPhase::Delete;
			}
			return true;
		}

		void DeleteNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			const int32 EndIndex = FMath::Min(NextDeleteIndex + ActorsPerDeleteChunk, Targets.Num());
			for (; NextDeleteIndex < EndIndex; ++NextDeleteIndex)
			{
				FLevelDeletionTarget& Target = Targets[NextDeleteIndex];
				AActor* Actor = Target.Actor.Get();
				if (!IsValid(Actor))
				{
					++AlreadyRemovedCount;
					continue;
				}
				Actor->Modify();
				if (World->EditorDestroyActor(Actor, true))
				{
					Deleted.Add(MakeShared<FJsonValueObject>(Target.Snapshot));
				}
				else
				{
					++FailedCount;
				}
			}
			Context.ReportProgress(0.1 + 0.75 * static_cast<double>(NextDeleteIndex) / FMath::Max(Targets.Num(), 1), TEXT("Deleting actors in bounded chunks"));
			if (NextDeleteIndex < Targets.Num())
			{
				return;
			}
			if (!Deleted.IsEmpty())
			{
				World->MarkPackageDirty();
			}
			RemainingIterator = MakeUnique<TActorIterator<AActor>>(World);
			Phase = ELevelDeletionPhase::CountRemaining;
		}

		void CountRemainingNextChunk(Execution::FMcpTaskExecutionContext& Context)
		{
			int32 Processed = 0;
			while (RemainingIterator.IsValid() && static_cast<bool>(*RemainingIterator) && Processed < ActorsPerDiscoveryChunk)
			{
				AActor* Actor = **RemainingIterator;
				++(*RemainingIterator);
				++Processed;
				if (MatchesActor(Actor))
				{
					++RemainingCount;
				}
			}
			Context.ReportProgress(0.9, TEXT("Verifying remaining actors"));
			if (!RemainingIterator.IsValid() || !static_cast<bool>(*RemainingIterator))
			{
				RemainingIterator.Reset();
				Phase = ELevelDeletionPhase::Complete;
			}
		}

		bool MatchesActor(AActor* Actor) const
		{
			if (!IsValid(Actor))
			{
				return false;
			}
			if (Action == TEXT("delete_by_folder"))
			{
				const FString ActorFolder = NormalizeFolderPath(Actor->GetFolderPath().ToString());
				return ActorFolder.Equals(FolderPath, ESearchCase::IgnoreCase) || (bRecursive && ActorFolder.StartsWith(FolderPath + TEXT("/"), ESearchCase::IgnoreCase));
			}

			if (!RequestedNames.IsEmpty() && !RequestedNames.Contains(FName(*Actor->GetName())) && !RequestedNames.Contains(FName(*Actor->GetActorLabel())))
			{
				return false;
			}
			if (!LabelPrefix.IsEmpty() && !Actor->GetActorLabel().StartsWith(LabelPrefix, ESearchCase::IgnoreCase))
			{
				return false;
			}
			if (!ClassName.IsEmpty())
			{
				if (RequestedClass && !Actor->IsA(RequestedClass))
				{
					return false;
				}
				if (!RequestedClass && !Actor->GetClass()->GetName().Contains(ClassName, ESearchCase::IgnoreCase))
				{
					return false;
				}
			}
			return Tag.IsEmpty() || Actor->ActorHasTag(FName(*Tag));
		}

		void TrackRequestedName(AActor* Actor)
		{
			if (!Actor || RequestedNames.IsEmpty())
			{
				return;
			}
			const FName ObjectName(*Actor->GetName());
			const FName ActorLabel(*Actor->GetActorLabel());
			if (RequestedNames.Contains(ObjectName))
			{
				FoundNames.Add(ObjectName);
			}
			if (RequestedNames.Contains(ActorLabel))
			{
				FoundNames.Add(ActorLabel);
			}
		}

		void BuildMissingNames()
		{
			for (const FString& Name : Names)
			{
				if (!FoundNames.Contains(FName(*Name)))
				{
					Missing.Add(MakeShared<FJsonValueString>(Name));
				}
			}
		}

		Execution::FMcpTaskStepResult Succeed() const
		{
			TArray<TSharedPtr<FJsonValue>> MatchedLabels;
			const int32 ReturnedLabelCount = FMath::Min(Targets.Num(), MaxReturnedLabels);
			for (int32 Index = 0; Index < ReturnedLabelCount; ++Index)
			{
				MatchedLabels.Add(MakeShared<FJsonValueString>(Targets[Index].Label));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("action"), Action);
			Result->SetBoolField(TEXT("dryRun"), bDryRun);
			Result->SetNumberField(TEXT("matchedCount"), Targets.Num());
			Result->SetNumberField(TEXT("deletedCount"), Deleted.Num());
			Result->SetNumberField(TEXT("alreadyRemovedCount"), AlreadyRemovedCount);
			Result->SetNumberField(TEXT("failedCount"), FailedCount);
			Result->SetNumberField(TEXT("remainingCount"), RemainingCount);
			Result->SetNumberField(TEXT("missingCount"), Missing.Num());
			Result->SetArrayField(TEXT("matchedLabels"), MatchedLabels);
			Result->SetBoolField(TEXT("matchedLabelsTruncated"), ReturnedLabelCount < Targets.Num());
			Result->SetArrayField(TEXT("deleted"), Deleted);
			Result->SetArrayField(TEXT("missing"), Missing);
			if (Action == TEXT("delete_by_folder"))
			{
				Result->SetStringField(TEXT("folderPath"), FolderPath);
				Result->SetBoolField(TEXT("recursive"), bRecursive);
			}
			return Execution::FMcpTaskStepResult::Succeeded(SuccessJson(Result));
		}

		Execution::FMcpTaskStepResult Fail(FString Reason) const
		{
			if (Reason.IsEmpty())
			{
				Reason = Error.IsEmpty() ? TEXT("Actor 批量删除失败。") : Error;
			}
			Execution::FMcpTaskStepResult Result = Execution::FMcpTaskStepResult::Failed(Reason);
			Result.ValueJson = ErrorJson(Reason);
			return Result;
		}

		static constexpr int32 ActorsPerDiscoveryChunk = 128;
		static constexpr int32 ActorsPerDeleteChunk = 8;
		TSharedPtr<FJsonObject> Arguments;
		FString Action;
		FString Error;
		UWorld* World = nullptr;
		ELevelDeletionPhase Phase = ELevelDeletionPhase::Resolve;
		TUniquePtr<TActorIterator<AActor>> DiscoveryIterator;
		TUniquePtr<TActorIterator<AActor>> RemainingIterator;
		TArray<FLevelDeletionTarget> Targets;
		TArray<TSharedPtr<FJsonValue>> Deleted;
		TArray<TSharedPtr<FJsonValue>> Missing;
		TArray<FString> Names;
		TSet<FName> RequestedNames;
		TSet<FName> FoundNames;
		FString LabelPrefix;
		FString ClassName;
		FString Tag;
		FString FolderPath;
		UClass* RequestedClass = nullptr;
		int32 MaxActors = 10000;
		int32 MaxReturnedLabels = 1000;
		int32 DiscoveredActorCount = 0;
		int32 NextDeleteIndex = 0;
		int32 AlreadyRemovedCount = 0;
		int32 FailedCount = 0;
		int32 RemainingCount = 0;
		bool bDryRun = false;
		bool bRecursive = true;
		bool bPrepared = false;
	};

	FString FUnrealAgentMCPUnrealLevelAdapter::DeleteActors(const TSharedPtr<FJsonObject>& Args)
	{
		(void)Args;
		return ErrorJson(TEXT("Actor 批量删除必须通过可恢复任务执行器运行。"));
	}

	TSharedPtr<Execution::IMcpTaskStepper> FUnrealAgentMCPUnrealLevelAdapter::CreateTaskStepper(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("action"), Action))
		{
			return nullptr;
		}
		if (Action == TEXT("delete_actors") || Action == TEXT("delete_by_folder"))
		{
			return MakeShared<FLevelActorDeletionStepper>(Args);
		}
		if (Action == TEXT("place_actors_batch"))
		{
			return CreateBatchTaskStepper(Args);
		}
		return nullptr;
	}
}
