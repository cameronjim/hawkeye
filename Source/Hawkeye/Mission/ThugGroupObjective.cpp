#include "Mission/ThugGroupObjective.h"

#include "Hawkeye.h"
#include "Combat/HealthComponent.h"
#include "EngineUtils.h"
#include "Mission/MissionSubsystem.h"
#include "World/ThugCharacter.h"

AThugGroupObjective::AThugGroupObjective()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AThugGroupObjective::BeginPlay()
{
	Super::BeginPlay();

	if (RegisterGroup() == 0)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no thug tagged '%s'; objective '%s' can never complete."),
			*GetName(), *GroupTag.ToString(), *ObjectiveId.ToString());
	}

	// Placed where the group stands, so the HUD marker can point at the fight like at a volume.
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && !ObjectiveId.IsNone())
	{
		Missions->RegisterObjectiveLocation(ObjectiveId, GetActorLocation());
	}
}

void AThugGroupObjective::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && !ObjectiveId.IsNone())
	{
		Missions->UnregisterObjectiveLocation(ObjectiveId);
	}

	Super::EndPlay(EndPlayReason);
}

int32 AThugGroupObjective::RegisterGroup()
{
	UWorld* World = GetWorld();
	if (!World || GroupTag.IsNone())
	{
		return Members.Num();
	}
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		if (It->ActorHasTag(GroupTag))
		{
			RegisterThug(*It);
		}
	}
	return Members.Num();
}

bool AThugGroupObjective::RegisterThug(AThugCharacter* Thug)
{
	UHealthComponent* Health = Thug ? Thug->GetHealthComponent() : nullptr;
	if (!Health || Members.Contains(Thug))
	{
		return false;
	}
	Members.Add(Thug);
	Health->OnDeath.AddDynamic(this, &AThugGroupObjective::HandleMemberDeath);
	return true;
}

int32 AThugGroupObjective::GetAliveCount() const
{
	int32 Alive = 0;
	for (const AThugCharacter* Thug : Members)
	{
		const UHealthComponent* Health = IsValid(Thug) ? Thug->GetHealthComponent() : nullptr;
		Alive += Health && Health->IsAlive() ? 1 : 0;
	}
	return Alive;
}

void AThugGroupObjective::HandleMemberDeath(UHealthComponent* /*Health*/, AActor* Killer)
{
	const int32 Alive = GetAliveCount();
	UE_LOG(LogHawkeye, Log, TEXT("%s: a '%s' thug is down (by %s); %d of %d left."),
		*GetName(), *GroupTag.ToString(), *GetNameSafe(Killer), Alive, Members.Num());
	if (Alive > 0 || bCleared)
	{
		return;
	}

	bCleared = true;
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this); Missions && Missions->HasObjective(ObjectiveId))
	{
		Missions->CompleteObjective(ObjectiveId);
	}
}
