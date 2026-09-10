// Copyright ZhaoZining. All Rights Reserved.

/**
 * @file UnrealAgentMCPUnrealEditorAdapter.Dialogs.cpp
 * @brief 模态对话框策略、可见对话框检查与按钮响应。
 */

#include "Adapters/Unreal/Editor/UnrealAgentMCPUnrealEditorAdapter.h"

#include "Core/Common/UnrealAgentMCPCommon.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace UnrealAgentMCP
{
	namespace
	{
		using FModalDialogDelegate = TDelegate<EAppReturnType::Type(EAppMsgCategory, EAppMsgType::Type, const FText&, const FText&)>;

		struct FDialogPolicy
		{
			FString Pattern;
			EAppReturnType::Type Response = EAppReturnType::Cancel;
		};

		struct FDialogState
		{
			TArray<FDialogPolicy> Policies;
			FModalDialogDelegate PreviousDelegate;
			bool bHookInstalled = false;
			FString LastTitle;
			FString LastMessage;
			FString LastResponse;
		};

		FDialogState& DialogState()
		{
			static FDialogState State;
			return State;
		}

		FString ResponseToString(const EAppReturnType::Type Response)
		{
			switch (Response)
			{
			case EAppReturnType::Yes:
				return TEXT("yes");
			case EAppReturnType::No:
				return TEXT("no");
			case EAppReturnType::Ok:
				return TEXT("ok");
			case EAppReturnType::Cancel:
				return TEXT("cancel");
			case EAppReturnType::Retry:
				return TEXT("retry");
			case EAppReturnType::Continue:
				return TEXT("continue");
			case EAppReturnType::YesAll:
				return TEXT("yesAll");
			case EAppReturnType::NoAll:
				return TEXT("noAll");
			default:
				return TEXT("cancel");
			}
		}

		bool ParseResponse(FString Value, EAppReturnType::Type& OutResponse)
		{
			Value.ToLowerInline();
			if (Value == TEXT("yes"))
				OutResponse = EAppReturnType::Yes;
			else if (Value == TEXT("no"))
				OutResponse = EAppReturnType::No;
			else if (Value == TEXT("ok"))
				OutResponse = EAppReturnType::Ok;
			else if (Value == TEXT("cancel"))
				OutResponse = EAppReturnType::Cancel;
			else if (Value == TEXT("retry"))
				OutResponse = EAppReturnType::Retry;
			else if (Value == TEXT("continue"))
				OutResponse = EAppReturnType::Continue;
			else if (Value == TEXT("yesall"))
				OutResponse = EAppReturnType::YesAll;
			else if (Value == TEXT("noall"))
				OutResponse = EAppReturnType::NoAll;
			else
				return false;
			return true;
		}

		EAppReturnType::Type SafeDefaultResponse(const EAppMsgType::Type MessageType)
		{
			switch (MessageType)
			{
			case EAppMsgType::Ok:
				return EAppReturnType::Ok;
			case EAppMsgType::YesNo:
			case EAppMsgType::YesNoCancel:
				return EAppReturnType::No;
			default:
				return EAppReturnType::Cancel;
			}
		}

		EAppReturnType::Type HandleModalDialog(const EAppMsgCategory Category, const EAppMsgType::Type MessageType, const FText& Message, const FText& Title)
		{
			FDialogState& State = DialogState();
			State.LastTitle = Title.ToString();
			State.LastMessage = Message.ToString();
			for (const FDialogPolicy& Policy : State.Policies)
			{
				if (State.LastTitle.Contains(Policy.Pattern, ESearchCase::IgnoreCase) || State.LastMessage.Contains(Policy.Pattern, ESearchCase::IgnoreCase))
				{
					State.LastResponse = ResponseToString(Policy.Response);
					return Policy.Response;
				}
			}
			if (State.PreviousDelegate.IsBound())
			{
				const EAppReturnType::Type Response = State.PreviousDelegate.Execute(Category, MessageType, Message, Title);
				State.LastResponse = ResponseToString(Response);
				return Response;
			}
			const EAppReturnType::Type Response = SafeDefaultResponse(MessageType);
			State.LastResponse = ResponseToString(Response);
			return Response;
		}

		void InstallDialogHook()
		{
			FDialogState& State = DialogState();
			if (State.bHookInstalled)
			{
				return;
			}
			State.PreviousDelegate = FCoreDelegates::ModalMessageDialog;
			FCoreDelegates::ModalMessageDialog.BindStatic(&HandleModalDialog);
			State.bHookInstalled = true;
		}

		void RemoveDialogHook()
		{
			FDialogState& State = DialogState();
			if (!State.bHookInstalled)
			{
				return;
			}
			FCoreDelegates::ModalMessageDialog = MoveTemp(State.PreviousDelegate);
			State.bHookInstalled = false;
		}

		void CollectDialogWidgets(const TSharedRef<SWidget>& Widget, TArray<FString>& OutTexts, TArray<TSharedRef<SButton>>& OutButtons, TArray<FString>& OutButtonLabels)
		{
			if (Widget->GetType() == TEXT("STextBlock"))
			{
				const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
				if (!Text.IsEmpty())
				{
					OutTexts.Add(Text);
				}
			}
			if (Widget->GetType() == TEXT("SButton"))
			{
				TSharedRef<SButton> Button = StaticCastSharedRef<SButton>(Widget);
				FString Label;
				FChildren* Children = Button->GetChildren();
				if (Children)
				{
					for (int32 Index = 0; Index < Children->Num(); ++Index)
					{
						const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
						if (Child->GetType() == TEXT("STextBlock"))
						{
							Label = StaticCastSharedRef<STextBlock>(Child)->GetText().ToString();
							break;
						}
					}
				}
				OutButtons.Add(Button);
				OutButtonLabels.Add(Label);
			}
			FChildren* Children = Widget->GetChildren();
			if (!Children)
			{
				return;
			}
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				CollectDialogWidgets(Children->GetChildAt(Index), OutTexts, OutButtons, OutButtonLabels);
			}
		}
	}

	void FUnrealAgentMCPUnrealEditorAdapter::ShutdownDialogState()
	{
		DialogState().Policies.Reset();
		RemoveDialogHook();
	}

	FString FUnrealAgentMCPUnrealEditorAdapter::Dialogs(const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		FDialogState& State = DialogState();
		if (Action == TEXT("set_dialog_policy"))
		{
			const FString Pattern = GetString(Args, { TEXT("pattern") });
			EAppReturnType::Type Response;
			if (Pattern.IsEmpty() || !ParseResponse(GetString(Args, { TEXT("response") }), Response))
			{
				return ErrorJson(TEXT("需要 pattern，以及有效的 response。"));
			}
			const int32 ExistingIndex = State.Policies.IndexOfByPredicate(
				[&Pattern](const FDialogPolicy& Policy)
				{
					return Policy.Pattern.Equals(Pattern, ESearchCase::IgnoreCase);
				});
			const bool bExisted = ExistingIndex != INDEX_NONE && State.Policies[ExistingIndex].Response == Response;
			if (ExistingIndex != INDEX_NONE)
			{
				State.Policies.RemoveAt(ExistingIndex);
			}
			State.Policies.Add({ Pattern, Response });
			InstallDialogHook();
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("pattern"), Pattern);
			Result->SetStringField(TEXT("response"), ResponseToString(Response));
			Result->SetBoolField(TEXT("existed"), bExisted);
			Result->SetNumberField(TEXT("policyCount"), State.Policies.Num());
			return SuccessJson(Result);
		}

		if (Action == TEXT("clear_dialog_policy"))
		{
			const FString Pattern = GetString(Args, { TEXT("pattern") });
			const int32 Removed = Pattern.IsEmpty() ? State.Policies.Num()
													: State.Policies.RemoveAll(
														  [&Pattern](const FDialogPolicy& Policy)
														  {
															  return Policy.Pattern.Equals(Pattern, ESearchCase::IgnoreCase);
														  });
			if (Pattern.IsEmpty())
			{
				State.Policies.Reset();
			}
			if (State.Policies.IsEmpty())
			{
				RemoveDialogHook();
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("removed"), Removed);
			Result->SetNumberField(TEXT("policyCount"), State.Policies.Num());
			return SuccessJson(Result);
		}

		if (Action == TEXT("get_dialog_policy"))
		{
			TArray<TSharedPtr<FJsonValue>> Policies;
			for (const FDialogPolicy& Policy : State.Policies)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("pattern"), Policy.Pattern);
				Item->SetStringField(TEXT("response"), ResponseToString(Policy.Response));
				Policies.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Policies.Num());
			Result->SetBoolField(TEXT("hookInstalled"), State.bHookInstalled);
			Result->SetArrayField(TEXT("policies"), Policies);
			return SuccessJson(Result);
		}

		if (!FSlateApplication::IsInitialized())
		{
			return ErrorJson(TEXT("Slate 当前没有初始化。"));
		}
		const TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveModalWindow();
		if (Action == TEXT("list_dialogs"))
		{
			TArray<TSharedPtr<FJsonValue>> Dialogs;
			if (Window.IsValid())
			{
				TArray<FString> Texts;
				TArray<TSharedRef<SButton>> Buttons;
				TArray<FString> ButtonLabels;
				CollectDialogWidgets(Window.ToSharedRef(), Texts, Buttons, ButtonLabels);
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("title"), Window->GetTitle().ToString());
				Item->SetStringField(TEXT("message"), FString::Join(Texts, TEXT("\n")));
				TArray<TSharedPtr<FJsonValue>> Labels;
				for (const FString& Label : ButtonLabels)
				{
					Labels.Add(MakeShared<FJsonValueString>(Label));
				}
				Item->SetArrayField(TEXT("buttons"), Labels);
				Dialogs.Add(MakeShared<FJsonValueObject>(Item));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Dialogs.Num());
			Result->SetArrayField(TEXT("dialogs"), Dialogs);
			Result->SetStringField(TEXT("lastInterceptedTitle"), State.LastTitle);
			Result->SetStringField(TEXT("lastInterceptedMessage"), State.LastMessage);
			Result->SetStringField(TEXT("lastResponse"), State.LastResponse);
			return SuccessJson(Result);
		}

		if (Action == TEXT("respond_to_dialog"))
		{
			if (!Window.IsValid())
			{
				return ErrorJson(TEXT("当前没有活动的模态对话框。"));
			}
			TArray<FString> Texts;
			TArray<TSharedRef<SButton>> Buttons;
			TArray<FString> ButtonLabels;
			CollectDialogWidgets(Window.ToSharedRef(), Texts, Buttons, ButtonLabels);
			const FString RequestedLabel = GetString(Args, { TEXT("buttonLabel"), TEXT("label") });
			double RequestedIndexNumber = -1.0;
			Args->TryGetNumberField(TEXT("buttonIndex"), RequestedIndexNumber);
			int32 ButtonIndex = static_cast<int32>(RequestedIndexNumber);
			if (!RequestedLabel.IsEmpty())
			{
				ButtonIndex = ButtonLabels.IndexOfByPredicate(
					[&RequestedLabel](const FString& Label)
					{
						return Label.Contains(RequestedLabel, ESearchCase::IgnoreCase);
					});
			}
			if (!Buttons.IsValidIndex(ButtonIndex))
			{
				TArray<TSharedPtr<FJsonValue>> Available;
				for (const FString& Label : ButtonLabels)
				{
					Available.Add(MakeShared<FJsonValueString>(Label));
				}
				TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
				Error->SetBoolField(TEXT("success"), false);
				Error->SetStringField(TEXT("error"), TEXT("未找到指定按钮。"));
				Error->SetArrayField(TEXT("availableButtons"), Available);
				return JsonObjectToString(Error);
			}
			FSlateApplication::Get().SetKeyboardFocus(Buttons[ButtonIndex]);
			const FGeometry Geometry = Buttons[ButtonIndex]->GetCachedGeometry();
			const FVector2D Center = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
			const FPointerEvent PointerEvent(0, Center, Center, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
			Buttons[ButtonIndex]->OnMouseButtonDown(Geometry, PointerEvent);
			const FReply Reply = Buttons[ButtonIndex]->OnMouseButtonUp(Geometry, PointerEvent);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("buttonIndex"), ButtonIndex);
			Result->SetStringField(TEXT("clickedButton"), ButtonLabels[ButtonIndex]);
			Result->SetBoolField(TEXT("handled"), Reply.IsEventHandled());
			return SuccessJson(Result);
		}

		return ErrorJson(TEXT("编辑器对话框动作没有有效实现分支。"));
	}
}
