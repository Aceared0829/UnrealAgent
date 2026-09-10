// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.Inspection.cpp
 * @brief 材质读取、参数目录、表达式目录、验证、统计与图谱导出实现。
 */

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.Internal.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "UObject/UObjectIterator.h"

namespace UnrealAgentMCP
{
	using namespace MaterialInternal;

	namespace
	{
		TSharedRef<FJsonObject> ColorJson(const FLinearColor& Color)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("r"), Color.R);
			Result->SetNumberField(TEXT("g"), Color.G);
			Result->SetNumberField(TEXT("b"), Color.B);
			Result->SetNumberField(TEXT("a"), Color.A);
			return Result;
		}

		void AddParameterInfo(UMaterialInterface* Material, const FString& Type, const TArray<FMaterialParameterInfo>& Infos, TArray<TSharedPtr<FJsonValue>>& OutValues)
		{
			for (const FMaterialParameterInfo& Info : Infos)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), Info.Name.ToString());
				Item->SetStringField(TEXT("type"), Type);
				Item->SetNumberField(TEXT("index"), Info.Index);
				Item->SetStringField(TEXT("association"),
					Info.Association == EMaterialParameterAssociation::LayerParameter       ? TEXT("LayerParameter")
						: Info.Association == EMaterialParameterAssociation::BlendParameter ? TEXT("BlendParameter")
																							: TEXT("GlobalParameter"));
				const FHashedMaterialParameterInfo Hashed(Info);
				if (Type == TEXT("scalar"))
				{
					float Value = 0.0f;
					if (Material->GetScalarParameterValue(Hashed, Value))
					{
						Item->SetNumberField(TEXT("value"), Value);
					}
				}
				else if (Type == TEXT("vector"))
				{
					FLinearColor Value;
					if (Material->GetVectorParameterValue(Hashed, Value))
					{
						Item->SetObjectField(TEXT("value"), ColorJson(Value));
					}
				}
				else if (Type == TEXT("texture"))
				{
					UTexture* Value = nullptr;
					if (Material->GetTextureParameterValue(Hashed, Value))
					{
						Item->SetStringField(TEXT("value"), Value ? Value->GetPathName() : TEXT(""));
					}
				}
				OutValues.Add(MakeShared<FJsonValueObject>(Item));
			}
		}

		TSharedRef<FJsonObject> ParameterCatalog(UMaterialInterface* Material)
		{
			TArray<TSharedPtr<FJsonValue>> Parameters;
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Material->GetAllScalarParameterInfo(Infos, Ids);
			AddParameterInfo(Material, TEXT("scalar"), Infos, Parameters);
			Infos.Reset();
			Ids.Reset();
			Material->GetAllVectorParameterInfo(Infos, Ids);
			AddParameterInfo(Material, TEXT("vector"), Infos, Parameters);
			Infos.Reset();
			Ids.Reset();
			Material->GetAllTextureParameterInfo(Infos, Ids);
			AddParameterInfo(Material, TEXT("texture"), Infos, Parameters);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Material->GetPathName());
			Result->SetArrayField(TEXT("parameters"), Parameters);
			Result->SetNumberField(TEXT("count"), Parameters.Num());
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> ExpressionArray(UMaterial* Material)
		{
			const TArray<UMaterialExpression*> Expressions = UMaterialEditingLibrary::GetMaterialExpressions(Material);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Expressions.Num(); ++Index)
			{
				Values.Add(MakeShared<FJsonValueObject>(DescribeExpression(Expressions[Index], Index)));
			}
			return Values;
		}

		TSharedRef<FJsonObject> ExportGraphObject(UMaterial* Material)
		{
			const TArray<UMaterialExpression*> Expressions = UMaterialEditingLibrary::GetMaterialExpressions(Material);
			TMap<UMaterialExpression*, FString> Ids;
			TArray<TSharedPtr<FJsonValue>> Nodes;
			for (int32 Index = 0; Index < Expressions.Num(); ++Index)
			{
				UMaterialExpression* Expression = Expressions[Index];
				const FString Id = FString::Printf(TEXT("E%d"), Index);
				Ids.Add(Expression, Id);
				TSharedRef<FJsonObject> Node = DescribeExpression(Expression, Index);
				Node->SetStringField(TEXT("id"), Id);
				Node->SetStringField(TEXT("expressionType"), Node->GetStringField(TEXT("type")));
				Nodes.Add(MakeShared<FJsonValueObject>(Node));
			}

			TArray<TSharedPtr<FJsonValue>> Connections;
			for (UMaterialExpression* Target : Expressions)
			{
				if (!Target)
				{
					continue;
				}
				for (FExpressionInputIterator It{ Target }; It; ++It)
				{
					FExpressionInput* Input = It.Input;
					if (!Input || !Input->Expression)
					{
						continue;
					}
					TSharedRef<FJsonObject> Connection = MakeShared<FJsonObject>();
					Connection->SetStringField(TEXT("source"), Ids.FindRef(Input->Expression));
					Connection->SetStringField(TEXT("target"), Ids.FindRef(Target));
					Connection->SetNumberField(TEXT("sourceOutputIndex"), Input->OutputIndex);
					Connection->SetStringField(TEXT("targetInput"), Target->GetInputName(It.Index).ToString());
					Connections.Add(MakeShared<FJsonValueObject>(Connection));
				}
			}

			TArray<TSharedPtr<FJsonValue>> PropertyConnections;
			for (int32 Index = 0; Index < MP_MAX; ++Index)
			{
				const EMaterialProperty Property = static_cast<EMaterialProperty>(Index);
				// 先检查真实输入，避免 UE 便捷接口解引用废弃属性的空输入。
				FExpressionInput* PropertyInput = Material->GetExpressionInputForProperty(Property);
				UMaterialExpression* Source = PropertyInput ? PropertyInput->Expression : nullptr;
				if (!Source || !Ids.Contains(Source))
				{
					continue;
				}
				TSharedRef<FJsonObject> Connection = MakeShared<FJsonObject>();
				Connection->SetStringField(TEXT("source"), Ids.FindRef(Source));
				Connection->SetStringField(TEXT("outputName"), UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, Property));
				Connection->SetStringField(TEXT("property"), MaterialPropertyName(Property));
				PropertyConnections.Add(MakeShared<FJsonValueObject>(Connection));
			}

			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), Material->GetPathName());
			Result->SetArrayField(TEXT("nodes"), Nodes);
			Result->SetArrayField(TEXT("connections"), Connections);
			Result->SetArrayField(TEXT("propertyConnections"), PropertyConnections);
			Result->SetNumberField(TEXT("nodeCount"), Nodes.Num());
			Result->SetNumberField(TEXT("connectionCount"), Connections.Num() + PropertyConnections.Num());
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealMaterialAdapter::ExecuteInspectionAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("list_expression_types"))
		{
			TArray<FString> Names;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(UMaterialExpression::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					FString Name = It->GetName();
					Name.RemoveFromStart(TEXT("MaterialExpression"));
					Names.AddUnique(Name);
				}
			}
			Names.Sort();
			TArray<TSharedPtr<FJsonValue>> Types;
			for (const FString& Name : Names)
			{
				Types.Add(MakeShared<FJsonValueString>(Name));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("expressionTypes"), Types);
			Result->SetNumberField(TEXT("count"), Types.Num());
			return SuccessJson(Result);
		}

		FString AssetPath;
		FString Error;
		if (!Args->TryGetStringField(TEXT("assetPath"), AssetPath) && !Args->TryGetStringField(TEXT("materialPath"), AssetPath))
		{
			return ErrorJson(TEXT("缺少 assetPath 或 materialPath。"));
		}
		UMaterialInterface* Interface = LoadMaterialInterface(AssetPath, Error);
		if (!Interface)
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("list_parameters"))
		{
			return SuccessJson(ParameterCatalog(Interface));
		}
		if (Action == TEXT("read"))
		{
			TSharedRef<FJsonObject> Result = ParameterCatalog(Interface);
			Result->SetStringField(TEXT("class"), Interface->GetClass()->GetPathName());
			if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Interface))
			{
				Result->SetStringField(TEXT("parentPath"), Instance->Parent ? Instance->Parent->GetPathName() : TEXT(""));
				Result->SetBoolField(TEXT("instance"), true);
			}
			if (UMaterial* Material = Cast<UMaterial>(Interface))
			{
				Result->SetBoolField(TEXT("instance"), false);
				Result->SetStringField(TEXT("domain"), UEnum::GetValueAsString(Material->MaterialDomain));
				Result->SetStringField(TEXT("blendMode"), UEnum::GetValueAsString(Material->BlendMode));
				Result->SetStringField(TEXT("shadingModel"), UEnum::GetValueAsString(Material->GetShadingModels().GetFirstShadingModel()));
				const TArray<TSharedPtr<FJsonValue>> Expressions = ExpressionArray(Material);
				Result->SetArrayField(TEXT("expressions"), Expressions);
				Result->SetNumberField(TEXT("expressionCount"), Expressions.Num());
			}
			return SuccessJson(Result);
		}

		UMaterial* Material = Cast<UMaterial>(Interface);
		if (!Material)
		{
			return ErrorJson(TEXT("该 action 需要基础材质。"));
		}
		if (Action == TEXT("list_expressions"))
		{
			const TArray<TSharedPtr<FJsonValue>> Expressions = ExpressionArray(Material);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("materialPath"), AssetPath);
			Result->SetArrayField(TEXT("expressions"), Expressions);
			Result->SetNumberField(TEXT("count"), Expressions.Num());
			return SuccessJson(Result);
		}
		if (Action == TEXT("export_graph"))
		{
			return SuccessJson(ExportGraphObject(Material));
		}
		if (Action == TEXT("get_shader_stats"))
		{
			const FMaterialStatistics Stats = UMaterialEditingLibrary::GetStatistics(Material);
			TSharedRef<FJsonObject> Result = ParameterCatalog(Material);
			Result->SetNumberField(TEXT("vertexShaderInstructions"), Stats.NumVertexShaderInstructions);
			Result->SetNumberField(TEXT("pixelShaderInstructions"), Stats.NumPixelShaderInstructions);
			Result->SetNumberField(TEXT("samplers"), Stats.NumSamplers);
			Result->SetNumberField(TEXT("vertexTextureSamples"), Stats.NumVertexTextureSamples);
			Result->SetNumberField(TEXT("pixelTextureSamples"), Stats.NumPixelTextureSamples);
			Result->SetNumberField(TEXT("virtualTextureSamples"), Stats.NumVirtualTextureSamples);
			Result->SetNumberField(TEXT("uvScalars"), Stats.NumUVScalars);
			return SuccessJson(Result);
		}
		if (Action == TEXT("validate"))
		{
			const TArray<UMaterialExpression*> Expressions = UMaterialEditingLibrary::GetMaterialExpressions(Material);
			TSet<UMaterialExpression*> Reachable;
			TArray<UMaterialExpression*> Pending;
			for (int32 Index = 0; Index < MP_MAX; ++Index)
			{
				// UE 5.8 的枚举中包含已废弃和虚拟属性，其输入指针可能为空。
				FExpressionInput* PropertyInput = Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(Index));
				if (PropertyInput && PropertyInput->Expression)
				{
					Pending.AddUnique(PropertyInput->Expression);
				}
			}
			while (!Pending.IsEmpty())
			{
				UMaterialExpression* Current = Pending.Pop();
				if (!Current || Reachable.Contains(Current))
				{
					continue;
				}
				Reachable.Add(Current);
				for (FExpressionInputIterator It{ Current }; It; ++It)
				{
					if (It.Input && It.Input->Expression)
					{
						Pending.AddUnique(It.Input->Expression);
					}
				}
			}
			TArray<TSharedPtr<FJsonValue>> Orphans;
			int32 NullCount = 0;
			for (int32 Index = 0; Index < Expressions.Num(); ++Index)
			{
				if (!Expressions[Index])
				{
					++NullCount;
				}
				else if (!Reachable.Contains(Expressions[Index]))
				{
					Orphans.Add(MakeShared<FJsonValueString>(Expressions[Index]->GetName()));
				}
			}
			const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
			TArray<TSharedPtr<FJsonValue>> Errors;
			for (const FString& Message : CompileErrors)
			{
				Errors.Add(MakeShared<FJsonValueString>(Message));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("assetPath"), AssetPath);
			Result->SetArrayField(TEXT("orphans"), Orphans);
			Result->SetArrayField(TEXT("compilerErrors"), Errors);
			Result->SetNumberField(TEXT("nullExpressions"), NullCount);
			Result->SetBoolField(TEXT("valid"), NullCount == 0 && CompileErrors.IsEmpty());
			return SuccessJson(Result);
		}

		return ErrorJson(FString::Printf(TEXT("未实现的材质检查 action：%s"), *Action));
	}
}
