#include "Combat/AnimNotifyState_ComboWindow.h"

#include "Combat/MeleeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace HawkeyeComboWindowNotify
{
	UMeleeComponent* FindMelee(const USkeletalMeshComponent* MeshComp)
	{
		const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
		return Owner ? Owner->FindComponentByClass<UMeleeComponent>() : nullptr;
	}
}

void UAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UMeleeComponent* Melee = HawkeyeComboWindowNotify::FindMelee(MeshComp))
	{
		Melee->NotifyComboWindowBegin(Animation);
	}
}

void UAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UMeleeComponent* Melee = HawkeyeComboWindowNotify::FindMelee(MeshComp))
	{
		Melee->NotifyComboWindowEnd(Animation);
	}
}

FString UAnimNotifyState_ComboWindow::GetNotifyName_Implementation() const
{
	return TEXT("ANS_ComboWindow");
}
