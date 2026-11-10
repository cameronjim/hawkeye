#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The zip's movement from claude-docs/gameplay-semantics.md ("Zip movement"), driven on the movement
 * component directly: entry speed, acceleration, a truthful velocity, the arrival carry, letting go,
 * a blocked line, frame-rate independence and a chain keeping its speed.
 */
namespace HawkeyeZipMovementTest
{
	/** Kate in the air at Location, still, with no controller (the movement ticks anyway). */
	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		if (UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr)
		{
			Movement->SetMovementMode(MOVE_Falling);
			Movement->Velocity = FVector::ZeroVector;
		}
		return Kate;
	}

	/** A box centred at Centre. */
	static AHawkeyeTestBlocker* SpawnBox(const FHawkeyeTestWorld& TestWorld, const FVector& Centre, const FVector& HalfExtent)
	{
		AHawkeyeTestBlocker* Box = Cast<AHawkeyeTestBlocker>(
			TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), Centre, FRotator::ZeroRotator));
		if (Box)
		{
			Box->SetExtent(HalfExtent);
		}
		return Box;
	}

	/** A line from where she is to End, no hop. */
	static FHawkeyeZipLine LineTo(const ACharacter* Kate, const FVector& End)
	{
		FHawkeyeZipLine Line;
		Line.Launch = Kate->GetActorLocation();
		Line.End = End;
		return Line;
	}

	/** Ticks at Hz until the zip ends or MaxSeconds pass; returns the time it ran. */
	static float RunZip(AHawkeyeAimTestCharacter* Kate, float Hz, float MaxSeconds)
	{
		const float Step = 1.f / Hz;
		int32 Ticks = 0;
		while (Kate->GetHawkeyeMovement()->IsZipping() && Ticks * Step < MaxSeconds)
		{
			Kate->TestTickMovement(Step);
			++Ticks;
		}
		return Ticks * Step;
	}

	static int32 ModeOf(const ACharacter* Character)
	{
		return static_cast<int32>(Character->GetCharacterMovement()->MovementMode.GetValue());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipEntrySpeed, "Hawkeye.Movement.Zip.EntryKeepsSpeedAlongTheLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipEntrySpeed::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	TestEqual(TEXT("ZipSpeed is 1800"), Movement->ZipSpeed, 1800.f);
	TestEqual(TEXT("ZipMinStartSpeed is 600"), Movement->ZipMinStartSpeed, 600.f);

	// 30 degrees off the line at 1400 cm/s: 1212 along it.
	const FVector End(3000.f, 0.f, 1000.f);
	Movement->Velocity = FVector(1400.f, 0.f, 0.f).RotateAngleAxis(30.f, FVector::UpVector);
	Movement->StartZip(LineTo(Kate, End));
	TestTrue(TEXT("Zipping"), Movement->IsZipping());
	TestEqual(TEXT("In the zip mode"), ModeOf(Kate), static_cast<int32>(MOVE_Custom));
	TestEqual(TEXT("Starts at her speed along the line"), Movement->GetZipLineSpeed(), 1400.f * FMath::Cos(PI / 6.f), 0.5f);
	Movement->ReleaseZip(false);

	Movement->Velocity = FVector(2600.f, 0.f, 0.f);
	Movement->StartZip(LineTo(Kate, End));
	TestEqual(TEXT("And never faster than ZipSpeed"), Movement->GetZipLineSpeed(), 1800.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipMinStart, "Hawkeye.Movement.Zip.StandstillStartsAtTheMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipMinStart::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	const FVector End(3000.f, 0.f, 1000.f);
	Movement->StartZip(LineTo(Kate, End));
	TestEqual(TEXT("From a standstill the line starts at 600 cm/s"), Movement->GetZipLineSpeed(), 600.f);
	Movement->ReleaseZip(false);

	Movement->Velocity = FVector(-900.f, 0.f, 0.f);
	Movement->StartZip(LineTo(Kate, End));
	TestEqual(TEXT("Moving away from the anchor, too"), Movement->GetZipLineSpeed(), 600.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipAcceleration, "Hawkeye.Movement.Zip.AcceleratesToAndHoldsZipSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipAcceleration::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	const FVector Start(0.f, 0.f, 1000.f);
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, Start);
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	TestEqual(TEXT("ZipAcceleration is 6000"), Movement->ZipAcceleration, 6000.f);
	Movement->StartZip(LineTo(Kate, Start + FVector(4000.f, 0.f, 0.f)));

	// 600 to 1800 at 6000 cm/s² takes 0.2 s and covers 600 * 0.2 + 3000 * 0.04 = 240 cm.
	for (int32 Tick = 0; Tick < 6; ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	TestEqual(TEXT("After 0.1 s it is at 1200 cm/s"), Movement->GetZipLineSpeed(), 1200.f, 0.5f);
	for (int32 Tick = 0; Tick < 6; ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	TestEqual(TEXT("After 0.2 s it is at 1800 cm/s"), Movement->GetZipLineSpeed(), 1800.f, 0.5f);
	TestEqual(TEXT("Having covered 240 cm"), static_cast<float>(Kate->GetActorLocation().X - Start.X), 240.f, 0.5f);
	for (int32 Tick = 0; Tick < 30; ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	TestEqual(TEXT("At 0.7 s it holds 1800 cm/s"), Movement->GetZipLineSpeed(), 1800.f);
	TestEqual(TEXT("And has covered 240 + 1800 * 0.5 cm"), static_cast<float>(Kate->GetActorLocation().X - Start.X), 1140.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipVelocity, "Hawkeye.Movement.Zip.VelocityIsTheDisplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipVelocity::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	// Standing on a roof, so the zip hops up to its line first.
	SpawnBox(TestWorld, FVector(0.f, 0.f, -100.f), FVector(300.f, 300.f, 100.f));
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 200.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	Kate->SetActorLocation(FVector(0.f, 0.f, Kate->GetSimpleCollisionHalfHeight() + 2.f));
	Movement->SetMovementMode(MOVE_Walking);
	FHawkeyeZipLine Line;
	Line.Launch = Kate->GetActorLocation() + FVector(0.f, 0.f, 120.f);
	Line.End = Line.Launch + FVector(2500.f, 0.f, 600.f);
	Line.bHop = true;
	Movement->StartZip(Line);
	TestTrue(TEXT("It hops first"), Movement->IsZipHopping());

	const float Step = 1.f / 60.f;
	int32 HopTicks = 0;
	int32 LineTicks = 0;
	float WorstError = 0.f;
	for (int32 Tick = 0; Tick < 60 && Movement->IsZipping(); ++Tick)
	{
		const bool bHopping = Movement->IsZipHopping();
		const FVector Before = Kate->GetActorLocation();
		Kate->TestTickMovement(Step);
		if (!Movement->IsZipping())
		{
			break;
		}
		const FVector Moved = (Kate->GetActorLocation() - Before) / Step;
		WorstError = FMath::Max(WorstError, static_cast<float>(FVector::Dist(Moved, Movement->Velocity)));
		HopTicks += bHopping ? 1 : 0;
		LineTicks += bHopping ? 0 : 1;
	}
	TestTrue(TEXT("Ticks were seen on the hop and on the line"), HopTicks > 0 && LineTicks > 0);
	TestTrue(FString::Printf(TEXT("Velocity is the displacement over the time on every tick (worst %.3f cm/s)"), WorstError),
		WorstError < 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipArrival, "Hawkeye.Movement.Zip.ArrivalCarriesSpeedOntoTheRoof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipArrival::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	const FVector Start(0.f, 0.f, 1000.f);
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, Start);
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement || !SpawnBox(TestWorld, FVector(2200.f, 0.f, 400.f), FVector(300.f, 300.f, 100.f)))
	{
		AddError(TEXT("Could not spawn Kate and the roof."));
		return false;
	}
	TestEqual(TEXT("ArrivalCarrySpeed is 600"), Movement->ArrivalCarrySpeed, 600.f);
	// Down onto a roof whose top is at 500; the landing point puts her feet 2 cm over it.
	const FVector End(2000.f, 0.f, 500.f + Kate->GetSimpleCollisionHalfHeight() + 2.f);
	Movement->StartZip(LineTo(Kate, End));
	RunZip(Kate, 60.f, 5.f);
	TestFalse(TEXT("The zip is over"), Movement->IsZipping());
	TestTrue(TEXT("On the landing point"), Kate->GetActorLocation().Equals(End, 0.5f));
	TestEqual(TEXT("Walking on the roof"), ModeOf(Kate), static_cast<int32>(MOVE_Walking));
	const FVector Along = (End - Start).GetSafeNormal2D();
	TestTrue(FString::Printf(TEXT("Moving on along the line at 600 cm/s (%s)"), *Movement->Velocity.ToCompactString()),
		Movement->Velocity.Equals(Along * 600.f, 1.f));

	// The same line onto nothing: she falls, carrying the same velocity.
	const FHawkeyeTestWorld OpenWorld;
	AHawkeyeAimTestCharacter* Open = SpawnKate(OpenWorld, Start);
	Open->GetHawkeyeMovement()->StartZip(LineTo(Open, End));
	RunZip(Open, 60.f, 5.f);
	TestTrue(TEXT("With no roof she still arrives on the landing point"), Open->GetActorLocation().Equals(End, 0.5f));
	TestEqual(TEXT("And falls"), ModeOf(Open), static_cast<int32>(MOVE_Falling));
	TestTrue(TEXT("Carrying the same 600 cm/s"), Open->GetHawkeyeMovement()->Velocity.Equals(Along * 600.f, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipJumpRelease, "Hawkeye.Movement.Zip.JumpLetGoKeepsVelocityPlusBoost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipJumpRelease::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	TestEqual(TEXT("ReleaseJumpBoost is 350"), Movement->ReleaseJumpBoost, 350.f);
	TestEqual(TEXT("ReleaseVelocityScale is 1"), Movement->ReleaseVelocityScale, 1.f);
	Movement->StartZip(LineTo(Kate, FVector(3000.f, 0.f, 1600.f)));
	for (int32 Tick = 0; Tick < 20; ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	const FVector OnTheLine = Movement->Velocity;
	TestTrue(TEXT("Moving fast along the line"), OnTheLine.Size() > 1000.f);

	Kate->Jump();
	TestFalse(TEXT("The jump key lets go"), Movement->IsZipping());
	TestEqual(TEXT("Falling"), ModeOf(Kate), static_cast<int32>(MOVE_Falling));
	TestTrue(FString::Printf(TEXT("With the zip's velocity and 350 cm/s more up (%s)"), *Movement->Velocity.ToCompactString()),
		Movement->Velocity.Equals(OnTheLine + FVector(0.f, 0.f, 350.f), 0.5f));
	TestTrue(TEXT("Still a controlled drop"), Kate->IsControlledDrop());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipCrouchRelease, "Hawkeye.Movement.Zip.CrouchLetGoHasNoBoost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipCrouchRelease::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	Movement->StartZip(LineTo(Kate, FVector(3000.f, 0.f, 1600.f)));
	for (int32 Tick = 0; Tick < 20; ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	const FVector OnTheLine = Movement->Velocity;
	Kate->GetGrappleComponent()->ReleaseZip(/*bJump=*/false);
	TestEqual(TEXT("Falling"), ModeOf(Kate), static_cast<int32>(MOVE_Falling));
	TestTrue(FString::Printf(TEXT("With exactly the zip's velocity (%s)"), *Movement->Velocity.ToCompactString()),
		Movement->Velocity.Equals(OnTheLine, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipBlocked, "Hawkeye.Movement.Zip.BlockedDropsWithNoVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipBlocked::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement || !SpawnBox(TestWorld, FVector(1000.f, 0.f, 1000.f), FVector(20.f, 300.f, 300.f)))
	{
		AddError(TEXT("Could not spawn Kate and the wall."));
		return false;
	}
	Movement->StartZip(LineTo(Kate, FVector(3000.f, 0.f, 1000.f)));
	RunZip(Kate, 60.f, 5.f);
	TestFalse(TEXT("The zip stopped"), Movement->IsZipping());
	TestTrue(TEXT("At the wall, not through it"), Kate->GetActorLocation().X < 1000.f);
	TestEqual(TEXT("Falling"), ModeOf(Kate), static_cast<int32>(MOVE_Falling));
	TestTrue(TEXT("With no velocity: a wall is not a launch"), Movement->Velocity.IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipFrameRate, "Hawkeye.Movement.Zip.SameAt30And120Hz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipFrameRate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FVector Start(0.f, 0.f, 1000.f);
	const FVector End(2400.f, 900.f, 1500.f);
	FVector At[2];
	float Arrival[2];
	const float Rates[2] = { 30.f, 120.f };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, Start);
		if (!Kate)
		{
			AddError(TEXT("Could not spawn Kate."));
			return false;
		}
		Kate->GetHawkeyeMovement()->StartZip(LineTo(Kate, End));
		// 0.4 s is a whole number of frames at both rates: through the acceleration and onto full speed.
		const int32 Frames = FMath::RoundToInt(0.4f * Rates[Index]);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Kate->TestTickMovement(1.f / Rates[Index]);
		}
		At[Index] = Kate->GetActorLocation();
		Arrival[Index] = 0.4f + RunZip(Kate, Rates[Index], 5.f);
		TestTrue(FString::Printf(TEXT("%.0f Hz arrives"), Rates[Index]), Kate->GetActorLocation().Equals(End, 0.5f));
	}
	TestTrue(FString::Printf(TEXT("At 0.4 s she is in the same place at 30 and 120 Hz (%.2f cm apart)"),
		FVector::Dist(At[0], At[1])), At[0].Equals(At[1], 1.f));
	// Arrival is seen on the frame it happens in, so the two can differ by up to one 30 Hz frame.
	TestTrue(FString::Printf(TEXT("And arrives at the same time to within a 30 Hz frame (%.3f s, %.3f s)"), Arrival[0], Arrival[1]),
		FMath::Abs(Arrival[0] - Arrival[1]) <= 1.f / 30.f + KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeZipChainSpeed, "Hawkeye.Movement.Zip.ChainKeepsTheSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeZipChainSpeed::RunTest(const FString& Parameters)
{
	using namespace HawkeyeZipMovementTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector(0.f, 0.f, 1000.f));
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	Movement->StartZip(LineTo(Kate, FVector(3000.f, 0.f, 1000.f)));
	for (int32 Tick = 0; Tick < 6; ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	const float Before = Movement->GetZipLineSpeed();
	TestEqual(TEXT("Still speeding up when the chain comes"), Before, 1200.f, 0.5f);

	// A new line square to the old one: a fresh start would drop to the 600 cm/s floor.
	const FVector Here = Kate->GetActorLocation();
	Movement->StartZip(LineTo(Kate, Here + FVector(0.f, 2500.f, 0.f)));
	TestTrue(TEXT("Redirected in the air"), Movement->IsZipping() && Movement->IsZipRedirecting());
	TestEqual(TEXT("The chain keeps the speed"), Movement->GetZipLineSpeed(), Before);
	Kate->TestTickMovement(1.f / 60.f);
	TestEqual(TEXT("And goes on accelerating from it"), Movement->GetZipLineSpeed(), Before + 100.f, 0.5f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
