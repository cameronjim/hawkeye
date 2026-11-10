#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ComboWindow.generated.h"

/**
 * The combo window of a strike montage: while it is open the next strike may start at once, cutting
 * this montage, and after a light that landed the chain stays open until it closes. Drives
 * UMeleeComponent on the mesh's owner; a montage without one keeps the 0.35 s chain timer.
 */
UCLASS(meta = (DisplayName = "ANS_ComboWindow"))
class HAWKEYE_API UAnimNotifyState_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
