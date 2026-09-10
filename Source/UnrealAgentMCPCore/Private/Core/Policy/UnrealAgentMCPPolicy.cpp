// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPPolicy.cpp
 * @brief 服务端策略、项目文件沙箱、资产路径沙箱与审计实现。
 */

#include "Core/Policy/UnrealAgentMCPPolicy.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace UnrealAgentMCP::Policy
{
	namespace
	{
		FString NormalizeRoot(FString Root)
		{
			if (Root.IsEmpty())
			{
				return FString();
			}
			Root = FPaths::ConvertRelativePathToFull(Root);
			FPaths::NormalizeDirectoryName(Root);
			FPaths::CollapseRelativeDirectories(Root);
			FPaths::MakeStandardFilename(Root);
			Root.RemoveFromEnd(TEXT("/"));
			return Root;
		}

		bool IsWithinRoot(const FString& Path, const FString& Root)
		{
			if (Root.IsEmpty())
			{
				return false;
			}
			return Path.Equals(Root, ESearchCase::IgnoreCase) || Path.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase);
		}

		FMcpPolicyDecision MakeDecision(const EMcpPolicyOutcome Outcome, FString Code, FString Reason)
		{
			FMcpPolicyDecision Decision;
			Decision.Outcome = Outcome;
			Decision.Code = MoveTemp(Code);
			Decision.Reason = MoveTemp(Reason);
			return Decision;
		}
	}

	FMcpServerPolicy FMcpServerPolicy::LocalProject(FString ProjectRoot)
	{
		FMcpServerPolicy Policy;
		Policy.ProjectRoot = MoveTemp(ProjectRoot);
		Policy.ConfirmationRisks = { EMcpToolRisk::FileMutation, EMcpToolRisk::Destructive, EMcpToolRisk::CodeExecution, EMcpToolRisk::ExternalProcess };
		return Policy;
	}

	FMcpProjectPathSandbox::FMcpProjectPathSandbox(FString InProjectRoot) : ProjectRoot(NormalizeRoot(MoveTemp(InProjectRoot)))
	{
	}

	bool FMcpProjectPathSandbox::ResolveForRead(const FString& InputPath, FString& OutResolvedPath, FString& OutError) const
	{
		return Resolve(InputPath, true, OutResolvedPath, OutError);
	}

	bool FMcpProjectPathSandbox::ResolveForWrite(const FString& InputPath, FString& OutResolvedPath, FString& OutError) const
	{
		return Resolve(InputPath, false, OutResolvedPath, OutError);
	}

	const FString& FMcpProjectPathSandbox::GetProjectRoot() const
	{
		return ProjectRoot;
	}

	bool FMcpProjectPathSandbox::Resolve(const FString& InputPath, const bool bRequireExisting, FString& OutResolvedPath, FString& OutError) const
	{
		OutResolvedPath.Reset();
		OutError.Reset();
		if (ProjectRoot.IsEmpty())
		{
			OutError = TEXT("服务端没有配置项目文件沙箱根目录。");
			return false;
		}
		if (InputPath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("文件路径不能为空。");
			return false;
		}

		FString Candidate = FPaths::IsRelative(InputPath) ? FPaths::Combine(ProjectRoot, InputPath) : InputPath;
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeFilename(Candidate);
		if (!FPaths::CollapseRelativeDirectories(Candidate))
		{
			OutError = TEXT("文件路径包含无法规范化的相对目录。");
			return false;
		}
		FPaths::MakeStandardFilename(Candidate);
		if (!IsWithinRoot(Candidate, ProjectRoot))
		{
			OutError = TEXT("文件路径超出项目根目录沙箱。");
			return false;
		}
		if (bRequireExisting && !IFileManager::Get().FileExists(*Candidate) && !IFileManager::Get().DirectoryExists(*Candidate))
		{
			OutError = TEXT("沙箱内目标文件或目录不存在。");
			return false;
		}
		if (!RejectSymlinkTraversal(Candidate, OutError))
		{
			return false;
		}
		OutResolvedPath = MoveTemp(Candidate);
		return true;
	}

	bool FMcpProjectPathSandbox::RejectSymlinkTraversal(const FString& ResolvedPath, FString& OutError) const
	{
		FString RelativePath = ResolvedPath;
		if (!FPaths::MakePathRelativeTo(RelativePath, *(ProjectRoot + TEXT("/"))))
		{
			OutError = TEXT("无法计算项目沙箱相对路径。");
			return false;
		}

		TArray<FString> Segments;
		RelativePath.ParseIntoArray(Segments, TEXT("/"), true);
		FString Current = ProjectRoot;
		for (const FString& Segment : Segments)
		{
			Current = FPaths::Combine(Current, Segment);
			if ((IFileManager::Get().FileExists(*Current) || IFileManager::Get().DirectoryExists(*Current)) && IFileManager::Get().IsSymlink(*Current))
			{
				OutError = TEXT("文件路径经过符号链接或目录联接，已拒绝。");
				return false;
			}
		}
		return true;
	}

	FMcpAssetPathSandbox::FMcpAssetPathSandbox(TArray<FString> InAllowedRoots) : AllowedRoots(MoveTemp(InAllowedRoots))
	{
		for (FString& Root : AllowedRoots)
		{
			Root.ReplaceInline(TEXT("\\"), TEXT("/"));
			Root.RemoveFromEnd(TEXT("/"));
		}
	}

	bool FMcpAssetPathSandbox::Validate(const FString& InputPath, FString& OutLongPackagePath, FString& OutError) const
	{
		OutLongPackagePath.Reset();
		OutError.Reset();
		FString Candidate = InputPath.TrimStartAndEnd();
		if (Candidate.IsEmpty())
		{
			OutError = TEXT("资产路径不能为空。");
			return false;
		}
		if (Candidate.Contains(TEXT("\\")) || Candidate.Contains(TEXT("..")) || !Candidate.StartsWith(TEXT("/")))
		{
			OutError = TEXT("资产路径必须是没有反斜杠和上级跳转的长包路径。");
			return false;
		}
		const int32 LastSlash = Candidate.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		const int32 ObjectSeparator = Candidate.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, LastSlash + 1);
		if (ObjectSeparator != INDEX_NONE)
		{
			Candidate.LeftInline(ObjectSeparator);
		}
		Candidate.RemoveFromEnd(TEXT("/"));
		if (Candidate.IsEmpty() || Candidate.Contains(TEXT("//")))
		{
			OutError = TEXT("资产长包路径格式无效。");
			return false;
		}

		bool bAllowed = false;
		for (const FString& Root : AllowedRoots)
		{
			if (Candidate.Equals(Root, ESearchCase::IgnoreCase) || Candidate.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase))
			{
				bAllowed = true;
				break;
			}
		}
		if (!bAllowed)
		{
			OutError = TEXT("资产路径超出服务端允许的内容根。");
			return false;
		}
		OutLongPackagePath = MoveTemp(Candidate);
		return true;
	}

	FMcpPolicyEngine::FMcpPolicyEngine(FMcpServerPolicy InPolicy)
		: ServerPolicy(MoveTemp(InPolicy)), ProjectPathSandbox(ServerPolicy.ProjectRoot), AssetPathSandbox(ServerPolicy.AllowedAssetRoots)
	{
	}

	FMcpPolicyDecision FMcpPolicyEngine::Evaluate(const FMcpToolDescriptor& Descriptor, const TSharedPtr<FJsonObject>& Arguments, const FMcpPolicyRequestContext& Context) const
	{
		const FString& EffectiveToolId = Descriptor.GetEffectiveToolId();
		if (ServerPolicy.bRequireAuthentication && !Context.bAuthenticated)
		{
			return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("authentication_required"), TEXT("服务端要求已认证调用方。"));
		}
		if (ServerPolicy.bRequireLocalConnection && !Context.bLocalConnection)
		{
			return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("local_connection_required"), TEXT("服务端仅允许本机连接。"));
		}
		if (ServerPolicy.DeniedTools.Contains(EffectiveToolId) || ServerPolicy.DeniedTools.Contains(Descriptor.Name))
		{
			return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("tool_denied"), TEXT("工具位于服务端拒绝列表。"));
		}
		if (!ServerPolicy.AllowedTools.IsEmpty() && !ServerPolicy.AllowedTools.Contains(EffectiveToolId) && !ServerPolicy.AllowedTools.Contains(Descriptor.Name))
		{
			return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("tool_not_allowed"), TEXT("工具不在服务端允许列表。"));
		}
		if (!ServerPolicy.AllowedProviders.IsEmpty() && !ServerPolicy.AllowedProviders.Contains(Descriptor.Provider))
		{
			return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("provider_not_allowed"), TEXT("工具 Provider 不在服务端允许列表。"));
		}
		if (static_cast<uint8>(Descriptor.Risk) > static_cast<uint8>(ServerPolicy.MaximumRisk))
		{
			return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("risk_exceeds_policy"), TEXT("工具风险等级超过服务端策略上限。"));
		}

		const TSharedPtr<FJsonObject> SafeArguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
		auto ReadPathValues = [&SafeArguments](const FString& ArgumentName)
		{
			TArray<FString> Paths;
			TArray<FString> Segments;
			ArgumentName.ParseIntoArray(Segments, TEXT("."), true);
			TSharedPtr<FJsonObject> Container = SafeArguments;
			for (int32 Index = 0; Index + 1 < Segments.Num(); ++Index)
			{
				const TSharedPtr<FJsonObject>* Child = nullptr;
				if (!Container.IsValid() || !Container->TryGetObjectField(Segments[Index], Child) || Child == nullptr)
				{
					return Paths;
				}
				Container = *Child;
			}
			if (!Container.IsValid() || Segments.IsEmpty())
			{
				return Paths;
			}
			const FString& LeafName = Segments.Last();
			FString Single;
			if (Container->TryGetStringField(LeafName, Single))
			{
				Paths.Add(Single);
				return Paths;
			}
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (Container->TryGetArrayField(LeafName, Values) && Values)
			{
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					FString Path;
					if (Value.IsValid() && Value->TryGetString(Path))
					{
						Paths.Add(Path);
					}
				}
			}
			return Paths;
		};
		for (const FString& ArgumentName : Descriptor.FilePathArguments)
		{
			for (const FString& Path : ReadPathValues(ArgumentName))
			{
				FString Resolved;
				FString PathError;
				const bool bReadOnly = Descriptor.Risk == EMcpToolRisk::ReadOnly;
				const bool bValid = bReadOnly ? ProjectPathSandbox.ResolveForRead(Path, Resolved, PathError) : ProjectPathSandbox.ResolveForWrite(Path, Resolved, PathError);
				if (!bValid)
				{
					return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("file_path_outside_sandbox"),
						FString::Printf(TEXT("参数 %s 被文件沙箱拒绝：%s"), *ArgumentName, *PathError));
				}
			}
		}
		for (const FString& ArgumentName : Descriptor.AssetPathArguments)
		{
			for (const FString& Path : ReadPathValues(ArgumentName))
			{
				if (Path.IsEmpty())
				{
					continue;
				}
				FString LongPackagePath;
				FString AssetError;
				if (!AssetPathSandbox.Validate(Path, LongPackagePath, AssetError))
				{
					return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("asset_path_outside_sandbox"),
						FString::Printf(TEXT("参数 %s 被资产沙箱拒绝：%s"), *ArgumentName, *AssetError));
				}
			}
		}

		for (const FString& ArgumentName : Descriptor.FilePathArguments)
		{
			FString Path;
			if (!SafeArguments->TryGetStringField(ArgumentName, Path))
			{
				continue;
			}
			FString Resolved;
			FString PathError;
			const bool bReadOnly = Descriptor.Risk == EMcpToolRisk::ReadOnly;
			const bool bValid = bReadOnly ? ProjectPathSandbox.ResolveForRead(Path, Resolved, PathError) : ProjectPathSandbox.ResolveForWrite(Path, Resolved, PathError);
			if (!bValid)
			{
				return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("file_path_outside_sandbox"), FString::Printf(TEXT("参数 %s 被文件沙箱拒绝：%s"), *ArgumentName, *PathError));
			}
		}
		for (const FString& ArgumentName : Descriptor.AssetPathArguments)
		{
			FString Path;
			if (!SafeArguments->TryGetStringField(ArgumentName, Path) || Path.IsEmpty())
			{
				continue;
			}
			FString LongPackagePath;
			FString AssetError;
			if (!AssetPathSandbox.Validate(Path, LongPackagePath, AssetError))
			{
				return MakeDecision(EMcpPolicyOutcome::Denied, TEXT("asset_path_outside_sandbox"), FString::Printf(TEXT("参数 %s 被资产沙箱拒绝：%s"), *ArgumentName, *AssetError));
			}
		}

		bool bArgumentConfirmed = false;
		if (Descriptor.bRequiresConfirmation)
		{
			FString Confirmation;
			bArgumentConfirmed =
				SafeArguments->TryGetStringField(Descriptor.ConfirmationArgument, Confirmation) && Confirmation.Equals(Descriptor.ConfirmationValue, ESearchCase::CaseSensitive);
		}
		const bool bExplicitlyConfirmed = Context.bConfirmed || bArgumentConfirmed;
		if (Descriptor.bRequiresConfirmation && !bExplicitlyConfirmed)
		{
			return MakeDecision(EMcpPolicyOutcome::ConfirmationRequired, TEXT("confirmation_required"),
				TEXT("This tool requires explicit confirmation enforced by the server policy."));
		}

		if (ServerPolicy.ConfirmationRisks.Contains(Descriptor.Risk) && !bExplicitlyConfirmed)
		{
			return MakeDecision(EMcpPolicyOutcome::ConfirmationRequired, TEXT("confirmation_required"), TEXT("服务端策略要求对该风险等级显式确认。"));
		}
		return MakeDecision(EMcpPolicyOutcome::Allowed, TEXT("allowed"), TEXT("服务端策略允许调用。"));
	}

	const FMcpServerPolicy& FMcpPolicyEngine::GetPolicy() const
	{
		return ServerPolicy;
	}

	FMcpAuditLog::FMcpAuditLog(const int32 InCapacity) : Capacity(FMath::Max(1, InCapacity))
	{
	}

	void FMcpAuditLog::Append(FMcpAuditRecord Record)
	{
		FScopeLock Lock(&Mutex);
		if (Records.Num() >= Capacity)
		{
			Records.RemoveAt(0, Records.Num() - Capacity + 1, EAllowShrinking::No);
		}
		Records.Add(MoveTemp(Record));
	}

	TArray<FMcpAuditRecord> FMcpAuditLog::List(const int32 MaxResults) const
	{
		FScopeLock Lock(&Mutex);
		const int32 Count = FMath::Clamp(MaxResults, 0, Records.Num());
		TArray<FMcpAuditRecord> Result;
		Result.Reserve(Count);
		for (int32 Index = Records.Num() - Count; Index < Records.Num(); ++Index)
		{
			Result.Add(Records[Index]);
		}
		return Result;
	}

	int32 FMcpAuditLog::Num() const
	{
		FScopeLock Lock(&Mutex);
		return Records.Num();
	}

	void FMcpAuditLog::Reset()
	{
		FScopeLock Lock(&Mutex);
		Records.Reset();
	}

	FString PolicyOutcomeToString(const EMcpPolicyOutcome Outcome)
	{
		switch (Outcome)
		{
		case EMcpPolicyOutcome::Allowed:
			return TEXT("Allowed");
		case EMcpPolicyOutcome::Denied:
			return TEXT("Denied");
		case EMcpPolicyOutcome::ConfirmationRequired:
			return TEXT("ConfirmationRequired");
		default:
			return TEXT("Unknown");
		}
	}
}
