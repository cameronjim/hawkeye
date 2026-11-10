#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/ParkourComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Parkour moves and hangs on the movement component (claude-docs/gameplay-semantics.md, "Traversal movement",
 * "Momentum out of a move", "Traversal clips"): the mode during a move and a hang, a truthful velocity, the
 * exit carry of a vault, a sprint vault and a mantle, the same path at 30 and 120 Hz, the obstacle passed only
 * by its own move, and the clip and timing rules as pure functions. No content: every move follows its path.
 * Kate stands at the origin facing +X with her feet at z = 0; obstacles start 100 cm ahead.
 */
namespace HawkeyeTraversalTest
{
	static constexpr float HalfHeight = 88.f;
	static constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

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

	/** Kate on a big floor at z = 0, Walking, facing +X; with Height > 0 a block that tall and Depth deep at x = 100. */
	static AHawkeyeAimTestCharacter* SpawnScene(const FHawkeyeTestWorld& TestWorld, float Height, float Depth)
	{
		SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
		if (Height > 0.f)
		{
			SpawnBox(TestWorld, FVector(100.f, -150.f, 0.f), FVector(100.f + Depth, 150.f, Height));
		}
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, HalfHeight + 2.f), FRotator::ZeroRotator));
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	/** Starts Move over what is ahead, moving at Speed along +X when it starts. */
	static bool StartOver(AHawkeyeAimTestCharacter* Kate, EHawkeyeParkourMove Move, float Speed)
	{
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		FHawkeyeParkourObstacle Obstacle;
		if (!Parkour->DetectObstacle(Parkour->ManualTriggerDistance, Obstacle))
		{
			return false;
		}
		Kate->GetCharacterMovement()->Velocity = FVector(Speed, 0.f, 0.f);
		return Parkour->StartMove(Move, Obstacle);
	}

	/** Ticks the movement at Hz until the move of ours is over; returns the time it took. */
	static float RunMove(AHawkeyeAimTestCharacter* Kate, float Hz, float MaxSeconds = 2.f)
	{
		const float Step = 1.f / Hz;
		float Time = 0.f;
		while (Kate->GetParkourComponent()->IsPerformingMove() && Time < MaxSeconds)
		{
			Kate->TestTickMovement(Step);
			Time += Step;
		}
		return Time;
	}

	/** A path move of Seconds along Keys' locations (yaw 0), ending as Exit. */
	static FHawkeyeTraversalMove PathMove(const TArray<FVector>& Points, float Seconds, EHawkeyeTraversalExit Exit)
	{
		FHawkeyeTraversalMove Move;
		Move.Seconds = Seconds;
		Move.Exit = Exit;
		for (const FVector& Point : Points)
		{
			Move.Keys.AddDefaulted_GetRef().Location = Point;
		}
		return Move;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalMode, "Hawkeye.Movement.Traversal.ModeDuringAMoveAndAHang", HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalMode::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 230.f, 300.f);
	UHawkeyeMovementComponent* Movement = Kate ? Kate->GetHawkeyeMovement() : nullptr;
	if (!Movement)
	{
		AddError(TEXT("Kate has no Hawkeye movement component."));
		return false;
	}
	TestTrue(TEXT("The jump key grabs the 230 cm ledge"), Kate->GetParkourComponent()->TryParkour(false));
	TestTrue(TEXT("The grab runs in the traversal mode"), Movement->IsTraversing() && !Movement->IsTraversalHolding());
	TestEqual(TEXT("Named so"), Movement->GetMovementName(), FString(TEXT("Traversal")));
	RunMove(Kate, 60.f);
	TestTrue(TEXT("Hanging"), Kate->GetParkourComponent()->IsHanging());
	TestTrue(TEXT("A hang is the same mode, held"), Movement->IsTraversing() && Movement->IsTraversalHolding());
	Kate->TestTickMovement(1.f / 60.f);
	TestTrue(TEXT("Holding still: no velocity"), Movement->Velocity.IsNearlyZero());
	Kate->GetParkourComponent()->DropFromHang();
	TestEqual(TEXT("Letting go falls"), static_cast<int32>(Movement->MovementMode.GetValue()), static_cast<int32>(MOVE_Falling));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalVelocity, "Hawkeye.Movement.Traversal.VelocityIsTheDisplacement",
	HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalVelocity::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 90.f, 60.f);
	if (!Kate || !TestTrue(TEXT("A vault starts"), StartOver(Kate, EHawkeyeParkourMove::Vault, 0.f)))
	{
		return false;
	}
	const float Step = 1.f / 60.f;
	float WorstError = 0.f;
	float FastestForward = 0.f;
	int32 Ticks = 0;
	while (Kate->GetParkourComponent()->IsPerformingMove() && Ticks < 120)
	{
		const FVector Before = Kate->GetActorLocation();
		Kate->TestTickMovement(Step);
		++Ticks;
		if (!Kate->GetParkourComponent()->IsPerformingMove())
		{
			break;
		}
		const FVector Moved = (Kate->GetActorLocation() - Before) / Step;
		WorstError = FMath::Max(WorstError, static_cast<float>(FVector::Dist(Moved, Kate->GetVelocity())));
		FastestForward = FMath::Max(FastestForward, static_cast<float>(Kate->GetVelocity().X));
	}
	TestTrue(FString::Printf(TEXT("Velocity is the displacement over the time on every tick (worst %.3f cm/s)"), WorstError),
		WorstError < 0.5f);
	TestTrue(FString::Printf(TEXT("And it is not zero: she is moving over the block (up to %.0f cm/s)"), FastestForward),
		FastestForward > 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalVaultCarry, "Hawkeye.Movement.Traversal.VaultLeavesWithItsEntrySpeed",
	HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalVaultCarry::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalTest;
	struct FCase
	{
		float Entry;
		float Exit;
		const TCHAR* What;
	};
	const FCase Cases[] = {
		{ 0.f, 300.f, TEXT("from a standstill: the 300 cm/s minimum") },
		{ 500.f, 500.f, TEXT("at a run: her 500 cm/s") },
		{ 700.f, 700.f, TEXT("at a sprint: her 700 cm/s") },
		{ 1000.f, 750.f, TEXT("faster than that: held to 750 cm/s") },
	};
	for (const FCase& Case : Cases)
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 90.f, 60.f);
		if (!Kate || !TestTrue(FString::Printf(TEXT("%s: a vault"), Case.What), StartOver(Kate, EHawkeyeParkourMove::Vault, Case.Entry)))
		{
			continue;
		}
		const float Seconds = RunMove(Kate, 120.f);
		const UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		TestEqual(FString::Printf(TEXT("%s: Walking on the far side"), Case.What), static_cast<int32>(Movement->MovementMode.GetValue()),
			static_cast<int32>(MOVE_Walking));
		TestEqual(FString::Printf(TEXT("%s: leaves along the vault"), Case.What), static_cast<float>(Movement->Velocity.X), Case.Exit, 1.f);
		TestEqual(FString::Printf(TEXT("%s: and only along it"), Case.What), static_cast<float>(Movement->Velocity.Size2D()), Case.Exit, 1.f);
		AddInfo(FString::Printf(TEXT("%s: the vault took %.2f s"), Case.What, Seconds));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalSprintVault, "Hawkeye.Movement.Traversal.SprintAutoVaultKeepsItsSpeed",
	HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalSprintVault::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 90.f, 60.f);
	if (!Kate)
	{
		return false;
	}
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	Kate->TestSetSprinting(true);
	Kate->GetCharacterMovement()->Velocity = FVector(700.f, 0.f, 0.f);
	TestTrue(TEXT("Sprinting into it vaults on its own"), Parkour->TryParkour(true));
	TestEqual(TEXT("A vault"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Vault);
	const float Seconds = RunMove(Kate, 60.f);
	TestTrue(FString::Printf(TEXT("Quicker than a standing vault (%.2f s, down to 0.35)"), Seconds),
		Seconds < Parkour->VaultSeconds - 0.05f && Seconds >= Parkour->VaultMinSeconds - 1.f / 60.f);
	TestTrue(FString::Printf(TEXT("Out the far side no slower than she went in (%.0f cm/s)"), Kate->GetVelocity().X),
		Kate->GetVelocity().X >= 700.f - 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalMantleCarry, "Hawkeye.Movement.Traversal.MantleStepsOnWalking",
	HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalMantleCarry::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalTest;
	for (const float Entry : { 0.f, 600.f })
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 150.f, 150.f);
		if (!Kate || !TestTrue(TEXT("A mantle"), StartOver(Kate, EHawkeyeParkourMove::Mantle, Entry)))
		{
			continue;
		}
		RunMove(Kate, 60.f);
		const UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		const FString What = FString::Printf(TEXT("Mantled in at %.0f cm/s"), Entry);
		TestEqual(What + TEXT(": Walking on the top"), static_cast<int32>(Movement->MovementMode.GetValue()), static_cast<int32>(MOVE_Walking));
		TestEqual(What + TEXT(": feet on it"), static_cast<float>(Kate->GetActorLocation().Z - HalfHeight), 150.f, 3.f);
		TestEqual(What + TEXT(": stepping on at 150 cm/s the way she faced"), static_cast<float>(Movement->Velocity.X),
			Kate->GetParkourComponent()->StepOnSpeed, 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalFrameRate, "Hawkeye.Movement.Traversal.SamePathAt30And120Hz",
	HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalFrameRate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalTest;
	// The movement alone: a curved path in open air, read at 0.1 s steps (3 ticks at 30 Hz, 12 at 120).
	TArray<FVector> Samples[2];
	const float Rates[2] = { 30.f, 120.f };
	for (int32 Run = 0; Run < 2; ++Run)
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 0.f, 0.f);
		if (!Kate)
		{
			return false;
		}
		const FVector Start = Kate->GetActorLocation();
		TArray<FVector> Points;
		for (int32 Key = 0; Key <= 8; ++Key)
		{
			const float A = Key / 8.f;
			Points.Add(Start + FVector(300.f * A, 80.f * FMath::Sin(PI * A), 50.f + 120.f * FMath::Sin(PI * A)));
		}
		Kate->GetHawkeyeMovement()->StartTraversal(PathMove(Points, 0.6f, EHawkeyeTraversalExit::Hold));
		const int32 TicksPerSample = FMath::RoundToInt32(Rates[Run] / 10.f);
		for (int32 Sample = 0; Sample < 7; ++Sample)
		{
			for (int32 Tick = 0; Tick < TicksPerSample; ++Tick)
			{
				Kate->TestTickMovement(1.f / Rates[Run]);
			}
			Samples[Run].Add(Kate->GetActorLocation());
		}
	}
	float Worst = 0.f;
	for (int32 Index = 0; Index < Samples[0].Num(); ++Index)
	{
		Worst = FMath::Max(Worst, static_cast<float>(FVector::Dist(Samples[0][Index], Samples[1][Index])));
	}
	TestTrue(FString::Printf(TEXT("Every 0.1 s she is in the same place at 30 and 120 Hz (worst %.3f cm)"), Worst), Worst < 0.05f);

	// A whole vault through the parkour component: the same landing at either rate.
	FVector Ends[2];
	for (int32 Run = 0; Run < 2; ++Run)
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 90.f, 60.f);
		if (!Kate || !StartOver(Kate, EHawkeyeParkourMove::Vault, 0.f))
		{
			AddError(TEXT("No vault"));
			return false;
		}
		const float Step = 1.f / Rates[Run];
		while (Kate->GetParkourComponent()->IsPerformingMove())
		{
			Kate->TestTickMovement(Step);
		}
		Ends[Run] = Kate->GetActorLocation();
	}
	TestTrue(FString::Printf(TEXT("The vault lands in the same place (%s and %s)"), *Ends[0].ToCompactString(), *Ends[1].ToCompactString()),
		Ends[0].Equals(Ends[1], 0.05f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalPassThrough, "Hawkeye.Movement.Traversal.PassesOnlyItsOwnObstacle",
	HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalPassThrough::RunTest(const FString& Parameters)
{
	using namespace HawkeyeTraversalTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnScene(TestWorld, 0.f, 0.f);
	if (!Kate)
	{
		return false;
	}
	UHawkeyeMovementComponent* Movement = Kate->GetHawkeyeMovement();
	const UPrimitiveComponent* Capsule = Kate->GetCapsuleComponent();
	const FVector Start = Kate->GetActorLocation();
	// Two walls across a straight path 600 cm along +X: the move's own at x = 200, another at x = 400.
	AHawkeyeTestBlocker* Own = SpawnBox(TestWorld, FVector(200.f, -200.f, 0.f), FVector(220.f, 200.f, 400.f));
	AHawkeyeTestBlocker* Other = SpawnBox(TestWorld, FVector(400.f, -200.f, 0.f), FVector(420.f, 200.f, 400.f));
	FHawkeyeTraversalMove Move = PathMove({ Start, Start + FVector(600.f, 0.f, 0.f) }, 0.5f, EHawkeyeTraversalExit::Hold);
	Move.PassThrough.Add(Own);
	Movement->StartTraversal(Move);
	TestTrue(TEXT("Its own obstacle is passed through while it runs"), Capsule->GetMoveIgnoreActors().Contains(Own));
	for (int32 Tick = 0; Tick < 40 && !Movement->IsTraversalHolding(); ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	const float Radius = Kate->GetCapsuleComponent()->GetScaledCapsuleRadius();
	TestTrue(FString::Printf(TEXT("Through its own wall, stopped by the other one (x = %.0f)"), Kate->GetActorLocation().X),
		Kate->GetActorLocation().X > 220.f + Radius && Kate->GetActorLocation().X <= 400.f - Radius + 1.f);
	TestFalse(TEXT("The other was never passed through"), Capsule->GetMoveIgnoreActors().Contains(Other));

	// Let go: the move's obstacle is solid again, so a move that does not name it is stopped by it.
	Movement->ReleaseTraversal(FVector::ZeroVector, FVector::ZeroVector);
	TestFalse(TEXT("Once the move is over it is not passed through"), Capsule->GetMoveIgnoreActors().Contains(Own));
	Kate->SetActorLocation(Start);
	Movement->StartTraversal(PathMove({ Start, Start + FVector(600.f, 0.f, 0.f) }, 0.5f, EHawkeyeTraversalExit::Hold));
	for (int32 Tick = 0; Tick < 40 && !Movement->IsTraversalHolding(); ++Tick)
	{
		Kate->TestTickMovement(1.f / 60.f);
	}
	TestTrue(FString::Printf(TEXT("A later move that does not name it stops at it (x = %.0f)"), Kate->GetActorLocation().X),
		Kate->GetActorLocation().X <= 200.f - Radius + 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalRules, "Hawkeye.Movement.Traversal.ClipHeightAndTimingRules",
	HawkeyeTraversalTest::Flags)

bool FHawkeyeTraversalRules::RunTest(const FString& Parameters)
{
	const UParkourComponent* Parkour = GetDefault<UParkourComponent>();
	const TArray<FHawkeyeParkourClip>& Mantles = Parkour->MantleClips;
	TestEqual(TEXT("Two mantle clips: the 1 m mantle and the 2.5 m climb"), Mantles.Num(), 2);
	TestEqual(TEXT("A 60 cm top: the 1 m mantle"), UParkourComponent::ChooseClipForHeight(Mantles, 60.f), 0);
	TestEqual(TEXT("150 cm: still the 1 m mantle"), UParkourComponent::ChooseClipForHeight(Mantles, 150.f), 0);
	TestEqual(TEXT("151 cm: the 2.5 m climb"), UParkourComponent::ChooseClipForHeight(Mantles, 151.f), 1);
	TestEqual(TEXT("200 cm: the 2.5 m climb"), UParkourComponent::ChooseClipForHeight(Mantles, 200.f), 1);
	TestEqual(TEXT("No clips: none"), UParkourComponent::ChooseClipForHeight({}, 100.f), static_cast<int32>(INDEX_NONE));
	FHawkeyeParkourClip Low;
	Low.MaxHeight = 100.f;
	TestEqual(TEXT("Only a clip for low tops: none for a tall one"), UParkourComponent::ChooseClipForHeight({ Low }, 120.f),
		static_cast<int32>(INDEX_NONE));

	using EMove = EHawkeyeParkourMove;
	TestEqual(TEXT("Vault from still: 300"), Parkour->GetExitSpeed(EMove::Vault, 0.f), 300.f);
	TestEqual(TEXT("Vault at a run: her speed"), Parkour->GetExitSpeed(EMove::Vault, 520.f), 520.f);
	TestEqual(TEXT("Vault faster than 750: 750"), Parkour->GetExitSpeed(EMove::Vault, 1200.f), 750.f);
	TestEqual(TEXT("Mantle: steps on at 150 whatever she came in at"), Parkour->GetExitSpeed(EMove::Mantle, 700.f), 150.f);
	TestEqual(TEXT("Climb: 150"), Parkour->GetExitSpeed(EMove::Climb, 0.f), 150.f);
	TestEqual(TEXT("A hang move leaves nothing"), Parkour->GetExitSpeed(EMove::LedgeGrab, 400.f), 0.f);

	TestEqual(TEXT("A vault from still takes 0.5 s"), Parkour->GetVaultSeconds(220.f, 0.f), 0.5f);
	TestEqual(TEXT("At a jog that covers it slower: 0.5 s"), Parkour->GetVaultSeconds(220.f, 300.f), 0.5f);
	TestEqual(TEXT("At 550 cm/s: 0.4 s"), Parkour->GetVaultSeconds(220.f, 550.f), 0.4f, 0.001f);
	TestEqual(TEXT("Never under 0.35 s"), Parkour->GetVaultSeconds(220.f, 2000.f), 0.35f);

	FHawkeyeTraversalMove Move;
	Move.Keys.AddDefaulted_GetRef().Location = FVector::ZeroVector;
	Move.Keys.AddDefaulted_GetRef().Location = FVector(100.f, 0.f, 0.f);
	Move.Keys.AddDefaulted_GetRef().Location = FVector(100.f, 100.f, 0.f);
	Move.Keys[2].Yaw = 90.f;
	TestTrue(TEXT("The path at a quarter: half way along its first step"), Move.Sample(0.25f).Location.Equals(FVector(50.f, 0.f, 0.f)));
	TestTrue(TEXT("At three quarters: half way along its second"), Move.Sample(0.75f).Location.Equals(FVector(100.f, 50.f, 0.f)));
	TestEqual(TEXT("With the yaw between its keys"), Move.Sample(0.75f).Yaw, 45.f);
	TestTrue(TEXT("Past the end: the end"), Move.Sample(1.5f).Location.Equals(FVector(100.f, 100.f, 0.f)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
