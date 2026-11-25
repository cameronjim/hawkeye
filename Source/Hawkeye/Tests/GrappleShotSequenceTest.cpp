#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/InventoryComponent.h"
#include "Tests/HawkeyeTestUtils.h"
#include "Traversal/GrappleRopeRules.h"
#include "Traversal/GrappleShotRules.h"
#include "World/CharacterLightRig.h"
#include "World/GrappleAnchor.h"
#include "World/GrappleArrowProjectile.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The grapple shot's rules (claude-docs/gameplay-semantics.md, "Grapple shot", "Grapple rope"): the clock from the press to
 * the pull is the arrow's own flight time, the release takes the snap draw out of it; the rope is slack in the air, snaps
 * straight when the arrow bites, shivers and settles, and on a release whips back and is gone; the character light rig's
 * lux follows the exposure; the trip's motion blur steps in and out.
 */
namespace HawkeyeGrappleShotTest
{
	static FHawkeyeGrappleShotSettings On()
	{
		FHawkeyeGrappleShotSettings Settings;
		Settings.bEnabled = true;
		return Settings;
	}

	static AGrappleArrowProjectile* FindFlyingArrow(UWorld* World)
	{
		for (TActorIterator<AGrappleArrowProjectile> It(World); It; ++It)
		{
			if (!It->HasArrived() && !It->IsActorBeingDestroyed())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static float Middle(const TArray<FVector>& Points)
	{
		return static_cast<float>(Points[Points.Num() / 2].Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleShotSchedule, "Hawkeye.Grapple.Shot.Schedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleShotSchedule::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleShotTest;
	const FHawkeyeGrappleShotSettings Settings = On();
	TestEqual(TEXT("The snap draw is raise plus draw: 0.18 s"), Settings.RaiseSeconds + Settings.DrawSeconds, 0.18f, 1e-4f);

	// A 24 m line at 6000 cm/s: 0.4 s press to pull as before; the draw takes 0.18 s of it, the arrow flies the other 0.22.
	const FGrappleShotSchedule Long = FGrappleShotRules::MakeSchedule(2400.f, 6000.f, false, Settings);
	TestEqual(TEXT("Press to pull is the old flight time"), Long.TotalSeconds, 0.4f, 1e-4f);
	TestEqual(TEXT("Released at the end of the snap draw"), Long.ReleaseSeconds, 0.18f, 1e-4f);
	TestEqual(TEXT("The arrow flies the rest"), Long.FlightSeconds, 0.22f, 1e-4f);
	TestEqual(TEXT("Release plus flight is the total"), Long.ReleaseSeconds + Long.FlightSeconds, Long.TotalSeconds, 1e-4f);
	TestEqual(TEXT("Raise plus draw is the release"), Long.RaiseSeconds + Long.DrawSeconds, Long.ReleaseSeconds, 1e-4f);
	TestEqual(TEXT("The arrow flies fast enough to arrive on time"), Long.ArrowSpeed * Long.FlightSeconds, 2400.f, 0.5f);
	TestEqual(TEXT("Uncut"), Long.Compression, 1.f, 1e-4f);

	for (const float Distance : { 300.f, 800.f, 1500.f, 2500.f })
	{
		const FGrappleShotSchedule Schedule = FGrappleShotRules::MakeSchedule(Distance, 6000.f, false, Settings);
		TestEqual(*FString::Printf(TEXT("%.0f cm: the shot costs no latency"), Distance), Schedule.TotalSeconds, Distance / 6000.f, 1e-4f);
		TestTrue(*FString::Printf(TEXT("%.0f cm: the arrow flies for a moment after the release"), Distance),
			Schedule.FlightSeconds > 0.01f);
		TestTrue(*FString::Printf(TEXT("%.0f cm: the release is within its share of the total"), Distance),
			Schedule.ReleaseSeconds <= Schedule.TotalSeconds * Settings.ReleaseMaxShare + 1e-4f);
	}

	// A short line squeezes the draw rather than delaying the pull.
	const FGrappleShotSchedule Short = FGrappleShotRules::MakeSchedule(600.f, 6000.f, false, Settings);
	TestEqual(TEXT("A 6 m line pulls at 0.1 s"), Short.TotalSeconds, 0.1f, 1e-4f);
	TestEqual(TEXT("Released at 60% of it"), Short.ReleaseSeconds, 0.06f, 1e-4f);
	TestTrue(TEXT("The snap draw is squeezed"), Short.Compression < 0.5f && Short.Compression > 0.f);

	// Extra latency is the owner's choice and adds to the total.
	FHawkeyeGrappleShotSettings Slower = Settings;
	Slower.ExtraLatencySeconds = 0.1f;
	TestEqual(TEXT("Extra latency adds to the total"), FGrappleShotRules::MakeSchedule(2400.f, 6000.f, false, Slower).TotalSeconds, 0.5f, 1e-4f);

	// A chain at speed 0 used to start at the press: its shot takes ChainTotalSeconds, or none.
	const FGrappleShotSchedule Chain = FGrappleShotRules::MakeSchedule(1800.f, 0.f, true, Settings);
	TestEqual(TEXT("A chain's compressed shot takes 0.1 s"), Chain.TotalSeconds, 0.1f, 1e-4f);
	TestTrue(TEXT("It still flies after the release"), Chain.FlightSeconds > 0.f && Chain.ReleaseSeconds < Chain.TotalSeconds);
	FHawkeyeGrappleShotSettings Instant = Settings;
	Instant.ChainTotalSeconds = 0.f;
	const FGrappleShotSchedule Now = FGrappleShotRules::MakeSchedule(1800.f, 0.f, true, Instant);
	TestEqual(TEXT("With no chain time the redirect is at the press"), Now.TotalSeconds, 0.f);
	TestTrue(TEXT("And the release is the press"), Now.HasReleased(0.f));

	// Off: the old shot.
	const FGrappleShotSchedule Off = FGrappleShotRules::MakeSchedule(2400.f, 6000.f, false, FHawkeyeGrappleShotSettings());
	TestEqual(TEXT("Off: released at the press"), Off.ReleaseSeconds, 0.f);
	TestEqual(TEXT("Off: the arrow flies the whole time"), Off.FlightSeconds, 0.4f, 1e-4f);
	TestEqual(TEXT("Off: at its own speed"), Off.ArrowSpeed, 6000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleShotDraw, "Hawkeye.Grapple.Shot.DrawAndPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleShotDraw::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleShotTest;
	const FGrappleShotSchedule S = FGrappleShotRules::MakeSchedule(2400.f, 6000.f, false, On());
	TestEqual(TEXT("Nothing drawn at the press"), FGrappleShotRules::DrawFractionAt(0.f, S), 0.f);
	TestEqual(TEXT("Fully drawn at the release"), FGrappleShotRules::DrawFractionAt(S.ReleaseSeconds, S), 1.f, 1e-4f);
	TestEqual(TEXT("Held full after it"), FGrappleShotRules::DrawFractionAt(S.ReleaseSeconds + 0.1f, S), 1.f, 1e-4f);
	float Last = -1.f;
	bool bMonotone = true;
	for (float T = 0.f; T <= S.ReleaseSeconds + 0.001f; T += 1.f / 120.f)
	{
		const float Fraction = FGrappleShotRules::DrawFractionAt(T, S);
		bMonotone = bMonotone && Fraction >= Last - 1e-5f;
		Last = Fraction;
	}
	TestTrue(TEXT("The draw only ever comes back"), bMonotone);
	TestEqual(TEXT("The arm is up at the end of the raise"), FGrappleShotRules::RaiseFractionAt(S.RaiseSeconds, S), 1.f, 1e-4f);
	TestEqual(TEXT("And down at the press"), FGrappleShotRules::RaiseFractionAt(0.f, S), 0.f);

	TestEqual(TEXT("Raise first"), static_cast<int32>(FGrappleShotRules::PhaseAt(0.01f, S, false)), static_cast<int32>(EGrappleShotPhase::Raise));
	TestEqual(TEXT("Then the draw"), static_cast<int32>(FGrappleShotRules::PhaseAt(S.RaiseSeconds + 0.01f, S, false)),
		static_cast<int32>(EGrappleShotPhase::Draw));
	TestEqual(TEXT("Then the arrow in the air"), static_cast<int32>(FGrappleShotRules::PhaseAt(S.ReleaseSeconds + 0.01f, S, false)),
		static_cast<int32>(EGrappleShotPhase::Flight));
	TestEqual(TEXT("Then stuck"), static_cast<int32>(FGrappleShotRules::PhaseAt(S.TotalSeconds, S, true)),
		static_cast<int32>(EGrappleShotPhase::Stuck));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleRopeStates, "Hawkeye.Grapple.Rope.StateMachine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleRopeStates::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleShotTest;
	const FHawkeyeGrappleRopeSettings Settings;
	const FVector Hand(0.f, 0.f, 150.f);
	const FVector End(1800.f, 0.f, 900.f);
	TArray<FVector> Points;

	FGrappleRope Rope;
	TestEqual(TEXT("Hidden to begin with"), static_cast<int32>(Rope.GetState()), static_cast<int32>(EGrappleRopeState::Hidden));
	Rope.Arrive();
	TestFalse(TEXT("An arrow that was never loosed bites nothing"), Rope.IsVisible());

	Rope.Begin();
	TestEqual(TEXT("Flying once the arrow leaves"), static_cast<int32>(Rope.GetState()), static_cast<int32>(EGrappleRopeState::Flying));
	TestEqual(TEXT("Fully slack in the air"), Rope.GetSlack(), 1.f);
	Rope.Step(0.1f, Settings);
	Rope.BuildPoints(Hand, End, Settings, Points);
	TestEqual(TEXT("SegmentCount pieces, one more point"), Points.Num(), Settings.SegmentCount + 1);
	TestTrue(TEXT("The near end is her hand"), Points[0].Equals(Hand, 0.01f));
	TestTrue(TEXT("The far end is the arrow"), Points.Last().Equals(End, 0.01f));
	const FVector Chord = FMath::Lerp(Hand, End, 0.5f);
	TestTrue(TEXT("A slack rope sags below the chord"), Middle(Points) < static_cast<float>(Chord.Z) - 20.f);
	TestTrue(TEXT("By no more than the cap"), FGrappleRope::MaxDeviation(Points) <= Settings.MaxSagCm + Settings.FlyWaveCm + 1.f);

	Rope.Arrive();
	TestEqual(TEXT("Taut when it bites"), static_cast<int32>(Rope.GetState()), static_cast<int32>(EGrappleRopeState::Taut));
	float LastSlack = Rope.GetSlack();
	bool bTightens = true;
	for (float T = 0.f; T < Settings.SnapSeconds; T += 1.f / 120.f)
	{
		Rope.Step(1.f / 120.f, Settings);
		bTightens = bTightens && Rope.GetSlack() <= LastSlack + 1e-5f;
		LastSlack = Rope.GetSlack();
	}
	TestTrue(TEXT("The slack only goes out"), bTightens);
	Rope.Step(0.05f, Settings);
	TestEqual(TEXT("Straight after the snap"), Rope.GetSlack(), 0.f, 1e-4f);

	// The shiver is there at first and gone later.
	FGrappleRope Shiver;
	Shiver.Begin();
	Shiver.Arrive();
	Shiver.Step(1.f / 90.f, Settings);
	Shiver.BuildPoints(Hand, End, Settings, Points);
	const float Early = FGrappleRope::MaxDeviation(Points);
	for (int32 Step = 0; Step < 360; ++Step)
	{
		Shiver.Step(1.f / 120.f, Settings);
	}
	Shiver.BuildPoints(Hand, End, Settings, Points);
	const float Late = FGrappleRope::MaxDeviation(Points);
	TestTrue(TEXT("It shivers as it goes taut"), Early > 1.f);
	TestTrue(TEXT("Three seconds later it is a straight line"), Late < 0.05f);
	TestTrue(TEXT("The straight rope runs from the hand to the arrow"),
		Points[Points.Num() / 2].Equals(Chord, 0.1f));

	// Let go: the near end comes away from her hand and is drawn to the arrow; the rope is gone in ReleaseSeconds.
	const FVector Moved = Hand + FVector(300.f, 0.f, 0.f);
	Shiver.Release(Hand);
	TestEqual(TEXT("Released"), static_cast<int32>(Shiver.GetState()), static_cast<int32>(EGrappleRopeState::Released));
	Shiver.Step(Settings.ReleaseSeconds * 0.5f, Settings);
	Shiver.BuildPoints(Moved, End, Settings, Points);
	TestFalse(TEXT("It no longer follows her hand"), Points[0].Equals(Moved, 1.f));
	TestTrue(TEXT("It is being drawn in toward the arrow"), FVector::Dist(Points[0], End) < FVector::Dist(Hand, End));
	TestTrue(TEXT("The arrow end stays put"), Points.Last().Equals(End, 0.01f));
	Shiver.Step(Settings.ReleaseSeconds, Settings);
	TestFalse(TEXT("And is gone"), Shiver.IsVisible());
	Shiver.Release(Hand);
	TestFalse(TEXT("Letting go of nothing does nothing"), Shiver.IsVisible());

	// A rope let go of in the air (the arrow still flying) whips away too.
	FGrappleRope Cut;
	Cut.Begin();
	Cut.Release(Hand);
	TestEqual(TEXT("Released from flight"), static_cast<int32>(Cut.GetState()), static_cast<int32>(EGrappleRopeState::Released));

	// A rope straight up and down sags sideways, not along itself, and never produces a NaN.
	FGrappleRope Vertical;
	Vertical.Begin();
	Vertical.BuildPoints(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 1000.f), Settings, Points);
	bool bFinite = true;
	for (const FVector& Point : Points)
	{
		bFinite = bFinite && FMath::IsFinite(static_cast<float>(Point.X)) && FMath::IsFinite(static_cast<float>(Point.Z));
	}
	TestTrue(TEXT("A vertical rope is finite"), bFinite);
	TestTrue(TEXT("And bellies to the side"), FGrappleRope::MaxDeviation(Points) > 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharLightRules, "Hawkeye.CharLight.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharLightRules::RunTest(const FString& Parameters)
{
	const FCharacterLightSettings S;
	TestEqual(TEXT("At the night's exposure the rig is as set"), FCharacterLightRules::ExposureScale(2.f, S), 1.f, 1e-4f);
	const float Day = FCharacterLightRules::ExposureScale(0.f, S);
	TestEqual(TEXT("By day: 2 EV of compensation, times the day's share"), Day, 4.f * S.DayRelative, 1e-4f);
	TestTrue(TEXT("A brighter exposure never raises the lights"),
		FCharacterLightRules::ExposureScale(3.f, S) <= FCharacterLightRules::ExposureScale(2.f, S));
	TestTrue(TEXT("Halfway between the two it is between them"),
		FCharacterLightRules::ExposureScale(1.f, S) > Day * 0.5f && FCharacterLightRules::ExposureScale(1.f, S) < 2.f);
	TestTrue(TEXT("Never negative"), FCharacterLightRules::ExposureScale(-9.f, S) >= 0.f);

	float Lux[3];
	UCharacterLightRigSubsystem::ComputeLux(S, 2.f, Lux);
	TestEqual(TEXT("Key"), Lux[0], S.KeyLux, 1e-4f);
	TestEqual(TEXT("Fill"), Lux[1], S.FillLux, 1e-4f);
	TestEqual(TEXT("Rim"), Lux[2], S.RimLux, 1e-4f);
	TestTrue(TEXT("The rim is the strongest, the fill the weakest"), Lux[2] > Lux[0] && Lux[0] > Lux[1]);
	TestTrue(TEXT("By day the key is under half of the night's lux"), [&]()
	{
		float DayLux[3];
		UCharacterLightRigSubsystem::ComputeLux(S, 0.f, DayLux);
		return DayLux[0] < Lux[0] * 0.5f;
	}());

	FCharacterLightSettings Off = S;
	Off.bEnabled = false;
	UCharacterLightRigSubsystem::ComputeLux(Off, 2.f, Lux);
	TestEqual(TEXT("Off is dark"), Lux[0] + Lux[1] + Lux[2], 0.f);

	// The lights turn with the camera: the same offsets from a camera turned 90 degrees are turned 90 degrees.
	const FRotator A = FCharacterLightRules::LightRotation(FRotator(0.f, 0.f, 0.f), S.KeyYaw, S.KeyPitch);
	const FRotator B = FCharacterLightRules::LightRotation(FRotator(0.f, 90.f, 0.f), S.KeyYaw, S.KeyPitch);
	TestEqual(TEXT("Same pitch"), B.Pitch, A.Pitch, 0.01f);
	TestEqual(TEXT("Yaw follows the camera"), FRotator::NormalizeAxis(B.Yaw - A.Yaw), 90.f, 0.01f);
	TestTrue(TEXT("The key shines down"), A.Pitch < -10.f);
	TestTrue(TEXT("The key comes from the left: it travels to the right of the view"), A.Yaw > 10.f);
	TestTrue(TEXT("The rim travels toward the camera"),
		FMath::Abs(FRotator::NormalizeAxis(FCharacterLightRules::LightRotation(FRotator::ZeroRotator, S.RimYaw, S.RimPitch).Yaw)) > 120.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharLightChannels, "Hawkeye.CharLight.PawnsAreInChannelOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharLightChannels::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Kate spawned"), Kate) || !TestNotNull(TEXT("She has a mesh"), Kate->GetMesh()))
	{
		return false;
	}
	TestFalse(TEXT("Not in channel 1 before"), Kate->GetMesh()->LightingChannels.bChannel1);
	TestTrue(TEXT("Her meshes change"), UCharacterLightRigSubsystem::ApplyChannels(Kate) >= 1);
	TestTrue(TEXT("Now in channel 1"), Kate->GetMesh()->LightingChannels.bChannel1);
	TestTrue(TEXT("And still in the default channel, so the street lights her"), Kate->GetMesh()->LightingChannels.bChannel0);
	TestEqual(TEXT("A second pass changes nothing"), UCharacterLightRigSubsystem::ApplyChannels(Kate), 0);
	TestEqual(TEXT("Nobody is a null pawn"), UCharacterLightRigSubsystem::ApplyChannels(nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTraversalBlur, "Hawkeye.Traversal.MotionBlurWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeTraversalBlur::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No weight keeps the base"), FGrappleShotRules::BlurAmount(0.5f, 0.1f, 0.f), 0.5f, 1e-5f);
	TestEqual(TEXT("Full weight is the trip's amount"), FGrappleShotRules::BlurAmount(0.5f, 0.1f, 1.f), 0.1f, 1e-5f);
	TestEqual(TEXT("Half is between"), FGrappleShotRules::BlurAmount(0.5f, 0.1f, 0.5f), 0.3f, 1e-5f);
	TestEqual(TEXT("A weight past 1 holds at 1"), FGrappleShotRules::BlurAmount(0.5f, 0.1f, 4.f), 0.1f, 1e-5f);

	float Weight = 0.f;
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		Weight = FGrappleShotRules::StepBlurWeight(Weight, true, 1.f / 60.f, 0.1f, 0.35f);
	}
	TestEqual(TEXT("It comes in over 0.1 s"), Weight, 1.f, 1e-4f);
	float Out = Weight;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Out = FGrappleShotRules::StepBlurWeight(Out, false, 1.f / 60.f, 0.1f, 0.35f);
	}
	TestTrue(TEXT("It goes out slower than it came in"), Out > 0.3f && Out < 0.9f);
	TestEqual(TEXT("And ends at nothing"), FGrappleShotRules::StepBlurWeight(0.01f, false, 1.f, 0.1f, 0.35f), 0.f);
	TestEqual(TEXT("An instant blur is instant"), FGrappleShotRules::StepBlurWeight(0.f, true, 0.001f, 0.f, 0.35f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeGrappleShotPlays, "Hawkeye.Grapple.Shot.PlaysBeforeTheArrowLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeGrappleShotPlays::RunTest(const FString& Parameters)
{
	using namespace HawkeyeGrappleShotTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator));
	AGrappleAnchor* Anchor = Cast<AGrappleAnchor>(
		TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), FVector(1800.f, 0.f, 500.f), FRotator::ZeroRotator));
	if (!Kate || !Anchor || !Kate->GetGrappleComponent() || !Kate->GetBowComponent() || !Kate->GetInventoryComponent())
	{
		AddError(TEXT("Could not spawn Kate and an anchor."));
		return false;
	}
	// A roof under the anchor so the landing has a floor.
	TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(2000.f, 0.f, 400.f), FRotator::ZeroRotator);

	UInventoryComponent* Inventory = Kate->GetInventoryComponent();
	UBowDefinition* Bow = NewObject<UBowDefinition>(Inventory);
	Bow->FullDrawSeconds = 0.8f;
	Inventory->GiveBow(Bow);
	UArrowDefinition* GrappleArrow = NewObject<UArrowDefinition>(Inventory);
	GrappleArrow->Slot = 2;
	GrappleArrow->Cap = 99;
	GrappleArrow->OnHitEffect = EArrowHitEffect::Grapple;
	Inventory->AddArrows(GrappleArrow, 1);

	UGrappleComponent* Grapple = Kate->GetGrappleComponent();
	UBowComponent* BowComponent = Kate->GetBowComponent();
	Grapple->Shot.bEnabled = true;
	Grapple->SetTargetForTest(Anchor);

	TestTrue(TEXT("The press is taken"), Grapple->TryFire());
	TestTrue(TEXT("The shot is pending: the arrow has not left"), Grapple->IsShotPending());
	TestTrue(TEXT("The press is a shot in the air as far as the pose and the HUD know"), Grapple->IsArrowInFlight());
	TestNull(TEXT("No arrow in the world yet"), FindFlyingArrow(TestWorld.Get()));
	TestTrue(TEXT("The bow is drawing: the string hand's pose is on"), BowComponent->IsDrawPose());
	TestFalse(TEXT("But she is not 'drawing the bow': she keeps running and the aim is not forced"), BowComponent->IsDrawing());
	TestTrue(TEXT("The bow is aimed at the anchor"), BowComponent->HasScriptedAim());
	TestEqual(TEXT("At the anchor"), BowComponent->ComputeAimPoint().Z, Anchor->GetMarkerLocation().Z, 0.01);
	TestFalse(TEXT("A second press is refused"), Grapple->TryFire());

	const FGrappleShotSchedule Schedule = Grapple->GetShotSchedule();
	TestTrue(TEXT("The release is in the future"), Schedule.ReleaseSeconds > 0.05f);
	const float Frame = 1.f / 60.f;
	float Elapsed = 0.f;
	float LastDraw = 0.f;
	bool bDrawGrows = true;
	while (Grapple->IsShotPending() && Elapsed < 1.f)
	{
		Grapple->TickShotForTest(Frame);
		Elapsed += Frame;
		if (Grapple->IsShotPending())
		{
			bDrawGrows = bDrawGrows && BowComponent->GetDrawFraction() >= LastDraw - 1e-5f;
			LastDraw = BowComponent->GetDrawFraction();
		}
	}
	TestTrue(TEXT("The string came back before the release"), bDrawGrows && LastDraw > 0.5f);
	TestEqual(TEXT("Released on the schedule"), Elapsed, Schedule.ReleaseSeconds, 2.f * Frame);
	TestFalse(TEXT("The draw is over"), BowComponent->IsDrawPose());
	TestTrue(TEXT("The bow stays up and on the line"), BowComponent->IsBowRaised() && BowComponent->HasScriptedAim());

	AGrappleArrowProjectile* Arrow = FindFlyingArrow(TestWorld.Get());
	if (!TestNotNull(TEXT("The arrow left the bow"), Arrow))
	{
		return false;
	}
	TestTrue(TEXT("Still counted as in flight"), Grapple->IsArrowInFlight());
	TestEqual(TEXT("A rope pays out behind it"), static_cast<int32>(Grapple->GetNewestRopeState()), static_cast<int32>(EGrappleRopeState::Flying));
	TestEqual(TEXT("Visible"), Grapple->GetVisibleRopeCount(), 1);

	// Fly it at the speed the schedule gave it: it arrives at the pull time, as the old arrow did.
	float Flown = 0.f;
	while (!Arrow->HasArrived() && Flown < 2.f)
	{
		Arrow->Advance(Frame);
		Grapple->TickShotForTest(Frame);
		Flown += Frame;
	}
	TestEqual(TEXT("Press to pull is the old flight time"), Elapsed + Flown, Schedule.TotalSeconds, 3.f * Frame);
	TestTrue(TEXT("It bit the anchor"), Arrow->HasArrived() && Anchor->GetStuckArrowCount() == 1);
	TestEqual(TEXT("The rope is taut"), static_cast<int32>(Grapple->GetNewestRopeState()), static_cast<int32>(EGrappleRopeState::Taut));
	TestTrue(TEXT("And the pull started"), Grapple->IsZipping());
	TestEqual(TEXT("The shot is stuck"), static_cast<int32>(Grapple->GetShotPhase()), static_cast<int32>(EGrappleShotPhase::Stuck));

	// Let go: the rope whips away and is gone; the arrow is taken out a moment after; the bow comes down.
	Grapple->ReleaseZip(false);
	TestFalse(TEXT("Let go"), Grapple->IsZipping());
	TestEqual(TEXT("The rope is released"), static_cast<int32>(Grapple->GetNewestRopeState()), static_cast<int32>(EGrappleRopeState::Released));
	for (int32 Step = 0; Step < 60 && Grapple->GetVisibleRopeCount() > 0; ++Step)
	{
		Grapple->TickShotForTest(Frame);
	}
	TestEqual(TEXT("The rope is gone in a fraction of a second"), Grapple->GetVisibleRopeCount(), 0);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Grapple->TickShotForTest(Frame);
	}
	TestEqual(TEXT("The arrow is taken out of the anchor"), Anchor->GetStuckArrowCount(), 0);
	TestFalse(TEXT("The bow's scripted aim is dropped"), BowComponent->HasScriptedAim());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
