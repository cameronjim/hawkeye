#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The hang vocabulary (claude-docs/gameplay-semantics.md, traversal, "Hang"): shimmy clamped to a ledge's ends and
 * stopped by what is in the way, outside and inside corners, the sideways leap and its reach, the hop back, the auto
 * climb and its roof-edge exception, and the hang camera's rules. Boxes, no content: the ledge line is probed along
 * the wall (the district's ledge splines are Hawkeye.Parkour.DistrictHangCorner's). Kate hangs facing +X from faces
 * at x = 100 (normal -X), so her right, the line's Along, is +Y.
 */
namespace HawkeyeHangTest
{
	static constexpr float HalfHeight = 88.f;
	static constexpr float Top = 800.f;
	static constexpr float Tick = 0.05f;

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

	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
		return Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(),
			FVector(-500.f, 0.f, HalfHeight), FRotator::ZeroRotator));
	}

	static float Feet(const ACharacter* Kate)
	{
		return Kate->GetActorLocation().Z - HalfHeight;
	}

	/** One tick of the movement, which runs a move and takes a hang to its held point. */
	static void TickMovement(const UParkourComponent* Parkour)
	{
		if (AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(Parkour->GetOwner()))
		{
			Kate->TestTickMovement(Tick);
		}
	}

	/** One hang tick: the rules, then the movement, unless the rules started a move (it runs from the next tick). */
	static void TickHang(UParkourComponent* Parkour)
	{
		Parkour->AdvanceHang(Tick);
		if (Parkour->IsHanging())
		{
			TickMovement(Parkour);
		}
	}

	/** Runs the current move of ours to its end (a grab, a corner, a leap), at most Seconds. */
	static void FinishMove(UParkourComponent* Parkour, float Seconds = 1.5f)
	{
		for (float Time = 0.f; Time < Seconds && Parkour->IsPerformingMove(); Time += Tick)
		{
			TickMovement(Parkour);
		}
	}

	/** Falling past the face at x = FaceX (facing +X) at Y with feet 200 below the top: she catches it and hangs. */
	static bool HangOn(AHawkeyeAimTestCharacter* Kate, float FaceX, float Y, float TopZ = Top)
	{
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Kate->SetActorLocation(FVector(FaceX - 45.f, Y, TopZ - 200.f + HalfHeight));
		Kate->SetActorRotation(FRotator::ZeroRotator);
		UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = FVector(0.f, 0.f, -150.f);
		if (!Parkour->TryCatchLedge())
		{
			return false;
		}
		FinishMove(Parkour);
		return Parkour->IsHanging();
	}

	/** The stick held at Input (a world direction) for Seconds of hang ticks; any move it starts runs to its end. */
	static void Hold(UParkourComponent* Parkour, const FVector& Input, float Seconds)
	{
		for (float Time = 0.f; Time < Seconds - KINDA_SMALL_NUMBER; Time += Tick)
		{
			Parkour->SetHangInput(Input);
			if (Parkour->IsHanging())
			{
				TickHang(Parkour);
			}
			else if (Parkour->IsPerformingMove())
			{
				TickMovement(Parkour);
			}
		}
		Parkour->SetHangInput(FVector::ZeroVector);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangShimmyEnds, "Hawkeye.Parkour.HangShimmyClampsToTheLedgeEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangShimmyEnds::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	// A 30 cm wall 400 wide: its ends have faces too narrow to hang round, so she stops there.
	SpawnBox(TestWorld, FVector(100.f, -200.f, 0.f), FVector(130.f, 200.f, Top));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging from the wall's top"), HangOn(Kate, 100.f, 0.f)))
	{
		return false;
	}
	const FHawkeyeLedgeLine& Line = Parkour->GetHangLine();
	TestTrue(TEXT("A ledge line"), Line.bValid);
	TestTrue(TEXT("Along is her right (+Y)"), Line.Along.Equals(FVector(0.f, 1.f, 0.f), 0.01f));
	TestEqual(TEXT("The line starts at the wall's left end"), Line.MinAlong, -200.f, 2.f);
	TestEqual(TEXT("And ends at its right end"), Line.MaxAlong, 200.f, 2.f);
	TestEqual(TEXT("Probed, no ledge data in a box world"), Line.DataSegments, 0);

	Hold(Parkour, FVector(0.f, 1.f, 0.f), 0.5f);
	TestEqual(TEXT("Half a second of full stick right: 60 cm at 120 cm/s"), static_cast<float>(Kate->GetActorLocation().Y), 60.f, 2.f);
	TestTrue(TEXT("Still hanging, shimmying"), Parkour->IsHanging() && Parkour->IsShimmying());
	TestEqual(TEXT("Feet still 145 cm below the edge"), Feet(Kate), Top - 145.f, 1.f);
	TestEqual(TEXT("Capsule still 36 cm off the wall"), static_cast<float>(Kate->GetActorLocation().X), 64.f, 1.f);
	Hold(Parkour, FVector(0.f, 0.5f, 0.f), 0.5f);
	TestEqual(TEXT("Half a stick: half the speed (30 cm more)"), static_cast<float>(Kate->GetActorLocation().Y), 90.f, 2.f);
	const FVector2D Hands = Parkour->GetHangHandOffsets();
	TestTrue(TEXT("Mid shimmy the hands are staggered, left on the left"), Hands.X < Hands.Y && !FMath::IsNearlyEqual(-Hands.X, Hands.Y, 1.f));

	Hold(Parkour, FVector(0.f, 1.f, 0.f), 3.f);
	TestEqual(TEXT("Stopped with both hands on the ledge: 25 cm short of its right end"),
		static_cast<float>(Kate->GetActorLocation().Y), 175.f, 2.f);
	TestTrue(TEXT("Still hanging (no corner round a 30 cm end)"), Parkour->IsHanging());
	TestTrue(TEXT("It says why"), Parkour->GetLastHangRefusal().Contains(TEXT("end")));
	Hold(Parkour, FVector(0.f, -1.f, 0.f), 4.f);
	TestEqual(TEXT("And 25 cm short of the left end"), static_cast<float>(Kate->GetActorLocation().Y), -175.f, 2.f);

	// The stick toward the wall at 45 degrees still shimmies; straight at it does not.
	const float Before = Kate->GetActorLocation().Y;
	Hold(Parkour, FVector(1.f, 1.f, 0.f).GetSafeNormal(), 0.25f);
	TestTrue(TEXT("Up and right: along the ledge"), Kate->GetActorLocation().Y > Before + 20.f);
	const float Mid = Kate->GetActorLocation().Y;
	Hold(Parkour, FVector(1.f, 0.f, 0.f), 0.25f);
	TestEqual(TEXT("Straight up: no shimmy"), static_cast<float>(Kate->GetActorLocation().Y), Mid, 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangShimmyObstacles, "Hawkeye.Parkour.HangShimmyStopsAtObstacles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangShimmyObstacles::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(130.f, 400.f, Top));
	// A fire-escape rail standing 60 cm out from the face, level with her body, at y = 100.
	AHawkeyeTestBlocker* Rail = SpawnBox(TestWorld, FVector(40.f, 100.f, Top - 240.f), FVector(100.f, 106.f, Top - 100.f));
	// A chimney on the wall's top, flush with its face, from y = -150 to -100.
	AHawkeyeTestBlocker* Chimney = SpawnBox(TestWorld, FVector(100.f, -150.f, Top), FVector(130.f, -100.f, Top + 150.f));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging"), HangOn(Kate, 100.f, 0.f)))
	{
		return false;
	}
	TestEqual(TEXT("The line runs on past the rail (it is under the top)"), Parkour->GetHangLine().MaxAlong, 400.f, 2.f);
	TestEqual(TEXT("And ends at the chimney on the top"), Parkour->GetHangLine().MinAlong, -100.f, 2.f);

	Hold(Parkour, FVector(0.f, 1.f, 0.f), 2.f);
	const float Radius = Kate->GetCapsuleComponent()->GetScaledCapsuleRadius();
	TestTrue(TEXT("The rail stops her body a capsule short of it"),
		FMath::IsWithinInclusive(static_cast<float>(Kate->GetActorLocation().Y), 100.f - Radius - 1.f, 100.f - Radius + 4.f));
	TestTrue(TEXT("Still hanging (the rail is no ledge at her height)"), Parkour->IsHanging());
	TestTrue(TEXT("It names the rail"), Parkour->GetLastHangRefusal().Contains(Rail->GetName()));

	Hold(Parkour, FVector(0.f, -1.f, 0.f), 3.f);
	TestEqual(TEXT("The chimney stops her leading hand: centre 25 cm short of it"),
		static_cast<float>(Kate->GetActorLocation().Y), -100.f + 25.f, 3.f);
	TestTrue(TEXT("It says what is on the top"), Parkour->GetLastHangRefusal().Contains(Chimney->GetName()));
	TestTrue(TEXT("Still hanging"), Parkour->IsHanging());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangOutsideCorner, "Hawkeye.Parkour.HangTurnsAnOutsideCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangOutsideCorner::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	// A building 400 x 400: its front at x = 100, its right side at y = 200 facing +Y, one top.
	SpawnBox(TestWorld, FVector(100.f, -200.f, 0.f), FVector(500.f, 200.f, Top));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging on the front"), HangOn(Kate, 100.f, 100.f)))
	{
		return false;
	}
	// Shimmy right into the end: 75 cm to it at 120 cm/s, then the corner.
	bool bTurned = false;
	float TurnSeconds = 0.f;
	for (float Time = 0.f; Time < 2.f; Time += Tick)
	{
		Parkour->SetHangInput(FVector(0.f, 1.f, 0.f));
		if (Parkour->IsHanging())
		{
			TickHang(Parkour);
			if (Parkour->GetActiveMove() == EHawkeyeParkourMove::HangCorner)
			{
				bTurned = true;
				TestEqual(TEXT("It starts at the end"), static_cast<float>(Kate->GetActorLocation().Y), 175.f, 3.f);
			}
		}
		else if (Parkour->GetActiveMove() == EHawkeyeParkourMove::HangCorner)
		{
			TickMovement(Parkour);
			TurnSeconds += Tick;
			if (FMath::IsNearlyEqual(TurnSeconds, 0.2f, 0.01f))
			{
				// Half way round: off the corner on the diagonal, turned about 45 degrees.
				const FVector Off = Kate->GetActorLocation() - FVector(100.f, 200.f, 0.f);
				TestTrue(TEXT("Half way round she is outside the corner"), Off.X < -10.f && Off.Y > 10.f);
				TestEqual(TEXT("Turned half way"), static_cast<float>(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw)), -45.f, 8.f);
			}
		}
		else
		{
			break;
		}
	}
	TestTrue(TEXT("Shimmying into the end turned the outside corner"), bTurned);
	TestEqual(TEXT("In 0.4 s"), TurnSeconds, 0.4f, Tick + 0.01f);
	TestTrue(TEXT("Hanging on the side"), Parkour->IsHanging());
	TestEqual(TEXT("Facing the side (-Y)"), static_cast<float>(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw)), -90.f, 1.f);
	TestEqual(TEXT("36 cm off the side"), static_cast<float>(Kate->GetActorLocation().Y), 236.f, 1.f);
	TestEqual(TEXT("Both hands on it: 25 cm round the corner"), static_cast<float>(Kate->GetActorLocation().X), 125.f, 3.f);
	TestEqual(TEXT("Feet 145 cm under the top"), Feet(Kate), Top - 145.f, 1.f);
	TestTrue(TEXT("The new line faces +Y"), Parkour->GetHangLine().Normal.Equals(FVector(0.f, 1.f, 0.f), 0.01f));
	TestEqual(TEXT("It runs the side's 400 cm"), Parkour->GetHangLine().Length(), 400.f, 3.f);
	// On along the side: her right is now +X.
	const float X = Kate->GetActorLocation().X;
	Hold(Parkour, FVector(1.f, 0.f, 0.f), 0.5f);
	TestEqual(TEXT("And on along it"), static_cast<float>(Kate->GetActorLocation().X), X + 60.f, 2.f);
	return true;
}

/**
 * The district's parapet corners (generate_city.parapet_ring): the parapet is one ring whose outer faces are the
 * facades, so at a corner nothing stands proud of either face. She shimmies to the end margin, 25 cm short of the
 * corner, and goes round onto the other face 25 cm from it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangParapetCorner, "Hawkeye.Parkour.HangTurnsAFlushParapetCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangParapetCorner::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	// The building to its roof 90 cm under the top, and the parapet on it as the generator builds it: the front strip
	// and the side strip butted at the corner, both flush with the facades (x = 100, y = 200).
	SpawnBox(TestWorld, FVector(100.f, -200.f, 0.f), FVector(500.f, 200.f, Top - 90.f));
	SpawnBox(TestWorld, FVector(100.f, -200.f, Top - 90.f), FVector(130.f, 200.f, Top));
	SpawnBox(TestWorld, FVector(130.f, 170.f, Top - 90.f), FVector(500.f, 200.f, Top));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging on the front parapet"), HangOn(Kate, 100.f, 0.f)))
	{
		return false;
	}
	bool bTurned = false;
	float StoppedAt = 0.f;
	int32 Clipping = 0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HangParapetCornerTest), false, Kate);
	for (float Time = 0.f; Time < 3.f; Time += Tick)
	{
		Parkour->SetHangInput(FVector(0.f, 1.f, 0.f));
		if (Parkour->IsHanging())
		{
			StoppedAt = Kate->GetActorLocation().Y;
			TickHang(Parkour);
			bTurned |= Parkour->GetActiveMove() == EHawkeyeParkourMove::HangCorner;
		}
		else if (Parkour->IsPerformingMove())
		{
			TickMovement(Parkour);
			// Round the corner without going through the parapet or the corner itself.
			Clipping += TestWorld.Get()->OverlapBlockingTestByChannel(Kate->GetActorLocation(), FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(30.f, HalfHeight - 4.f), Params) ? 1 : 0;
		}
		if (bTurned && Parkour->IsHanging())
		{
			break;
		}
	}
	AddInfo(FString::Printf(TEXT("She stopped at y = %.0f; round at %s"), StoppedAt, *Kate->GetActorLocation().ToCompactString()));
	TestEqual(TEXT("Nothing stops her short of the end margin (25 cm from the corner)"), StoppedAt, 175.f, 3.f);
	TestTrue(TEXT("She turns the corner"), bTurned && Parkour->IsHanging());
	TestEqual(TEXT("Never inside the parapet on the way round"), Clipping, 0);
	TestEqual(TEXT("Facing the side (-Y)"), static_cast<float>(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw)), -90.f, 1.f);
	TestEqual(TEXT("36 cm off the side"), static_cast<float>(Kate->GetActorLocation().Y), 236.f, 1.f);
	TestEqual(TEXT("25 cm round the corner"), static_cast<float>(Kate->GetActorLocation().X), 125.f, 3.f);
	TestEqual(TEXT("Feet 145 cm under the parapet top"), Feet(Kate), Top - 145.f, 1.f);
	return true;
}

/**
 * A concave parapet corner: a wing standing out of the front (its face toward her at y = 200), the parapet ring
 * running on round the inside of the corner, mitred there. She shimmies into the wing and turns onto its face.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangConcaveParapetCorner, "Hawkeye.Parkour.HangTurnsAConcaveParapetCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangConcaveParapetCorner::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	// An L footprint: the front at x = 100 from y = -400 to 200, the wing's face at y = 200 from x = -300 to 100.
	// Roofs 90 cm under the top; the parapet strips on the front (x 100..130) and the wing face (y 200..230) meet at
	// the mitred inner corner (130, 230).
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(400.f, 600.f, Top - 90.f));
	SpawnBox(TestWorld, FVector(-300.f, 200.f, 0.f), FVector(100.f, 600.f, Top - 90.f));
	SpawnBox(TestWorld, FVector(100.f, -400.f, Top - 90.f), FVector(130.f, 230.f, Top));
	SpawnBox(TestWorld, FVector(-300.f, 200.f, Top - 90.f), FVector(100.f, 230.f, Top));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging on the front parapet"), HangOn(Kate, 100.f, 0.f)))
	{
		return false;
	}
	bool bTurned = false;
	int32 Clipping = 0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HangConcaveParapetTest), false, Kate);
	for (float Time = 0.f; Time < 3.f; Time += Tick)
	{
		Parkour->SetHangInput(FVector(0.f, 1.f, 0.f));
		if (Parkour->IsHanging())
		{
			TickHang(Parkour);
			bTurned |= Parkour->GetActiveMove() == EHawkeyeParkourMove::HangCorner;
		}
		else if (Parkour->IsPerformingMove())
		{
			TickMovement(Parkour);
			Clipping += TestWorld.Get()->OverlapBlockingTestByChannel(Kate->GetActorLocation(), FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(30.f, HalfHeight - 4.f), Params) ? 1 : 0;
		}
		if (bTurned && Parkour->IsHanging())
		{
			break;
		}
	}
	TestTrue(TEXT("Shimmying into the wing turned the inside corner"), bTurned && Parkour->IsHanging());
	TestEqual(TEXT("Never inside the parapet on the way round"), Clipping, 0);
	TestEqual(TEXT("Facing the wing (+Y)"), static_cast<float>(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw)), 90.f, 1.f);
	TestEqual(TEXT("36 cm off the wing"), static_cast<float>(Kate->GetActorLocation().Y), 164.f, 2.f);
	TestTrue(TEXT("Clear of the front she came from"),
		FMath::IsWithinInclusive(static_cast<float>(Kate->GetActorLocation().X), 50.f, 66.f));
	TestEqual(TEXT("Feet 145 cm under the parapet top"), Feet(Kate), Top - 145.f, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangInsideCorner, "Hawkeye.Parkour.HangTurnsAnInsideCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangInsideCorner::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	// The front at x = 100, and a wing standing out of it from y = 200 (its face toward her at y = 200), one top.
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(400.f, 400.f, Top));
	SpawnBox(TestWorld, FVector(-300.f, 200.f, 0.f), FVector(100.f, 600.f, Top));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging on the front"), HangOn(Kate, 100.f, 0.f)))
	{
		return false;
	}
	bool bTurned = false;
	for (float Time = 0.f; Time < 3.f; Time += Tick)
	{
		Parkour->SetHangInput(FVector(0.f, 1.f, 0.f));
		if (Parkour->IsHanging())
		{
			TickHang(Parkour);
			bTurned |= Parkour->GetActiveMove() == EHawkeyeParkourMove::HangCorner;
		}
		else if (Parkour->IsPerformingMove())
		{
			TickMovement(Parkour);
		}
		if (bTurned && Parkour->IsHanging())
		{
			break;
		}
	}
	TestTrue(TEXT("Shimmying into the wing turned the inside corner"), bTurned);
	TestTrue(TEXT("Hanging on the wing"), Parkour->IsHanging());
	TestEqual(TEXT("Facing the wing (+Y)"), static_cast<float>(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw)), 90.f, 1.f);
	TestEqual(TEXT("36 cm off the wing"), static_cast<float>(Kate->GetActorLocation().Y), 164.f, 2.f);
	TestTrue(TEXT("Clear of the front she came from"),
		FMath::IsWithinInclusive(static_cast<float>(Kate->GetActorLocation().X), 50.f, 66.f));
	TestTrue(TEXT("The new line faces -Y"), Parkour->GetHangLine().Normal.Equals(FVector(0.f, -1.f, 0.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangCornerRefused, "Hawkeye.Parkour.HangStopsWithNoLedgeRoundTheCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangCornerRefused::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	// The front ends at y = 200 where a taller wing stands out: its face is there, but its top is 150 cm higher.
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(400.f, 400.f, Top));
	SpawnBox(TestWorld, FVector(-300.f, 200.f, 0.f), FVector(100.f, 600.f, Top + 150.f));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging on the front"), HangOn(Kate, 100.f, 0.f)))
	{
		return false;
	}
	Hold(Parkour, FVector(0.f, 1.f, 0.f), 3.f);
	TestTrue(TEXT("At the taller wing she stops, still hanging"), Parkour->IsHanging());
	TestFalse(TEXT("No corner move"), Parkour->IsPerformingMove());
	TestTrue(TEXT("Stopped against the wing"), Kate->GetActorLocation().Y > 150.f && Kate->GetActorLocation().Y < 172.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangLeap, "Hawkeye.Parkour.HangLeapSideways",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangLeap::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	struct FCase
	{
		const TCHAR* What;
		FVector Min;
		FVector Max;
		bool bLeaps;
		float LandX;
	};
	// From a 30 cm wall ending at y = 100, hanging at y = 60: the next section at the same height after a gap.
	const FCase Cases[] = {
		{ TEXT("a parapet section 140 cm on"), FVector(100.f, 240.f, 0.f), FVector(130.f, 600.f, Top), true, 64.f },
		{ TEXT("a section 20 cm lower after the same gap"), FVector(100.f, 240.f, 0.f), FVector(130.f, 600.f, Top - 20.f), true, 64.f },
		{ TEXT("a fire-escape rail 90 cm out from the wall, 10 cm lower"), FVector(10.f, 240.f, Top - 100.f), FVector(16.f, 600.f, Top - 10.f),
			true, -26.f },
		{ TEXT("a section after a 220 cm gap (the hang would be about 290 cm on)"), FVector(100.f, 320.f, 0.f), FVector(130.f, 600.f, Top), false, 0.f },
		{ TEXT("a section 100 cm higher"), FVector(100.f, 240.f, 0.f), FVector(130.f, 600.f, Top + 100.f), false, 0.f },
	};
	for (const FCase& Case : Cases)
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBox(TestWorld, FVector(100.f, -300.f, 0.f), FVector(130.f, 100.f, Top));
		SpawnBox(TestWorld, Case.Min, Case.Max);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		if (!TestTrue(FString::Printf(TEXT("%s: hanging"), Case.What), HangOn(Kate, 100.f, 60.f)))
		{
			continue;
		}
		const FVector Start = Kate->GetActorLocation();
		Parkour->SetHangInput(FVector(0.f, 1.f, 0.f));
		const bool bJumped = Parkour->JumpFromHang();
		if (!Case.bLeaps)
		{
			TestFalse(FString::Printf(TEXT("%s: no leap"), Case.What), bJumped);
			TestTrue(FString::Printf(TEXT("%s: still hanging where she was"), Case.What),
				Parkour->IsHanging() && Kate->GetActorLocation().Equals(Start, 0.1f));
			TestFalse(FString::Printf(TEXT("%s: it says why"), Case.What), Parkour->GetLastHangRefusal().IsEmpty());
			AddInfo(FString::Printf(TEXT("%s: %s"), Case.What, *Parkour->GetLastHangRefusal()));
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: jump right leaps"), Case.What), bJumped);
		TestEqual(FString::Printf(TEXT("%s: a hang leap"), Case.What), Parkour->GetActiveMove(), EHawkeyeParkourMove::HangLeap);
		FinishMove(Parkour);
		TestTrue(FString::Printf(TEXT("%s: hanging on it"), Case.What), Parkour->IsHanging());
		const FVector End = Kate->GetActorLocation();
		TestTrue(FString::Printf(TEXT("%s: both hands on it (25 cm in from its end)"), Case.What),
			FMath::IsWithinInclusive(static_cast<float>(End.Y), 262.f, 280.f));
		TestEqual(FString::Printf(TEXT("%s: 36 cm off its face"), Case.What), static_cast<float>(End.X), Case.LandX, 1.f);
		TestTrue(FString::Printf(TEXT("%s: within the 250 cm reach"), Case.What), FVector::Dist2D(Start, End) <= 250.f);
		TestEqual(FString::Printf(TEXT("%s: feet 145 cm under its top"), Case.What), Feet(Kate) + 145.f,
			static_cast<float>(Case.Max.Z), 1.f);
		AddInfo(FString::Printf(TEXT("%s: leapt %.0f cm"), Case.What, FVector::Dist2D(Start, End)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangHop, "Hawkeye.Parkour.HangHopBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangHop::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	{
		// An alley 150 cm wide: the wall behind her faces her back, its top 20 cm lower.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBox(TestWorld, FVector(100.f, -300.f, 0.f), FVector(400.f, 300.f, Top));
		SpawnBox(TestWorld, FVector(-600.f, -300.f, 0.f), FVector(-86.f, 300.f, Top - 20.f));
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		if (TestTrue(TEXT("Hanging"), HangOn(Kate, 100.f, 0.f)))
		{
			Parkour->SetHangInput(FVector(-1.f, 0.f, 0.f));
			TestTrue(TEXT("Jump with the stick back hops"), Parkour->JumpFromHang());
			TestEqual(TEXT("A hang hop"), Parkour->GetActiveMove(), EHawkeyeParkourMove::HangHop);
			FinishMove(Parkour);
			TestTrue(TEXT("Hanging on the wall behind"), Parkour->IsHanging());
			TestEqual(TEXT("Turned round (facing -X)"), static_cast<float>(FMath::Abs(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw))), 180.f, 1.f);
			TestEqual(TEXT("36 cm off it"), static_cast<float>(Kate->GetActorLocation().X), -50.f, 1.f);
			TestEqual(TEXT("Feet 145 cm under its top"), Feet(Kate), Top - 20.f - 145.f, 1.f);
		}
	}
	{
		// The wall behind is 260 cm off: too far, so a controlled drop, turned round and pushing off.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBox(TestWorld, FVector(100.f, -300.f, 0.f), FVector(400.f, 300.f, Top));
		SpawnBox(TestWorld, FVector(-600.f, -300.f, 0.f), FVector(-196.f, 300.f, Top));
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		if (TestTrue(TEXT("Hanging again"), HangOn(Kate, 100.f, 0.f)))
		{
			Parkour->SetHangInput(FVector(-1.f, 0.f, 0.f));
			TestTrue(TEXT("Jump with the stick back still does something"), Parkour->JumpFromHang());
			TestFalse(TEXT("Not hanging"), Parkour->IsHanging());
			TestFalse(TEXT("No move of ours"), Parkour->IsPerformingMove());
			TestEqual(TEXT("Falling"), static_cast<int32>(Kate->GetCharacterMovement()->MovementMode.GetValue()),
				static_cast<int32>(MOVE_Falling));
			TestEqual(TEXT("Turned round"), static_cast<float>(FMath::Abs(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw))), 180.f, 1.f);
			TestTrue(TEXT("Pushing off the wall"), Kate->GetCharacterMovement()->Velocity.X < -100.f);
			TestTrue(TEXT("It says why"), Parkour->GetLastHangRefusal().Contains(TEXT("200")));
			TestTrue(TEXT("The late catch is looking for a ledge on the way down"), Parkour->IsLateCatchArmed());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangAutoClimb, "Hawkeye.Parkour.HangAutoClimbAndTheParapetGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangAutoClimb::RunTest(const FString& Parameters)
{
	using namespace HawkeyeHangTest;
	{
		// A roof: the top runs on 300 cm behind the edge.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBox(TestWorld, FVector(100.f, -300.f, 0.f), FVector(400.f, 300.f, Top));
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		if (TestTrue(TEXT("Hanging from the roof's edge"), HangOn(Kate, 100.f, 0.f)))
		{
			TestFalse(TEXT("A roof is not a guarded parapet"), Parkour->IsHangTopGuarded());
			Hold(Parkour, FVector::ZeroVector, 5.f);
			TestTrue(TEXT("No timeout: still hanging 5 s later"), Parkour->IsHanging());
			Hold(Parkour, FVector(1.f, 0.f, 0.f), 0.4f);
			TestTrue(TEXT("The stick up 0.4 s: still hanging"), Parkour->IsHanging());
			Hold(Parkour, FVector::ZeroVector, 0.1f);
			Hold(Parkour, FVector(1.f, 0.f, 0.f), 0.4f);
			TestTrue(TEXT("Let go and pushed again: the clock started over"), Parkour->IsHanging());
			Hold(Parkour, FVector(1.f, 0.f, 0.f), 0.15f);
			TestFalse(TEXT("Held past 0.5 s: she climbs"), Parkour->IsHanging());
			TestEqual(TEXT("A climb"), Parkour->GetLastMove(), EHawkeyeParkourMove::Climb);
		}
	}
	{
		// A parapet wall 80 cm thick, room to stand on it, and the street 800 cm down on its far side.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBox(TestWorld, FVector(100.f, -300.f, 0.f), FVector(180.f, 300.f, Top));
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		if (TestTrue(TEXT("Hanging from the parapet"), HangOn(Kate, 100.f, 0.f)))
		{
			TestTrue(TEXT("Its top is guarded"), Parkour->IsHangTopGuarded());
			Hold(Parkour, FVector(1.f, 0.f, 0.f), 1.5f);
			TestTrue(TEXT("The stick up 1.5 s: still hanging"), Parkour->IsHanging());
			TestTrue(TEXT("The roof-edge guard says so"), Parkour->GetLastHangRefusal().Contains(TEXT("roof-edge guard")));
			Kate->Jump();
			TestEqual(TEXT("The jump key still climbs"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Climb);
		}
	}
	{
		// A roof parapet seen from outside (after a drop to hang): 30 cm thick, the roof 90 cm under its top behind it.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBox(TestWorld, FVector(100.f, -300.f, 0.f), FVector(130.f, 300.f, Top));
		SpawnBox(TestWorld, FVector(130.f, -300.f, 0.f), FVector(900.f, 300.f, Top - 90.f));
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		if (TestTrue(TEXT("Hanging from the roof parapet"), HangOn(Kate, 100.f, 0.f)))
		{
			TestFalse(TEXT("The roof behind it is no drop"), Parkour->IsHangTopGuarded());
			Hold(Parkour, FVector(1.f, 0.f, 0.f), 0.6f);
			TestEqual(TEXT("The stick up climbs over onto the roof"), Parkour->GetLastMove(), EHawkeyeParkourMove::Climb);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHangCamera, "Hawkeye.Parkour.HangCameraKeepsOffTheWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHangCamera::RunTest(const FString& Parameters)
{
	const FVector Wall(-1.f, 0.f, 0.f);   // she faces +X; looking straight at the wall is yaw 0
	TestEqual(TEXT("Looking at the wall: unchanged"), UParkourComponent::ComputeHangCameraYaw(20.f, Wall, 65.f), 20.f, 0.01f);
	TestEqual(TEXT("Swung 90 to the right: held at 65"), UParkourComponent::ComputeHangCameraYaw(90.f, Wall, 65.f), 65.f, 0.01f);
	TestEqual(TEXT("Swung 90 to the left: held at -65"), UParkourComponent::ComputeHangCameraYaw(-90.f, Wall, 65.f), -65.f, 0.01f);
	TestEqual(TEXT("Behind the wall's plane (looking away): held at the limit"),
		FMath::Abs(UParkourComponent::ComputeHangCameraYaw(170.f, Wall, 65.f)), 65.f, 0.01f);
	TestEqual(TEXT("Another wall (facing +Y, looked at along -Y at yaw -90)"),
		UParkourComponent::ComputeHangCameraYaw(0.f, FVector(0.f, 1.f, 0.f), 65.f), -25.f, 0.01f);

	TestEqual(TEXT("Straight at the wall: the right shoulder"), UParkourComponent::ComputeHangSocketSide(0.f, Wall, 1.f), 1.f);
	TestEqual(TEXT("Looking along it to the left (its right side is the wall): the left shoulder"),
		UParkourComponent::ComputeHangSocketSide(-60.f, Wall, 1.f), -1.f);
	TestEqual(TEXT("Looking along it to the right: the right shoulder"), UParkourComponent::ComputeHangSocketSide(60.f, Wall, -1.f), 1.f);
	TestEqual(TEXT("Between the thresholds: kept (left)"), UParkourComponent::ComputeHangSocketSide(-15.f, Wall, -1.f), -1.f);
	TestEqual(TEXT("Between the thresholds: kept (right)"), UParkourComponent::ComputeHangSocketSide(-15.f, Wall, 1.f), 1.f);

	// On the character: hanging with the camera looking left along the wall, the shoulder offset eases to the left.
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeHangTest::SpawnKate(TestWorld);
	HawkeyeHangTest::SpawnBox(TestWorld, FVector(100.f, -300.f, 0.f), FVector(400.f, 300.f, HawkeyeHangTest::Top));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (TestTrue(TEXT("Hanging"), HawkeyeHangTest::HangOn(Kate, 100.f, 0.f)))
	{
		// No controller in a test world: the actor's yaw stands in for the camera's.
		Kate->SetActorRotation(FRotator(0.f, -60.f, 0.f));
		FHawkeyeCameraTargets Targets(350.f, FVector(0.f, 70.f, 60.f), 90.f);
		for (int32 Step = 0; Step < 12; ++Step)
		{
			Targets.SocketOffset.Y = 70.f;
			Parkour->ApplyHangCamera(Targets, 0.05f);
		}
		TestEqual(TEXT("After 0.6 s the lens is over the left shoulder"), static_cast<float>(Targets.SocketOffset.Y), -70.f, 0.5f);
		Kate->SetActorRotation(FRotator::ZeroRotator);
	}
	return true;
}

#endif
