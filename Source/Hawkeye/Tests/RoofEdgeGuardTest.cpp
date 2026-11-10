#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The roof-edge guard (claude-docs/gameplay-semantics.md, traversal; 2026-09-29 after a 1679 cm fall in Cameron's
 * log): the jump key refuses a parapet with a roof edge behind it, and the plain jump that used to follow could carry
 * her over it. Now one press heading for it mantles her onto its top and she stays there (walking off it only back
 * toward the roof); where its top has no room, the plain jump is held short of it; a second press within 0.3 s
 * leaps over after all. Kate stands at the origin facing +X with her feet at z = 0 on a roof that ends at x = 130,
 * a 90 cm, 30 cm thick parapet on its edge (front face at x = 100), and the street 600 cm below. The world does not
 * tick; moves are advanced by hand.
 */
namespace HawkeyeRoofEdgeGuardTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
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

	/** The roof, the parapet and (DropBelow cm under the roof) the street; Kate on the roof, facing the parapet. */
	static AHawkeyeAimTestCharacter* SpawnScene(const FHawkeyeTestWorld& TestWorld, float DropBelow, bool bParapet = true)
	{
		SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(130.f, 3000.f, 0.f));
		SpawnBox(TestWorld, FVector(130.f, -3000.f, -DropBelow - 100.f), FVector(3000.f, 3000.f, -DropBelow));
		if (bParapet)
		{
			SpawnBox(TestWorld, FVector(100.f, -1000.f, 0.f), FVector(130.f, 1000.f, 90.f));
		}
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
			AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, HalfHeight), FRotator::ZeroRotator));
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	/** Walking at the parapet: the stick pushed toward +X (as Input_Move leaves it) and 250 cm/s that way. */
	static void WalkAtIt(ACharacter* Kate)
	{
		Kate->AddMovementInput(FVector::ForwardVector, 1.f);
		Kate->ConsumeMovementInputVector();
		Kate->GetCharacterMovement()->Velocity = FVector(250.f, 0.f, 0.f);
	}

	static float Feet(const ACharacter* Kate)
	{
		return Kate->GetActorLocation().Z - HalfHeight;
	}

	/** Runs the current move to its end in 1/60 s steps; returns the lowest feet height seen while over the parapet. */
	static float RunMove(ACharacter* Kate, UParkourComponent* Parkour, float MaxSeconds = 3.f)
	{
		float LowestOver = BIG_NUMBER;
		for (float T = 0.f; T < MaxSeconds && Parkour->IsPerformingMove(); T += 1.f / 60.f)
		{
			Parkour->AdvanceMove(1.f / 60.f);
			const float X = Kate->GetActorLocation().X;
			if (X > 100.f - 30.f && X < 130.f + 30.f)
			{
				LowestOver = FMath::Min(LowestOver, Feet(Kate));
			}
		}
		return LowestOver;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRoofEdgeGuardOnePress, "Hawkeye.Parkour.RoofEdgeGuard.OnePressStopsOnTheParapet",
	HawkeyeRoofEdgeGuardTest::Flags)

bool FHawkeyeRoofEdgeGuardOnePress::RunTest(const FString& Parameters)
{
	using namespace HawkeyeRoofEdgeGuardTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 600.f);
	UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
	if (!Parkour)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	// What the fan sees: a refused parapet with a 600 cm drop past it (the jump key allows a 400 cm one).
	FHawkeyeParkourObstacle Obstacle;
	EHawkeyeParkourMove Move = EHawkeyeParkourMove::None;
	FString WhyNot;
	FHawkeyeParkourObstacle RoofEdge;
	TestFalse(TEXT("The fan refuses the parapet"),
		Parkour->ProbeJumpFan(FVector::ForwardVector, Parkour->ManualTriggerDistance, false, Obstacle, Move, WhyNot, &RoofEdge));
	TestTrue(TEXT("And names it a roof edge"), RoofEdge.bFound && Parkour->IsRoofEdgeParapet(RoofEdge));
	TestEqual(TEXT("600 cm down past it"), RoofEdge.FarSideDrop, 600.f, 5.f);
	FVector Stand;
	TestTrue(TEXT("Its top has room for her"), Parkour->FindParapetStand(RoofEdge, Stand));

	WalkAtIt(Kate);
	Kate->Jump();
	TestTrue(TEXT("One press: the guard's mantle"), Parkour->IsGuardMantle());
	TestEqual(TEXT("A mantle"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Mantle);
	TestEqual(TEXT("Ours, not the sample's"), Parkour->GetLastRoute(), EHawkeyeParkourRoute::Procedural);
	RunMove(Kate, Parkour);
	TestFalse(TEXT("The mantle is over"), Parkour->IsPerformingMove());
	TestEqual(TEXT("She stands on the parapet top"), Feet(Kate), 90.f, 4.f);
	TestTrue(TEXT("Over the parapet, not past it"), Kate->GetActorLocation().X > 100.f && Kate->GetActorLocation().X < 130.f);
	TestTrue(TEXT("Perched"), Parkour->IsPerchedOnParapet());
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	TestFalse(TEXT("Walking off it is off"), Movement->bCanWalkOffLedges);

	// The stick still at the drop: the top's edge holds her; back toward the roof it lets her step down.
	Kate->AddMovementInput(FVector::ForwardVector, 1.f);
	Kate->ConsumeMovementInputVector();
	Parkour->TickRoofEdgeGuard(1.f / 60.f);
	TestFalse(TEXT("Toward the drop she cannot walk off"), Movement->bCanWalkOffLedges);
	Kate->AddMovementInput(-FVector::ForwardVector, 1.f);
	Kate->ConsumeMovementInputVector();
	Parkour->TickRoofEdgeGuard(1.f / 60.f);
	TestTrue(TEXT("Back toward the roof she can"), Movement->bCanWalkOffLedges);
	Kate->SetActorLocation(FVector(0.f, 0.f, HalfHeight + 2.f));
	Parkour->TickRoofEdgeGuard(1.f / 60.f);
	TestFalse(TEXT("Back on the roof the perch is over"), Parkour->IsPerchedOnParapet());
	TestTrue(TEXT("And walking off ledges is back"), Movement->bCanWalkOffLedges);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRoofEdgeGuardDoubleTap, "Hawkeye.Parkour.RoofEdgeGuard.DoubleTapLeaps",
	HawkeyeRoofEdgeGuardTest::Flags)

bool FHawkeyeRoofEdgeGuardDoubleTap::RunTest(const FString& Parameters)
{
	using namespace HawkeyeRoofEdgeGuardTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 600.f);
	UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
	if (!Parkour)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	WalkAtIt(Kate);
	Kate->Jump();
	TestTrue(TEXT("The first press mantles"), Parkour->IsGuardMantle());
	// 0.15 s later, the second press.
	for (int32 Step = 0; Step < 9; ++Step)
	{
		Parkour->AdvanceMove(1.f / 60.f);
		Parkour->TickRoofEdgeGuard(1.f / 60.f);
	}
	Kate->Jump();
	TestTrue(TEXT("The double tap: a leap"), Parkour->IsGuardLeap());
	TestFalse(TEXT("No longer the mantle"), Parkour->IsGuardMantle());
	const float LowestOver = RunMove(Kate, Parkour);
	TestTrue(TEXT("Her feet clear the parapet top on the way over"), LowestOver > 90.f);
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	TestTrue(TEXT("Past its back edge"), Kate->GetActorLocation().X > 130.f + 30.f);
	TestEqual(TEXT("Falling"), static_cast<int32>(Movement->MovementMode.GetValue()), static_cast<int32>(MOVE_Falling));
	TestTrue(TEXT("Out over the drop at leap speed"), Movement->Velocity.X >= Parkour->GuardLeapSpeed - 1.f);
	TestFalse(TEXT("Not perched"), Parkour->IsPerchedOnParapet());

	// A second press after the window is not a double tap: during the mantle it is dropped, not buffered.
	const FHawkeyeTestWorld Late;
	AHawkeyeAimTestCharacter* Kate2 = SpawnScene(Late, 600.f);
	UParkourComponent* Parkour2 = Kate2->GetParkourComponent();
	WalkAtIt(Kate2);
	Kate2->Jump();
	for (int32 Step = 0; Step < 24; ++Step)
	{
		Parkour2->AdvanceMove(1.f / 60.f);
		Parkour2->TickRoofEdgeGuard(1.f / 60.f);
	}
	Kate2->Jump();
	TestFalse(TEXT("0.4 s later: no leap"), Parkour2->IsGuardLeap());
	TestFalse(TEXT("And no jump buffered to fire off the top"), Parkour2->IsJumpBuffered());
	RunMove(Kate2, Parkour2);
	TestTrue(TEXT("She ends on the top"), Parkour2->IsPerchedOnParapet() && FMath::IsNearlyEqual(Feet(Kate2), 90.f, 4.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRoofEdgeGuardHeldJump, "Hawkeye.Parkour.RoofEdgeGuard.NoRoomOnTopHoldsTheJumpShort",
	HawkeyeRoofEdgeGuardTest::Flags)

bool FHawkeyeRoofEdgeGuardHeldJump::RunTest(const FString& Parameters)
{
	using namespace HawkeyeRoofEdgeGuardTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 600.f);
	// A sign hung 60 cm over the back half of the parapet top: open air at its front edge, but no room for her on it.
	SpawnBox(TestWorld, FVector(118.f, -1000.f, 150.f), FVector(135.f, 1000.f, 400.f));
	UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
	if (!Parkour)
	{
		AddError(TEXT("Could not spawn the scene."));
		return false;
	}
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	WalkAtIt(Kate);
	Kate->Jump();
	TestFalse(TEXT("No mantle: nowhere to stand"), Parkour->IsPerformingMove());
	TestTrue(TEXT("A plain jump, held short"), Parkour->IsGuardFlight());
	TestTrue(TEXT("Nothing left toward the drop"), Movement->Velocity.X <= 0.f);
	// In the air, the stick's air control pushing her at it: held at nothing toward it, every tick.
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(320.f, 40.f, 300.f);
	Parkour->TickRoofEdgeGuard(1.f / 60.f);
	TestTrue(TEXT("Held short in the air"), Movement->Velocity.X <= 0.f);
	TestEqual(TEXT("The rest of her speed is hers"), Movement->Velocity.Z, 300.0, 0.01);

	// The double tap in the air: over it after all.
	Kate->Jump();
	TestTrue(TEXT("A double tap in the air leaps"), Parkour->IsGuardLeap());
	TestFalse(TEXT("No longer held"), Parkour->IsGuardFlight());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeRoofEdgeGuardLeavesOthers, "Hawkeye.Parkour.RoofEdgeGuard.OnlyAtARoofEdgeParapet",
	HawkeyeRoofEdgeGuardTest::Flags)

bool FHawkeyeRoofEdgeGuardLeavesOthers::RunTest(const FString& Parameters)
{
	using namespace HawkeyeRoofEdgeGuardTest;
	{
		// An open roof edge (no parapet): a deliberate leap is a plain jump, untouched.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 600.f, /*bParapet=*/false);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		WalkAtIt(Kate);
		Kate->Jump();
		TestFalse(TEXT("Open edge: no move"), Parkour->IsPerformingMove());
		TestFalse(TEXT("Open edge: not held"), Parkour->IsGuardFlight());
		TestEqual(TEXT("Open edge: her speed is kept"), Kate->GetCharacterMovement()->Velocity.X, 250.0, 0.01);
	}
	{
		// A parapet over a 300 cm drop: still the jump key's vault.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 300.f);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		WalkAtIt(Kate);
		Kate->Jump();
		TestEqual(TEXT("300 cm drop: a vault"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Vault);
		TestFalse(TEXT("Not the guard's"), Parkour->IsGuardMantle());
	}
	{
		// Standing still at the roof-edge parapet with the stick let go: a hop in place, nothing to guard.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 600.f);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Kate->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		Kate->Jump();
		TestFalse(TEXT("Still, stick centred: no mantle"), Parkour->IsPerformingMove());
		TestFalse(TEXT("And no guard"), Parkour->IsGuardFlight());
	}
	{
		// Sprinting at it still never auto vaults off the roof.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 600.f);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Kate->TestSetSprinting(true);
		Kate->GetCharacterMovement()->Velocity = FVector(700.f, 0.f, 0.f);
		TestFalse(TEXT("The sprint trigger refuses"), Parkour->TryParkour(true));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
