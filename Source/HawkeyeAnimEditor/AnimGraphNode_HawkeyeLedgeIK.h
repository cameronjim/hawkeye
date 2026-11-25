#pragma once

#include "CoreMinimal.h"

#include "AnimGraphNode_SkeletalControlBase.h"
#include "Player/LedgeIKNode.h"

#include "AnimGraphNode_HawkeyeLedgeIK.generated.h"

/** The editor-only graph node for FAnimNode_HawkeyeLedgeIK. UHawkeyeRetargetGraphBuilder (in the game module, which cannot
 * see this module) finds the class by path and puts one after the retarget and the secondary motion on Kate's body. */
UCLASS()
class UAnimGraphNode_HawkeyeLedgeIK : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_HawkeyeLedgeIK Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

protected:
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
};
