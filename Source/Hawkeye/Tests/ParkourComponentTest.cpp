#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Vault, mantle and ledge grab (claude-docs/gameplay-semantics.md, traversal): which move each
 * height and shape calls for, what the probe reads off box obstacles, and the procedural moves
 * themselves (a plain character has no AnimBP, so the capsule follows the plain arc). Kate stands
 * at the origin facing +X with her feet at z = 0 on a big floor; obstacles start 100 cm ahead.
 */
namespace HawkeyeParkourTest
{
	static constexpr float HalfHeight = 88.f;

	static AHawkeyeTestBlocker* SpawnBox(const FHawkeyeTestWorld& TestWorld, const FVector& Min, const FVector& Max)
	{
		AHawkeyeTestBlocker* Box = Cast<AHawkeyeTestBlocker>(
			TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), (Min + Max) * 0.5f, FRotator::ZeroRotator));
		if (Box)
		{
			Box->SetExtent((Max - Min) * 0.5f);
		}
		return Box;
	}

	/** Kate on a floor whose top is z = 0, feet on it (walking floats them up to 2.4 cm), facing +X. */
	static AHawkeyeAimTestCharacter* SpawnKateOnFloor(const FHawkeyeTestWorld& TestWorld, bool bFloor = true)
	{
		if (bFloor)
		{
			SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
		}
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
			AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, HalfHeight), FRotator::ZeroRotator));
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	/** An obstacle whose front face is at x = 100, Depth deep, Height tall, 300 wide. */
	static AHawkeyeTestBlocker* SpawnObstacle(const FHawkeyeTestWorld& TestWorld, float Height, float Depth, float Front = 100.f)
	{
		return SpawnBox(TestWorld, FVector(Front, -150.f, 0.f), FVector(Front + Depth, 150.f, Height));
	}

	static float Feet(const ACharacter* Kate)
	{
		return Kate->GetActorLocation().Z - HalfHeight;
	}

	static int32 ModeOf(const ACharacter* Character)
	{
		return static_cast<int32>(Character->GetCharacterMovement()->MovementMode.GetValue());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourChooseMove, "Hawkeye.Parkour.ChooseMoveByHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourChooseMove::RunTest(const FString& Parameters)
{
	const UParkourComponent* Parkour = NewObject<UParkourComponent>();
	using EMove = EHawkeyeParkourMove;
	TestEqual(TEXT("59 cm: nothing"), Parkour->ChooseMove(59.f, true, true), EMove::None);
	TestEqual(TEXT("60 cm, clear beyond: vault"), Parkour->ChooseMove(60.f, true, false), EMove::Vault);
	TestEqual(TEXT("110 cm, clear beyond: vault"), Parkour->ChooseMove(110.f, true, true), EMove::Vault);
	TestEqual(TEXT("90 cm, not clear, can stand: mantle"), Parkour->ChooseMove(90.f, false, true), EMove::Mantle);
	TestEqual(TEXT("90 cm, not clear, nowhere to stand: nothing"), Parkour->ChooseMove(90.f, false, false), EMove::None);
	TestEqual(TEXT("111 cm is too high to vault even when clear"), Parkour->ChooseMove(111.f, true, false), EMove::None);
	TestEqual(TEXT("111 cm with a standing surface: mantle"), Parkour->ChooseMove(111.f, true, true), EMove::Mantle);
	TestEqual(TEXT("200 cm: mantle"), Parkour->ChooseMove(200.f, false, true), EMove::Mantle);
	TestEqual(TEXT("201 cm: ledge grab"), Parkour->ChooseMove(201.f, false, true), EMove::LedgeGrab);
	TestEqual(TEXT("260 cm: ledge grab, even with nowhere to stand (hang and drop)"),
		Parkour->ChooseMove(260.f, false, false), EMove::LedgeGrab);
	TestEqual(TEXT("261 cm: nothing"), Parkour->ChooseMove(261.f, false, true), EMove::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourDetectVault, "Hawkeye.Parkour.DetectVaultBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourDetectVault::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld);
	SpawnObstacle(TestWorld, 90.f, 60.f);
	UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
	if (!TestNotNull(TEXT("Kate has a parkour component"), Parkour))
	{
		return false;
	}

	FHawkeyeParkourObstacle Obstacle;
	TestTrue(TEXT("A 90 cm block 66 cm ahead is found"), Parkour->DetectObstacle(120.f, Obstacle));
	TestEqual(TEXT("Its height (walking floats the capsule up to 2.4 cm)"), Obstacle.Height, 90.f, 3.f);
	TestEqual(TEXT("Its distance from the capsule"), Obstacle.Distance, 66.f, 1.f);
	TestTrue(TEXT("Facing back at Kate"), Obstacle.WallNormal.Equals(FVector(-1.f, 0.f, 0.f), 0.01f));
	TestTrue(TEXT("Back edge within reach"), Obstacle.Depth >= 60.f && Obstacle.Depth <= 75.f);
	TestTrue(TEXT("Clear on the far side"), Obstacle.bClearBeyond);
	TestEqual(TEXT("Landing on the same floor"), Obstacle.LandingDrop, 0.f, 3.f);
	TestTrue(TEXT("Landing beyond the block"), Obstacle.LandingPoint.X > 160.f + 34.f);
	TestEqual(TEXT("Sprinting into it vaults"), Parkour->ChooseMoveFor(Obstacle, true), EHawkeyeParkourMove::Vault);
	TestEqual(TEXT("So does the jump key"), Parkour->ChooseMoveFor(Obstacle, false), EHawkeyeParkourMove::Vault);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourDetectMantleAndLedge, "Hawkeye.Parkour.DetectMantleAndLedge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourDetectMantleAndLedge::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	struct FCase
	{
		float Height;
		float Depth;
		EHawkeyeParkourMove Expected;
		const TCHAR* What;
	};
	const FCase Cases[] = {
		{ 150.f, 150.f, EHawkeyeParkourMove::Mantle, TEXT("150 cm, 150 deep: mantle") },
		{ 90.f, 300.f, EHawkeyeParkourMove::Mantle, TEXT("90 cm but 300 deep (no back edge): mantle") },
		{ 230.f, 300.f, EHawkeyeParkourMove::LedgeGrab, TEXT("230 cm wall: ledge grab") },
		{ 40.f, 60.f, EHawkeyeParkourMove::None, TEXT("40 cm kerb: nothing (a step or a jump)") },
		{ 400.f, 300.f, EHawkeyeParkourMove::None, TEXT("400 cm wall: nothing") },
	};
	for (const FCase& Case : Cases)
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld);
		SpawnObstacle(TestWorld, Case.Height, Case.Depth);
		FHawkeyeParkourObstacle Obstacle;
		Kate->GetParkourComponent()->DetectObstacle(120.f, Obstacle);
		TestEqual(Case.What, Kate->GetParkourComponent()->ChooseMoveFor(Obstacle, true), Case.Expected);
		if (Case.Expected != EHawkeyeParkourMove::None)
		{
			TestEqual(FString(Case.What) + TEXT(" (height)"), Obstacle.Height, Case.Height, 3.f);
			TestTrue(FString(Case.What) + TEXT(" (somewhere to stand on top)"), Obstacle.bStandingSurface);
			TestEqual(FString(Case.What) + TEXT(" (stand point on the top)"), static_cast<float>(Obstacle.StandPoint.Z), Case.Height, 1.f);
		}
	}

	const FHawkeyeTestWorld FarWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(FarWorld);
	SpawnObstacle(FarWorld, 90.f, 60.f, 300.f);
	FHawkeyeParkourObstacle Far;
	TestFalse(TEXT("A block 266 cm away is out of the 120 cm reach"), Kate->GetParkourComponent()->DetectObstacle(120.f, Far));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourParapetDrop, "Hawkeye.Parkour.AutoNeverVaultsOffARoof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourParapetDrop::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	// A roof ending at x = 130 with a 90 cm, 30 cm thick parapet on its edge, and the street 300 cm below.
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(130.f, 3000.f, 0.f));
	SpawnBox(TestWorld, FVector(130.f, -3000.f, -400.f), FVector(3000.f, 3000.f, -300.f));
	SpawnObstacle(TestWorld, 90.f, 30.f);
	AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld, false);
	FHawkeyeParkourObstacle Obstacle;
	TestTrue(TEXT("The parapet is found"), Kate->GetParkourComponent()->DetectObstacle(120.f, Obstacle));
	TestTrue(TEXT("There is a floor beyond"), Obstacle.bClearBeyond);
	TestEqual(TEXT("300 cm down"), Obstacle.LandingDrop, 300.f, 3.f);
	TestFalse(TEXT("Too thin to stand on, too far down to climb over onto"), Obstacle.bStandingSurface);
	TestEqual(TEXT("Sprinting at it does nothing"), Kate->GetParkourComponent()->ChooseMoveFor(Obstacle, true),
		EHawkeyeParkourMove::None);
	TestEqual(TEXT("The jump key vaults it (within the 400 cm roll height)"),
		Kate->GetParkourComponent()->ChooseMoveFor(Obstacle, false), EHawkeyeParkourMove::Vault);
	TestFalse(TEXT("TryParkour(auto) refuses"), Kate->GetParkourComponent()->TryParkour(true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourVaultMove, "Hawkeye.Parkour.VaultMoveLocksInputForItsLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourVaultMove::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld);
	SpawnObstacle(TestWorld, 90.f, 60.f);
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	TestEqual(TEXT("Vault takes 0.5 s"), Parkour->GetMoveSeconds(EHawkeyeParkourMove::Vault), 0.5f);

	TestTrue(TEXT("The jump key starts it"), Parkour->TryParkour(false));
	TestEqual(TEXT("A vault"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Vault);
	TestEqual(TEXT("Moved by the component (no sample on a plain character)"), Parkour->GetLastRoute(),
		EHawkeyeParkourRoute::Procedural);
	TestTrue(TEXT("Input is locked"), Parkour->IsLockingInput() && Kate->IsTraversing());
	TestEqual(TEXT("Flying while it runs"), ModeOf(Kate), static_cast<int32>(MOVE_Flying));

	float PeakFeet = Feet(Kate);
	float FeetOverBlock = -1.f;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Parkour->AdvanceMove(0.1f);
		PeakFeet = FMath::Max(PeakFeet, Feet(Kate));
		if (Kate->GetActorLocation().X > 100.f && Kate->GetActorLocation().X < 160.f)
		{
			FeetOverBlock = FMath::Max(FeetOverBlock, Feet(Kate));
		}
	}
	TestTrue(TEXT("0.4 s in, still locked"), Parkour->IsLockingInput());
	TestTrue(TEXT("Her feet clear the 90 cm top while over the block"), FeetOverBlock >= 90.f);
	Parkour->AdvanceMove(0.1f);
	Parkour->AdvanceMove(0.1f);
	TestFalse(TEXT("Past 0.5 s the lock is gone"), Parkour->IsLockingInput());
	TestFalse(TEXT("And the move is over"), Parkour->IsPerformingMove());
	TestTrue(TEXT("She is on the far side"), Kate->GetActorLocation().X > 160.f + 34.f);
	TestEqual(TEXT("Feet on the floor again"), Feet(Kate), 2.f, 1.f);
	TestEqual(TEXT("Walking"), ModeOf(Kate), static_cast<int32>(MOVE_Walking));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourMantleMove, "Hawkeye.Parkour.MantleEndsOnTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourMantleMove::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld);
	SpawnObstacle(TestWorld, 150.f, 150.f);
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	TestTrue(TEXT("The jump key starts a mantle"), Parkour->TryParkour(false));
	TestEqual(TEXT("Mantle"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Mantle);
	for (int32 Step = 0; Step < 7; ++Step)
	{
		Parkour->AdvanceMove(0.1f);
	}
	TestTrue(TEXT("0.7 s in, still going (0.8 s)"), Parkour->IsPerformingMove());
	Parkour->AdvanceMove(0.1f);
	Parkour->AdvanceMove(0.1f);
	TestFalse(TEXT("Done after 0.8 s"), Parkour->IsPerformingMove());
	TestEqual(TEXT("Feet on the 150 cm top"), Feet(Kate), 152.f, 1.f);
	TestTrue(TEXT("Standing on it, capsule clear of the edge"), Kate->GetActorLocation().X >= 100.f + 34.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourLedge, "Hawkeye.Parkour.LedgeHangClimbAndDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourLedge::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld);
	SpawnObstacle(TestWorld, 230.f, 300.f);
	UParkourComponent* Parkour = Kate->GetParkourComponent();

	TestTrue(TEXT("The jump key grabs a 230 cm ledge"), Parkour->TryParkour(false));
	TestEqual(TEXT("Ledge grab"), Parkour->GetActiveMove(), EHawkeyeParkourMove::LedgeGrab);
	for (int32 Step = 0; Step < 8; ++Step)
	{
		Parkour->AdvanceMove(0.05f);
	}
	TestTrue(TEXT("Hanging after the 0.35 s jump"), Parkour->IsHanging());
	TestTrue(TEXT("Input locked while hanging"), Parkour->IsLockingInput());
	TestEqual(TEXT("Feet HangBelowLedge (145 cm) below the ledge"), Feet(Kate), 230.f - 145.f, 1.f);
	TestEqual(TEXT("Capsule HangBackFromEdge (36 cm) out from the wall"), static_cast<float>(Kate->GetActorLocation().X), 64.f, 1.f);

	Kate->Jump();
	TestEqual(TEXT("Jump climbs"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Climb);
	TestFalse(TEXT("No longer hanging"), Parkour->IsHanging());
	for (int32 Step = 0; Step < 11; ++Step)
	{
		Parkour->AdvanceMove(0.1f);
	}
	TestFalse(TEXT("The 1 s climb is over"), Parkour->IsBusy());
	TestEqual(TEXT("Feet on top"), Feet(Kate), 232.f, 1.f);

	const FHawkeyeTestWorld DropWorld;
	AHawkeyeAimTestCharacter* Dropper = SpawnKateOnFloor(DropWorld);
	SpawnObstacle(DropWorld, 230.f, 300.f);
	UParkourComponent* DropParkour = Dropper->GetParkourComponent();
	DropParkour->TryParkour(false);
	for (int32 Step = 0; Step < 8; ++Step)
	{
		DropParkour->AdvanceMove(0.05f);
	}
	TestTrue(TEXT("Hanging again"), DropParkour->IsHanging());
	TestTrue(TEXT("Drop lets go"), DropParkour->DropFromHang());
	TestFalse(TEXT("Not hanging"), DropParkour->IsHanging());
	TestFalse(TEXT("Nothing locked"), DropParkour->IsLockingInput());
	TestEqual(TEXT("Falling"), ModeOf(Dropper), static_cast<int32>(MOVE_Falling));
	Dropper->GetCharacterMovement()->Velocity = FVector(0.f, 0.f, -200.f);
	TestFalse(TEXT("The same ledge is not caught straight away"), DropParkour->TryCatchLedge());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourCatch, "Hawkeye.Parkour.FallingCatchesALedge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourCatch::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld);
	// A wall topping out at 800, Kate in the air in front of it with her feet 200 below the top.
	SpawnObstacle(TestWorld, 800.f, 300.f);
	Kate->SetActorLocation(FVector(30.f, 0.f, 600.f + HalfHeight));
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	UParkourComponent* Parkour = Kate->GetParkourComponent();

	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.f, 0.f, 150.f);
	TestFalse(TEXT("Still rising: no catch"), Parkour->TryCatchLedge());
	Movement->Velocity = FVector(0.f, 0.f, -150.f);
	TestTrue(TEXT("Falling: caught"), Parkour->TryCatchLedge());
	TestEqual(TEXT("A ledge grab"), Parkour->GetActiveMove(), EHawkeyeParkourMove::LedgeGrab);
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Parkour->AdvanceMove(0.05f);
	}
	TestTrue(TEXT("Hanging after the 0.15 s catch"), Parkour->IsHanging());
	TestEqual(TEXT("Feet 145 cm below the top"), Feet(Kate), 800.f - 145.f, 1.f);
	return true;
}

/**
 * Indoors a wall runs up to the floor above, level with the top of the ceiling slab over her head. A
 * stumble beside it used to catch that as a ledge 330 cm up and hang her from the ceiling; a ledge now
 * needs open air on her side from head height up to it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourCeilingIsNotALedge, "Hawkeye.Parkour.CeilingIsNotALedge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourCeilingIsNotALedge::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourTest;
	for (const bool bCeiling : { false, true })
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKateOnFloor(TestWorld);
		// A wall 330 tall at x = 100, and (indoors) a 20 cm ceiling slab over her from 310 to 330.
		SpawnObstacle(TestWorld, 330.f, 300.f);
		if (bCeiling)
		{
			SpawnBox(TestWorld, FVector(-400.f, -300.f, 310.f), FVector(100.f, 300.f, 330.f));
		}
		Kate->SetActorLocation(FVector(30.f, 0.f, 2.f + HalfHeight));
		UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = FVector(0.f, 0.f, -150.f);
		const bool bCaught = Kate->GetParkourComponent()->TryCatchLedge();
		if (bCeiling)
		{
			TestFalse(TEXT("Under a ceiling, the wall's top behind it is no ledge"), bCaught);
		}
		else
		{
			TestTrue(TEXT("In the open, the same wall's top 328 cm up is caught"), bCaught);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
