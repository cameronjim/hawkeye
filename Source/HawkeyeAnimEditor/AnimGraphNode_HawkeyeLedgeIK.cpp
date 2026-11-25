#include "AnimGraphNode_HawkeyeLedgeIK.h"

#define LOCTEXT_NAMESPACE "HawkeyeLedgeIK"

FText UAnimGraphNode_HawkeyeLedgeIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Title", "Hawkeye Ledge IK");
}

FText UAnimGraphNode_HawkeyeLedgeIK::GetTooltipText() const
{
	return GetControllerDescription();
}

FText UAnimGraphNode_HawkeyeLedgeIK::GetControllerDescription() const
{
	return LOCTEXT("Description", "Hands on the ledge's edge, the body hung from them, the feet on the wall");
}

#undef LOCTEXT_NAMESPACE
