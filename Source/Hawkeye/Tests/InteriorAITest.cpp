#include "Combat/HealthComponent.h"
#include "Combat/TakedownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "Navigation/NavLinkProxy.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Tests/InteriorTestKit.h"
#include "World/DoorActor.h"
#include "World/PickupActor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "World/ThugHearing.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The AI indoors (claude-docs/gameplay-semantics.md, "Interiors"): the rules with no map (a squad alert stays in
 * its crew, a carried keycard drops where its carrier falls, a calm indoor archer sees only his cone, the
 * hearing rule), then the sample auction house, L_Int_Sample, in a real world: its four enemies as the layout
 * placed them, sight through a doorway and down from the gallery but not through a wall, hearing through walls
 * and doors, the locked vault closing the navmesh, a thug taking the stair, ragdolls and knockdowns staying in
 * their room, the gunner's cover on the gallery. The laps are InteriorLapTest.cpp.
 */
namespace HawkeyeInteriorAITest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	static constexpr EAutomationTestFlags MapFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter;

	static AThugAIController* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& Location, EThugWeapon Weapon, FName Group,
		AThugCharacter*& OutThug)
	{
		OutThug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		AThugAIController* Brain = Cast<AThugAIController>(TestWorld.SpawnActor(AThugAIController::StaticClass(), Location, FRotator::ZeroRotator));
		if (!OutThug || !Brain)
		{
			return nullptr;
		}
		OutThug->Weapon = Weapon;
		OutThug->AlertGroup = Group;
		Brain->Possess(OutThug);
		return Brain;
	}

	/**
	 * Queues: open L_Int_Sample, stop every thug thinking the moment it is up (before the lobby patrol reaches
	 * the office door and opens it), make Kate invulnerable, wait for the navmesh.
	 */
	static void AddOpenSample(FAutomationTestBase* Test)
	{
		using namespace HawkeyeInteriorKit;
		// Always a fresh copy: the tests before this one leave bodies and open doors behind.
		AutomationOpenMap(MapPath, /*bForceReload=*/true);
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
		{
			UWorld* World = FindWorld();
			if (!IsMap(World, MapPath) || !FindPlayer(World))
			{
				return false;
			}
			FreezeThugs(World);
			if (UHealthComponent* Health = FindPlayer(World)->GetHealthComponent())
			{
				Health->SetInvulnerable(true);
			}
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			return Nav && Nav->GetDefaultNavDataInstance() && !Nav->IsNavigationBuildInProgress();
		}, [Test]()
		{
			Test->AddError(TEXT("L_Int_Sample did not come up with a player and a built navmesh within 25 s."));
			return true;
		}, 25.f));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	}

	/** He stands where he is put, calm (or walks on his patrol from there): thinking on, senses on unless bPacified. */
	static void Wake(AThugCharacter* Thug, bool bPacified = false, bool bPatrol = false)
	{
		if (AThugAIController* Brain = HawkeyeInteriorKit::BrainOf(Thug))
		{
			if (!bPatrol)
			{
				Thug->PatrolPoints.Reset();
			}
			// Pacified drops him to Calm and clears his target; then the senses come back on unless asked not to.
			Brain->SetPacified(true);
			Brain->SetPacified(bPacified);
			Brain->SetThinkingEnabled(true);
		}
	}

	static ADoorActor* DoorNear(UWorld* World, const FVector2D& Where)
	{
		for (ADoorActor* Door : HawkeyeInteriorKit::All<ADoorActor>(World))
		{
			if (FVector2D::Distance(FVector2D(Door->GetActorLocation()), Where) < 60.f)
			{
				return Door;
			}
		}
		return nullptr;
	}

	/** The sample's doors, at their thresholds (Sample.json). */
	static const FVector2D OfficeDoor(250.f, 600.f);
	static const FVector2D VaultDoor(2200.f, 1150.f);
	static const FVector2D GalleryDoor(800.f, 685.f);

	static const TCHAR* StateName(EThugAlertState State)
	{
		return State == EThugAlertState::Alerted ? TEXT("alerted") : State == EThugAlertState::Suspicious ? TEXT("suspicious") : TEXT("calm");
	}
}

// --- Rules with no map -----------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorSquadCrew, "Hawkeye.Interior.SquadAlertStaysInItsCrew", HawkeyeInteriorAITest::Flags)

bool FHawkeyeInteriorSquadCrew::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, -800.f, 0.f), FRotator::ZeroRotator));
	AThugCharacter* Spotter = nullptr;
	AThugCharacter* Mate = nullptr;
	AThugCharacter* Stranger = nullptr;
	AThugCharacter* Loner = nullptr;
	AThugAIController* SpotterBrain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Bat, TEXT("hall"), Spotter);
	SpawnThug(TestWorld, FVector(600.f, 0.f, 0.f), EThugWeapon::Fists, TEXT("hall"), Mate);
	SpawnThug(TestWorld, FVector(-600.f, 0.f, 0.f), EThugWeapon::Fists, TEXT("front"), Stranger);
	SpawnThug(TestWorld, FVector(0.f, 600.f, 0.f), EThugWeapon::Fists, NAME_None, Loner);
	if (!Kate || !SpotterBrain || !Mate || !Stranger || !Loner)
	{
		AddError(TEXT("Could not spawn the crews."));
		return false;
	}
	SpotterBrain->SetTarget(Kate);
	SpotterBrain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, SpotterBrain->GunshotLoudnessThreshold);
	SpotterBrain->Think(1.6f);
	TestEqual(TEXT("His own crew, 6 m off with a line to him: suspicious"), Mate->GetAlertState(), EThugAlertState::Suspicious);
	TestEqual(TEXT("Another crew just as near: calm"), Stranger->GetAlertState(), EThugAlertState::Calm);
	TestEqual(TEXT("A thug with no crew: calm"), Loner->GetAlertState(), EThugAlertState::Calm);

	// Two with no crew are one squad, as in the district.
	AThugCharacter* Other = nullptr;
	AThugAIController* LonerBrain = HawkeyeInteriorKit::BrainOf(Loner);
	SpawnThug(TestWorld, FVector(600.f, 600.f, 0.f), EThugWeapon::Fists, NAME_None, Other);
	LonerBrain->SetTarget(Kate);
	LonerBrain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, LonerBrain->GunshotLoudnessThreshold);
	LonerBrain->Think(1.6f);
	TestEqual(TEXT("No crew tells no crew"), Other ? Other->GetAlertState() : EThugAlertState::Calm, EThugAlertState::Suspicious);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorCarriedKeycard, "Hawkeye.Interior.CarriedKeycardDropsWhereHeFalls", HawkeyeInteriorAITest::Flags)

bool FHawkeyeInteriorCarriedKeycard::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Archer = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector(0.f, 0.f, 98.f), EThugWeapon::Bow, TEXT("hall"), Archer);
	APickupActor* Card = Cast<APickupActor>(TestWorld.SpawnActor(APickupActor::StaticClass(), FVector(900.f, 0.f, 0.f), FRotator::ZeroRotator));
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(-300.f, 0.f, 98.f), FRotator::ZeroRotator));
	if (!Brain || !Card || !Kate)
	{
		AddError(TEXT("Could not spawn the archer, the card or Kate."));
		return false;
	}
	Card->KeycardId = TEXT("vault");
	Archer->CarriedPickups.Add(Card);
	Card->SetCarried(true);
	TestTrue(TEXT("Carried: hidden"), Card->IsHidden());
	TestFalse(TEXT("Carried: nothing to take"), IInteractable::Execute_CanInteract(Card, Kate));
	TestFalse(TEXT("Carried: taking it does nothing"), Card->ApplyTo(Kate));
	TestFalse(TEXT("She has no card"), Kate->HasKeycard(TEXT("vault")));

	Archer->SetActorLocation(FVector(300.f, 200.f, 98.f));
	Archer->GetHealthComponent()->ApplyDamage(9999.f, Kate);
	TestFalse(TEXT("Dropped: no longer carried"), Card->IsCarried());
	TestFalse(TEXT("Dropped: shown"), Card->IsHidden());
	const FVector Feet = Archer->GetActorLocation() - FVector(0.f, 0.f, Archer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	TestTrue(FString::Printf(TEXT("At his feet (%s, feet %s)"), *Card->GetActorLocation().ToCompactString(), *Feet.ToCompactString()),
		FVector::Dist2D(Card->GetActorLocation(), Feet) <= 41.f
		&& FMath::Abs(Card->GetActorLocation().Z - Feet.Z - Card->HoverHeight) < 1.f);
	TestTrue(TEXT("Dropped: she can take it"), IInteractable::Execute_CanInteract(Card, Kate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorArcherCone, "Hawkeye.Interior.CalmArcherSeesOnlyHisCone", HawkeyeInteriorAITest::Flags)

bool FHawkeyeInteriorArcherCone::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	for (const bool bAllRound : { true, false })
	{
		for (const bool bInFront : { true, false })
		{
			FHawkeyeTestWorld TestWorld;
			AThugCharacter* Archer = nullptr;
			AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Bow, NAME_None, Archer);
			const FVector Where(bInFront ? 1000.f : -1000.f, 0.f, 0.f);
			AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), Where, FRotator::ZeroRotator));
			if (!Brain || !Kate)
			{
				AddError(TEXT("Could not spawn the archer or Kate."));
				return false;
			}
			Archer->bArcherSeesAllRound = bAllRound;
			Brain->SetTarget(Kate);
			Brain->Think(0.25f);
			const bool bSaw = Archer->GetAlertState() != EThugAlertState::Calm;
			const bool bWant = bAllRound || bInFront;
			TestEqual(FString::Printf(TEXT("%s archer, Kate 10 m %s him: %s"), bAllRound ? TEXT("A rooftop") : TEXT("An indoor"),
				bInFront ? TEXT("in front of") : TEXT("behind"), bWant ? TEXT("seen") : TEXT("unseen")), bSaw, bWant);
			if (!bAllRound && !bInFront)
			{
				// Once he is looking for her he looks all round.
				Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, 1.f);
				Brain->Think(0.25f);
				Brain->Think(0.25f);
				Brain->Think(0.25f);
				TestEqual(TEXT("Suspicious, the indoor archer sees her behind him and confirms"), Archer->GetAlertState(),
					EThugAlertState::Alerted);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorHearingRule, "Hawkeye.Interior.HearingRule", HawkeyeInteriorAITest::Flags)

bool FHawkeyeInteriorHearingRule::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	UWorld* World = TestWorld.Get();
	const FVector Noise(0.f, 0.f, 90.f);
	const FVector Ear(600.f, 0.f, 150.f);
	using namespace HawkeyeThugHearing;
	TestTrue(TEXT("Open air, 6 m, range 7.2 m: heard straight"), Hear(World, Noise, Ear, 720.f, {}).bDirect);
	TestFalse(TEXT("Out of range: not heard"), Hear(World, Noise, Ear, 500.f, {}).bHeard);

	AHawkeyeTestBlocker* Counter = Cast<AHawkeyeTestBlocker>(TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(),
		FVector(300.f, 0.f, 60.f), FRotator::ZeroRotator));
	Counter->SetExtent(FVector(20.f, 300.f, 60.f));
	TestTrue(TEXT("Over a 1.2 m counter (a balustrade): heard"), Hear(World, Noise, Ear, 720.f, {}).bHeard);

	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(),
		FVector(300.f, 0.f, 160.f), FRotator::ZeroRotator));
	Wall->SetExtent(FVector(10.f, 300.f, 170.f));
	const FHeardNoise Walled = Hear(World, Noise, Ear, 720.f, {});
	TestFalse(TEXT("Through a wall to the ceiling, no way round (no navmesh): not heard"), Walled.bHeard);
	TestEqual(TEXT("No path"), Walled.PathLength, -1.f);

	TestEqual(TEXT("A takedown makes a 0.6 noise (the body drop)"), GetDefault<UTakedownComponent>()->TakedownNoiseLoudness, 0.6f);
	TestEqual(TEXT("A closed door quarters the range"), ClosedDoorFactor, 0.25f);
	return true;
}

// --- The sample in a real world ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorEnemiesInPlace, "Hawkeye.Interior.EnemiesInPlace", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorEnemiesInPlace::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	AddOpenSample(this);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		TestEqual(TEXT("Four thugs"), All<AThugCharacter>(World).Num(), 4);
		struct FWant { const TCHAR* Id; EThugWeapon Weapon; const TCHAR* Group; const FBox2D* Room; float FloorZ; };
		const FWant Wants[] = { { TEXT("lobby"), EThugWeapon::Fists, TEXT("front"), &Lobby, 0.f },
			{ TEXT("hall"), EThugWeapon::Bat, TEXT("hall"), &Hall, 0.f }, { TEXT("gallery"), EThugWeapon::Pistol, TEXT("hall"), &Gallery, FloorHeight },
			{ TEXT("vault"), EThugWeapon::Bow, TEXT("hall"), &Hall, 0.f } };
		for (const FWant& Want : Wants)
		{
			const AThugCharacter* Thug = Enemy(World, Want.Id);
			if (!TestNotNull(FString::Printf(TEXT("The %s enemy"), Want.Id), Thug))
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("%s: weapon"), Want.Id), Thug->Weapon == Want.Weapon);
			TestEqual(FString::Printf(TEXT("%s: crew"), Want.Id), Thug->AlertGroup, FName(Want.Group));
			TestTrue(FString::Printf(TEXT("%s: in his room on his floor (%s)"), Want.Id, *Feet(Thug).ToCompactString()),
				Want.Room->IsInside(FVector2D(Thug->GetActorLocation())) && FMath::Abs(Feet(Thug).Z - Want.FloorZ) < 20.f);
			TestEqual(FString::Printf(TEXT("%s: two patrol points"), Want.Id), Thug->PatrolPoints.Num(), 2);
			TestFalse(FString::Printf(TEXT("%s: nobody indoors sees all round"), Want.Id), Thug->bArcherSeesAllRound);
			TestNotNull(FString::Printf(TEXT("%s: driven by a thug brain"), Want.Id), BrainOf(Thug));
		}
		const AThugCharacter* Gunner = Enemy(World, TEXT("gallery"));
		TestTrue(TEXT("The gunner's points face the hall while he waits"), Gunner && Gunner->PatrolPoints.Num() == 2
			&& Gunner->PatrolPoints[0]->ActorHasTag(TEXT("PatrolFacing")) && FMath::Abs(Gunner->PatrolPoints[0]->GetActorRotation().Yaw) < 1.f);
		const AThugCharacter* Archer = Enemy(World, TEXT("vault"));
		const APickupActor* Card = Archer && Archer->CarriedPickups.Num() == 1 ? Archer->CarriedPickups[0].Get() : nullptr;
		TestTrue(TEXT("The archer carries the vault keycard, out of sight"), Card && Card->KeycardId == FName(TEXT("vault"))
			&& Card->IsCarried() && Card->IsHidden());
		TestEqual(TEXT("No other pickup in the building"), All<APickupActor>(World).Num(), 1);

		const ADoorActor* VaultDoorActor = DoorNear(World, VaultDoor);
		TestTrue(TEXT("The vault door is locked, its link out of the navmesh (Null)"), VaultDoorActor && VaultDoorActor->bLocked
			&& VaultDoorActor->NavLink && VaultDoorActor->NavLink->PointLinks.Num() == 1
			&& VaultDoorActor->NavLink->PointLinks[0].GetAreaClass() == UNavArea_Null::StaticClass());
		const ADoorActor* OfficeDoorActor = DoorNear(World, OfficeDoor);
		TestTrue(TEXT("The office door has its point link"), OfficeDoorActor && OfficeDoorActor->NavLink && OfficeDoorActor->NavLink->PointLinks.Num() == 1);
		return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorSightLines, "Hawkeye.Interior.SightLines", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorSightLines::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	struct FCase
	{
		const TCHAR* What;
		const TCHAR* Who;
		FVector ThugFeet;
		float ThugYaw;
		FVector KateFeet;
		bool bOpenOfficeDoor;
		bool bSeen;
		bool bOnPatrol;
	};
	// The gunner waiting at his south point by the gallery rail (facing the hall, his gaze pitched down 25
	// degrees) with her 5.5 m out on the hall floor, 4 m below; a thug in the office looking north at her
	// 5.5 m off, through the solid wall, the shut door, then the open doorway; a thug in the lobby turned
	// away from her (his eyes follow his body, whatever way he spawned facing).
	TSharedRef<TArray<FCase>> Cases = MakeShared<TArray<FCase>>(TArray<FCase>{
		{ TEXT("gallery to the hall floor, over the rail"), TEXT("gallery"), FVector(1150.f, 1150.f, FloorHeight), 0.f, FVector(1700.f, 1150.f, 0.f), false, true, true },
		{ TEXT("office to the lobby through the wall"), TEXT("lobby"), FVector(130.f, 900.f, 0.f), -90.f, FVector(130.f, 350.f, 0.f), false, false, false },
		{ TEXT("office to the lobby through the shut door"), TEXT("lobby"), FVector(250.f, 900.f, 0.f), -90.f, FVector(250.f, 350.f, 0.f), false, false, false },
		{ TEXT("office to the lobby through the open doorway"), TEXT("lobby"), FVector(250.f, 900.f, 0.f), -90.f, FVector(250.f, 350.f, 0.f), true, true, false },
		{ TEXT("lobby, her behind him"), TEXT("hall"), FVector(200.f, 300.f, 0.f), 90.f, FVector(200.f, 60.f, 0.f), false, false, false },
		{ TEXT("lobby, turned to face her"), TEXT("hall"), FVector(200.f, 300.f, 0.f), -90.f, FVector(200.f, 60.f, 0.f), false, true, false },
	});
	TSharedRef<int32> Index = MakeShared<int32>(0);
	TSharedRef<double> Since = MakeShared<double>(0.0);
	AddOpenSample(this);
	for (int32 Step = 0; Step < Cases->Num(); ++Step)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Cases, Step, Since]()
		{
			UWorld* World = FindWorld();
			const FCase& Case = (*Cases)[Step];
			FreezeThugs(World);
			for (AThugAIController* Brain : All<AThugAIController>(World))
			{
				Brain->SetPacified(true);
			}
			if (Case.bOpenOfficeDoor)
			{
				if (ADoorActor* Door = DoorNear(World, OfficeDoor))
				{
					Door->OpenNow(nullptr);
				}
			}
			AThugCharacter* Thug = Enemy(World, Case.Who);
			PlaceThug(Thug, Case.ThugFeet, Case.ThugYaw);
			Place(World, Case.KateFeet, 0.f);
			Wake(Thug, /*bPacified=*/false, Case.bOnPatrol);
			*Since = World->GetTimeSeconds();
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Cases, Step, Since]()
		{
			UWorld* World = FindWorld();
			const AThugCharacter* Thug = Enemy(World, (*Cases)[Step].Who);
			// Seen, or 3 s of looking without.
			return (Thug && Thug->GetAlertState() != EThugAlertState::Calm) || World->GetTimeSeconds() - *Since > 3.0;
		}, []() { return true; }, 10.f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Cases, Step]()
		{
			UWorld* World = FindWorld();
			const FCase& Case = (*Cases)[Step];
			const AThugCharacter* Thug = Enemy(World, Case.Who);
			const bool bSeen = Thug && Thug->GetAlertState() != EThugAlertState::Calm;
			TestEqual(FString::Printf(TEXT("Sight %s: %s (he is %s)"), Case.What, Case.bSeen ? TEXT("seen") : TEXT("unseen"),
				Thug ? StateName(Thug->GetAlertState()) : TEXT("missing")), bSeen, Case.bSeen);
			return true;
		}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorHearingWalls, "Hawkeye.Interior.HearingThroughWalls", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorHearingWalls::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	AddOpenSample(this);
	// The rule on the sample's walls, straight: a noise in the office by its door, heard in the lobby just
	// past the door and in the hall behind two walls, 7.2 m of range (a takedown).
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		ADoorActor* Door = DoorNear(World, OfficeDoor);
		if (!TestTrue(TEXT("The office door, shut"), Door && !Door->IsOpen()))
		{
			return true;
		}
		const FVector Noise(250.f, 800.f, 96.f);
		const FVector LobbyEar(250.f, 400.f, 156.f);
		const FVector HallEar(900.f, 800.f, 156.f);
		using namespace HawkeyeThugHearing;
		const FHeardNoise Shut = Hear(World, Noise, LobbyEar, 720.f, {});
		TestFalse(FString::Printf(TEXT("Through the shut office door: not heard (path %.0f cm, %d closed door)"), Shut.PathLength, Shut.ClosedDoors),
			Shut.bHeard);
		TestEqual(TEXT("Its path goes through the shut door"), Shut.ClosedDoors, 1);
		const FHeardNoise HallHeard = Hear(World, Noise, HallEar, 720.f, {});
		TestFalse(FString::Printf(TEXT("Into the hall, 6.5 m through two walls: not heard (path %.0f cm)"), HallHeard.PathLength), HallHeard.bHeard);
		Door->OpenNow(nullptr);
		const FHeardNoise Open = Hear(World, Noise, LobbyEar, 720.f, {});
		TestTrue(FString::Printf(TEXT("Through the open doorway: heard (direct %d, path %.0f cm)"), Open.bDirect ? 1 : 0, Open.PathLength), Open.bHeard);
		const FHeardNoise HallOpen = Hear(World, Noise, HallEar, 720.f, {});
		TestFalse(FString::Printf(TEXT("Into the hall with the door open: still not heard (path %.0f cm)"), HallOpen.PathLength), HallOpen.bHeard);
		return true;
	}));
	return true;
}

/**
 * The same with real takedowns and real ears: a thug taken down in the office with the door shut is not heard
 * by the thug standing in the lobby past it nor by the one in the hall behind the walls; with the door open,
 * the next is heard in the lobby and still not in the hall.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorTakedownHeard, "Hawkeye.Interior.TakedownHeardOnlyThroughAnOpenDoor", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorTakedownHeard::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	AddOpenSample(this);
	for (const bool bOpen : { false, true })
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, bOpen]()
		{
			UWorld* World = FindWorld();
			AHawkeyeCharacter* Kate = FindPlayer(World);
			AThugCharacter* Victim = Enemy(World, bOpen ? TEXT("vault") : TEXT("gallery"));
			AThugCharacter* LobbyThug = Enemy(World, TEXT("lobby"));
			AThugCharacter* HallThug = Enemy(World, TEXT("hall"));
			ADoorActor* Door = DoorNear(World, OfficeDoor);
			if (!Kate || !Victim || !LobbyThug || !HallThug || !Door)
			{
				AddError(TEXT("Missing Kate, a thug or the office door."));
				return true;
			}
			if (bOpen)
			{
				Door->OpenNow(nullptr);
			}
			TestEqual(TEXT("The office door is as the case wants"), Door->IsOpen(), bOpen);
			// Standing, calm, senses on, facing away from the office so only an ear can tell them.
			PlaceThug(LobbyThug, FVector(250.f, 400.f, 0.f), -90.f);
			PlaceThug(HallThug, FVector(900.f, 800.f, 0.f), 0.f);
			Wake(LobbyThug);
			Wake(HallThug);
			// The last case's body is out of the way of this one.
			if (bOpen)
			{
				if (AThugCharacter* Body = Enemy(World, TEXT("gallery")))
				{
					Body->Destroy();
				}
			}
			PlaceThug(Victim, FVector(250.f, 900.f, 0.f), 90.f);
			Place(World, FVector(250.f, 790.f, 0.f), 90.f);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.4f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, bOpen]()
		{
			UWorld* World = FindWorld();
			AHawkeyeCharacter* Kate = FindPlayer(World);
			const bool bDone = Kate && Kate->GetTakedownComponent() && Kate->GetTakedownComponent()->TryTakedown();
			TestTrue(FString::Printf(TEXT("Takedown in the office (door %s)"), bOpen ? TEXT("open") : TEXT("shut")), bDone);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, bOpen]()
		{
			UWorld* World = FindWorld();
			const AThugCharacter* LobbyThug = Enemy(World, TEXT("lobby"));
			const AThugCharacter* HallThug = Enemy(World, TEXT("hall"));
			const EThugAlertState LobbyState = LobbyThug ? LobbyThug->GetAlertState() : EThugAlertState::Calm;
			TestEqual(FString::Printf(TEXT("Door %s: the lobby thug past it %s (he is %s)"), bOpen ? TEXT("open") : TEXT("shut"),
				bOpen ? TEXT("hears it") : TEXT("does not"), StateName(LobbyState)), LobbyState != EThugAlertState::Calm, bOpen);
			TestEqual(FString::Printf(TEXT("Door %s: the hall thug behind the walls does not (he is %s)"), bOpen ? TEXT("open") : TEXT("shut"),
				HallThug ? StateName(HallThug->GetAlertState()) : TEXT("missing")), HallThug ? HallThug->GetAlertState() : EThugAlertState::Alerted,
				EThugAlertState::Calm);
			FreezeThugs(World);
			return true;
		}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorLockedVault, "Hawkeye.Interior.LockedVaultClosesThePath", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorLockedVault::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	AddOpenSample(this);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		// The vault floor inside the door has navmesh of its own: the question is only whether it joins the hall's.
		FNavLocation InVault;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		TestTrue(TEXT("There is navmesh just inside the vault door"), Nav && Nav->ProjectPointToNavigation(FVector(2280.f, 1150.f, 60.f), InVault,
			FVector(50.f, 50.f, 100.f)));
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(2000.f, 1150.f, 60.f),
			FVector(2280.f, 1150.f, 60.f));
		TestTrue(FString::Printf(TEXT("Locked: no whole path from the hall into the vault (%s)"), Path && Path->IsValid()
			? (Path->IsPartial() ? TEXT("partial") : TEXT("complete")) : TEXT("none")), !Path || !Path->IsValid() || Path->IsPartial());
		// A noise in the vault: the bat thug by its door goes to look, and must not walk into the locked door.
		AThugCharacter* Thug = Enemy(World, TEXT("hall"));
		PlaceThug(Thug, FVector(2000.f, 1150.f, 0.f), 0.f);
		Wake(Thug);
		BrainOf(Thug)->ReportStimulus(EStimulusKind::Hearing, FVector(2300.f, 1150.f, 90.f), true, 1.f);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		// Whatever path he walks, none of it is on the far side of the locked door.
		const AThugAIController* Brain = BrainOf(Enemy(FindWorld(), TEXT("hall")));
		const UPathFollowingComponent* Following = Brain ? Brain->GetPathFollowingComponent() : nullptr;
		const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
		int32 Inside = 0;
		for (const FNavPathPoint& Point : Path.IsValid() ? Path->GetPathPoints() : TArray<FNavPathPoint>())
		{
			Inside += Vault.IsInside(FVector2D(Point.Location)) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("Investigating the vault, his path (%d points) stops at the locked door"),
			Path.IsValid() ? Path->GetPathPoints().Num() : 0), Inside, 0);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		const AThugCharacter* Thug = Enemy(World, TEXT("hall"));
		TestTrue(FString::Printf(TEXT("He is still in the hall (%s)"), Thug ? *Thug->GetActorLocation().ToCompactString() : TEXT("?")),
			Thug && Thug->GetActorLocation().X < 2200.f && Hall.IsInside(FVector2D(Thug->GetActorLocation())));
		FreezeThugs(World);
		ADoorActor* Door = DoorNear(World, VaultDoor);
		if (!TestNotNull(TEXT("The vault door"), Door))
		{
			return true;
		}
		Door->OpenNow(FindPlayer(World));
		TestTrue(TEXT("Open, its link is back in (Default)"), Door->NavLink && Door->NavLink->PointLinks.Num() == 1
			&& Door->NavLink->PointLinks[0].GetAreaClass() == UNavArea_Default::StaticClass());
		return true;
	}));
	// The link's tile is rebuilt with it in.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, FVector(2000.f, 1150.f, 60.f),
			FVector(2280.f, 1150.f, 60.f));
		TestTrue(FString::Printf(TEXT("Open: a whole path into the vault (%s, %d points, ends %s)"), Path && Path->IsValid()
			? (Path->IsPartial() ? TEXT("partial") : TEXT("complete")) : TEXT("none"), Path ? Path->PathPoints.Num() : 0,
			Path && Path->PathPoints.Num() ? *Path->PathPoints.Last().ToCompactString() : TEXT("-")), Path && Path->IsValid() && !Path->IsPartial());
		return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorStairClimb, "Hawkeye.Interior.ThugTakesTheStair", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorStairClimb::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	TSharedRef<double> Started = MakeShared<double>(0.0);
	AddOpenSample(this);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Started]()
	{
		UWorld* World = FindWorld();
		AThugCharacter* Thug = Enemy(World, TEXT("hall"));
		AThugAIController* Brain = BrainOf(Thug);
		if (!TestNotNull(TEXT("The bat thug"), Brain))
		{
			return true;
		}
		// Thinking off, so nothing but this order moves him: the hall floor to the gallery, by the lobby,
		// the stair and its upper door (shut; it opens for him).
		PlaceThug(Thug, FVector(1500.f, 700.f, 0.f), 180.f);
		const EPathFollowingRequestResult::Type Result = Brain->MoveToLocation(FVector(1000.f, 700.f, FloorHeight + 90.f), 50.f);
		TestTrue(TEXT("A move from the hall floor to the gallery is accepted"), Result != EPathFollowingRequestResult::Failed);
		*Started = World->GetTimeSeconds();
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		const AThugCharacter* Thug = Enemy(FindWorld(), TEXT("hall"));
		return Thug && Gallery.IsInside(FVector2D(Thug->GetActorLocation())) && Feet(Thug).Z > FloorHeight - 20.f
			&& FVector::Dist2D(Thug->GetActorLocation(), FVector(1000.f, 700.f, 0.f)) < 120.f;
	}, [this]()
	{
		const AThugCharacter* Thug = Enemy(FindWorld(), TEXT("hall"));
		AddError(FString::Printf(TEXT("The bat thug did not reach the gallery within 40 s (at %s)."), Thug ? *Thug->GetActorLocation().ToCompactString() : TEXT("?")));
		return true;
	}, 40.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Started]()
	{
		UWorld* World = FindWorld();
		const ADoorActor* Door = DoorNear(World, GalleryDoor);
		TestTrue(TEXT("The stair's upper door opened for him"), Door && Door->IsOpen());
		AddInfo(FString::Printf(TEXT("Hall floor to the gallery by the stair: %.1f s."), World->GetTimeSeconds() - *Started));
		return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorRagdollWalls, "Hawkeye.Interior.RagdollStaysInTheRoom", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorRagdollWalls::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	AddOpenSample(this);
	// A blast throw (1500 cm/s), a heavy strike's knockdown and a death ragdoll thrown at 1500 cm/s, each by a
	// different thug 50 cm from the office's east wall (x 500, 20 cm thick) and sent straight at it.
	struct FThrow { const TCHAR* What; const TCHAR* Who; float Y; float Launch; bool bKill; };
	static const FThrow Throws[] = { { TEXT("a blast"), TEXT("hall"), 700.f, 1500.f, false },
		{ TEXT("a heavy strike"), TEXT("gallery"), 1010.f, -1.f, false }, { TEXT("a death"), TEXT("lobby"), 1200.f, 1500.f, true } };
	for (const FThrow& Throw : Throws)
	{
		const FThrow Copy = Throw;
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Copy]()
		{
			UWorld* World = FindWorld();
			AThugCharacter* Thug = Enemy(World, Copy.Who);
			AHawkeyeCharacter* Kate = FindPlayer(World);
			PlaceThug(Thug, FVector(440.f, Copy.Y, 0.f), 180.f);
			Place(World, FVector(100.f, Copy.Y, 0.f), 0.f);
			if (!Thug || !Kate)
			{
				return true;
			}
			if (Copy.bKill)
			{
				Thug->GetHealthComponent()->ApplyDamage(9999.f, Kate);
				if (USkeletalMeshComponent* Body = Thug->GetMesh(); Body && Body->IsSimulatingPhysics())
				{
					Body->SetAllPhysicsLinearVelocity(FVector(Copy.Launch, 0.f, 300.f));
				}
			}
			else
			{
				Thug->KnockdownFor(Kate, 3.f, Copy.Launch);
			}
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Copy]()
		{
			const AThugCharacter* Thug = Enemy(FindWorld(), Copy.Who);
			const USkeletalMeshComponent* Body = Thug ? Thug->GetMesh() : nullptr;
			const FVector Pelvis = Body && Body->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE ? Body->GetBoneLocation(TEXT("pelvis"))
				: (Thug ? Thug->GetActorLocation() : FVector::ZeroVector);
			TestTrue(FString::Printf(TEXT("After %s at the wall he is still in the office: pelvis %s, capsule %s"), Copy.What,
				*Pelvis.ToCompactString(), Thug ? *Thug->GetActorLocation().ToCompactString() : TEXT("?")),
				Thug && Office.IsInside(FVector2D(Pelvis)) && Pelvis.X < 500.f - 5.f && Office.IsInside(FVector2D(Thug->GetActorLocation()))
				&& Pelvis.Z > -20.f);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorGalleryCover, "Hawkeye.Interior.GalleryCover", HawkeyeInteriorAITest::MapFlags)

bool FHawkeyeInteriorGalleryCover::RunTest(const FString& Parameters)
{
	using namespace HawkeyeInteriorAITest;
	using namespace HawkeyeInteriorKit;
	AddOpenSample(this);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		UWorld* World = FindWorld();
		AThugCharacter* Gunner = Enemy(World, TEXT("gallery"));
		AThugAIController* Brain = BrainOf(Gunner);
		AHawkeyeCharacter* Kate = FindPlayer(World);
		if (!Brain || !Kate)
		{
			AddError(TEXT("No gunner or no Kate."));
			return true;
		}
		// The gunner at the rail over the hall, Kate on the hall floor 6.5 m out.
		PlaceThug(Gunner, FVector(1150.f, 700.f, FloorHeight), 0.f);
		Place(World, FVector(1800.f, 700.f, 0.f), 180.f);
		Brain->SetTarget(Kate);
		FVector Cover;
		const bool bFound = Brain->FindCoverPoint(Kate->GetActorLocation(), Cover);
		if (!TestTrue(TEXT("He finds cover from the hall floor"), bFound))
		{
			return true;
		}
		AddInfo(FString::Printf(TEXT("Gallery cover from %s: %s"), *Kate->GetActorLocation().ToCompactString(), *Cover.ToCompactString()));
		TestTrue(FString::Printf(TEXT("On the gallery, his level (%s)"), *Cover.ToCompactString()),
			Gallery.IsInside(FVector2D(Cover)) && FMath::Abs(Cover.Z - Gunner->GetActorLocation().Z) < 150.f);
		FNavLocation OnNav;
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		TestTrue(TEXT("Somewhere he can stand"), Nav && Nav->ProjectPointToNavigation(Cover, OnNav, FVector(50.f, 50.f, 150.f)));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(InteriorCover), false, Gunner);
		TestFalse(TEXT("Not inside a wall or a prop"), World->OverlapBlockingTestByChannel(Cover, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(25.f), Params));
		FCollisionQueryParams Line(SCENE_QUERY_STAT(InteriorCoverLine), false, Kate);
		Line.AddIgnoredActor(Gunner);
		TestTrue(TEXT("Out of her line"), World->LineTraceTestByChannel(Kate->GetActorLocation() + FVector(0.f, 0.f, 60.f), Cover, ECC_Visibility, Line));
		return true;
	}));
	return true;
}

#endif
