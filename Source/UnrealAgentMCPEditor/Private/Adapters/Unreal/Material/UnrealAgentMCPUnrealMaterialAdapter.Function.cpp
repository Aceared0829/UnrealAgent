// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.Function.cpp
 * @brief MaterialFunction 创建、表达式添加、连线与检查实现。
 */

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.Internal.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunction.h"

namespace UnrealAgentMCP
{
	using namespace MaterialInternal;

	namespace
	{
		bool ResolveFunctionInputType(const FString& Name, EFunctionInputType& OutType)
		{
			const TMap<FString, EFunctionInputType> Types{ { TEXT("scalar"), FunctionInput_Scalar }, { TEXT("vector2"), FunctionInput_Vector2 },
				{ TEXT("vector3"), FunctionInput_Vector3 }, { TEXT("vector4"), FunctionInput_Vector4 }, { TEXT("texture2d"), FunctionInput_Texture2D },
				{ TEXT("texturecube"), FunctionInput_TextureCube }, { TEXT("texture2darray"), FunctionInput_Texture2DArray },
				{ TEXT("texturevolume"), FunctionInput_VolumeTexture }, { TEXT("staticbool"), FunctionInput_StaticBool },
				{ TEXT("materialattributes"), FunctionInput_MaterialAttributes } };
			if (const EFunctionInputType* Type = Types.Find(Name.ToLower()))
			{
				OutType = *Type;
				return true;
			}
			return false;
		}

		TSharedPtr<FJsonValue> FunctionIdentity(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field)
		{
			return Args->TryGetField(Field);
		}
	}

	FString FUnrealAgentMCPUnrealMaterialAdapter::ExecuteFunctionAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_function"))
		{
			FString Name;
			FString Error;
			if (!RequireString(Args, TEXT("name"), Name, Error))
			{
				return ErrorJson(Error);
			}
			FString PackagePath = TEXT("/Game/Materials/Functions");
			Args->TryGetStringField(TEXT("packagePath"), PackagePath);
			UMaterialFunction* Function = CreateMaterialFunction(Name, PackagePath, Error);
			if (!Function)
			{
				return ErrorJson(Error);
			}
			Args->TryGetStringField(TEXT("description"), Function->Description);
			MarkFunctionChanged(Function);
			if (!SaveAsset(Function, Error))
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("functionPath"), Function->GetPathName());
			Result->SetStringField(TEXT("description"), Function->Description);
			return SuccessJson(Result);
		}

		FString FunctionPath;
		FString Error;
		if (!RequireString(Args, TEXT("functionPath"), FunctionPath, Error))
		{
			return ErrorJson(Error);
		}
		UMaterialFunction* Function = LoadMaterialFunction(FunctionPath, Error);
		if (!Function)
		{
			return ErrorJson(Error);
		}

		if (Action == TEXT("list_function_expressions"))
		{
			const TArray<UMaterialExpression*> Expressions = UMaterialEditingLibrary::GetMaterialFunctionExpressions(Function);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Expressions.Num(); ++Index)
			{
				Values.Add(MakeShared<FJsonValueObject>(DescribeExpression(Expressions[Index], Index)));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("functionPath"), FunctionPath);
			Result->SetArrayField(TEXT("expressions"), Values);
			Result->SetNumberField(TEXT("count"), Values.Num());
			return SuccessJson(Result);
		}

		if (Action == TEXT("add_function_expression"))
		{
			FString Type;
			if (!RequireString(Args, TEXT("expressionType"), Type, Error))
			{
				return ErrorJson(Error);
			}
			UClass* Class = ResolveExpressionClass(Type, Error);
			if (!Class)
			{
				return ErrorJson(Error);
			}
			double X = 0.0;
			double Y = 0.0;
			Args->TryGetNumberField(TEXT("positionX"), X);
			Args->TryGetNumberField(TEXT("positionY"), Y);
			UMaterialExpression* Expression = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, Class, static_cast<int32>(X), static_cast<int32>(Y));
			if (!Expression)
			{
				return ErrorJson(TEXT("材质函数表达式创建失败。"));
			}
			if (UMaterialExpressionFunctionInput* Input = Cast<UMaterialExpressionFunctionInput>(Expression))
			{
				FString InputName;
				FString InputType;
				Args->TryGetStringField(TEXT("inputName"), InputName);
				Args->TryGetStringField(TEXT("inputType"), InputType);
				if (!InputName.IsEmpty())
				{
					Input->InputName = FName(*InputName);
					Input->Desc = InputName;
				}
				EFunctionInputType ResolvedInputType = FunctionInput_Scalar;
				if (!InputType.IsEmpty() && !ResolveFunctionInputType(InputType, ResolvedInputType))
				{
					UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, Expression);
					return ErrorJson(TEXT("inputType 无效。"));
				}
				if (!InputType.IsEmpty())
				{
					Input->InputType = ResolvedInputType;
				}
			}
			if (UMaterialExpressionFunctionOutput* Output = Cast<UMaterialExpressionFunctionOutput>(Expression))
			{
				FString OutputName;
				if (Args->TryGetStringField(TEXT("outputName"), OutputName))
				{
					Output->OutputName = FName(*OutputName);
					Output->Desc = OutputName;
				}
			}
			MarkFunctionChanged(Function);
			if (!SaveAsset(Function, Error))
			{
				return ErrorJson(Error);
			}
			const TArray<UMaterialExpression*> Expressions = UMaterialEditingLibrary::GetMaterialFunctionExpressions(Function);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("functionPath"), FunctionPath);
			Result->SetObjectField(TEXT("expression"), DescribeExpression(Expression, Expressions.IndexOfByKey(Expression)));
			return SuccessJson(Result);
		}

		if (Action == TEXT("connect_function_expressions"))
		{
			UMaterialExpression* Source = ResolveFunctionExpression(Function, FunctionIdentity(Args, TEXT("sourceExpression")));
			UMaterialExpression* Target = ResolveFunctionExpression(Function, FunctionIdentity(Args, TEXT("targetExpression")));
			if (!Source || !Target)
			{
				return ErrorJson(TEXT("材质函数源或目标表达式不存在。"));
			}
			FString SourceOutput;
			FString TargetInput;
			Args->TryGetStringField(TEXT("sourceOutput"), SourceOutput);
			Args->TryGetStringField(TEXT("targetInput"), TargetInput);
			if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Source, SourceOutput, Target, TargetInput))
			{
				return ErrorJson(TEXT("材质函数表达式连线失败。"));
			}
			MarkFunctionChanged(Function);
			if (!SaveAsset(Function, Error))
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("functionPath"), FunctionPath);
			Result->SetStringField(TEXT("source"), Source->GetName());
			Result->SetStringField(TEXT("target"), Target->GetName());
			Result->SetBoolField(TEXT("connected"), true);
			return SuccessJson(Result);
		}

		return ErrorJson(FString::Printf(TEXT("未实现的材质函数 action：%s"), *Action));
	}
}
