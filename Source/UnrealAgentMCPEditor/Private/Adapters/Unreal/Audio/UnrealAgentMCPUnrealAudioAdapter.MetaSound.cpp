// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealAudioAdapter.MetaSound.cpp
 * @brief MetaSound 资产创建、节点发现、图编排、默认值与批量创作实现。
 */

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.h"

#include "Adapters/Unreal/Audio/UnrealAgentMCPUnrealAudioAdapter.Internal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendSearchEngine.h"

namespace UnrealAgentMCP
{
	using namespace AudioPrivate;

	namespace
	{
		FMetaSoundNodeHandle ParseNodeHandle(const TSharedPtr<FJsonObject>& Args, const FString& Field, FString& OutError)
		{
			FString Text;
			OutError = RequireString(Args, Field, Text);
			FGuid Guid;
			if (OutError.IsEmpty() && !FGuid::Parse(Text, Guid))
			{
				OutError = FString::Printf(TEXT("字段 %s 不是有效节点 GUID。"), *Field);
			}
			return FMetaSoundNodeHandle(Guid);
		}

		FMetasoundFrontendLiteral MakeLiteral(const FString& DataType, const TSharedPtr<FJsonValue>& Value, FName& OutDataType)
		{
			UMetaSoundBuilderSubsystem& Subsystem = UMetaSoundBuilderSubsystem::GetChecked();
			if (DataType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
			{
				return Subsystem.CreateBoolMetaSoundLiteral(Value.IsValid() && Value->Type == EJson::Boolean ? Value->AsBool() : false, OutDataType);
			}
			if (DataType.Equals(TEXT("Int32"), ESearchCase::IgnoreCase) || DataType.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
			{
				return Subsystem.CreateIntMetaSoundLiteral(Value.IsValid() ? static_cast<int32>(Value->AsNumber()) : 0, OutDataType);
			}
			if (DataType.Equals(TEXT("String"), ESearchCase::IgnoreCase))
			{
				return Subsystem.CreateStringMetaSoundLiteral(Value.IsValid() ? Value->AsString() : FString(), OutDataType);
			}
			return Subsystem.CreateFloatMetaSoundLiteral(Value.IsValid() && Value->Type == EJson::Number ? static_cast<float>(Value->AsNumber()) : 0.0f, OutDataType);
		}

		UObject* LoadMetaSoundObject(const TSharedPtr<FJsonObject>& Args, FString& OutError)
		{
			FString Path;
			OutError = RequireString(Args, TEXT("assetPath"), Path);
			UObject* Asset = OutError.IsEmpty() ? LoadAsset(Path, UObject::StaticClass()) : nullptr;
			if ((!Asset || !Cast<IMetaSoundDocumentInterface>(Asset)) && OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("找不到 MetaSound 资产：%s"), *Path);
				return nullptr;
			}
			return Asset;
		}
	}

	FString FUnrealAgentMCPUnrealAudioAdapter::ExecuteMetaSoundAction(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (Action == TEXT("create_metasound"))
		{
			FString Name;
			if (const FString Error = RequireString(Args, TEXT("name"), Name); !Error.IsEmpty())
			{
				return Failure(Error);
			}
			const FString PackagePath = OptionalString(Args, TEXT("packagePath"), TEXT("/Game/Audio"));
			const FString ExistingPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *Name, *Name);
			if (UObject* Existing = LoadAsset(ExistingPath, UObject::StaticClass()))
			{
				if (Cast<IMetaSoundDocumentInterface>(Existing))
				{
					TSharedRef<FJsonObject> Result = SuccessObject();
					Result->SetStringField(TEXT("assetPath"), Existing->GetPathName());
					Result->SetBoolField(TEXT("created"), false);
					return Serialize(Result);
				}
			}
			FMetaSoundBuilderNodeOutputHandle OnPlay;
			FMetaSoundBuilderNodeInputHandle OnFinished;
			TArray<FMetaSoundBuilderNodeInputHandle> AudioOutputs;
			EMetaSoundBuilderResult BuilderResult = EMetaSoundBuilderResult::Failed;
			UMetaSoundSourceBuilder* Builder =
				UMetaSoundBuilderSubsystem::GetChecked().CreateSourceBuilder(*FString::Printf(TEXT("WorldData_%s_%s"), *Name, *FGuid::NewGuid().ToString()), OnPlay, OnFinished,
					AudioOutputs, BuilderResult, EMetaSoundOutputAudioFormat::Mono, OptionalBool(Args, TEXT("oneShot"), true));
			if (!Builder || BuilderResult != EMetaSoundBuilderResult::Succeeded || !GEditor)
			{
				return Failure(TEXT("创建 MetaSound Source Builder 失败。"));
			}
			UMetaSoundEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>();
			EMetaSoundBuilderResult BuildResult = EMetaSoundBuilderResult::Failed;
			TScriptInterface<IMetaSoundDocumentInterface> Built = EditorSubsystem
				? EditorSubsystem->BuildToAsset(Builder, OptionalString(Args, TEXT("author"), TEXT("UnrealAgentMCP")), Name, PackagePath, BuildResult)
				: TScriptInterface<IMetaSoundDocumentInterface>();
			UObject* Asset = Built.GetObject();
			if (!Asset || BuildResult != EMetaSoundBuilderResult::Succeeded)
			{
				return Failure(TEXT("构建 MetaSound 资产失败。"));
			}
			SaveAsset(Asset);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), Asset->GetPathName());
			Result->SetBoolField(TEXT("created"), true);
			return Serialize(Result);
		}

		if (Action == TEXT("metasound_list_node_classes"))
		{
			const FString Search = OptionalString(Args, TEXT("search"));
			const int32 Limit = FMath::Max(1, OptionalInt(Args, TEXT("limit"), 500));
			Metasound::Frontend::ISearchEngine& SearchEngine = Metasound::Frontend::ISearchEngine::Get();
			SearchEngine.Prime();
			const TArray<Metasound::Frontend::FMetaSoundClassInfo> Classes = SearchEngine.FindAllClasses(Metasound::Frontend::ISearchEngine::EResultVersion::Highest, true);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const Metasound::Frontend::FMetaSoundClassInfo& Info : Classes)
			{
				const FString ClassName = Info.ClassName.ToString();
				if (!Search.IsEmpty() && !ClassName.Contains(Search, ESearchCase::IgnoreCase))
				{
					continue;
				}
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("className"), ClassName);
				Item->SetStringField(TEXT("namespace"), Info.ClassName.Namespace.ToString());
				Item->SetStringField(TEXT("name"), Info.ClassName.Name.ToString());
				Item->SetStringField(TEXT("variant"), Info.ClassName.Variant.ToString());
				Item->SetNumberField(TEXT("majorVersion"), Info.Version.Major);
				Item->SetNumberField(TEXT("minorVersion"), Info.Version.Minor);
				Items.Add(MakeShared<FJsonValueObject>(Item));
				if (Items.Num() >= Limit)
				{
					break;
				}
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetNumberField(TEXT("count"), Items.Num());
			Result->SetArrayField(TEXT("classes"), Items);
			return Serialize(Result);
		}

		if (Action == TEXT("metasound_author"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
			if (!Args.IsValid() || !Args->TryGetArrayField(TEXT("operations"), Operations) || !Operations)
			{
				return Failure(TEXT("缺少 operations 数组。"));
			}
			TArray<TSharedPtr<FJsonValue>> Results;
			for (const TSharedPtr<FJsonValue>& Value : *Operations)
			{
				const TSharedPtr<FJsonObject> Operation = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Operation.IsValid())
				{
					continue;
				}
				const TSharedRef<FJsonObject> Routed = MakeShared<FJsonObject>(*Operation);
				if (!Routed->HasField(TEXT("assetPath")) && Args->HasField(TEXT("assetPath")))
				{
					Routed->SetStringField(TEXT("assetPath"), Args->GetStringField(TEXT("assetPath")));
				}
				FString OperationAction;
				Routed->TryGetStringField(TEXT("action"), OperationAction);
				Results.Add(MakeShared<FJsonValueString>(ExecuteMetaSoundAction(OperationAction, Routed)));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("results"), Results);
			return Serialize(Result);
		}

		FString Error;
		UObject* MetaSoundObject = LoadMetaSoundObject(Args, Error);
		if (!MetaSoundObject)
		{
			return Failure(Error);
		}
		if (Action == TEXT("metasound_get_graph"))
		{
			IMetaSoundDocumentInterface* Interface = CastChecked<IMetaSoundDocumentInterface>(MetaSoundObject);
			const FMetasoundFrontendDocument& Document = Interface->GetConstDocument();
			TArray<TSharedPtr<FJsonValue>> Nodes;
			const FMetasoundFrontendGraph& Graph = Document.RootGraph.GetConstDefaultGraph();
			for (const FMetasoundFrontendNode& Node : Graph.Nodes)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("nodeId"), Node.GetID().ToString());
				Item->SetStringField(TEXT("name"), Node.Name.ToString());
				TArray<TSharedPtr<FJsonValue>> Inputs;
				for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Inputs)
				{
					TSharedRef<FJsonObject> Pin = MakeShared<FJsonObject>();
					Pin->SetStringField(TEXT("name"), Vertex.Name.ToString());
					Pin->SetStringField(TEXT("type"), Vertex.TypeName.ToString());
					Inputs.Add(MakeShared<FJsonValueObject>(Pin));
				}
				TArray<TSharedPtr<FJsonValue>> Outputs;
				for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Outputs)
				{
					TSharedRef<FJsonObject> Pin = MakeShared<FJsonObject>();
					Pin->SetStringField(TEXT("name"), Vertex.Name.ToString());
					Pin->SetStringField(TEXT("type"), Vertex.TypeName.ToString());
					Outputs.Add(MakeShared<FJsonValueObject>(Pin));
				}
				Item->SetArrayField(TEXT("inputs"), Inputs);
				Item->SetArrayField(TEXT("outputs"), Outputs);
				Nodes.Add(MakeShared<FJsonValueObject>(Item));
			}
			TArray<TSharedPtr<FJsonValue>> Edges;
			for (const FMetasoundFrontendEdge& Edge : Graph.Edges)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("fromNodeId"), Edge.FromNodeID.ToString());
				Item->SetStringField(TEXT("fromVertexId"), Edge.FromVertexID.ToString());
				Item->SetStringField(TEXT("toNodeId"), Edge.ToNodeID.ToString());
				Item->SetStringField(TEXT("toVertexId"), Edge.ToVertexID.ToString());
				Edges.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetArrayField(TEXT("nodes"), Nodes);
			Result->SetArrayField(TEXT("edges"), Edges);
			return Serialize(Result);
		}

		UMetaSoundBuilderBase* Builder = RequireMetaSoundBuilder(Args, Error);
		if (!Builder)
		{
			return Failure(Error);
		}
		if (Action == TEXT("metasound_build"))
		{
			// 资产 Builder 直接维护序列化文档；保存时由 MetaSound 编辑器完成注册与编译。
			if (!SaveAsset(MetaSoundObject))
			{
				return Failure(TEXT("保存 MetaSound Builder 文档失败。"));
			}
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("assetPath"), MetaSoundObject->GetPathName());
			return Serialize(Result);
		}

		EMetaSoundBuilderResult BuilderResult = EMetaSoundBuilderResult::Failed;
		if (Action == TEXT("metasound_add_input") || Action == TEXT("metasound_add_output"))
		{
			FString Name;
			if (const FString Required = RequireString(Args, TEXT("name"), Name); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			const FString RequestedType = OptionalString(Args, TEXT("dataType"), TEXT("Float"));
			FName DataType;
			const FMetasoundFrontendLiteral Literal = MakeLiteral(RequestedType, Args->TryGetField(TEXT("defaultValue")), DataType);
			FGuid NodeId;
			if (Action == TEXT("metasound_add_input"))
			{
				NodeId = Builder->AddGraphInputNode(*Name, DataType, Literal, BuilderResult, false).NodeID;
			}
			else
			{
				NodeId = Builder->AddGraphOutputNode(*Name, DataType, Literal, BuilderResult, false).NodeID;
			}
			if (BuilderResult != EMetaSoundBuilderResult::Succeeded)
			{
				return Failure(TEXT("添加 MetaSound 图输入或输出失败。"));
			}
			SaveAsset(MetaSoundObject);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("nodeId"), NodeId.ToString());
			Result->SetStringField(TEXT("dataType"), DataType.ToString());
			return Serialize(Result);
		}

		if (Action == TEXT("metasound_add_node"))
		{
			FString ClassText;
			if (const FString Required = RequireString(Args, TEXT("className"), ClassText); !Required.IsEmpty())
			{
				return Failure(Required);
			}
			FMetasoundFrontendClassName ClassName;
			if (!FMetasoundFrontendClassName::Parse(ClassText, ClassName))
			{
				ClassName = FMetasoundFrontendClassName(*OptionalString(Args, TEXT("namespace")), *ClassText, *OptionalString(Args, TEXT("variant")));
			}
			const FMetaSoundNodeHandle Handle = Builder->AddNodeByClassName(ClassName, BuilderResult, OptionalInt(Args, TEXT("majorVersion"), 1));
			if (BuilderResult != EMetaSoundBuilderResult::Succeeded || !Handle.IsSet())
			{
				return Failure(FString::Printf(TEXT("添加 MetaSound 节点失败：%s"), *ClassText));
			}
			SaveAsset(MetaSoundObject);
			TSharedRef<FJsonObject> Result = SuccessObject();
			Result->SetStringField(TEXT("nodeId"), Handle.NodeID.ToString());
			Result->SetStringField(TEXT("className"), ClassName.ToString());
			return Serialize(Result);
		}

		if (Action == TEXT("metasound_set_default"))
		{
			const FMetaSoundNodeHandle Node = ParseNodeHandle(Args, TEXT("nodeId"), Error);
			FString InputName;
			if (!Error.IsEmpty() || !(Error = RequireString(Args, TEXT("inputName"), InputName)).IsEmpty())
			{
				return Failure(Error);
			}
			const FString DataTypeText = OptionalString(Args, TEXT("dataType"), TEXT("Float"));
			FName DataType;
			const FMetasoundFrontendLiteral Literal = MakeLiteral(DataTypeText, Args->TryGetField(TEXT("value")), DataType);
			const FMetaSoundBuilderNodeInputHandle Input = Builder->FindNodeInputByName(Node, *InputName, BuilderResult);
			if (BuilderResult == EMetaSoundBuilderResult::Succeeded)
			{
				Builder->SetNodeInputDefault(Input, Literal, BuilderResult);
			}
			if (BuilderResult != EMetaSoundBuilderResult::Succeeded)
			{
				return Failure(TEXT("设置 MetaSound 节点默认值失败。"));
			}
			SaveAsset(MetaSoundObject);
			return Serialize(SuccessObject());
		}

		FString OutputName;
		FString InputName;
		if (Action == TEXT("metasound_connect"))
		{
			const FMetaSoundNodeHandle From = ParseNodeHandle(Args, TEXT("fromNodeId"), Error);
			if (!Error.IsEmpty())
				return Failure(Error);
			const FMetaSoundNodeHandle To = ParseNodeHandle(Args, TEXT("toNodeId"), Error);
			if (!Error.IsEmpty())
				return Failure(Error);
			if (!(Error = RequireString(Args, TEXT("outputName"), OutputName)).IsEmpty() || !(Error = RequireString(Args, TEXT("inputName"), InputName)).IsEmpty())
			{
				return Failure(Error);
			}
			Builder->ConnectNodes(From, *OutputName, To, *InputName, BuilderResult);
		}
		else if (Action == TEXT("metasound_connect_input"))
		{
			const FMetaSoundNodeHandle To = ParseNodeHandle(Args, TEXT("nodeId"), Error);
			FString GraphInput;
			if (!Error.IsEmpty() || !(Error = RequireString(Args, TEXT("graphInput"), GraphInput)).IsEmpty() ||
				!(Error = RequireString(Args, TEXT("inputName"), InputName)).IsEmpty())
			{
				return Failure(Error);
			}
			Builder->ConnectGraphInputToNode(*GraphInput, To, *InputName, BuilderResult);
		}
		else if (Action == TEXT("metasound_connect_audio_out"))
		{
			const FMetaSoundNodeHandle From = ParseNodeHandle(Args, TEXT("nodeId"), Error);
			if (!Error.IsEmpty() || !(Error = RequireString(Args, TEXT("outputName"), OutputName)).IsEmpty())
			{
				return Failure(Error);
			}
			const FMetaSoundBuilderNodeOutputHandle SourceOutput = Builder->FindNodeOutputByName(From, *OutputName, BuilderResult);
			if (BuilderResult == EMetaSoundBuilderResult::Succeeded)
			{
				const TArray<FName> GraphOutputs = Builder->GetGraphOutputNames(BuilderResult);
				BuilderResult = EMetaSoundBuilderResult::Failed;
				for (const FName& GraphOutputName : GraphOutputs)
				{
					FName DataType;
					FMetaSoundBuilderNodeInputHandle GraphOutput;
					EMetaSoundBuilderResult FindResult = EMetaSoundBuilderResult::Failed;
					Builder->FindGraphOutputNode(GraphOutputName, DataType, GraphOutput, FindResult);
					if (FindResult == EMetaSoundBuilderResult::Succeeded && DataType == FName(TEXT("Audio")))
					{
						Builder->ConnectNodes(SourceOutput, GraphOutput, BuilderResult);
						if (BuilderResult == EMetaSoundBuilderResult::Succeeded)
						{
							break;
						}
					}
				}
			}
		}
		else
		{
			const FMetaSoundNodeHandle From = ParseNodeHandle(Args, TEXT("nodeId"), Error);
			FString GraphOutput;
			if (!Error.IsEmpty() || !(Error = RequireString(Args, TEXT("outputName"), OutputName)).IsEmpty())
			{
				return Failure(Error);
			}
			GraphOutput = OptionalString(Args, TEXT("graphOutput"));
			if (GraphOutput.IsEmpty())
			{
				return Failure(TEXT("缺少必填参数 'graphOutput'。"));
			}
			Builder->ConnectNamedNodeOutputToNamedGraphOutput(From, *OutputName, *GraphOutput, BuilderResult);
		}
		if (BuilderResult != EMetaSoundBuilderResult::Succeeded)
		{
			return Failure(TEXT("MetaSound 端口连接失败，名称、类型或方向不匹配。"));
		}
		SaveAsset(MetaSoundObject);
		return Serialize(SuccessObject());
	}
}
