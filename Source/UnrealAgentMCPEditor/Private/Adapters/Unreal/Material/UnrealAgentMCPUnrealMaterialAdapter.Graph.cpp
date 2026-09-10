// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealMaterialAdapter.Graph.cpp
 * @brief 材质表达式创建、属性修改、连线、断线和图谱导入实现。
 */

#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.h"

#include "Adapters/Unreal/Actors/UnrealAgentMCPPropertyWriter.h"
#include "Adapters/Unreal/Material/UnrealAgentMCPUnrealMaterialAdapter.Internal.h"
#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Infrastructure/Transactions/UnrealAgentMCPEditorTransaction.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Engine/Texture.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/UnrealType.h"

namespace UnrealAgentMCP
{
	using namespace MaterialInternal;

	namespace
	{
		TSharedPtr<FJsonValue> Identity(const TSharedPtr<FJsonObject>& Args, const TCHAR* Primary, const TCHAR* Fallback = nullptr)
		{
			TSharedPtr<FJsonValue> Value = Args->TryGetField(Primary);
			return !Value.IsValid() && Fallback ? Args->TryGetField(Fallback) : Value;
		}

		bool SetExpressionTypedValue(UMaterialExpression* Expression, const TSharedPtr<FJsonValue>& Value, FString& OutError)
		{
			if (!Expression || !Value.IsValid())
			{
				OutError = TEXT("表达式或 value 无效。");
				return false;
			}
			double Number = 0.0;
			if (UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
			{
				if (!Value->TryGetNumber(Number))
				{
					OutError = TEXT("Constant value 必须是数字。");
					return false;
				}
				Constant->R = static_cast<float>(Number);
				return true;
			}
			if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
			{
				if (!Value->TryGetNumber(Number))
				{
					OutError = TEXT("ScalarParameter value 必须是数字。");
					return false;
				}
				Scalar->DefaultValue = static_cast<float>(Number);
				return true;
			}
			if (UMaterialExpressionConstant2Vector* Vector2 = Cast<UMaterialExpressionConstant2Vector>(Expression))
			{
				FVector2D Parsed;
				if (!TryReadVector2(Value, Parsed))
				{
					OutError = TEXT("Constant2Vector value 无效。");
					return false;
				}
				Vector2->R = static_cast<float>(Parsed.X);
				Vector2->G = static_cast<float>(Parsed.Y);
				return true;
			}
			FLinearColor Color;
			if (UMaterialExpressionConstant3Vector* Vector3 = Cast<UMaterialExpressionConstant3Vector>(Expression))
			{
				if (!TryReadColor(Value, Color))
				{
					OutError = TEXT("Constant3Vector value 无效。");
					return false;
				}
				Vector3->Constant = Color;
				return true;
			}
			if (UMaterialExpressionConstant4Vector* Vector4 = Cast<UMaterialExpressionConstant4Vector>(Expression))
			{
				if (!TryReadColor(Value, Color))
				{
					OutError = TEXT("Constant4Vector value 无效。");
					return false;
				}
				Vector4->Constant = Color;
				return true;
			}
			if (UMaterialExpressionVectorParameter* VectorParameter = Cast<UMaterialExpressionVectorParameter>(Expression))
			{
				if (!TryReadColor(Value, Color))
				{
					OutError = TEXT("VectorParameter value 无效。");
					return false;
				}
				VectorParameter->DefaultValue = Color;
				return true;
			}
			OutError = TEXT("该表达式不支持通用 value，请使用 propertyName。");
			return false;
		}

		bool SetTexture(UMaterialExpression* Expression, const FString& TexturePath, FString& OutError)
		{
			UMaterialExpressionTextureBase* TextureExpression = Cast<UMaterialExpressionTextureBase>(Expression);
			if (!TextureExpression)
			{
				OutError = TEXT("目标表达式不是 Texture 表达式。");
				return false;
			}
			UEditorAssetSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
			UTexture* Texture = Subsystem ? Cast<UTexture>(Subsystem->LoadAsset(TexturePath)) : nullptr;
			if (!Texture)
			{
				OutError = FString::Printf(TEXT("未找到纹理：%s"), *TexturePath);
				return false;
			}
			TextureExpression->Texture = Texture;
			return true;
		}

		void SetOptionalExpressionMetadata(UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Args)
		{
			FString Text;
			if (Args->TryGetStringField(TEXT("name"), Text))
			{
				Expression->Desc = Text;
			}
			if (Args->TryGetStringField(TEXT("parameterName"), Text))
			{
				if (FNameProperty* Property = FindFProperty<FNameProperty>(Expression->GetClass(), TEXT("ParameterName")))
				{
					void* Address = Property->ContainerPtrToValuePtr<void>(Expression);
					Property->SetPropertyValue(Address, FName(*Text));
				}
			}
			if (Args->TryGetStringField(TEXT("group"), Text))
			{
				if (FNameProperty* Property = FindFProperty<FNameProperty>(Expression->GetClass(), TEXT("Group")))
				{
					void* Address = Property->ContainerPtrToValuePtr<void>(Expression);
					Property->SetPropertyValue(Address, FName(*Text));
				}
			}
			double Number = 0.0;
			if (Args->TryGetNumberField(TEXT("sortPriority"), Number))
			{
				if (FIntProperty* Property = FindFProperty<FIntProperty>(Expression->GetClass(), TEXT("SortPriority")))
				{
					Property->SetPropertyValue_InContainer(Expression, static_cast<int32>(Number));
				}
			}
		}

		UMaterialExpression* AddExpression(UMaterial* Material, const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			FString Type;
			if (!RequireString(Args, TEXT("expressionType"), Type, OutError))
			{
				return nullptr;
			}
			UClass* Class = ResolveExpressionClass(Type, OutError);
			if (!Class)
			{
				return nullptr;
			}
			double X = 0.0;
			double Y = 0.0;
			Args->TryGetNumberField(TEXT("positionX"), X);
			Args->TryGetNumberField(TEXT("positionY"), Y);
			UMaterialExpression* Expression = UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, static_cast<int32>(X), static_cast<int32>(Y));
			if (!Expression)
			{
				OutError = TEXT("材质表达式创建失败。");
				return nullptr;
			}
			SetOptionalExpressionMetadata(Expression, Args);
			TSharedPtr<FJsonValue> Default = Args->TryGetField(TEXT("defaultValue"));
			if (!Default.IsValid())
			{
				Default = Args->TryGetField(TEXT("value"));
			}
			if (Default.IsValid())
			{
				FString IgnoredError;
				SetExpressionTypedValue(Expression, Default, IgnoredError);
			}
			const TSharedPtr<FJsonObject>* Channels = nullptr;
			if (Args->TryGetObjectField(TEXT("channels"), Channels) && Channels && Channels->IsValid())
			{
				if (UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
				{
					bool bChannel = false;
					if ((*Channels)->TryGetBoolField(TEXT("r"), bChannel))
						Mask->R = bChannel;
					if ((*Channels)->TryGetBoolField(TEXT("g"), bChannel))
						Mask->G = bChannel;
					if ((*Channels)->TryGetBoolField(TEXT("b"), bChannel))
						Mask->B = bChannel;
					if ((*Channels)->TryGetBoolField(TEXT("a"), bChannel))
						Mask->A = bChannel;
				}
			}
			return Expression;
		}

		TSharedRef<FJsonObject> MaterialPathResult(UMaterial* Material)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
			Result->SetNumberField(TEXT("expressionCount"), UMaterialEditingLibrary::GetNumMaterialExpressions(Material));
			return Result;
		}
	}

	FString FUnrealAgentMCPUnrealMaterialAdapter::ExecuteGraphAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FString MaterialPath;
		FString Error;
		if (!RequireString(Args, Action == TEXT("disconnect_property") || Action == TEXT("set_base_color") ? TEXT("assetPath") : TEXT("materialPath"), MaterialPath, Error))
		{
			if (!Args->TryGetStringField(TEXT("materialPath"), MaterialPath) && !Args->TryGetStringField(TEXT("assetPath"), MaterialPath))
			{
				return ErrorJson(Error);
			}
		}
		UMaterial* Material = LoadMaterial(MaterialPath, Error);
		if (!Material)
		{
			return ErrorJson(Error);
		}
		TUniquePtr<Transactions::FUnrealAgentMCPCompensatingEditorTransaction> ImportCompensation;

		if (Action == TEXT("add_expression"))
		{
			UMaterialExpression* Expression = AddExpression(Material, Args, Error);
			if (!Expression)
			{
				return ErrorJson(Error);
			}
			MarkMaterialChanged(Material);
			if (!SaveAsset(Material, Error))
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MaterialPathResult(Material);
			const TArray<UMaterialExpression*> Expressions = UMaterialEditingLibrary::GetMaterialExpressions(Material);
			Result->SetObjectField(TEXT("expression"), DescribeExpression(Expression, Expressions.IndexOfByKey(Expression)));
			return SuccessJson(Result);
		}

		if (Action == TEXT("set_expression_value"))
		{
			UMaterialExpression* Expression = ResolveExpression(Material, Identity(Args, TEXT("expressionIndex"), TEXT("expressionName")));
			if (!Expression)
			{
				return ErrorJson(TEXT("未找到目标表达式。"));
			}
			FString TexturePath;
			FString PropertyName;
			if (Args->TryGetStringField(TEXT("texturePath"), TexturePath))
			{
				if (!SetTexture(Expression, TexturePath, Error))
				{
					return ErrorJson(Error);
				}
			}
			else if (Args->TryGetStringField(TEXT("propertyName"), PropertyName))
			{
				FProperty* Property = FindFProperty<FProperty>(Expression->GetClass(), *PropertyName);
				if (!Property)
				{
					return ErrorJson(FString::Printf(TEXT("表达式属性不存在：%s"), *PropertyName));
				}
				if (!PropertyWriter::SetPropertyFromJson(Expression, Property, Args->TryGetField(TEXT("value")), Error))
				{
					return ErrorJson(Error);
				}
			}
			else if (!SetExpressionTypedValue(Expression, Args->TryGetField(TEXT("value")), Error))
			{
				return ErrorJson(Error);
			}
		}
		else if (Action == TEXT("set_custom_expression"))
		{
			UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(ResolveExpression(Material, Identity(Args, TEXT("expressionIndex"), TEXT("expressionName"))));
			if (!Custom)
			{
				return ErrorJson(TEXT("目标不是 MaterialExpressionCustom。"));
			}
			bool bChanged = false;
			FString Text;
			if (Args->TryGetStringField(TEXT("code"), Text))
			{
				Custom->Code = Text;
				bChanged = true;
			}
			if (Args->TryGetStringField(TEXT("description"), Text))
			{
				Custom->Description = Text;
				bChanged = true;
			}
			if (Args->TryGetStringField(TEXT("outputType"), Text))
			{
				const TMap<FString, ECustomMaterialOutputType> Types{ { TEXT("float1"), CMOT_Float1 }, { TEXT("float2"), CMOT_Float2 }, { TEXT("float3"), CMOT_Float3 },
					{ TEXT("float4"), CMOT_Float4 }, { TEXT("materialattributes"), CMOT_MaterialAttributes } };
				const ECustomMaterialOutputType* OutputType = Types.Find(Text.ToLower());
				if (!OutputType)
				{
					return ErrorJson(TEXT("outputType 无效。"));
				}
				Custom->OutputType = *OutputType;
				bChanged = true;
			}
			const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
			if (Args->TryGetArrayField(TEXT("inputs"), Inputs) && Inputs)
			{
				Custom->Inputs.Reset();
				for (const TSharedPtr<FJsonValue>& InputValue : *Inputs)
				{
					FString InputName;
					if (!InputValue->TryGetString(InputName))
					{
						const TSharedPtr<FJsonObject>* InputObject = nullptr;
						if (!InputValue->TryGetObject(InputObject) || !InputObject || !InputObject->IsValid() || !(*InputObject)->TryGetStringField(TEXT("name"), InputName))
						{
							return ErrorJson(TEXT("Custom inputs 项无效。"));
						}
					}
					FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
					Input.InputName = FName(*InputName);
				}
				bChanged = true;
			}
			TSharedRef<FJsonObject> Result = MaterialPathResult(Material);
			Result->SetStringField(TEXT("code"), Custom->Code);
			Result->SetStringField(TEXT("description"), Custom->Description);
			Result->SetNumberField(TEXT("inputCount"), Custom->Inputs.Num());
			Result->SetBoolField(TEXT("changed"), bChanged);
			if (bChanged)
			{
				MarkMaterialChanged(Material);
				if (!SaveAsset(Material, Error))
				{
					return ErrorJson(Error);
				}
			}
			return SuccessJson(Result);
		}
		else if (Action == TEXT("disconnect_property"))
		{
			FString PropertyName;
			if (!RequireString(Args, TEXT("property"), PropertyName, Error))
			{
				return ErrorJson(Error);
			}
			EMaterialProperty Property = MP_BaseColor;
			if (!ResolveMaterialProperty(PropertyName, Property))
			{
				return ErrorJson(TEXT("不支持的材质输出属性。"));
			}
			const bool bDisconnected = UMaterialEditingLibrary::DisconnectMaterialProperty(Material, Property);
			MarkMaterialChanged(Material);
			if (!SaveAsset(Material, Error))
			{
				return ErrorJson(Error);
			}
			TSharedRef<FJsonObject> Result = MaterialPathResult(Material);
			Result->SetBoolField(TEXT("disconnected"), bDisconnected);
			Result->SetStringField(TEXT("property"), MaterialPropertyName(Property));
			return SuccessJson(Result);
		}
		else if (Action == TEXT("set_base_color"))
		{
			FLinearColor Color;
			if (!TryReadColor(Args->TryGetField(TEXT("color")), Color))
			{
				return ErrorJson(TEXT("color 格式无效。"));
			}
			UMaterialExpressionConstant3Vector* Expression = Cast<UMaterialExpressionConstant3Vector>(
				UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -300, -150));
			if (!Expression)
			{
				return ErrorJson(TEXT("基础色表达式创建失败。"));
			}
			Expression->Constant = Color;
			Expression->Desc = TEXT("Unreal Agent 基础色");
			UMaterialEditingLibrary::ConnectMaterialProperty(Expression, TEXT(""), MP_BaseColor);
		}
		else if (Action == TEXT("connect_texture"))
		{
			FString TexturePath;
			FString PropertyName;
			if (!RequireString(Args, TEXT("texturePath"), TexturePath, Error) || !RequireString(Args, TEXT("property"), PropertyName, Error))
			{
				return ErrorJson(Error);
			}
			EMaterialProperty Property = MP_BaseColor;
			if (!ResolveMaterialProperty(PropertyName, Property))
			{
				return ErrorJson(TEXT("不支持的材质输出属性。"));
			}
			UMaterialExpressionTextureSample* Expression =
				Cast<UMaterialExpressionTextureSample>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSample::StaticClass(), -350, 0));
			if (!Expression || !SetTexture(Expression, TexturePath, Error))
			{
				return ErrorJson(Error.IsEmpty() ? TEXT("纹理表达式创建失败。") : Error);
			}
			Expression->Desc = TEXT("Unreal Agent 纹理输入");
			if (!UMaterialEditingLibrary::ConnectMaterialProperty(Expression, TEXT("RGB"), Property))
			{
				return ErrorJson(TEXT("纹理到材质输出连线失败。"));
			}
		}
		else if (Action == TEXT("connect_expressions"))
		{
			UMaterialExpression* Source = ResolveExpression(Material, Identity(Args, TEXT("sourceExpression")));
			UMaterialExpression* Target = ResolveExpression(Material, Identity(Args, TEXT("targetExpression")));
			if (!Source || !Target)
			{
				return ErrorJson(TEXT("源或目标表达式不存在。"));
			}
			FString SourceOutput;
			FString TargetInput;
			Args->TryGetStringField(TEXT("sourceOutput"), SourceOutput);
			Args->TryGetStringField(TEXT("targetInput"), TargetInput);
			if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Source, SourceOutput, Target, TargetInput))
			{
				return ErrorJson(TEXT("表达式连线失败。"));
			}
		}
		else if (Action == TEXT("connect_to_property"))
		{
			UMaterialExpression* Expression = ResolveExpression(Material, Identity(Args, TEXT("expressionName"), TEXT("expressionIndex")));
			FString PropertyName;
			if (!Expression || !RequireString(Args, TEXT("property"), PropertyName, Error))
			{
				return ErrorJson(Expression ? Error : TEXT("未找到目标表达式。"));
			}
			EMaterialProperty Property = MP_BaseColor;
			if (!ResolveMaterialProperty(PropertyName, Property))
			{
				return ErrorJson(TEXT("不支持的材质输出属性。"));
			}
			FString Output;
			Args->TryGetStringField(TEXT("outputName"), Output);
			if (!UMaterialEditingLibrary::ConnectMaterialProperty(Expression, Output, Property))
			{
				return ErrorJson(TEXT("连接材质输出失败。"));
			}
		}
		else if (Action == TEXT("delete_expression"))
		{
			UMaterialExpression* Expression = ResolveExpression(Material, Identity(Args, TEXT("expressionName"), TEXT("expressionIndex")));
			if (!Expression)
			{
				return ErrorJson(TEXT("未找到目标表达式。"));
			}
			UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
		}
		else if (Action == TEXT("import_graph") || Action == TEXT("build_graph"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (!Args->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
			{
				return ErrorJson(TEXT("缺少 nodes 数组。"));
			}
			if (Action == TEXT("import_graph"))
			{
				ImportCompensation = MakeUnique<Transactions::FUnrealAgentMCPCompensatingEditorTransaction>(FText::FromString(TEXT("Unreal Agent 导入材质图")),
					[Material](FString& RestoreError)
					{
						MarkMaterialChanged(Material);
						return SaveAsset(Material, RestoreError);
					});
				if (!ImportCompensation->IsReady(Error))
				{
					return ErrorJson(Error);
				}
				Material->Modify();
				// UE 5.8 的批量删除会边遍历边修改集合；使用快照避免跳过节点。
				const TArray<UMaterialExpression*> ExistingExpressions = UMaterialEditingLibrary::GetMaterialExpressions(Material);
				for (UMaterialExpression* Existing : ExistingExpressions)
				{
					UMaterialEditingLibrary::DeleteMaterialExpression(Material, Existing);
				}
			}
			TMap<FString, UMaterialExpression*> ById;
			for (int32 Index = 0; Index < Nodes->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject>* Node = nullptr;
				if (!(*Nodes)[Index]->TryGetObject(Node) || !Node || !Node->IsValid())
				{
					return ErrorJson(FString::Printf(TEXT("nodes[%d] 不是对象。"), Index));
				}
				TSharedRef<FJsonObject> NodeArgs = MakeShared<FJsonObject>(**Node);
				if (!NodeArgs->HasField(TEXT("expressionType")))
				{
					FString Type;
					if (NodeArgs->TryGetStringField(TEXT("type"), Type))
					{
						NodeArgs->SetStringField(TEXT("expressionType"), Type);
					}
				}
				UMaterialExpression* Expression = AddExpression(Material, NodeArgs, Error);
				if (!Expression)
				{
					return ErrorJson(FString::Printf(TEXT("nodes[%d] 创建失败：%s"), Index, *Error));
				}
				FString Id = FString::FromInt(Index);
				NodeArgs->TryGetStringField(TEXT("id"), Id);
				ById.Add(Id, Expression);
			}
			const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
			if (Args->TryGetArrayField(TEXT("connections"), Connections) && Connections)
			{
				for (const TSharedPtr<FJsonValue>& ConnectionValue : *Connections)
				{
					const TSharedPtr<FJsonObject>* Connection = nullptr;
					if (!ConnectionValue->TryGetObject(Connection) || !Connection || !Connection->IsValid())
					{
						return ErrorJson(TEXT("connections 项无效。"));
					}
					FString SourceId;
					FString TargetId;
					(*Connection)->TryGetStringField(TEXT("source"), SourceId);
					(*Connection)->TryGetStringField(TEXT("target"), TargetId);
					UMaterialExpression* const* Source = ById.Find(SourceId);
					UMaterialExpression* const* Target = ById.Find(TargetId);
					FString SourceOutput;
					FString TargetInput;
					(*Connection)->TryGetStringField(TEXT("sourceOutput"), SourceOutput);
					(*Connection)->TryGetStringField(TEXT("targetInput"), TargetInput);
					if (!Source || !Target || !UMaterialEditingLibrary::ConnectMaterialExpressions(*Source, SourceOutput, *Target, TargetInput))
					{
						return ErrorJson(TEXT("图谱节点连线失败。"));
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
			if (Args->TryGetArrayField(TEXT("propertyConnections"), Outputs) && Outputs)
			{
				for (const TSharedPtr<FJsonValue>& OutputValue : *Outputs)
				{
					const TSharedPtr<FJsonObject>* Output = nullptr;
					if (!OutputValue->TryGetObject(Output) || !Output || !Output->IsValid())
					{
						return ErrorJson(TEXT("propertyConnections 项无效。"));
					}
					FString SourceId;
					FString PropertyName;
					(*Output)->TryGetStringField(TEXT("source"), SourceId);
					(*Output)->TryGetStringField(TEXT("property"), PropertyName);
					UMaterialExpression* const* Source = ById.Find(SourceId);
					EMaterialProperty Property = MP_BaseColor;
					FString OutputName;
					(*Output)->TryGetStringField(TEXT("outputName"), OutputName);
					if (!Source || !ResolveMaterialProperty(PropertyName, Property) || !UMaterialEditingLibrary::ConnectMaterialProperty(*Source, OutputName, Property))
					{
						return ErrorJson(TEXT("图谱材质输出连线失败。"));
					}
				}
			}
			UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		}
		else
		{
			return ErrorJson(FString::Printf(TEXT("未实现的材质图 action：%s"), *Action));
		}

		MarkMaterialChanged(Material);
		if (!SaveAsset(Material, Error))
		{
			return ErrorJson(Error);
		}
		if (ImportCompensation && !ImportCompensation->Register(Error))
		{
			return ErrorJson(Error);
		}
		TSharedRef<FJsonObject> Result = MaterialPathResult(Material);
		Result->SetStringField(TEXT("action"), Action);
		return SuccessJson(Result);
	}
}
