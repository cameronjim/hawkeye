#include "Combat/Takedownable.h"
#include "EngineUtils.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "World/PickupActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeThugTest
{
	/** A thug with a controller possessing it, so the state machine has a pawn to drive. */
	static AThugAIController* SpawnPossessedThug(const FHawkeyeTestWorld& TestWorld, AThugCharacter*& OutThug)
	{
		OutThug = Cast<AThugCharacter>(
			TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		AThugAIController* Controller = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));

		if (OutThug && Controller)
		{
			Controller->Possess(OutThug);
		}
		return Controller;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugDefaults, "Hawkeye.Thug.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugDefaults::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Thug)
	{
		AddError(TEXT("Failed to spawn the thug."));
		return false;
	}

	TestTrue(TEXT("Tagged Thug so the takedown sweep finds him"), Thug->ActorHasTag(FName(TEXT("Thug"))));
	TestEqual(TEXT("Starts Calm"), Thug->GetAlertState(), EThugAlertState::Calm);

	const UWeaponComponent* Weapon = Thug->GetWeaponComponent();
	if (!Weapon)
	{
		AddError(TEXT("The thug has no UWeaponComponent."));
		return false;
	}
	TestTrue(TEXT("Thugs start armed"), Weapon->HasWeapon());
	TestEqual(TEXT("Thugs hit for 12"), Weapon->Damage, 12.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugAlertedRefusesTakedown, "Hawkeye.Thug.AlertedRefusesTakedown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugAlertedRefusesTakedown::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Thug)
	{
		AddError(TEXT("Failed to spawn the thug."));
		return false;
	}

	TestTrue(TEXT("A calm thug can be taken down"), ITakedownable::Execute_CanBeTakenDown(Thug, nullptr));

	Thug->SetAlertState(EThugAlertState::Suspicious);
	TestTrue(TEXT("A suspicious thug can still be taken down"), ITakedownable::Execute_CanBeTakenDown(Thug, nullptr));

	Thug->SetAlertState(EThugAlertState::Alerted);
	TestFalse(TEXT("An alerted thug cannot"), ITakedownable::Execute_CanBeTakenDown(Thug, nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugAlertStateChangeBroadcasts, "Hawkeye.Thug.AlertStateChangeBroadcasts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugAlertStateChangeBroadcasts::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Thug)
	{
		AddError(TEXT("Failed to spawn the thug."));
		return false;
	}

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Thug->OnAlertStateChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleAlertStateChanged);

	Thug->SetAlertState(EThugAlertState::Suspicious);
	TestEqual(TEXT("One broadcast"), Listener->AlertStateChangedCount, 1);
	TestEqual(TEXT("New state reported"), Listener->LastNewAlertState, EThugAlertState::Suspicious);

	Thug->SetAlertState(EThugAlertState::Suspicious);
	TestEqual(TEXT("Setting the same state is silent"), Listener->AlertStateChangedCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugHearingDrivesState, "Hawkeye.Thug.HearingDrivesState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugHearingDrivesState::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Controller = HawkeyeThugTest::SpawnPossessedThug(TestWorld, Thug);
	if (!Controller || !Thug)
	{
		AddError(TEXT("Failed to spawn the thug and its controller."));
		return false;
	}

	const FVector Noise(800.f, 0.f, 0.f);

	// Footsteps: loudness below the gunshot threshold.
	Controller->ReportStimulus(EStimulusKind::Hearing, Noise, /*bSuccessful=*/true, /*Loudness=*/1.f);
	TestEqual(TEXT("Footsteps make him suspicious"), Thug->GetAlertState(), EThugAlertState::Suspicious);
	TestEqual(TEXT("And he remembers where"), Controller->GetLastStimulusLocation(), Noise);

	// A gunshot is unambiguous.
	Controller->ReportStimulus(EStimulusKind::Hearing, Noise, /*bSuccessful=*/true, /*Loudness=*/3.f);
	TestEqual(TEXT("A gunshot alerts him at once"), Thug->GetAlertState(), EThugAlertState::Alerted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugSightConfirmsBeforeAlerting, "Hawkeye.Thug.SightConfirmsBeforeAlerting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugSightConfirmsBeforeAlerting::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Controller = HawkeyeThugTest::SpawnPossessedThug(TestWorld, Thug);
	if (!Controller || !Thug)
	{
		AddError(TEXT("Failed to spawn the thug and its controller."));
		return false;
	}

	Controller->ReportStimulus(EStimulusKind::Sight, FVector(600.f, 0.f, 0.f), /*bSuccessful=*/true);
	TestEqual(TEXT("A first sighting is only suspicious"), Thug->GetAlertState(), EThugAlertState::Suspicious);

	// Still not long enough: SightConfirmSeconds is 0.6.
	ThinkThug(Controller, 0.25f);
	TestEqual(TEXT("A glimpse does not alert him"), Thug->GetAlertState(), EThugAlertState::Suspicious);

	ThinkThug(Controller, 0.5f);
	TestEqual(TEXT("A sustained look does"), Thug->GetAlertState(), EThugAlertState::Alerted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugLosesTargetBackToSuspicious, "Hawkeye.Thug.LosesTargetBackToSuspicious",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugLosesTargetBackToSuspicious::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Controller = HawkeyeThugTest::SpawnPossessedThug(TestWorld, Thug);
	if (!Controller || !Thug)
	{
		AddError(TEXT("Failed to spawn the thug and its controller."));
		return false;
	}

	Controller->ReportStimulus(EStimulusKind::Hearing, FVector(400.f, 0.f, 0.f), /*bSuccessful=*/true, /*Loudness=*/3.f);
	TestEqual(TEXT("Alerted by the gunshot"), Thug->GetAlertState(), EThugAlertState::Alerted);

	// LoseTargetSeconds is 5; six seconds of nothing drops him back.
	for (int32 Step = 0; Step < 6; ++Step)
	{
		ThinkThug(Controller, 1.f);
	}

	TestEqual(TEXT("With nothing to shoot at he falls back to Suspicious"),
		Thug->GetAlertState(), EThugAlertState::Suspicious);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugDropsLootOnDeath, "Hawkeye.Thug.DropsLootOnDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeThugDropsLootOnDeath::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Thug)
	{
		AddError(TEXT("Failed to spawn the thug."));
		return false;
	}

	Thug->DropOnDeath.Add(APickupActor::StaticClass());
	Thug->DropOnDeath.Add(APickupActor::StaticClass());

	ITakedownable::Execute_OnTakedown(Thug, nullptr);

	TArray<AActor*> Pickups;
	for (TActorIterator<APickupActor> It(TestWorld.Get()); It; ++It)
	{
		Pickups.Add(*It);
	}

	TestEqual(TEXT("Both pickups dropped"), Pickups.Num(), 2);
	TestFalse(TEXT("The thug is dead"), Thug->GetHealthComponent()->IsAlive());

	// OnDeath also drops, so the takedown must not produce a second set.
	Thug->DropLoot();
	int32 SecondCount = 0;
	for (TActorIterator<APickupActor> It(TestWorld.Get()); It; ++It)
	{
		++SecondCount;
	}
	TestEqual(TEXT("Loot only ever drops once"), SecondCount, 2);

	return true;
}

#endif
