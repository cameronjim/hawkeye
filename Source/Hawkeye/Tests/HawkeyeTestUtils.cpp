#include "Tests/HawkeyeTestUtils.h"

#include "Player/InventoryComponent.h"

#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Mission/MissionTracker.h"

FHawkeyeTestWorld::FHawkeyeTestWorld()
{
	World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (World && GEngine)
	{
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);

		// APawn::ShouldTakeDamage refuses every hit in a world with no authority game mode, so a
		// test world without one silently swallows all weapon damage. A bare AGameModeBase is
		// enough, and deliberately not the project's: tests never load Content.
		World->SetGameInstance(NewObject<UGameInstance>(GEngine));
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->DefaultGameMode = AGameModeBase::StaticClass();
		}
		World->SetGameMode(FURL());

		// Without this the world never marks its actors initialized, and AActor::PostActorConstruction
		// then skips PostInitializeComponents and BeginPlay entirely - so anything an actor wires up
		// there (a thug binding OnDeath, for one) is silently missing in tests but present in game.
		World->InitializeActorsForPlay(FURL());
		World->SetBegunPlay(true);
	}
}

FHawkeyeTestWorld::~FHawkeyeTestWorld()
{
	if (!World)
	{
		return;
	}

	if (GEngine)
	{
		GEngine->DestroyWorldContext(World);
	}

	World->DestroyWorld(/*bInformEngineOfWorld=*/false);
	World = nullptr;
}

AActor* FHawkeyeTestWorld::SpawnActor(TSubclassOf<AActor> ActorClass, const FVector& Location, const FRotator& Rotation) const
{
	if (!World || !ActorClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
}

void UHawkeyeTestListener::HandleHealthChanged(UHealthComponent* /*HealthComponent*/, float NewHealth, float Delta, AActor* DamageInstigator)
{
	++HealthChangedCount;
	LastNewHealth = NewHealth;
	LastHealthDelta = Delta;
	if (Delta < 0.f)
	{
		DamageInstigators.Add(DamageInstigator);
	}
}

void UHawkeyeTestListener::HandleDeath(UHealthComponent* /*HealthComponent*/, AActor* Killer)
{
	++DeathCount;
	LastKiller = Killer;
}

void UHawkeyeTestListener::HandlePhaseChanged(int32 OldPhaseIndex, int32 NewPhaseIndex, FBossPhase /*Phase*/)
{
	++PhaseChangedCount;
	LastOldPhaseIndex = OldPhaseIndex;
	LastNewPhaseIndex = NewPhaseIndex;

	if (WatchedHealth)
	{
		bWatchedHealthInvulnerableAtPhaseChange = WatchedHealth->IsInvulnerable();
	}
}

void UHawkeyeTestListener::HandleTransitionFinished(int32 /*PhaseIndex*/)
{
	++TransitionFinishedCount;
}

void UHawkeyeTestListener::HandleAmmoChanged(int32 CurrentAmmo, int32 ReserveAmmo)
{
	++AmmoChangedCount;
	LastMagazine = CurrentAmmo;
	LastReserve = ReserveAmmo;
}

void UHawkeyeTestListener::HandleEmptyClick()
{
	++EmptyClickCount;
}

void UHawkeyeTestListener::HandleWeaponHit(AActor* HitActor, float DamageDealt)
{
	++WeaponHitCount;
	LastWeaponHitActor = HitActor;
	LastWeaponHitDamage = DamageDealt;
}

void UHawkeyeTestListener::HandleInventoryChanged()
{
	++InventoryChangedCount;
}

void UHawkeyeTestListener::HandleActiveSlotChanged(int32 OldSlot, int32 NewSlot)
{
	++ActiveSlotChangedCount;
	LastOldArrowSlot = OldSlot;
	LastNewArrowSlot = NewSlot;
}

void UHawkeyeTestListener::HandleChallengeRetry()
{
	++ChallengeRetryCount;
}

void UHawkeyeTestListener::HandleChallengeLeave()
{
	++ChallengeLeaveCount;
}

void UHawkeyeTestListener::HandleDrawChanged(float Fraction)
{
	++DrawChangedCount;
	LastDrawFraction = Fraction;
}

void UHawkeyeTestListener::HandleArrowFired(UArrowDefinition* /*Arrow*/)
{
	++ArrowFiredCount;
}

void UHawkeyeTestListener::HandleArrowHit(AActor* /*HitActor*/, float Damage, bool bHeadshot)
{
	++ArrowHitCount;
	LastArrowHitDamage = Damage;
	bLastArrowHitHeadshot = bHeadshot;
}

void UHawkeyeTestListener::HandleStaggered(UHealthComponent* /*HealthComponent*/, AActor* /*DamageInstigator*/)
{
	++StaggeredCount;
}

void UHawkeyeTestListener::HandleTakedownPerformed(AActor* Target)
{
	++TakedownCount;
	LastTakedownTarget = Target;
}

void UHawkeyeTestListener::HandleAlertStateChanged(EThugAlertState OldState, EThugAlertState NewState)
{
	++AlertStateChangedCount;
	LastOldAlertState = OldState;
	LastNewAlertState = NewState;
}

void UHawkeyeTestListener::HandleMissionStarted(UMissionDefinition* Mission)
{
	++MissionStartedCount;
	LastStartedMission = Mission;

	if (WatchedTracker)
	{
		bCurrentObjectiveSetAtMissionStart = WatchedTracker->GetCurrentObjective() != nullptr;
	}
}

void UHawkeyeTestListener::HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex)
{
	++ObjectiveUpdatedCount;
	LastObjective = Objective;
	LastObjectiveIndex = ObjectiveIndex;
}

void UHawkeyeTestListener::HandleMissionComplete(UMissionDefinition* /*Mission*/)
{
	++MissionCompleteCount;

	// The mission is over, so nothing is carried into the next one.
	if (InventoryToClearOnMissionComplete)
	{
		InventoryToClearOnMissionComplete->Clear();
	}
}

void UHawkeyeTestListener::HandleFlashbackRequested(UFlashbackDefinition* /*Flashback*/)
{
	++FlashbackRequestedCount;
	bFlashbackFollowedMissionComplete = MissionCompleteCount == 1;
}

void UHawkeyeTestListener::HandleFlashbackFinished(UFlashbackDefinition* /*Flashback*/)
{
	++FlashbackFinishedCount;
}

void AHawkeyeAimTestCharacter::TestSetSprinting(bool bInSprinting)
{
	bIsSprinting = bInSprinting;
	if (bInSprinting)
	{
		StopAim();
	}
	UpdateMaxWalkSpeed();
}

void AHawkeyeAimTestCharacter::TestSetMoveInput(float Magnitude, float HeldSeconds)
{
	MoveInputMagnitude = Magnitude;
	MoveInputHeldSeconds = HeldSeconds;
	UpdateMaxWalkSpeed();
}

void AHawkeyeAimTestCharacter::TestTickMovement(float DeltaSeconds)
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bRunPhysicsWithNoController = true;
		Movement->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}
}

float AHawkeyeAimTestCharacter::MaxWalkSpeed() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return Movement ? Movement->MaxWalkSpeed : 0.f;
}

void UHawkeyeTestListener::HandleSettingsChanged(FHawkeyeSettings Settings)
{
	++SettingsChangedCount;
	LastLookSensitivity = Settings.LookSensitivity;
}

void UHawkeyeTestListener::HandleGrappleLanded(AGrappleAnchor* Anchor)
{
	++GrappleLandedCount;
	LastGrappleAnchor = Anchor;
}

void UHawkeyeTestListener::HandleGrappleCancelled(AGrappleAnchor* Anchor)
{
	++GrappleCancelledCount;
	LastGrappleAnchor = Anchor;
}

void UHawkeyeTestListener::HandleMeleeWindup(FName AttackName, float /*WindupSeconds*/)
{
	++MeleeWindupCount;
	LastMeleeWindupName = AttackName;
	if (WatchedMeleeVictim)
	{
		VictimHealthAtWindup = WatchedMeleeVictim->GetCurrentHealth();
	}
}

void UHawkeyeTestListener::HandleMeleeLanded(AActor* HitActor, float DamageDealt, FName /*AttackName*/)
{
	++MeleeLandedCount;
	LastMeleeHitActor = HitActor;
	LastMeleeDamage = DamageDealt;
}

AHawkeyeTestBlocker::AHawkeyeTestBlocker()
{
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetBoxExtent(FVector(50.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
}

AHawkeyeTestSolidTakedownTarget::AHawkeyeTestSolidTakedownTarget()
{
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetBoxExtent(FVector(30.f, 30.f, 90.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_Pawn);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Tags.Add(FName(TEXT("Thug")));
}

void AHawkeyeTestBlocker::SetExtent(const FVector& HalfExtent)
{
	Box->SetBoxExtent(HalfExtent, /*bUpdateOverlaps=*/false);
}
