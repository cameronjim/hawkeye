#include "AnimGraphNode_HawkeyeBodyPose.h"

#define LOCTEXT_NAMESPACE "HawkeyeBodyPose"

FText UAnimGraphNode_HawkeyeBodyPose::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("Title", "Hawkeye Body Pose");
}

FText UAnimGraphNode_HawkeyeBodyPose::GetTooltipText() const
{
	return GetControllerDescription();
}

FText UAnimGraphNode_HawkeyeBodyPose::GetControllerDescription() const
{
	return LOCTEXT("Description", "Authored hand poses, the rest arm and the gripping wrist");
}

#undef LOCTEXT_NAMESPACE
