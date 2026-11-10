#include "Mission/ObjectiveTriggerVolume.h"

#include "Hawkeye.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Mission/MissionSubsystem.h"

AObjectiveTriggerVolume::AObjectiveTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bNetLoadOnClient = true;
}

void AObjectiveTriggerVolume::BeginPlay()
{
	Super::BeginPlay();

	OnActorBeginOverlap.AddDynamic(this, &AObjectiveTriggerVolume::HandleActorBeginOverlap);

	if (UMissionSubsystem* MissionSubsystem = UMissionSubsystem::Get(this); MissionSubsystem && !ObjectiveId.IsNone())
	{
		MissionSubsystem->RegisterObjectiveLocation(ObjectiveId, GetActorLocation());
	}
}

void AObjectiveTriggerVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMissionSubsystem* MissionSubsystem = UMissionSubsystem::Get(this); MissionSubsystem && !ObjectiveId.IsNone())
	{
		MissionSubsystem->UnregisterObjectiveLocation(ObjectiveId);
	}

	Super::EndPlay(EndPlayReason);
}

void AObjectiveTriggerVolume::HandleActorBeginOverlap(AActor* /*OverlappedActor*/, AActor* OtherActor)
{
	if (!OtherActor || (bOnlyOnce && bHasTriggered))
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (bPlayerOnly && (!Pawn || !Pawn->IsPlayerControlled()))
	{
		return;
	}

	UMissionSubsystem* MissionSubsystem = UMissionSubsystem::Get(this);
	if (!MissionSubsystem)
	{
		return;
	}

	if (MissionSubsystem->CompleteObjective(ObjectiveId))
	{
		bHasTriggered = true;
		OnObjectiveTriggered(OtherActor);

		UE_LOG(LogHawkeye, Verbose, TEXT("Objective '%s' completed by overlap with %s."),
			*ObjectiveId.ToString(), *OtherActor->GetName());
	}
}
