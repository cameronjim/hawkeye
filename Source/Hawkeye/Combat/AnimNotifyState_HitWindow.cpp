#include "Combat/AnimNotifyState_HitWindow.h"

#include "Combat/MeleeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace HawkeyeHitWindowNotify
{
	UMeleeComponent* FindMelee(const USkeletalMeshComponent* MeshComp)
	{
		const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
		return Owner ? Owner->FindComponentByClass<UMeleeComponent>() : nullptr;
	}
}

void UAnimNotifyState_HitWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMeleeComponent* Melee = HawkeyeHitWindowNotify::FindMelee(MeshComp))
	{
		Melee->NotifyHitWindowBegin(Animation);
	}
}

void UAnimNotifyState_HitWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMeleeComponent* Melee = HawkeyeHitWindowNotify::FindMelee(MeshComp))
	{
		Melee->NotifyHitWindowEnd(Animation);
	}
}

FString UAnimNotifyState_HitWindow::GetNotifyName_Implementation() const
{
	return TEXT("ANS_HitWindow");
}
