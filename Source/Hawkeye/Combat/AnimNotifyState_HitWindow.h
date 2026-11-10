#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_HitWindow.generated.h"

/**
 * The hit window of a strike montage: the swing sweeps for a target from when it opens, each tick,
 * until it finds one or the window closes (a miss). Drives UMeleeComponent on the mesh's owner; a
 * montage without one keeps the attack's wind-up timer.
 */
UCLASS(meta = (DisplayName = "ANS_HitWindow"))
class HAWKEYE_API UAnimNotifyState_HitWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
