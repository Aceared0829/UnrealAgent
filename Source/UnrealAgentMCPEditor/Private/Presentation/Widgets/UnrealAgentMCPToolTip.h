#pragma once

#include "CoreMinimal.h"
#include "Widgets/IToolTip.h"

namespace UnrealAgentMCPToolTip
{
	TSharedRef<IToolTip> Make(const FText& Text);
	TSharedRef<IToolTip> Make(const TAttribute<FText>& Text);
}
