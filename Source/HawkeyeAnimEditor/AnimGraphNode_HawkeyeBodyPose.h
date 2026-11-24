#pragma once

#include "CoreMinimal.h"

#include "AnimGraphNode_SkeletalControlBase.h"
#include "Player/BodyPoseNode.h"

#include "AnimGraphNode_HawkeyeBodyPose.generated.h"

/** The editor-only graph node for FAnimNode_HawkeyeBodyPose. UHawkeyeRetargetGraphBuilder (in the game module, which cannot
 * see this module) finds the class by path and puts one after the retarget. */
UCLASS()
class UAnimGraphNode_HawkeyeBodyPose : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_HawkeyeBodyPose Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

protected:
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
