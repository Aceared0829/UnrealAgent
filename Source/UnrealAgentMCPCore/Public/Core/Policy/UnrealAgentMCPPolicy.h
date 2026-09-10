// Copyright ZhaoZining. All Rights Reserved.

#pragma once

/**
 * @file UnrealAgentMCPPolicy.h
 * @brief Unreal Agent 服务端强制策略、路径沙箱与调用审计。
 */

#include "CoreMinimal.h"
#include "Core/Tooling/UnrealAgentMCPToolDescriptor.h"

class FJsonObject;

namespace UnrealAgentMCP::Policy
{
	/** 策略引擎对一次调用的强制决策。 */
	enum class EMcpPolicyOutcome : uint8
	{
		Allowed,
		Denied,
		ConfirmationRequired
	};

	/** 不依赖具体协议的调用方身份与来源。 */
	struct UNREALAGENTMCPCORE_API FMcpPolicyRequestContext
	{
		FString ClientId = TEXT("internal");
		FString SessionId;
		FString Source = TEXT("Internal");
		/** 仅用于审计关联的 Agent Provider；不得作为授权依据。 */
		FString Provider = TEXT("internal");
		bool bAuthenticated = true;
		bool bLocalConnection = true;
		bool bConfirmed = false;
	};

	/** 服务端策略配置；客户端模式不能覆盖这些限制。 */
	struct UNREALAGENTMCPCORE_API FMcpServerPolicy
	{
		bool bRequireAuthentication = true;
		bool bRequireLocalConnection = true;
		EMcpToolRisk MaximumRisk = EMcpToolRisk::ExternalProcess;
		TSet<FString> AllowedTools;
		TSet<FString> DeniedTools;
		TSet<FName> AllowedProviders;
		TSet<EMcpToolRisk> ConfirmationRisks;
		FString ProjectRoot;
		TArray<FString> AllowedAssetRoots = { TEXT("/Game") };

		static FMcpServerPolicy LocalProject(FString ProjectRoot);
	};

	/** 一次策略判断的结构化结果。 */
	struct UNREALAGENTMCPCORE_API FMcpPolicyDecision
	{
		EMcpPolicyOutcome Outcome = EMcpPolicyOutcome::Denied;
		FString Code;
		FString Reason;

		bool IsAllowed() const
		{
			return Outcome == EMcpPolicyOutcome::Allowed;
		}
	};

	/** 将普通文件路径约束在一个规范化项目根目录内。 */
	class UNREALAGENTMCPCORE_API FMcpProjectPathSandbox
	{
	public:
		explicit FMcpProjectPathSandbox(FString InProjectRoot);

		bool ResolveForRead(const FString& InputPath, FString& OutResolvedPath, FString& OutError) const;
		bool ResolveForWrite(const FString& InputPath, FString& OutResolvedPath, FString& OutError) const;
		const FString& GetProjectRoot() const;

	private:
		bool Resolve(const FString& InputPath, bool bRequireExisting, FString& OutResolvedPath, FString& OutError) const;
		bool RejectSymlinkTraversal(const FString& ResolvedPath, FString& OutError) const;

		FString ProjectRoot;
	};

	/** 校验 Unreal 资产长包路径是否位于允许内容根内。 */
	class UNREALAGENTMCPCORE_API FMcpAssetPathSandbox
	{
	public:
		explicit FMcpAssetPathSandbox(TArray<FString> InAllowedRoots);

		bool Validate(const FString& InputPath, FString& OutLongPackagePath, FString& OutError) const;

	private:
		TArray<FString> AllowedRoots;
	};

	/** 服务端唯一策略决策点。 */
	class UNREALAGENTMCPCORE_API FMcpPolicyEngine
	{
	public:
		explicit FMcpPolicyEngine(FMcpServerPolicy InPolicy);

		FMcpPolicyDecision Evaluate(const FMcpToolDescriptor& Descriptor, const TSharedPtr<FJsonObject>& Arguments, const FMcpPolicyRequestContext& Context) const;
		const FMcpServerPolicy& GetPolicy() const;

	private:
		FMcpServerPolicy ServerPolicy;
		FMcpProjectPathSandbox ProjectPathSandbox;
		FMcpAssetPathSandbox AssetPathSandbox;
	};

	/** 不记录原始参数和密钥的最小调用审计项。 */
	struct UNREALAGENTMCPCORE_API FMcpAuditRecord
	{
		FGuid TraceId;
		FGuid RequestId;
		FDateTime Timestamp;
		FString ClientId;
		FString SessionId;
		FString Source;
		/** 发起请求的已知 Agent Provider；未知来源保留 unknown。 */
		FString Provider;
		FString ToolName;
		EMcpToolRisk Risk = EMcpToolRisk::ReadOnly;
		EMcpPolicyOutcome Outcome = EMcpPolicyOutcome::Denied;
		FString DecisionCode;
		/** 仅记录 Schema 错误的 JSONPath，不记录原始 arguments 或其值。 */
		TArray<FString> SchemaErrorPaths;
		bool bAccepted = false;
	};

	/** 有容量上限的线程安全内存审计日志。 */
	class UNREALAGENTMCPCORE_API FMcpAuditLog
	{
	public:
		explicit FMcpAuditLog(int32 InCapacity = 2048);

		void Append(FMcpAuditRecord Record);
		TArray<FMcpAuditRecord> List(int32 MaxResults = 200) const;
		int32 Num() const;
		void Reset();

	private:
		mutable FCriticalSection Mutex;
		TArray<FMcpAuditRecord> Records;
		int32 Capacity;
	};

	UNREALAGENTMCPCORE_API FString PolicyOutcomeToString(EMcpPolicyOutcome Outcome);
}
