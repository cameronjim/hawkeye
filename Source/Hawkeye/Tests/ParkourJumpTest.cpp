#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/ParkourComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The forgiving jump key (claude-docs/gameplay-semantics.md, traversal, 2026-09-28): the fan probe
 * round the stick or the facing, the 180 cm reach and the 40 cm floor, the late catch after a
 * plain jump, and the buffered press during a move. Every press goes through AHawkeyeCharacter::Jump,
 * the path the key takes. Kate stands at the origin facing +X with her feet at z = 0; the world
 * does not tick, so a jump is flown by hand: the mode set to Falling and the capsule stepped along
 * a ballistic arc, TickJumpAssist called each step.
 */
namespace HawkeyeParkourJumpTest
{
	static constexpr float HalfHeight = 88.f;
	static constexpr float Radius = 34.f;

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

	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, float Yaw = 0.f)
	{
		SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
			AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Yaw, 0.f)));
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	/** A block whose front face is at x = Front, Height tall, Depth deep, 400 wide. */
	static AHawkeyeTestBlocker* SpawnBlock(const FHawkeyeTestWorld& TestWorld, float Height, float Depth, float Front)
	{
		return SpawnBox(TestWorld, FVector(Front, -200.f, 0.f), FVector(Front + Depth, 200.f, Height));
	}

	/** The stick pushed along Direction (world), as Input_Move leaves it for the movement to consume. */
	static void PushStick(ACharacter* Kate, const FVector& Direction)
	{
		Kate->AddMovementInput(Direction.GetSafeNormal2D(), 1.f);
		Kate->ConsumeMovementInputVector();
	}

	static float Feet(const ACharacter* Kate)
	{
		return Kate->GetActorLocation().Z - HalfHeight;
	}

	/**
	 * Flies a plain jump by hand at Velocity for up to Seconds, calling TickJumpAssist each 1/60 s,
	 * until a move starts. Returns the seconds flown; stops early if she would reach the ground.
	 */
	static float FlyJump(ACharacter* Kate, UParkourComponent* Parkour, FVector Velocity, float Seconds)
	{
		UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Falling);
		const float Dt = 1.f / 60.f;
		float Flown = 0.f;
		const float GroundZ = Kate->GetActorLocation().Z;
		while (Flown < Seconds && !Parkour->IsBusy())
		{
			Velocity.Z -= 980.f * Dt;
			Movement->Velocity = Velocity;
			FVector Next = Kate->GetActorLocation() + Velocity * Dt;
			if (Next.Z < GroundZ - 400.f)
			{
				break;
			}
			Kate->SetActorLocation(Next, /*bSweep=*/true);
			Movement->Velocity = Velocity;
			Parkour->TickJumpAssist(Dt);
			Flown += Dt;
		}
		return Flown;
	}

	/** Runs the current move to its end by ticking the movement, at most Seconds. */
	static void RunMove(AHawkeyeAimTestCharacter* Kate, float Seconds)
	{
		for (float T = 0.f; T < Seconds && Kate->GetParkourComponent()->IsPerformingMove(); T += 0.05f)
		{
			Kate->TestTickMovement(0.05f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourJumpHeights, "Hawkeye.Parkour.JumpKeyAtEveryHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourJumpHeights::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourJumpTest;
	using EMove = EHawkeyeParkourMove;
	struct FCase
	{
		float Height;
		float Depth;
		EMove Expected;
	};
	// Thin walls at vault height (a clear floor beyond), deep blocks above it.
	const FCase Cases[] = {
		{ 50.f, 60.f, EMove::Vault },
		{ 90.f, 60.f, EMove::Vault },
		{ 150.f, 300.f, EMove::Mantle },
		{ 200.f, 300.f, EMove::Mantle },
		{ 240.f, 300.f, EMove::LedgeGrab },
		{ 280.f, 300.f, EMove::None },
	};
	enum class EApproach : uint8 { Straight, Touching, FarStraight, Facing30, Stick30 };
	const TCHAR* ApproachNames[] = { TEXT("straight at 66 cm"), TEXT("standing against it"), TEXT("straight at 166 cm"),
		TEXT("facing 30 degrees off"), TEXT("facing sideways, stick 30 degrees off") };

	for (const FCase& Case : Cases)
	{
		for (int32 A = 0; A < 5; ++A)
		{
			const EApproach Approach = static_cast<EApproach>(A);
			const FString What = FString::Printf(TEXT("%.0f cm block, %s"), Case.Height, ApproachNames[A]);
			const FHawkeyeTestWorld TestWorld;
			const float Yaw = Approach == EApproach::Facing30 ? 30.f : (Approach == EApproach::Stick30 ? 90.f : 0.f);
			AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, Yaw);
			const float Front = Approach == EApproach::Touching ? Radius + 1.f : (Approach == EApproach::FarStraight ? 200.f : 100.f);
			SpawnBlock(TestWorld, Case.Height, Case.Depth, Front);
			UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
			if (!Parkour)
			{
				AddError(TEXT("No Kate"));
				return false;
			}
			if (Approach == EApproach::Stick30)
			{
				PushStick(Kate, FVector(1.f, 0.f, 0.f).RotateAngleAxis(30.f, FVector::UpVector));
			}

			Kate->Jump();
			TestEqual(What, Parkour->GetActiveMove(), Case.Expected);
			if (Case.Expected == EMove::None)
			{
				TestTrue(What + TEXT(": the press says why"), Parkour->GetLastJumpRefusal().Contains(TEXT("too tall")));
				TestTrue(What + TEXT(": a plain jump keeps looking"), Parkour->IsLateCatchArmed());
				if (Approach == EApproach::FarStraight)
				{
					continue;   // standing 166 cm off, a straight-up jump never comes within reach
				}
				// Up she goes: the late catch takes the 280 cm top as a hang as soon as it is in a grab's reach.
				FlyJump(Kate, Parkour, FVector(0.f, 0.f, 420.f), 0.8f);
				TestEqual(What + TEXT(": the late catch grabs it on the way up"), Parkour->GetActiveMove(), EMove::LedgeGrab);
				RunMove(Kate, 1.f);
				TestTrue(What + TEXT(": and hangs from it"), Parkour->IsHanging());
			}
			else if (Case.Expected == EMove::LedgeGrab || Case.Expected == EMove::Mantle)
			{
				TestFalse(What + TEXT(": no late catch once a move started"), Parkour->IsLateCatchArmed());
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourJumpRefusals, "Hawkeye.Parkour.JumpKeyLogsWhyNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourJumpRefusals::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourJumpTest;
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		Kate->Jump();
		TestFalse(TEXT("Open ground: no move"), Kate->GetParkourComponent()->IsBusy());
		TestTrue(TEXT("Because there is nothing in the fan"),
			Kate->GetParkourComponent()->GetLastJumpRefusal().Contains(TEXT("no obstacle")));
	}
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBlock(TestWorld, 30.f, 60.f, 100.f);
		Kate->Jump();
		TestFalse(TEXT("A 30 cm kerb: no move"), Kate->GetParkourComponent()->IsBusy());
	}
	{
		// A 150 cm block with a wall set 30 cm back on its top: no room to stand there.
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBlock(TestWorld, 150.f, 300.f, 100.f);
		SpawnBox(TestWorld, FVector(130.f, -200.f, 200.f), FVector(400.f, 200.f, 400.f));
		Kate->Jump();
		TestEqual(TEXT("Never a mantle onto a top the capsule cannot stand on"),
			Kate->GetParkourComponent()->GetActiveMove(), EHawkeyeParkourMove::None);
		TestTrue(TEXT("And the log says so"), Kate->GetParkourComponent()->GetLastJumpRefusal().Contains(TEXT("no room")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourJumpEarly, "Hawkeye.Parkour.JumpPressedEarlyWhileRunning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourJumpEarly::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourJumpTest;
	// The old key found a wall within 120 cm of the capsule. Running at 500 cm/s, 0.2 s early is 100 cm
	// further: a press at 166 cm is inside the new reach; one at 220 cm is not, and the late catch has it.
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBlock(TestWorld, 150.f, 300.f, Radius + 166.f);
		PushStick(Kate, FVector::ForwardVector);
		Kate->GetCharacterMovement()->Velocity = FVector(500.f, 0.f, 0.f);
		Kate->Jump();
		TestEqual(TEXT("166 cm out: the press itself mantles"), Kate->GetParkourComponent()->GetActiveMove(),
			EHawkeyeParkourMove::Mantle);
	}
	for (const float Height : { 90.f, 150.f, 200.f, 240.f })
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBlock(TestWorld, Height, 300.f, Radius + 220.f);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		PushStick(Kate, FVector::ForwardVector);
		Kate->Jump();
		const FString What = FString::Printf(TEXT("%.0f cm block, pressed 220 cm out at a run"), Height);
		TestFalse(What + TEXT(": nothing in reach at the press"), Parkour->IsBusy());
		TestTrue(What + TEXT(": the late catch is on"), Parkour->IsLateCatchArmed());
		const float Flown = FlyJump(Kate, Parkour, FVector(500.f, 0.f, 420.f), 0.8f);
		TestTrue(What + TEXT(": caught in the air inside 0.8 s"), Parkour->IsBusy() && Flown <= 0.8f);
		const EHawkeyeParkourMove Move = Parkour->GetActiveMove();
		TestTrue(What + TEXT(": onto it or hanging from it"), Move == EHawkeyeParkourMove::Mantle
			|| Move == EHawkeyeParkourMove::LedgeGrab || Move == EHawkeyeParkourMove::Vault);
		RunMove(Kate, 1.f);
		TestTrue(What + TEXT(": up there (on top or hanging)"),
			Parkour->IsHanging() || Feet(Kate) >= Height - 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourJumpBuffer, "Hawkeye.Parkour.JumpBufferedDuringAMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourJumpBuffer::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourJumpTest;
	for (const bool bLate : { false, true })
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		// A 150 cm block, and a second 150 cm step on top of it 150 cm further in.
		SpawnBlock(TestWorld, 150.f, 500.f, 100.f);
		SpawnBox(TestWorld, FVector(300.f, -200.f, 150.f), FVector(800.f, 200.f, 300.f));
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Kate->Jump();
		TestEqual(TEXT("The first press mantles"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Mantle);

		// Mashing: pressed 0.6 s into the 0.8 s mantle (0.2 s before it ends), or 0.3 s in (0.5 s before).
		const float PressAt = bLate ? 0.3f : 0.6f;
		for (float T = 0.f; T < PressAt - 0.01f; T += 0.05f)
		{
			Kate->TestTickMovement(0.05f);
			Parkour->TickJumpAssist(0.05f);
		}
		Kate->Jump();
		TestTrue(TEXT("A press mid-move is held, not eaten"), Parkour->IsJumpBuffered() || bLate);
		TestEqual(TEXT("And does not disturb the move"), Parkour->GetActiveMove(), EHawkeyeParkourMove::Mantle);
		for (float T = 0.f; T < 0.6f && Parkour->IsPerformingMove(); T += 0.05f)
		{
			Parkour->TickJumpAssist(0.05f);
			Kate->TestTickMovement(0.05f);
		}
		TestFalse(TEXT("The mantle is over"), Parkour->IsPerformingMove());
		Parkour->TickJumpAssist(0.02f);
		if (bLate)
		{
			TestFalse(TEXT("A press 0.5 s before the end has lapsed (0.3 s buffer)"), Parkour->IsBusy());
		}
		else
		{
			TestEqual(TEXT("The held press fires at the end: the next step is mantled"), Parkour->GetActiveMove(),
				EHawkeyeParkourMove::Mantle);
			RunMove(Kate, 1.f);
			TestEqual(TEXT("And she is on the second step"), Feet(Kate), 302.f, 3.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourLateCatchFalling, "Hawkeye.Parkour.LateCatchFallingPastALedge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourLateCatchFalling::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourJumpTest;
	// Jumped off a 600 cm roof towards a wall whose top is 350 cm up, 66 cm ahead: she falls past it.
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	SpawnBlock(TestWorld, 350.f, 300.f, 100.f);
	Kate->SetActorLocation(FVector(0.f, 0.f, 520.f + HalfHeight));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(0.f, 0.f, -50.f);
	Parkour->ArmLateCatch();
	TestTrue(TEXT("Armed"), Parkour->IsLateCatchArmed());
	FlyJump(Kate, Parkour, FVector(0.f, 0.f, -50.f), 0.8f);
	TestTrue(TEXT("Caught on the way down"), Parkour->IsBusy());
	TestTrue(TEXT("Before she has fallen more than a storey past the top"), Feet(Kate) > 350.f - 330.f);
	RunMove(Kate, 1.f);
	TestTrue(TEXT("On top of the wall or hanging from its edge"),
		Parkour->IsHanging() || FMath::IsNearlyEqual(Feet(Kate), 352.f, 3.f));

	// Landing ends the window: a jump that comes down on open ground catches nothing later.
	const FHawkeyeTestWorld OpenWorld;
	AHawkeyeAimTestCharacter* Runner = SpawnKate(OpenWorld);
	UParkourComponent* RunnerParkour = Runner->GetParkourComponent();
	Runner->Jump();
	TestTrue(TEXT("Armed by the plain jump"), RunnerParkour->IsLateCatchArmed());
	Runner->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	RunnerParkour->TickJumpAssist(0.05f);
	Runner->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	RunnerParkour->TickJumpAssist(0.05f);
	TestFalse(TEXT("Landing cancels it"), RunnerParkour->IsLateCatchArmed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourJumpStandstill, "Hawkeye.Parkour.JumpOverAThinWallFromAStandstill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourJumpStandstill::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourJumpTest;
	using EMove = EHawkeyeParkourMove;
	// 2026-09-29, "if you try to jump moving forward with no momentum, it still takes a hundred tries to
	// hurdle over": one press at a thin wall, from pressed against it to 30 cm off, standing, walking or
	// jogging, square or turned 20 degrees, is a vault or a mantle, from the press or the late catch
	// within 0.8 s, and never a plain jump into the wall.
	struct FWall
	{
		float Height;
		float Depth;
	};
	const FWall Walls[] = { { 90.f, 30.f }, { 70.f, 40.f }, { 100.f, 40.f }, { 40.f, 30.f }, { 110.f, 30.f } };
	for (const FWall& Wall : Walls)
	{
		for (const float Gap : { 0.f, 5.f, 10.f, 30.f })
		{
			for (const float Speed : { 0.f, 100.f, 250.f })
			{
				for (const float Yaw : { 0.f, 20.f })
				{
					// The park walls and the extra heights only from a standstill, square and turned.
					const bool bThin90 = Wall.Height == 90.f;
					if (!bThin90 && Speed > 0.f)
					{
						continue;
					}
					const FString What = FString::Printf(TEXT("%.0f cm wall %.0f deep, %.0f cm off, %.0f cm/s, facing %.0f deg"),
						Wall.Height, Wall.Depth, Gap, Speed, Yaw);
					const FHawkeyeTestWorld TestWorld;
					AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, Yaw);
					UParkourComponent* Parkour = Kate ? Kate->GetParkourComponent() : nullptr;
					if (!Parkour)
					{
						AddError(TEXT("No Kate"));
						return false;
					}
					// The capsule's front is Gap from the face along the facing.
					const float Front = (Radius + Gap) / FMath::Cos(FMath::DegreesToRadians(Yaw));
					SpawnBlock(TestWorld, Wall.Height, Wall.Depth, Front);
					const FVector Facing = Kate->GetActorForwardVector();
					if (Speed > 0.f)
					{
						PushStick(Kate, Facing);
						Kate->GetCharacterMovement()->Velocity = Facing * Speed;
					}
					Kate->Jump();
					EMove Move = Parkour->GetActiveMove();
					const bool bFromPress = Move != EMove::None;
					if (!bFromPress)
					{
						TestTrue(What + TEXT(": a plain jump keeps looking"), Parkour->IsLateCatchArmed());
						FlyJump(Kate, Parkour, FVector(Facing.X * Speed, Facing.Y * Speed, 420.f), 0.8f);
						Move = Parkour->GetActiveMove();
					}
					TestTrue(What + TEXT(": vaulted or mantled (") + UEnum::GetValueAsString(Move) + TEXT(", ")
						+ Parkour->GetLastJumpRefusal() + TEXT(")"), Move == EMove::Vault || Move == EMove::Mantle);
					RunMove(Kate, 1.f);
					TestTrue(What + TEXT(": over it or on it"),
						Kate->GetActorLocation().X > Front + Wall.Depth || Feet(Kate) >= Wall.Height - 1.f);
				}
			}
		}
	}

	// The 150 cm block (150 deep) from a standstill, pressed against it or near: mantled.
	for (const float Gap : { 0.f, 5.f, 10.f })
	{
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		SpawnBlock(TestWorld, 150.f, 150.f, Radius + Gap);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Kate->Jump();
		if (!Parkour->IsBusy())
		{
			FlyJump(Kate, Parkour, FVector(0.f, 0.f, 420.f), 0.8f);
		}
		TestEqual(FString::Printf(TEXT("150 cm block %.0f cm off, standing: mantled"), Gap), Parkour->GetActiveMove(), EMove::Mantle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeParkourLateCatchTakeOff, "Hawkeye.Parkour.LateCatchMeasuresFromTheTakeOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeParkourLateCatchTakeOff::RunTest(const FString& Parameters)
{
	using namespace HawkeyeParkourJumpTest;
	// A jog and a press with nothing in reach; a 90 cm wall 150 cm ahead comes into the late catch's
	// 100 cm only once her feet are 50 to 70 cm up, its top 20 to 40 cm above them but 90 above the
	// take-off, so the late catch goes over it. A second press on the way up does not move the take-off.
	for (const bool bMash : { false, true })
	{
		const FString What = bMash ? TEXT("Pressed again in the air") : TEXT("One press");
		const FHawkeyeTestWorld TestWorld;
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		PushStick(Kate, FVector::ForwardVector);
		Kate->GetCharacterMovement()->Velocity = FVector(250.f, 0.f, 0.f);
		Kate->Jump();
		TestTrue(What + TEXT(": a plain jump"), !Parkour->IsBusy() && Parkour->IsLateCatchArmed());
		SpawnBlock(TestWorld, 90.f, 30.f, Radius + 150.f);
		FlyJump(Kate, Parkour, FVector(250.f, 0.f, 420.f), 0.12f);
		TestFalse(What + TEXT(": not yet in the late catch's reach"), Parkour->IsBusy());
		if (bMash)
		{
			Kate->Jump();
		}
		if (!Parkour->IsBusy())
		{
			FlyJump(Kate, Parkour, Kate->GetCharacterMovement()->Velocity, 0.68f);
		}
		const EHawkeyeParkourMove Move = Parkour->GetActiveMove();
		TestTrue(What + TEXT(": over the wall (") + UEnum::GetValueAsString(Move) + TEXT(", ") + Parkour->GetLastJumpRefusal() + TEXT(")"),
			Move == EHawkeyeParkourMove::Vault || Move == EHawkeyeParkourMove::Mantle);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
