#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Player/CameraRules.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeSpringArmComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The camera rules in claude-docs/gameplay-semantics.md, "Camera rules": the lens-height guarantee at every pitch and
 * in every mode, the rate limits, the lag clamp, the hysteresis, the probe against the near plane, and the arm's
 * floor rule, shoulder sweep, cuts and clock (in a test world with a floor and a wall as blockers).
 */
namespace HawkeyeCameraTest
{
	/** A Kate whose hip and aim cameras are the real ones, or null. */
	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector,
			FRotator::ZeroRotator));
	}

	/** One camera mode as a pure function of the pitch: what the character's layers do to the targets. */
	struct FMode
	{
		const TCHAR* Name;
		bool bAim;
		/** Added to the arm (fight +70, a finisher's push, indoors a cap), cm. */
		float ArmDelta;
		float ArmCap;
		/** Added to the socket's height (a landing's dip), cm. */
		float SocketDeltaZ;
		/** 0 hip, 1 aim; the guarantee in between is the smaller of the two. */
		float AimBlend;
	};

	static const FMode Modes[] = {
		{ TEXT("Hip"), false, 0.f, 0.f, 0.f, 0.f },
		{ TEXT("Aim"), true, 0.f, 0.f, 0.f, 1.f },
		{ TEXT("Indoor"), false, 0.f, 250.f, 0.f, 0.f },
		{ TEXT("Fight"), false, 70.f, 0.f, 0.f, 0.f },
		{ TEXT("Finisher push"), false, -60.f, 0.f, 0.f, 0.f },
		{ TEXT("Landing dip"), false, 0.f, 0.f, -30.f, 0.f },
		{ TEXT("Fight and dip"), false, 70.f, 0.f, -30.f, 0.f },
		{ TEXT("Aim and push"), true, -60.f, 0.f, -30.f, 1.f },
		{ TEXT("Halfway in the aim blend"), false, 0.f, 0.f, 0.f, 0.5f },
	};

	/** The camera for a mode, built the way AHawkeyeCharacter::UpdateCamera builds it. */
	static FHawkeyeCameraTargets BuildMode(const AHawkeyeAimTestCharacter* Kate, const FMode& Mode, float Pitch, float HalfHeight)
	{
		using namespace HawkeyeCameraRules;
		const FHawkeyeCameraTargets Hip = ComputePitchTargets(Kate->TestHipCamera(), Kate->TestHipPitchCurve(), Pitch, HalfHeight);
		const FHawkeyeCameraTargets Aim = ComputePitchTargets(Kate->TestAimCamera(), Kate->TestAimPitchCurve(), Pitch, HalfHeight);
		FHawkeyeCameraTargets Blend = FHawkeyeCameraTargets::Lerp(Hip, Aim, Mode.AimBlend);
		if (Mode.ArmCap > 0.f)
		{
			Blend.ArmLength = FMath::Min(Blend.ArmLength, Mode.ArmCap);
		}
		Blend.ArmLength += Mode.ArmDelta;
		Blend.SocketOffset.Z += Mode.SocketDeltaZ;
		const float MinLens = FMath::Lerp(Kate->TestHipPitchCurve().MinLensHeight, Kate->TestAimPitchCurve().MinLensHeight, Mode.AimBlend);
		return EnsureLensHeight(Blend, Pitch, HalfHeight, MinLens);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraLensHeightGuarantee, "Hawkeye.Camera.LensHeightGuarantee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraLensHeightGuarantee::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeCameraTest::SpawnKate(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}

	// Standing, and in the half-height capsule of a roll or a slide.
	const float HalfHeights[] = { Kate->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 50.f };
	int32 Checked = 0;
	float WorstHip = TNumericLimits<float>::Max();
	float WorstAim = TNumericLimits<float>::Max();
	for (const float HalfHeight : HalfHeights)
	{
		for (const HawkeyeCameraTest::FMode& Mode : HawkeyeCameraTest::Modes)
		{
			for (float Pitch = -70.f; Pitch <= 75.f; Pitch += 0.5f)
			{
				const FHawkeyeCameraTargets Targets = HawkeyeCameraTest::BuildMode(Kate, Mode, Pitch, HalfHeight);
				const float Height = HawkeyeCameraRules::LensHeightAboveFeet(Pitch, Targets.ArmLength, Targets.SocketOffset,
					Targets.PivotLift, HalfHeight);
				// Hip is 80 and aim is 60; a blend of the two between them.
				const float Required = FMath::Lerp(80.f, 60.f, Mode.AimBlend);
				++Checked;
				if (Mode.AimBlend == 0.f)
				{
					WorstHip = FMath::Min(WorstHip, Height);
				}
				if (Mode.AimBlend == 1.f)
				{
					WorstAim = FMath::Min(WorstAim, Height);
				}
				if (Height < Required - 0.01f)
				{
					AddError(FString::Printf(TEXT("%s at pitch %.1f, half height %.0f: the lens is %.1f cm above her feet, less than %.0f."),
						Mode.Name, Pitch, HalfHeight, Height, Required));
					return false;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d cases; the lowest lens is %.1f cm (hip) and %.1f cm (aim) above her feet."), Checked, WorstHip, WorstAim));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraPitchCurveShape, "Hawkeye.Camera.PitchCurveShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraPitchCurveShape::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeCameraTest::SpawnKate(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}

	const FHawkeyeCameraTargets Level = Kate->ComputeCameraTargets(false, 0.f);
	TestEqual(TEXT("Level hip arm is the hip camera's"), Level.ArmLength, 350.f);
	TestEqual(TEXT("Level hip lift is none"), Level.PivotLift, 0.f);
	TestEqual(TEXT("Level hip FOV"), Level.FieldOfView, 90.f);
	const FHawkeyeCameraTargets LevelAim = Kate->ComputeCameraTargets(true, 0.f);
	TestEqual(TEXT("Level aim arm is the aim camera's"), LevelAim.ArmLength, 180.f);
	TestEqual(TEXT("Level aim lift is none"), LevelAim.PivotLift, 0.f);
	TestEqual(TEXT("Level aim FOV"), LevelAim.FieldOfView, 70.f);

	TestEqual(TEXT("Up to 5 degrees nothing changes (arm)"), Kate->ComputeCameraTargets(false, 5.f).ArmLength, 350.f, 0.01f);
	TestTrue(TEXT("Above 5 degrees the arm shortens"), Kate->ComputeCameraTargets(false, 15.f).ArmLength < 350.f);
	const FHawkeyeCameraTargets Steep = Kate->ComputeCameraTargets(false, 60.f);
	TestEqual(TEXT("At 60 degrees the arm is the curve's"), Steep.ArmLength, Kate->TestHipPitchCurve().ArmUp, 0.01f);
	TestEqual(TEXT("At 60 degrees the socket is the curve's"), static_cast<float>(Steep.SocketOffset.Z), Kate->TestHipPitchCurve().SocketZUp, 0.01f);
	TestEqual(TEXT("At 60 degrees the view is wider"), Steep.FieldOfView, 90.f + Kate->TestHipPitchCurve().FovUp, 0.01f);
	TestEqual(TEXT("Past 60 it holds (arm)"), Kate->ComputeCameraTargets(false, 75.f).ArmLength, Steep.ArmLength, 0.01f);
	TestEqual(TEXT("The shoulder is untouched by pitch"), static_cast<float>(Steep.SocketOffset.Y), 70.f);
	TestTrue(TEXT("Looking down moves the arm out"), Kate->ComputeCameraTargets(false, -40.f).ArmLength > 350.f);
	TestEqual(TEXT("Straight down is the curve's extra"), Kate->ComputeCameraTargets(false, -70.f).ArmLength,
		350.f + Kate->TestHipPitchCurve().ArmDown, 0.01f);
	TestEqual(TEXT("The aim's FOV does not follow the pitch"), Kate->ComputeCameraTargets(true, 60.f).FieldOfView, 70.f, 0.01f);

	// The lift is continuous and never falls as the look goes up from level to the limit.
	float PreviousLift = 0.f;
	for (float Pitch = 0.f; Pitch <= 75.f; Pitch += 0.25f)
	{
		const float Lift = Kate->ComputeCameraTargets(false, Pitch).PivotLift;
		TestTrue(FString::Printf(TEXT("The lift does not drop going up (%.2f)"), Pitch), Lift >= PreviousLift - 0.01f);
		TestTrue(FString::Printf(TEXT("The lift does not jump (%.2f)"), Pitch), Lift - PreviousLift < 4.f);
		PreviousLift = Lift;
	}

	// Indoors the arm is capped and the lens still clears its height.
	Kate->SetIndoorCamera(true);
	for (float Pitch = -70.f; Pitch <= 75.f; Pitch += 5.f)
	{
		const FHawkeyeCameraTargets Indoor = Kate->ComputeCameraTargets(false, Pitch);
		TestTrue(TEXT("Indoors the hip arm is capped"), Indoor.ArmLength <= Kate->TestIndoorArmLength() + 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraPureRules, "Hawkeye.Camera.PureRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraPureRules::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCameraRules;

	// The smooth maximum is the maximum away from the kink, and never under it.
	TestEqual(TEXT("SmoothMax far apart"), SmoothMax(0.f, -50.f, 20.f), 0.f);
	TestEqual(TEXT("SmoothMax far apart, swapped"), SmoothMax(80.f, 10.f, 20.f), 80.f);
	TestTrue(TEXT("SmoothMax at the kink is above both"), SmoothMax(0.f, 0.f, 20.f) > 0.f);
	for (float B = -30.f; B <= 30.f; B += 1.f)
	{
		TestTrue(TEXT("SmoothMax never under the larger"), SmoothMax(0.f, B, 20.f) >= FMath::Max(0.f, B));
	}

	// Look pitch near its limits.
	TestEqual(TEXT("Mid-range input is untouched"), PitchLimitScale(0.f, 3.f, -70.f, 75.f, 12.f, 0.35f), 1.f);
	TestEqual(TEXT("At the top the input is at the minimum scale"), PitchLimitScale(75.f, 3.f, -70.f, 75.f, 12.f, 0.35f), 0.35f, 0.001f);
	TestEqual(TEXT("At the bottom too"), PitchLimitScale(-70.f, -3.f, -70.f, 75.f, 12.f, 0.35f), 0.35f, 0.001f);
	TestEqual(TEXT("Moving away from a limit is never slowed"), PitchLimitScale(75.f, -3.f, -70.f, 75.f, 12.f, 0.35f), 1.f);
	TestTrue(TEXT("Between: eased"), PitchLimitScale(69.f, 3.f, -70.f, 75.f, 12.f, 0.35f) > 0.35f
		&& PitchLimitScale(69.f, 3.f, -70.f, 75.f, 12.f, 0.35f) < 1.f);

	// The clock the lag runs on.
	TestEqual(TEXT("No dilation, no change"), UnscaledDelta(0.016f, 1.f), 0.016f);
	TestEqual(TEXT("Hit stop's 0.1 is taken out"), UnscaledDelta(0.0016f, 0.1f), 0.016f, 0.0001f);
	TestEqual(TEXT("The finisher's 0.25"), UnscaledDelta(0.004f, 0.25f), 0.016f, 0.0001f);
	TestTrue(TEXT("A frozen world does not make an enormous step"), UnscaledDelta(0.0000016f, 0.0001f) <= 0.05f);

	// The lag clamp: sideways and up separately.
	const FVector Clamped = ClampLagOffset(FVector(180.f, 0.f, 90.f), 75.f, 40.f);
	TestEqual(TEXT("Horizontal lag clamped to 75"), static_cast<float>(Clamped.X), 75.f, 0.01f);
	TestEqual(TEXT("Vertical lag clamped to 40"), static_cast<float>(Clamped.Z), 40.f, 0.01f);
	const FVector Diagonal = ClampLagOffset(FVector(90.f, 90.f, -90.f), 75.f, 40.f);
	TestEqual(TEXT("A diagonal lag is clamped by its length"), static_cast<float>(FVector(Diagonal.X, Diagonal.Y, 0.f).Size()), 75.f, 0.01f);
	TestEqual(TEXT("Below too"), static_cast<float>(Diagonal.Z), -40.f, 0.01f);
	TestEqual(TEXT("Inside both: untouched"), ClampLagOffset(FVector(20.f, 10.f, 5.f), 75.f, 40.f), FVector(20.f, 10.f, 5.f));
	TestEqual(TEXT("Zero means no limit"), ClampLagOffset(FVector(500.f, 0.f, 500.f), 0.f, 0.f), FVector(500.f, 0.f, 500.f));

	// The jump's dead zone.
	TestEqual(TEXT("Inside the dead zone the pivot does not follow"), DeadZoneOffset(50.f, 60.f), 0.f);
	TestEqual(TEXT("Past it only the excess follows"), DeadZoneOffset(90.f, 60.f), 30.f);
	TestEqual(TEXT("Down too"), DeadZoneOffset(-100.f, 60.f), -40.f);

	// The rate limits.
	FRates Rates;
	FHawkeyeCameraTargets A(350.f, FVector(0.f, 70.f, 60.f), 90.f);
	FHawkeyeCameraTargets B(100.f, FVector(0.f, -70.f, 160.f), 120.f, 300.f);
	const FHawkeyeCameraTargets Step = LimitTargetStep(A, B, 0.1f, Rates);
	TestEqual(TEXT("The arm moves at most its rate"), Step.ArmLength, 350.f - Rates.ArmPerSecond * 0.1f, 0.01f);
	TestEqual(TEXT("The shoulder at most its rate"), static_cast<float>(Step.SocketOffset.Y), 70.f - Rates.SocketPlanarPerSecond * 0.1f, 0.01f);
	TestEqual(TEXT("The socket's height at most its rate"), static_cast<float>(Step.SocketOffset.Z), 60.f + Rates.SocketVerticalPerSecond * 0.1f, 0.01f);
	TestEqual(TEXT("The FOV at most its rate"), Step.FieldOfView, 90.f + Rates.FovPerSecond * 0.1f, 0.01f);
	TestEqual(TEXT("The lift rises at once"), Step.PivotLift, 300.f, 0.01f);
	const FHawkeyeCameraTargets Falling = LimitTargetStep(B, A, 0.1f, Rates);
	TestEqual(TEXT("The lift falls at its rate"), Falling.PivotLift, 300.f - Rates.LiftFallPerSecond * 0.1f, 0.01f);
	TestEqual(TEXT("No time passed, no limit to apply: the target"), LimitTargetStep(A, B, 0.f, Rates).ArmLength, 100.f);
	TestTrue(TEXT("The aim blend (350 to 180 in 0.15 s) fits under the arm's rate"), 170.f / 0.15f < Rates.ArmPerSecond);
	TestTrue(TEXT("And its FOV (20 degrees in 0.15 s)"), 20.f / 0.15f < Rates.FovPerSecond);

	// A capsule that shrinks holds the pivot and eases it down; growing is at once.
	TestEqual(TEXT("A 40 cm drop is held"), EasedHalfHeight(90.f, 50.f, 0.f, 200.f), 90.f);
	TestEqual(TEXT("and eases"), EasedHalfHeight(90.f, 50.f, 0.1f, 200.f), 70.f, 0.01f);
	TestEqual(TEXT("Standing up is at once"), EasedHalfHeight(50.f, 90.f, 0.016f, 200.f), 90.f);

	// Collision easing: snap in, ease out, never past the sweep.
	TestEqual(TEXT("A wall snaps the arm in"), EaseFraction(1.f, 1.f, 0.3f, 0.016f, 6.f), 0.3f);
	const float Out = EaseFraction(0.3f, 1.f, 1.f, 0.016f, 6.f);
	TestTrue(TEXT("Eases back out"), Out > 0.3f && Out < 0.5f);
	TestEqual(TEXT("But never past what the sweep allows"), EaseFraction(0.9f, 1.f, 0.5f, 0.1f, 6.f), 0.5f);
	TestTrue(TEXT("A feeler eases the arm in"), EaseFeelerFraction(1.f, 0.7f, 0.1f) < 1.f);
	TestEqual(TEXT("But never below its floor"), EaseFeelerFraction(0.6f, 0.1f, 10.f), 0.55f, 0.001f);
	TestEqual(TEXT("A touching hit is backed off a hair"), BackOffFraction(0.5f, 100.f), 0.485f, 0.0001f);
	TestEqual(TEXT("No hit is no back off"), BackOffFraction(1.f, 100.f), 1.f);
	TestEqual(TEXT("A hit at the start stays at the start"), BackOffFraction(0.005f, 100.f), 0.f);

	// The floor rule.
	TestEqual(TEXT("A lens 10 above a floor needs to rise 25 for 35 of clearance"), RiseNeededForFloor(10.f, 0.f, NoFloor, 35.f), 25.f);
	TestEqual(TEXT("No floor, no rise"), RiseNeededForFloor(10.f, NoFloor, NoFloor, 35.f), 0.f);
	TestEqual(TEXT("The higher of the two floors counts"), RiseNeededForFloor(10.f, 0.f, 100.f, 35.f), 125.f);
	TestEqual(TEXT("Clear already"), RiseNeededForFloor(200.f, 0.f, 0.f, 35.f), 0.f);
	float Correction = 0.f;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Correction = EaseFloorCorrection(Correction, 100.f, 1.f / 60.f);
		TestTrue(TEXT("The correction is never more than 12 short of the need"), Correction >= 100.f - 12.f - 0.01f);
	}
	TestTrue(TEXT("It falls slowly once the need is gone"), EaseFloorCorrection(100.f, 0.f, 0.1f) > 80.f);

	// The body hysteresis.
	TestTrue(TEXT("Inside 90 it hides"), ShouldHideBody(false, 80.f, 90.f, 120.f));
	TestFalse(TEXT("Between the two it stays shown"), ShouldHideBody(false, 100.f, 90.f, 120.f));
	TestTrue(TEXT("Between the two it stays hidden"), ShouldHideBody(true, 100.f, 90.f, 120.f));
	TestFalse(TEXT("Past 120 it comes back"), ShouldHideBody(true, 125.f, 90.f, 120.f));
	TestTrue(TEXT("but not before it has been hidden for the dwell"), ShouldHideBody(true, 125.f, 90.f, 120.f, 0.5f, 1.05f));
	TestFalse(TEXT("and then it does"), ShouldHideBody(true, 125.f, 90.f, 120.f, 1.2f, 1.05f));
	TestFalse(TEXT("A hide waits out the dwell too"), ShouldHideBody(false, 50.f, 90.f, 120.f, 0.5f, 1.05f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraProbeClearsNearPlane, "Hawkeye.Camera.ProbeClearsNearPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraProbeClearsNearPlane::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeCameraTest::SpawnKate(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}

	// The default near clip plane is 10 cm. Widest view the camera asks for (MaxFieldOfView), the squarest screen we ship (4:3).
	const float NearPlane = 10.f;
	const float Probe = Kate->TestProbeSize();
	for (const float Aspect : { 4.f / 3.f, 16.f / 9.f, 21.f / 9.f })
	{
		for (const float Fov : { 70.f, 90.f, HawkeyeCameraRules::MaxFieldOfView })
		{
			const float Corner = HawkeyeCameraRules::NearPlaneCornerDistance(Fov, Aspect, NearPlane);
			TestTrue(FString::Printf(TEXT("The probe (%.0f) clears the near-plane corner (%.1f) at FOV %.0f, aspect %.2f"), Probe, Corner, Fov, Aspect),
				Probe > Corner + 1.5f);
		}
	}
	TestTrue(TEXT("The old 12 cm probe did not: 16:9 at FOV 90"), HawkeyeCameraRules::NearPlaneCornerDistance(90.f, 16.f / 9.f, NearPlane) > 12.f);

	// No camera the character asks for is wider than the limit the probe was sized for.
	for (float Pitch = -70.f; Pitch <= 75.f; Pitch += 1.f)
	{
		TestTrue(TEXT("Hip FOV within the limit"), Kate->ComputeCameraTargets(false, Pitch).FieldOfView <= HawkeyeCameraRules::MaxFieldOfView + 0.01f);
		TestTrue(TEXT("Aim FOV within the limit"), Kate->ComputeCameraTargets(true, Pitch).FieldOfView <= HawkeyeCameraRules::MaxFieldOfView + 0.01f);
	}
	TestTrue(TEXT("Hide below, show above"), Kate->TestHideBodyDistance() < Kate->TestShowBodyDistance());
	// Hidden before she fills more than about a fifth of the frame: her shoulders (45 cm) at the hide distance, at the hip FOV.
	const float HipFov = Kate->ComputeCameraTargets(false, 0.f).FieldOfView;
	const float ShoulderFraction = 2.f * FMath::RadiansToDegrees(FMath::Atan(22.5f / Kate->TestHideBodyDistance())) / HipFov;
	TestTrue(*FString::Printf(TEXT("Her shoulders are %.0f%% of the hip frame at the hide distance"), ShoulderFraction * 100.f),
		ShoulderFraction <= 0.22f);
	// ...and aiming, whose lens sits out over the shoulder, is never inside the show distance (it would hide her).
	const FHawkeyeCameraTargets Aim = Kate->ComputeCameraTargets(true, 0.f);
	const float AimLens = FVector(Aim.ArmLength, Aim.SocketOffset.Y, Aim.SocketOffset.Z).Size();
	TestTrue(*FString::Printf(TEXT("The aim lens (%.0f cm) is past the show distance"), AimLens), AimLens > Kate->TestShowBodyDistance());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraTeleportCuts, "Hawkeye.Camera.TeleportCuts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraTeleportCuts::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = HawkeyeCameraTest::SpawnKate(TestWorld);
	if (!TestNotNull(TEXT("Kate spawned"), Kate) || !TestNotNull(TEXT("with the Hawkeye arm"), Kate->GetCameraBoom()))
	{
		return false;
	}
	const int32 Before = Kate->GetCameraCutCount();
	Kate->NotifyTeleported();
	TestEqual(TEXT("NotifyTeleported counts a cut"), Kate->GetCameraCutCount(), Before + 1);
	Kate->NotifyTeleported();
	TestEqual(TEXT("and every one after it"), Kate->GetCameraCutCount(), Before + 2);
	return true;
}

namespace HawkeyeCameraTest
{
	/** An actor holding an arm and nothing else, on a floor whose top is Z 0 and (optionally) a wall. */
	struct FArmRig
	{
		AActor* Holder = nullptr;
		UHawkeyeSpringArmComponent* Arm = nullptr;

		bool Build(const FHawkeyeTestWorld& TestWorld, const FVector& Location, float Pitch, float ArmLength, const FVector& Socket)
		{
			Holder = TestWorld.SpawnActor(AActor::StaticClass(), Location, FRotator::ZeroRotator);
			if (!Holder)
			{
				return false;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
			Holder->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocation(Location);
			Arm = NewObject<UHawkeyeSpringArmComponent>(Holder, TEXT("Arm"));
			Arm->SetupAttachment(Root);
			Arm->RegisterComponent();
			Arm->SetWorldRotation(FRotator(Pitch, 0.f, 0.f));
			Arm->TargetArmLength = ArmLength;
			Arm->SocketOffset = Socket;
			Arm->bEnableCameraLag = true;
			Arm->CameraLagSpeed = 10.f;
			Arm->bEnableCameraRotationLag = false;
			return true;
		}

		FVector Lens() const { return Arm->GetSocketLocation(USpringArmComponent::SocketName); }
	};

	static void SpawnFloor(const FHawkeyeTestWorld& TestWorld)
	{
		AHawkeyeTestBlocker* Floor = Cast<AHawkeyeTestBlocker>(TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(0.f, 0.f, -50.f), FRotator::ZeroRotator));
		if (Floor)
		{
			Floor->SetExtent(FVector(4000.f, 4000.f, 50.f));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraArmFloorRule, "Hawkeye.Camera.Arm.FloorRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraArmFloorRule::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	HawkeyeCameraTest::SpawnFloor(TestWorld);
	// 25 degrees up on flat ground with no lift: the geometry puts the lens about 10 cm above the floor.
	HawkeyeCameraTest::FArmRig Rig;
	if (!TestTrue(TEXT("Rig built"), Rig.Build(TestWorld, FVector(0.f, 0.f, 90.f), 25.f, 330.f, FVector(0.f, 70.f, 67.f))))
	{
		return false;
	}
	const float Raw = HawkeyeCameraRules::LensHeightAboveFeet(25.f, 330.f, FVector(0.f, 70.f, 67.f), 0.f, 90.f);
	TestTrue(TEXT("Without the rule this camera is under the floor clearance"), Raw < 35.f);

	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Rig.Arm->UpdateImmediately(1.f / 60.f);
		TestTrue(FString::Printf(TEXT("Frame %d: the lens is at least 23 cm above the floor (is %.1f)"), Frame, Rig.Lens().Z),
			Rig.Lens().Z >= 35.f - 12.f - 0.5f);
	}
	TestTrue(TEXT("The rule did the lifting"), Rig.Arm->GetFloorCorrection() > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraArmCuts, "Hawkeye.Camera.Arm.CutsAndLagClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraArmCuts::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	HawkeyeCameraTest::FArmRig Rig;
	if (!TestTrue(TEXT("Rig built"), Rig.Build(TestWorld, FVector(0.f, 0.f, 90.f), 0.f, 350.f, FVector(0.f, 70.f, 60.f))))
	{
		return false;
	}
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	const int32 Start = Rig.Arm->GetCutCount();

	// The lag never trails her by more than the clamp, however fast she goes (3 m a frame is a zip at a stutter; use
	// 100 cm a frame so it is not a cut).
	for (int32 Frame = 1; Frame <= 30; ++Frame)
	{
		Rig.Holder->SetActorLocation(FVector(Frame * 100.f, 0.f, 90.f));
		Rig.Arm->UpdateImmediately(1.f / 60.f);
		const FVector Trail = Rig.Arm->PreviousDesiredLoc - Rig.Holder->GetActorLocation();
		TestTrue(FString::Printf(TEXT("Frame %d: the lag trails by at most 75 (is %.1f)"), Frame, FVector(Trail.X, Trail.Y, 0.f).Size()),
			FVector(Trail.X, Trail.Y, 0.f).Size() <= 75.f + 0.1f);
	}
	TestEqual(TEXT("Fast, but not a cut"), Rig.Arm->GetCutCount(), Start);

	// A teleport is a cut: the lag history is the new pose at once. 200 cm is more than a takedown's hop, short of the snap distance.
	const FVector Hop(3200.f, 0.f, 90.f);
	Rig.Holder->SetActorLocation(Hop, false, nullptr, ETeleportType::TeleportPhysics);
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	TestEqual(TEXT("A teleport cuts"), Rig.Arm->GetCutCount(), Start + 1);
	TestTrue(TEXT("and the lag does not trail the old place"), FVector::Dist2D(Rig.Arm->PreviousDesiredLoc, Hop) < 1.f);
	TestTrue(TEXT("and the lens is at the new place"), FVector::Dist2D(Rig.Lens(), Hop) < 400.f);

	// The same hop with no teleport type is the lag's to carry.
	Rig.Holder->SetActorLocation(Hop + FVector(200.f, 0.f, 0.f));
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	TestEqual(TEXT("A walk-sized hop is not a cut"), Rig.Arm->GetCutCount(), Start + 1);

	// A jump past the snap distance with no teleport type is a cut too.
	Rig.Holder->SetActorLocation(FVector(9000.f, 0.f, 90.f));
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	TestEqual(TEXT("A pivot jump cuts"), Rig.Arm->GetCutCount(), Start + 2);

	// ResetLag is one.
	Rig.Arm->ResetLag();
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	TestEqual(TEXT("ResetLag cuts"), Rig.Arm->GetCutCount(), Start + 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraArmRotationCap, "Hawkeye.Camera.Arm.RotationCatchUpIsCapped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraArmRotationCap::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	HawkeyeCameraTest::FArmRig Rig;
	if (!TestTrue(TEXT("Rig built"), Rig.Build(TestWorld, FVector(0.f, 0.f, 90.f), 0.f, 350.f, FVector(0.f, 0.f, 0.f))))
	{
		return false;
	}
	Rig.Arm->bEnableCameraRotationLag = true;
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	// An 80 degree swing is under the cut (90), so the lag has to cover it; its opening rate (12 times the gap) would be 16
	// degrees in a 60 fps frame, over the 700 degrees a second cap (11.7).
	Rig.Arm->SetWorldRotation(FRotator(0.f, 80.f, 0.f));
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	const float Turned = static_cast<float>(FMath::Abs(Rig.Arm->PreviousDesiredRot.Yaw));
	TestTrue(FString::Printf(TEXT("The view turned %.1f degrees, at most 700 / 60"), Turned), Turned <= 700.f / 60.f + 0.05f);
	TestTrue(TEXT("and it did move"), Turned > 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraArmShoulder, "Hawkeye.Camera.Arm.ShoulderSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraArmShoulder::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	HawkeyeCameraTest::SpawnFloor(TestWorld);
	// A wall on her right (+Y), its face 60 cm out; the shoulder offset is 70.
	AHawkeyeTestBlocker* Wall = Cast<AHawkeyeTestBlocker>(TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(-200.f, 70.f, 150.f),
		FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Wall spawned"), Wall))
	{
		return false;
	}
	Wall->SetExtent(FVector(800.f, 10.f, 150.f));

	HawkeyeCameraTest::FArmRig Rig;
	if (!TestTrue(TEXT("Rig built"), Rig.Build(TestWorld, FVector(0.f, 0.f, 120.f), 0.f, 300.f, FVector(0.f, 70.f, 60.f))))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Rig.Arm->UpdateImmediately(1.f / 60.f);
	}
	const float Probe = Rig.Arm->ProbeSize;
	TestTrue(TEXT("The wall shortened the shoulder"), Rig.Arm->GetShoulderScale() < 0.7f);
	TestTrue(TEXT("The lens keeps its probe's radius off the wall's face"), Rig.Lens().Y <= 60.f - Probe + 0.5f);
	TestTrue(TEXT("and is on her side of it, not pulled in behind her"), FVector::Dist(Rig.Lens(), FVector(0.f, 0.f, 120.f)) > 200.f);

	// Clear of the wall, the shoulder comes back out, eased.
	Wall->SetActorLocation(FVector(-200.f, 5000.f, 150.f));
	Rig.Arm->UpdateImmediately(1.f / 60.f);
	TestTrue(TEXT("Eased out, not a pop"), Rig.Arm->GetShoulderScale() < 0.9f);
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Rig.Arm->UpdateImmediately(1.f / 60.f);
	}
	TestTrue(TEXT("and fully out in the end"), Rig.Arm->GetShoulderScale() > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraArmClock, "Hawkeye.Camera.Arm.LagRunsOnTheRealClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraArmClock::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AWorldSettings* Settings = TestWorld.Get() ? TestWorld.Get()->GetWorldSettings() : nullptr;
	if (!Settings)
	{
		AddWarning(TEXT("The test world has no world settings; the dilation case is skipped."));
		return true;
	}

	auto Run = [&](float Dilation, float Delta)
	{
		Settings->SetTimeDilation(Dilation);
		HawkeyeCameraTest::FArmRig Rig;
		Rig.Build(TestWorld, FVector(0.f, 0.f, 90.f), 0.f, 350.f, FVector(0.f, 0.f, 0.f));
		Rig.Arm->UpdateImmediately(Delta);
		Rig.Holder->SetActorLocation(FVector(60.f, 0.f, 90.f));
		Rig.Arm->UpdateImmediately(Delta);
		const float Caught = static_cast<float>(Rig.Arm->PreviousDesiredLoc.X);
		Rig.Holder->Destroy();
		return Caught;
	};
	const float Normal = Run(1.f, 1.f / 60.f);
	const float HitStop = Run(0.1f, 1.f / 600.f);
	Settings->SetTimeDilation(1.f);
	TestEqual(TEXT("A frame of hit stop moves the lag as far as a normal frame"), HitStop, Normal, Normal * 0.1f);
	TestTrue(TEXT("and it did move"), Normal > 1.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
