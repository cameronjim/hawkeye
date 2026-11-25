#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/CameraRules.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/ParkourComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The ledge body on the parkour component (claude-docs/gameplay-semantics.md, "Ledge body"), on boxes: a roof dismount
 * plants and turns, then hangs with its hands on the edge; a catch swings in under the hands and settles; a climb presses the
 * palms onto the top; a let-go fades the IK; a short ledge closes the hands up; and the capsule's path is exactly what it
 * was with the ledge body off. No content: no AnimBP, so the clips are the fallback and only the pose numbers are read.
 */
namespace HawkeyeLedgeParkourTest
{
	static constexpr float HalfHeight = 88.f;
	static constexpr float Tick = 0.05f;
	static constexpr float Top = 800.f;

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

	/** A roof whose top is z = 0 ending at x = RoofEnd, a 90 cm, 30 cm thick parapet on its edge, the street 600 below. */
	static void SpawnRoofEdge(const FHawkeyeTestWorld& TestWorld, float RoofEnd = 110.f)
	{
		SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(RoofEnd, 3000.f, 0.f));
		SpawnBox(TestWorld, FVector(RoofEnd, -3000.f, -700.f), FVector(3000.f, 3000.f, -600.f));
		SpawnBox(TestWorld, FVector(RoofEnd - 30.f, -300.f, 0.f), FVector(RoofEnd, 300.f, 90.f));
	}

	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& FeetAt, float Yaw = 0.f)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
			AHawkeyeAimTestCharacter::StaticClass(), FeetAt + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Yaw, 0.f)));
		if (Kate)
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	/** The movement (which runs a move and holds a hang) and then the component's own tick (the ledge body), one step. */
	static void Step(UParkourComponent* Parkour)
	{
		Cast<AHawkeyeAimTestCharacter>(Parkour->GetOwner())->TestTickMovement(Tick);
		static_cast<UActorComponent*>(Parkour)->TickComponent(Tick, LEVELTICK_All, nullptr);
	}

	static void StepFor(UParkourComponent* Parkour, float Seconds)
	{
		for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Tick)
		{
			Step(Parkour);
		}
	}

	/** Falling past the face at x = FaceX (facing +X) with the feet 200 below the top: she catches it and hangs. */
	static bool CatchLedge(AHawkeyeAimTestCharacter* Kate, float FaceX, float Y, float DownSpeed = 150.f)
	{
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Kate->SetActorLocation(FVector(FaceX - 45.f, Y, Top - 200.f + HalfHeight));
		Kate->SetActorRotation(FRotator::ZeroRotator);
		UCharacterMovementComponent* Movement = Kate->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = FVector(0.f, 0.f, -DownSpeed);
		return Parkour->TryCatchLedge();
	}

	static float YawOf(const AActor* Actor)
	{
		return static_cast<float>(FRotator::NormalizeAxis(Actor->GetActorRotation().Yaw));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeRoofDismount, "Hawkeye.Ledge.RoofDismountPlantsTurnsAndHangs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeRoofDismount::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnRoofEdge(TestWorld);
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("The dismount starts"), Parkour->TryDropToHang(FVector::ForwardVector, TEXT("test"))))
	{
		return false;
	}
	TestEqual(TEXT("No turn has happened at the start"), YawOf(Kate), 0.f, 0.5f);

	// Step through the 0.5 s move: the stages come in order, the turn is monotonic and done by the plant's end.
	TArray<ELedgeStage> Stages;
	float LastYaw = 0.f;
	bool bMonotonic = true;
	float YawAtTurnEnd = -1.f;
	float PlantHandAlpha = -1.f;
	float HandAlphaAtHang = -1.f;
	// Eleven steps (0.55 s): ten of 0.05 s can add up to a hair under the move's 0.5 s.
	for (int32 StepIndex = 1; StepIndex <= 11; ++StepIndex)
	{
		Step(Parkour);
		const ELedgeStage Stage = Parkour->GetLedgeStage();
		if (Stage != ELedgeStage::None && (Stages.Num() == 0 || Stages.Last() != Stage))
		{
			Stages.Add(Stage);
		}
		const float Yaw = FMath::Abs(YawOf(Kate));
		bMonotonic = bMonotonic && Yaw >= LastYaw - 0.5f;
		LastYaw = Yaw;
		if (StepIndex == 5)
		{
			// 0.25 s into 0.5 s: past the 0.225 s the turn takes.
			YawAtTurnEnd = Yaw;
		}
		FLedgeBodyPose Pose;
		if (StepIndex == 2 && Parkour->GetLedgeBodyPose(Pose))
		{
			PlantHandAlpha = Pose.HandAlpha[1];
		}
	}
	TestTrue(TEXT("She turns round without ever turning back"), bMonotonic);
	TestEqual(TEXT("The turn is done by half way through (the plant): facing the wall"), YawAtTurnEnd, 180.f, 2.f);
	TestTrue(TEXT("The stages ran plant, lower, settle in that order"),
		Stages.Num() >= 2 && Stages[0] == ELedgeStage::Plant && Stages.Contains(ELedgeStage::Lower));
	TestTrue(TEXT("and not backwards (plant before lower before settle)"),
		Stages.IndexOfByKey(ELedgeStage::Plant) < Stages.IndexOfByKey(ELedgeStage::Lower));
	TestTrue(TEXT("The hand is not yet fully on the edge in the first moments of the plant"), PlantHandAlpha < 0.95f);
	TestTrue(TEXT("Hanging after 0.5 s"), Parkour->IsHanging());
	TestEqual(TEXT("Feet 145 cm under the parapet top, as before the ledge body"), static_cast<float>(Kate->GetActorLocation().Z - HalfHeight), 90.f - 145.f, 1.f);
	TestEqual(TEXT("36 cm out from the outer face"), static_cast<float>(Kate->GetActorLocation().X), 146.f, 1.f);
	TestEqual(TEXT("facing the wall"), FMath::Abs(YawOf(Kate)), 180.f, 1.f);

	// A moment of hanging: the hands are on the edge at shoulder width, the body is held to it.
	StepFor(Parkour, 0.3f);
	FLedgeBodyPose Pose;
	if (!TestTrue(TEXT("There is a ledge pose while she hangs"), Parkour->GetLedgeBodyPose(Pose)))
	{
		return false;
	}
	HandAlphaAtHang = Pose.HandAlpha[0];
	TestTrue(TEXT("The hands are held in full"), HandAlphaAtHang > 0.95f && Pose.HandAlpha[1] > 0.95f);
	TestTrue(TEXT("The face's normal points out of the wall (+X here)"), Pose.Normal.Equals(FVector(1.f, 0.f, 0.f), 0.01f));
	TestEqual(TEXT("The wall's plane runs through the edge (x = 110)"), Pose.PlaneD, 110.f, 1.f);
	TestEqual(TEXT("The wrists are on the top, a hand's thickness over it"), static_cast<float>(Pose.Hand[0].Z), 90.f + Pose.Tuning.WristHeight, 0.5f);
	TestEqual(TEXT("and just out over the edge"), static_cast<float>(Pose.Hand[1].X), 110.f + Pose.Tuning.WristOut, 0.5f);
	TestEqual(TEXT("The hands are a shoulder width apart"), static_cast<float>(FMath::Abs(Pose.Hand[1].Y - Pose.Hand[0].Y)), 2.f * Pose.Tuning.HandHalfGap, 1.5f);
	TestEqual(TEXT("and centred on her"), static_cast<float>(Pose.Hand[0].Y + Pose.Hand[1].Y), 0.f, 1.5f);
	TestEqual(TEXT("The grip is closed"), Pose.Grip[0], 1.f, 0.05f);
	TestTrue(TEXT("The body and the feet are held to the wall"), Pose.BodyAlpha > 0.9f && Pose.FootAlpha > 0.9f);

	// Left alone the hang settles into its steady stage.
	StepFor(Parkour, 2.0f);
	TestEqual(TEXT("Settled after a couple of seconds"), static_cast<int32>(Parkour->GetLedgeStage()), static_cast<int32>(ELedgeStage::Hang));
	TestTrue(TEXT("with the swing still"), FMath::Abs(Parkour->GetLedgeSwingDegrees()) < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgePathUnchanged, "Hawkeye.Ledge.CapsulePathIsUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgePathUnchanged::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeParkourTest;
	// The same dismount, a catch and a climb with the ledge body on and off: the capsule is where it was, every step.
	TArray<FVector> Runs[2];
	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
		const FHawkeyeTestWorld TestWorld;
		SpawnRoofEdge(TestWorld);
		AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
		UParkourComponent* Parkour = Kate->GetParkourComponent();
		Parkour->bLedgeBodyPose = Mode == 1;
		if (!TestTrue(TEXT("The dismount starts"), Parkour->TryDropToHang(FVector::ForwardVector, TEXT("test"))))
		{
			return false;
		}
		for (int32 StepIndex = 0; StepIndex < 14; ++StepIndex)
		{
			Step(Parkour);
			Runs[Mode].Add(Kate->GetActorLocation());
		}
		// Climb back up (a roof to stand on), the same either way.
		Parkour->ClimbFromHang();
		for (int32 StepIndex = 0; StepIndex < 24; ++StepIndex)
		{
			Step(Parkour);
			Runs[Mode].Add(Kate->GetActorLocation());
		}
	}
	TestEqual(TEXT("The same number of steps"), Runs[0].Num(), Runs[1].Num());
	float Worst = 0.f;
	for (int32 Index = 0; Index < FMath::Min(Runs[0].Num(), Runs[1].Num()); ++Index)
	{
		Worst = FMath::Max(Worst, static_cast<float>((Runs[0][Index] - Runs[1][Index]).Size()));
	}
	TestTrue(*FString::Printf(TEXT("The capsule's path is the same with the ledge body on and off (worst %.3f cm)"), Worst), Worst < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeCatchSwing, "Hawkeye.Ledge.CatchSwingsInAndSettles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeCatchSwing::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(400.f, 400.f, Top));
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(-500.f, 0.f, HalfHeight), FRotator::ZeroRotator));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("The fall catches the ledge"), CatchLedge(Kate, 100.f, 0.f, 500.f)))
	{
		return false;
	}
	TArray<ELedgeStage> Stages;
	float PeakSwing = 0.f;
	float LowestSwing = 99.f;
	int32 StepsToHang = 0;
	while (Parkour->IsPerformingMove() && StepsToHang < 30)
	{
		Step(Parkour);
		++StepsToHang;
		const ELedgeStage Stage = Parkour->GetLedgeStage();
		if (Stage != ELedgeStage::None && (Stages.Num() == 0 || Stages.Last() != Stage))
		{
			Stages.Add(Stage);
		}
		PeakSwing = FMath::Max(PeakSwing, Parkour->GetLedgeSwingDegrees());
	}
	TestTrue(TEXT("Hanging"), Parkour->IsHanging());
	TestTrue(TEXT("A catch reaches, then catches (a 0.15 s move)"),
		Stages.Num() >= 1 && Stages[0] == ELedgeStage::Reach && Stages.Contains(ELedgeStage::Catch));
	TestTrue(TEXT("The swing has started: the feet are out from the wall"), PeakSwing > 5.f);

	// 3.5 s of hanging: the swing goes in, touches the wall, and dies away.
	for (int32 StepIndex = 0; StepIndex < 70; ++StepIndex)
	{
		Step(Parkour);
		LowestSwing = FMath::Min(LowestSwing, Parkour->GetLedgeSwingDegrees());
		PeakSwing = FMath::Max(PeakSwing, Parkour->GetLedgeSwingDegrees());
	}
	TestTrue(TEXT("She swung in as far as the wall lets her and no further"),
		LowestSwing >= Parkour->LedgeBody.SwingWallDegrees - 0.01f && LowestSwing <= Parkour->LedgeBody.SwingWallDegrees + 1.f);
	TestTrue(TEXT("and is hanging still"), FMath::Abs(Parkour->GetLedgeSwingDegrees()) < 0.6f);
	TestEqual(TEXT("in the steady hang stage"), static_cast<int32>(Parkour->GetLedgeStage()), static_cast<int32>(ELedgeStage::Hang));

	// A catch from a slower fall starts with less swing.
	AHawkeyeAimTestCharacter* Slow = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(-500.f, 1000.f, HalfHeight), FRotator::ZeroRotator));
	SpawnBox(TestWorld, FVector(100.f, 600.f, 0.f), FVector(400.f, 1400.f, Top));
	UParkourComponent* SlowParkour = Slow->GetParkourComponent();
	if (TestTrue(TEXT("The slow fall catches"), CatchLedge(Slow, 100.f, 1000.f, 0.f)))
	{
		float SlowPeak = 0.f;
		int32 Steps = 0;
		while (SlowParkour->IsPerformingMove() && Steps++ < 30)
		{
			Step(SlowParkour);
			SlowPeak = FMath::Max(SlowPeak, SlowParkour->GetLedgeSwingDegrees());
		}
		TestTrue(*FString::Printf(TEXT("A standing catch swings less than a fast one (%.1f against %.1f degrees)"), SlowPeak, PeakSwing),
			SlowPeak > 0.f && SlowPeak < PeakSwing);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeClimbHands, "Hawkeye.Ledge.ClimbPressesThePalmsOntoTheTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeClimbHands::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(600.f, 400.f, Top));
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(-500.f, 0.f, HalfHeight), FRotator::ZeroRotator));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging"), CatchLedge(Kate, 100.f, 0.f)))
	{
		return false;
	}
	for (int32 StepIndex = 0; StepIndex < 100 && (Parkour->IsPerformingMove() || StepIndex < 60); ++StepIndex)
	{
		Step(Parkour);
	}
	if (!TestTrue(TEXT("Hanging on a roof"), Parkour->IsHanging()))
	{
		return false;
	}
	if (!TestTrue(TEXT("The climb starts"), Parkour->ClimbFromHang()))
	{
		return false;
	}
	TArray<ELedgeStage> Stages;
	float PullHandX = 0.f;
	float ChestHandX = 0.f;
	float ChestHandAlpha = 0.f;
	float StandHandAlpha = 1.f;
	for (int32 StepIndex = 0; StepIndex < 20 && Parkour->IsPerformingMove(); ++StepIndex)
	{
		Step(Parkour);
		const ELedgeStage Stage = Parkour->GetLedgeStage();
		if (Stage != ELedgeStage::None && (Stages.Num() == 0 || Stages.Last() != Stage))
		{
			Stages.Add(Stage);
		}
		// The pose is read even when it is no longer held (standing up lets the IK go): its weights are what is asked.
		FLedgeBodyPose Pose;
		Parkour->GetLedgeBodyPose(Pose);
		if (Stage == ELedgeStage::Pull)
		{
			PullHandX = static_cast<float>(Pose.Hand[1].X);
		}
		if (Stage == ELedgeStage::ChestOver)
		{
			ChestHandX = static_cast<float>(Pose.Hand[1].X);
			ChestHandAlpha = Pose.HandAlpha[1];
		}
		if (Stage == ELedgeStage::Stand)
		{
			StandHandAlpha = FMath::Min(StandHandAlpha, Pose.HandAlpha[1]);
		}
	}
	TestTrue(TEXT("The climb runs pull, chest over, knee up, stand, in order"),
		Stages.Num() >= 3 && Stages[0] == ELedgeStage::Pull && Stages.IndexOfByKey(ELedgeStage::ChestOver) > 0
			&& Stages.IndexOfByKey(ELedgeStage::KneeUp) > Stages.IndexOfByKey(ELedgeStage::ChestOver));
	// The face is at x = 100 facing -X, so back onto the top is +X.
	TestTrue(*FString::Printf(TEXT("Over the edge the palms are on the top, further back (x %.0f against %.0f)"), ChestHandX, PullHandX),
		ChestHandX > PullHandX + 5.f);
	TestTrue(TEXT("and held"), ChestHandAlpha > 0.9f);
	TestTrue(TEXT("Standing up lets go of the IK"), StandHandAlpha < 0.2f);
	StepFor(Parkour, 0.5f);
	FLedgeBodyPose After;
	TestFalse(TEXT("A moment after the climb nothing holds her to the ledge"), Parkour->GetLedgeBodyPose(After));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeVaultRise, "Hawkeye.Ledge.AuthoredVaultKeepsTheHipsOverTheTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeVaultRise::RunTest(const FString& Parameters)
{
	// The authored vault is a hurdle: the hips pass 25 cm over the top (the capsule's centre starts 90 up), never under 10 cm of rise.
	TestEqual(TEXT("A 90 cm top"), UParkourComponent::GetAuthoredVaultRise(90.f), 25.f);
	TestEqual(TEXT("A 110 cm top"), UParkourComponent::GetAuthoredVaultRise(110.f), 45.f);
	TestEqual(TEXT("A low top has the minimum"), UParkourComponent::GetAuthoredVaultRise(60.f), 10.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeCamera, "Hawkeye.Ledge.CameraClosesInAndLiftsForTheClimb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeCamera::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(600.f, 400.f, Top));
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(-500.f, 0.f, HalfHeight), FRotator::ZeroRotator));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging"), CatchLedge(Kate, 100.f, 0.f)))
	{
		return false;
	}
	FHawkeyeCameraTargets Free;
	Free.ArmLength = 340.f;
	for (int32 StepIndex = 0; StepIndex < 100 && (Parkour->IsPerformingMove() || StepIndex < 60); ++StepIndex)
	{
		Step(Parkour);
		FHawkeyeCameraTargets Wanted = Free;
		Parkour->ApplyHangCamera(Wanted, Tick);
	}
	FHawkeyeCameraTargets Hang = Free;
	Parkour->ApplyHangCamera(Hang, Tick);
	TestTrue(*FString::Printf(TEXT("Hanging the boom is shorter (%.0f of 340)"), Hang.ArmLength),
		Hang.ArmLength < 340.f * 0.9f && Hang.ArmLength > 340.f * 0.6f);
	TestTrue(TEXT("and the pivot is lifted"), Hang.PivotLift >= Parkour->LedgeCameraLift - 1.f);
	TestFalse(TEXT("The climb's lift is not in yet"), Parkour->GetLedgeClimbCameraAlpha() > 0.f);
	if (!TestTrue(TEXT("The climb starts"), Parkour->ClimbFromHang()))
	{
		return false;
	}
	float AlphaAtHalfClimb = 0.f;
	FHawkeyeCameraTargets Climb = Free;
	for (int32 StepIndex = 0; StepIndex < 10; ++StepIndex)
	{
		Step(Parkour);
		Climb = Free;
		Parkour->ApplyHangCamera(Climb, Tick);
		AlphaAtHalfClimb = Parkour->GetLedgeClimbCameraAlpha();
	}
	TestTrue(*FString::Printf(TEXT("Half a second into the climb the lens is up with her (alpha %.2f, lift %.0f over the hang's %.0f)"),
		AlphaAtHalfClimb, Climb.PivotLift, Hang.PivotLift), AlphaAtHalfClimb > 0.99f && Climb.PivotLift > Hang.PivotLift + Parkour->LedgeClimbCameraLift - 1.f);
	StepFor(Parkour, 1.5f);
	for (int32 StepIndex = 0; StepIndex < 40; ++StepIndex)
	{
		FHawkeyeCameraTargets Wanted = Free;
		Parkour->ApplyHangCamera(Wanted, Tick);
	}
	TestTrue(TEXT("Standing on top it all eases away"), Parkour->GetLedgeClimbCameraAlpha() < 0.01f && Parkour->GetLedgeFrameAlpha() < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeRelease, "Hawkeye.Ledge.LetGoFadesTheHands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeRelease::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	SpawnBox(TestWorld, FVector(100.f, -400.f, 0.f), FVector(130.f, 400.f, Top));
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(-500.f, 0.f, HalfHeight), FRotator::ZeroRotator));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging"), CatchLedge(Kate, 100.f, 0.f)))
	{
		return false;
	}
	StepFor(Parkour, 1.0f);
	FLedgeBodyPose Held;
	TestTrue(TEXT("Held by the ledge"), Parkour->GetLedgeBodyPose(Held));
	TestTrue(TEXT("Dropping lets go"), Parkour->DropFromHang());
	// After the let-go: the stage is Release, the hands fade out over ReleaseSeconds and the hand targets stay on the edge.
	float LastAlpha = 2.f;
	bool bFading = true;
	bool bSawRelease = false;
	FVector HandWas = Held.Hand[0];
	bool bTargetsStayed = true;
	for (int32 StepIndex = 0; StepIndex < 8; ++StepIndex)
	{
		Step(Parkour);
		FLedgeBodyPose Pose;
		if (!Parkour->GetLedgeBodyPose(Pose))
		{
			break;
		}
		bSawRelease = bSawRelease || Parkour->GetLedgeStage() == ELedgeStage::Release;
		bFading = bFading && Pose.HandAlpha[0] <= LastAlpha + 0.001f;
		LastAlpha = Pose.HandAlpha[0];
		bTargetsStayed = bTargetsStayed && Pose.Hand[0].Equals(HandWas, 0.5);
	}
	TestTrue(TEXT("The let-go is a release stage"), bSawRelease);
	TestTrue(TEXT("The hands fade out, never back in"), bFading);
	TestTrue(TEXT("and the hand targets do not move while they do"), bTargetsStayed);
	StepFor(Parkour, 0.3f);
	FLedgeBodyPose After;
	TestFalse(TEXT("A fraction of a second later nothing is held"), Parkour->GetLedgeBodyPose(After));
	TestEqual(TEXT("and the stage is none"), static_cast<int32>(Parkour->GetLedgeStage()), static_cast<int32>(ELedgeStage::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeShortLedge, "Hawkeye.Ledge.ShortLedgeClosesTheHands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeShortLedge::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeParkourTest;
	const FHawkeyeTestWorld TestWorld;
	SpawnBox(TestWorld, FVector(-3000.f, -3000.f, -100.f), FVector(3000.f, 3000.f, 0.f));
	// A post 40 cm wide: the hands have 40 - 2 * 8 = 24 cm between their margins.
	SpawnBox(TestWorld, FVector(100.f, -20.f, 0.f), FVector(130.f, 20.f, Top));
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(-500.f, 0.f, HalfHeight), FRotator::ZeroRotator));
	UParkourComponent* Parkour = Kate->GetParkourComponent();
	if (!TestTrue(TEXT("Hanging from a short post"), CatchLedge(Kate, 100.f, 0.f)))
	{
		return false;
	}
	StepFor(Parkour, 1.5f);
	FLedgeBodyPose Pose;
	if (!TestTrue(TEXT("There is a pose"), Parkour->GetLedgeBodyPose(Pose)))
	{
		return false;
	}
	const float Margin = Pose.Tuning.HandEndMargin;
	const float Y0 = static_cast<float>(Pose.Hand[0].Y);
	const float Y1 = static_cast<float>(Pose.Hand[1].Y);
	TestTrue(*FString::Printf(TEXT("Both hands are inside the post's ends by the margin (y %.1f and %.1f)"), Y0, Y1),
		FMath::Abs(Y0) <= 20.f - Margin + 1.5f && FMath::Abs(Y1) <= 20.f - Margin + 1.5f);
	TestTrue(*FString::Printf(TEXT("The gap is no more than what is left (%.1f cm of 24)"), FMath::Abs(Y1 - Y0)),
		FMath::Abs(Y1 - Y0) <= 40.f - 2.f * Margin + 1.5f);
	return true;
}

#endif
