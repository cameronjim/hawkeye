#include "Misc/AutomationTest.h"
#include "Traversal/LedgeBodyRules.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The ledge body's plain rules (claude-docs/gameplay-semantics.md, "Ledge body"): the stages of each move and their
 * timings, what each stage holds, where the hands go on an edge (shoulder width, the ends, a ledge too short for two
 * hands), the two-bone solve, the reach weight, the swing's pendulum, and the frames the hand is turned by. No world, no content.
 */
namespace HawkeyeLedgeTest
{
	/** Every stage covers 0 to 1 in order with no gap. */
	static bool Covers(const FLedgeSequence& Sequence)
	{
		if (Sequence.IsEmpty() || !FMath::IsNearlyZero(Sequence.Phases[0].Start) || !FMath::IsNearlyEqual(Sequence.Phases.Last().End, 1.f))
		{
			return false;
		}
		for (int32 Index = 0; Index < Sequence.Phases.Num(); ++Index)
		{
			const FLedgePhase& Phase = Sequence.Phases[Index];
			if (Phase.End <= Phase.Start)
			{
				return false;
			}
			if (Index > 0 && !FMath::IsNearlyEqual(Phase.Start, Sequence.Phases[Index - 1].End))
			{
				return false;
			}
		}
		return true;
	}

	/** A wall whose face points -X (the hang tests' wall: she hangs facing +X from x < 100), the edge at x = 100, z = 800, her right +Y. */
	static FLedgeEdge Wall(float MinAlong = -300.f, float MaxAlong = 300.f)
	{
		FLedgeEdge Edge;
		Edge.Point = FVector(100.f, 0.f, 800.f);
		Edge.Normal = FVector(-1.f, 0.f, 0.f);
		// Along is up x (-normal): her right as she faces the wall (-normal).
		Edge.Along = FVector::CrossProduct(FVector::UpVector, -Edge.Normal).GetSafeNormal();
		Edge.MinAlong = MinAlong;
		Edge.MaxAlong = MaxAlong;
		return Edge;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeSequences, "Hawkeye.Ledge.Sequences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeSequences::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeTest;
	struct FCase
	{
		EHawkeyeParkourMove Move;
		bool bFalling;
		const TCHAR* Name;
		ELedgeStage First;
		ELedgeStage Last;
	};
	const FCase Cases[] = {
		{EHawkeyeParkourMove::DropToHang, false, TEXT("drop to hang"), ELedgeStage::Plant, ELedgeStage::Settle},
		{EHawkeyeParkourMove::LedgeGrab, false, TEXT("grab"), ELedgeStage::Reach, ELedgeStage::Settle},
		{EHawkeyeParkourMove::LedgeGrab, true, TEXT("catch"), ELedgeStage::Reach, ELedgeStage::Catch},
		{EHawkeyeParkourMove::Climb, false, TEXT("climb"), ELedgeStage::Pull, ELedgeStage::Stand},
		{EHawkeyeParkourMove::Mantle, false, TEXT("mantle"), ELedgeStage::Reach, ELedgeStage::Stand},
		{EHawkeyeParkourMove::Vault, false, TEXT("vault"), ELedgeStage::VaultPlant, ELedgeStage::VaultSwing},
		{EHawkeyeParkourMove::HangLeap, false, TEXT("leap"), ELedgeStage::Release, ELedgeStage::Catch},
		{EHawkeyeParkourMove::HangHop, false, TEXT("hop"), ELedgeStage::Release, ELedgeStage::Catch},
		{EHawkeyeParkourMove::HangCorner, false, TEXT("corner"), ELedgeStage::Hang, ELedgeStage::Hang},
	};
	for (const FCase& Case : Cases)
	{
		const FLedgeSequence Sequence = HawkeyeLedge::GetSequence(Case.Move, Case.bFalling);
		TestTrue(*FString::Printf(TEXT("%s: the stages cover 0 to 1 in order"), Case.Name), Covers(Sequence));
		float StageAlpha = -1.f;
		TestEqual(*FString::Printf(TEXT("%s: starts in %d"), Case.Name, static_cast<int32>(Case.First)),
			static_cast<int32>(HawkeyeLedge::StageAt(Sequence, 0.f, StageAlpha)), static_cast<int32>(Case.First));
		TestEqual(*FString::Printf(TEXT("%s: the stage alpha is 0 at the start"), Case.Name), StageAlpha, 0.f);
		TestEqual(*FString::Printf(TEXT("%s: ends in %d"), Case.Name, static_cast<int32>(Case.Last)),
			static_cast<int32>(HawkeyeLedge::StageAt(Sequence, 1.f, StageAlpha)), static_cast<int32>(Case.Last));
		TestEqual(*FString::Printf(TEXT("%s: the stage alpha is 1 at the end"), Case.Name), StageAlpha, 1.f, 0.001f);
	}
	TestTrue(TEXT("A move with no ledge body has no sequence"), HawkeyeLedge::GetSequence(EHawkeyeParkourMove::None).IsEmpty());

	// The roof dismount: plants, lowers, settles, and the turn is done before the lowering is.
	const FLedgeSequence Drop = HawkeyeLedge::GetSequence(EHawkeyeParkourMove::DropToHang);
	float StageAlpha = 0.f;
	TestEqual(TEXT("Dismount: planted at 0.15"), static_cast<int32>(HawkeyeLedge::StageAt(Drop, 0.15f, StageAlpha)), static_cast<int32>(ELedgeStage::Plant));
	TestEqual(TEXT("Dismount: half way through the plant"), StageAlpha, 0.5f, 0.01f);
	TestEqual(TEXT("Dismount: lowering at 0.5"), static_cast<int32>(HawkeyeLedge::StageAt(Drop, 0.5f, StageAlpha)), static_cast<int32>(ELedgeStage::Lower));
	FHawkeyeLedgeBodySettings Settings;
	TestTrue(TEXT("Dismount: she has turned to face the wall before the lowering ends"),
		HawkeyeLedge::TurnFraction(EHawkeyeParkourMove::DropToHang, Settings) < Drop.Phases[1].End);
	TestEqual(TEXT("Any other move turns over all of its time"), HawkeyeLedge::TurnFraction(EHawkeyeParkourMove::HangHop, Settings), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeWeights, "Hawkeye.Ledge.StageWeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeWeights::RunTest(const FString& Parameters)
{
	// Every weight stays between 0 and 1 over every stage.
	for (uint8 Stage = static_cast<uint8>(ELedgeStage::None); Stage <= static_cast<uint8>(ELedgeStage::VaultSwing); ++Stage)
	{
		for (int32 Step = 0; Step <= 10; ++Step)
		{
			const FLedgeStageWeights W = HawkeyeLedge::StageWeights(static_cast<ELedgeStage>(Stage), Step / 10.f);
			TestTrue(*FString::Printf(TEXT("stage %d at %d/10 stays in 0..1"), Stage, Step),
				W.Hands >= 0.f && W.Hands <= 1.f && W.Feet >= 0.f && W.Feet <= 1.f && W.Body >= 0.f && W.Body <= 1.f && W.Grip >= 0.f && W.Grip <= 1.f);
		}
	}
	const FLedgeStageWeights Hang = HawkeyeLedge::StageWeights(ELedgeStage::Hang, 0.4f);
	TestTrue(TEXT("A hang holds the hands, the feet, the body and the grip in full"),
		Hang.Hands == 1.f && Hang.Feet == 1.f && Hang.Body == 1.f && Hang.Grip == 1.f);
	TestEqual(TEXT("A plant starts with no hand on the edge"), HawkeyeLedge::StageWeights(ELedgeStage::Plant, 0.f).Hands, 0.f);
	TestEqual(TEXT("and ends with it held"), HawkeyeLedge::StageWeights(ELedgeStage::Plant, 1.f).Hands, 1.f);
	TestEqual(TEXT("A reach has no IK until the hands come to the edge"), HawkeyeLedge::StageWeights(ELedgeStage::Reach, 0.3f).Hands, 0.f);
	TestEqual(TEXT("A release lets the hands go"), HawkeyeLedge::StageWeights(ELedgeStage::Release, 1.f).Hands, 0.f);
	TestEqual(TEXT("and starts with them held"), HawkeyeLedge::StageWeights(ELedgeStage::Release, 0.f).Hands, 1.f);
	TestEqual(TEXT("Standing up on the top lets go of the IK"), HawkeyeLedge::StageWeights(ELedgeStage::Stand, 0.5f).Hands, 0.f);
	TestTrue(TEXT("The chest over the edge presses with a looser grip"),
		HawkeyeLedge::StageWeights(ELedgeStage::ChestOver, 0.5f).Grip < HawkeyeLedge::StageWeights(ELedgeStage::Pull, 0.5f).Grip);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeHands, "Hawkeye.Ledge.HandPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeHands::RunTest(const FString& Parameters)
{
	using namespace HawkeyeLedgeTest;
	const FHawkeyeLedgeBodySettings Settings;
	{
		const FLedgeEdge Edge = Wall();
		TestTrue(TEXT("Along is her right: +Y for a wall whose face points -X"), Edge.Along.Equals(FVector(0.f, 1.f, 0.f), 0.001f));
		const FLedgeHandPlan Plan = HawkeyeLedge::PlanHands(Edge, 0.f, Settings);
		TestFalse(TEXT("A long ledge is not cramped"), Plan.bCramped);
		TestEqual(TEXT("The hands are a shoulder width apart"), Plan.Spacing, 2.f * Settings.HandHalfGap, 0.01f);
		TestEqual(TEXT("left of her centre"), static_cast<float>(Plan.Hand[0].Y), -Settings.HandHalfGap, 0.01f);
		TestEqual(TEXT("and right of it"), static_cast<float>(Plan.Hand[1].Y), Settings.HandHalfGap, 0.01f);
		TestEqual(TEXT("The wrists ride just above the top"), static_cast<float>(Plan.Hand[0].Z), 800.f + Settings.WristHeight, 0.01f);
		TestEqual(TEXT("and just out over the face's edge"), static_cast<float>(Plan.Hand[1].X), 100.f - Settings.WristOut, 0.01f);
	}
	{
		// Pushing up over the edge the palms go back onto the top, away from the face.
		const FLedgeHandPlan Plan = HawkeyeLedge::PlanHands(Wall(), Settings.PushBack, Settings);
		TestEqual(TEXT("Pushed back: the wrists are PushBack cm in from the edge (less the wrist's own 1 cm out)"),
			static_cast<float>(Plan.Hand[0].X), 100.f + Settings.PushBack - Settings.WristOut, 0.01f);
	}
	{
		// The pair is slid along to stay inside a ledge's end: the right end 12 cm away, so the right hand keeps its margin.
		const FLedgeEdge Edge = Wall(-300.f, 12.f);
		const FLedgeHandPlan Plan = HawkeyeLedge::PlanHands(Edge, 0.f, Settings);
		TestEqual(TEXT("The right hand stays the margin inside the end"), Plan.AlongOffset[1], 12.f - Settings.HandEndMargin, 0.01f);
		TestEqual(TEXT("and the gap is kept"), Plan.Spacing, 2.f * Settings.HandHalfGap, 0.01f);
		TestTrue(TEXT("so the left hand is further left"), Plan.AlongOffset[0] < Plan.AlongOffset[1] - 30.f);
	}
	{
		// A 40 cm ledge (a fire-escape landing's rail post, a short wall): 40 - 2 * 8 = 24 cm for the hands, narrowed to fit.
		const FLedgeEdge Edge = Wall(-20.f, 20.f);
		const FLedgeHandPlan Plan = HawkeyeLedge::PlanHands(Edge, 0.f, Settings);
		TestFalse(TEXT("24 cm is room for two hands"), Plan.bCramped);
		TestEqual(TEXT("The gap is what is left"), Plan.Spacing, 24.f, 0.01f);
		TestEqual(TEXT("and both hands are inside the margins"), static_cast<float>(Plan.Hand[0].Y), -12.f, 0.01f);
		TestEqual(TEXT("each"), static_cast<float>(Plan.Hand[1].Y), 12.f, 0.01f);
	}
	{
		// A ledge shorter than the minimum: the hands close up on its middle (the usable part).
		const FLedgeEdge Edge = Wall(-12.f, 12.f);
		const FLedgeHandPlan Plan = HawkeyeLedge::PlanHands(Edge, 0.f, Settings);
		TestTrue(TEXT("8 cm is under the minimum gap: cramped"), Plan.bCramped);
		TestEqual(TEXT("the hands are 8 cm apart at most"), Plan.Spacing, 8.f, 0.01f);
		TestEqual(TEXT("centred"), static_cast<float>(Plan.Hand[0].Y + Plan.Hand[1].Y), 0.f, 0.01f);
	}
	{
		// A ledge shorter than the two margins: both hands on its middle.
		const FLedgeEdge Edge = Wall(-6.f, 10.f);
		const FLedgeHandPlan Plan = HawkeyeLedge::PlanHands(Edge, 0.f, Settings);
		TestTrue(TEXT("Cramped"), Plan.bCramped);
		TestEqual(TEXT("Both on the line's middle (2 cm)"), static_cast<float>(Plan.Hand[0].Y), 2.f, 0.01f);
		TestEqual(TEXT("the same point"), static_cast<float>(Plan.Hand[1].Y), 2.f, 0.01f);
	}
	{
		// One hand asked for past the end is held at the margin.
		const FVector Wrist = HawkeyeLedge::WristAt(Wall(-50.f, 50.f), 500.f, 0.f, Settings);
		TestEqual(TEXT("A wrist past the end is held at the margin"), static_cast<float>(Wrist.Y), 50.f - Settings.HandEndMargin, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeLimbs, "Hawkeye.Ledge.TwoBoneAndReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeLimbs::RunTest(const FString& Parameters)
{
	const FHawkeyeLedgeBodySettings Settings;
	const FVector Shoulder(0.f, 0.f, 100.f);
	{
		const FVector Target(0.f, -30.f, 140.f);
		const FTwoBoneSolution Solution = HawkeyeLedge::SolveTwoBone(Shoulder, 28.f, 25.f, Target, FVector(0.f, 40.f, 100.f));
		TestFalse(TEXT("A target within reach is not clamped"), Solution.bClamped);
		TestTrue(TEXT("The end is on the target"), Solution.End.Equals(Target, 0.01f));
		TestEqual(TEXT("The upper segment keeps its length"), static_cast<float>((Solution.Joint - Shoulder).Size()), 28.f, 0.01f);
		TestEqual(TEXT("and the lower one"), static_cast<float>((Solution.End - Solution.Joint).Size()), 25.f, 0.01f);
		// The pole is behind her (+Y); the elbow bends that way, away from the line to the target.
		TestTrue(TEXT("The elbow bends toward the pole"), Solution.Joint.Y > 0.5 * (Shoulder.Y + Target.Y));
	}
	{
		const FVector Far(0.f, -90.f, 100.f);
		const FTwoBoneSolution Solution = HawkeyeLedge::SolveTwoBone(Shoulder, 28.f, 25.f, Far, FVector(0.f, 0.f, 60.f));
		TestTrue(TEXT("A target out of reach is clamped"), Solution.bClamped);
		TestTrue(TEXT("the end is on the line to it, at most the arm's length"),
			(Solution.End - Shoulder).Size() <= 53.f && FVector::DotProduct((Solution.End - Shoulder).GetSafeNormal(), FVector(0.f, -1.f, 0.f)) > 0.999);
		TestEqual(TEXT("The segments still keep their lengths"), static_cast<float>((Solution.Joint - Shoulder).Size()), 28.f, 0.01f);
	}
	{
		const FTwoBoneSolution Solution = HawkeyeLedge::SolveTwoBone(Shoulder, 28.f, 25.f, Shoulder + FVector(0.f, 0.f, 20.f), FVector(0.f, 0.f, 120.f));
		TestTrue(TEXT("A pole straight along the limb still gives an elbow"), Solution.Joint.ContainsNaN() == false);
		TestEqual(TEXT("with the lengths kept"), static_cast<float>((Solution.End - Solution.Joint).Size()), 25.f, 0.01f);
	}
	TestEqual(TEXT("A hand within the arm's length is fully weighted"), HawkeyeLedge::ReachWeight(40.f, 50.f, Settings), 1.f);
	TestEqual(TEXT("one much further than it can reach is left to the clip"), HawkeyeLedge::ReachWeight(90.f, 50.f, Settings), 0.f);
	const float Part = HawkeyeLedge::ReachWeight(55.f, 50.f, Settings);
	TestTrue(TEXT("and in between it eases"), Part > 0.f && Part < 1.f);
	TestTrue(TEXT("a little further, a little less"), HawkeyeLedge::ReachWeight(58.f, 50.f, Settings) < Part);

	// The body hangs from its hands: ShoulderOut out from the wall, the arms 94% stretched.
	const FVector Mid(100.f, 0.f, 802.5f);
	const FVector Hang = HawkeyeLedge::ShoulderTarget(Mid, FVector(1.f, 0.f, 0.f), 50.f, Settings);
	TestEqual(TEXT("The shoulders hang out from the wall"), static_cast<float>(Hang.X - Mid.X), Settings.ShoulderOut, 0.01f);
	TestEqual(TEXT("directly under the hands along the edge"), static_cast<float>(Hang.Y), 0.f, 0.01f);
	TestEqual(TEXT("with the arms 94% straight"), static_cast<float>((Mid - Hang).Size()), 50.f * Settings.ArmStraightness, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeSwing, "Hawkeye.Ledge.PendulumSettles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeSwing::RunTest(const FString& Parameters)
{
	const FHawkeyeLedgeBodySettings Settings;
	TestTrue(TEXT("A catch from a faster fall starts the swing wider"),
		HawkeyeLedge::CatchKick(800.f, Settings) > HawkeyeLedge::CatchKick(100.f, Settings));
	TestEqual(TEXT("A standing grab (no speed) is the base kick"), HawkeyeLedge::CatchKick(0.f, Settings), Settings.CatchKickBase, 0.001f);
	TestEqual(TEXT("A very fast fall is capped"), HawkeyeLedge::CatchKick(100000.f, Settings), Settings.MaxKick, 0.001f);

	// From 16 degrees: it swings in, touches the wall, comes back out less, and is still inside 3 s.
	FLedgePendulum Swing;
	Swing.Kick(16.f);
	float Wall = 0.f;
	float Lowest = 99.f;
	float SettledAt = -1.f;
	float PeakAfterWall = 0.f;
	bool bTouched = false;
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		Swing.Step(1.f / 60.f, Settings.SwingOmega, Settings.SwingDamping, Settings.SwingWallDegrees, Settings.SwingRestitution);
		Lowest = FMath::Min(Lowest, Swing.Angle);
		if (Swing.Angle <= Settings.SwingWallDegrees + 0.001f)
		{
			bTouched = true;
		}
		else if (bTouched)
		{
			PeakAfterWall = FMath::Max(PeakAfterWall, Swing.Angle);
		}
		if (SettledAt < 0.f && Swing.IsSettled())
		{
			SettledAt = (Frame + 1) / 60.f;
		}
		Wall = Swing.Angle;
	}
	TestTrue(TEXT("The feet meet the wall (the swing reaches the wall's angle)"), bTouched);
	TestTrue(TEXT("and never go through it"), Lowest >= Settings.SwingWallDegrees - 0.001f);
	TestTrue(TEXT("The swing comes back out after the wall, smaller than it went in"), PeakAfterWall > 0.5f && PeakAfterWall < 16.f);
	TestTrue(TEXT("It is still (under half a degree) within 3.5 s"), SettledAt > 0.f && SettledAt < 3.5f);
	TestTrue(TEXT("and stays still"), Swing.IsSettled());
	TestTrue(TEXT("at rest hanging straight (within a degree of the wall's stop or zero)"), FMath::Abs(Wall) < 1.f);

	// The same swing at 30 Hz and 144 Hz: the same answer within a degree (frame-rate independent).
	FLedgePendulum Slow;
	FLedgePendulum Fast;
	Slow.Kick(12.f);
	Fast.Kick(12.f);
	for (int32 Frame = 0; Frame < 15; ++Frame)
	{
		Slow.Step(1.f / 30.f, Settings.SwingOmega, Settings.SwingDamping, Settings.SwingWallDegrees, Settings.SwingRestitution);
	}
	for (int32 Frame = 0; Frame < 72; ++Frame)
	{
		Fast.Step(1.f / 144.f, Settings.SwingOmega, Settings.SwingDamping, Settings.SwingWallDegrees, Settings.SwingRestitution);
	}
	TestEqual(TEXT("Half a second at 30 Hz and at 144 Hz agree"), Slow.Angle, Fast.Angle, 1.f);

	// An undamped pendulum with no wall keeps swinging; this one with damping loses amplitude every period.
	FLedgePendulum Free;
	Free.Kick(10.f);
	float FirstPeak = 0.f;
	float SecondPeak = 0.f;
	float LastAngle = Free.Angle;
	float LastRate = 0.f;
	int32 Peaks = 0;
	for (int32 Frame = 0; Frame < 600 && Peaks < 3; ++Frame)
	{
		Free.Step(1.f / 120.f, Settings.SwingOmega, Settings.SwingDamping, -90.f, 0.f);
		const float Rate = Free.Angle - LastAngle;
		if (LastRate > 0.f && Rate <= 0.f)
		{
			++Peaks;
			if (Peaks == 1)
			{
				FirstPeak = Free.Angle;
			}
			else if (Peaks == 2)
			{
				SecondPeak = Free.Angle;
			}
		}
		LastAngle = Free.Angle;
		LastRate = Rate;
	}
	TestTrue(TEXT("Each swing is smaller than the one before"), FirstPeak > 0.f && SecondPeak > 0.f && SecondPeak < FirstPeak);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeLedgeFrames, "Hawkeye.Ledge.HandFramesAndSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeLedgeFrames::RunTest(const FString& Parameters)
{
	const FVector Normal(1.f, 0.f, 0.f);
	FVector Fingers;
	FVector Palm;
	HawkeyeLedge::HandAxes(Normal, 18.f, Fingers, Palm);
	TestTrue(TEXT("The fingers point into the wall"), Fingers.X < -0.9);
	TestTrue(TEXT("tipped down onto the top"), Fingers.Z < -0.2 && Fingers.Z > -0.4);
	TestTrue(TEXT("The palm faces down"), Palm.Z < -0.9);
	TestEqual(TEXT("and square to the fingers"), static_cast<float>(FVector::DotProduct(Fingers, Palm)), 0.f, 0.001f);

	// A hand frame (fingers +X, palm -Z in its bone) is turned onto those axes.
	const FQuat Turn = HawkeyeLedge::AlignFrames(FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), Fingers, Palm);
	TestTrue(TEXT("The turn puts the fingers where wanted"), Turn.RotateVector(FVector(1.f, 0.f, 0.f)).Equals(Fingers, 0.001f));
	TestTrue(TEXT("and the palm"), Turn.RotateVector(FVector(0.f, 0.f, -1.f)).Equals(Palm, 0.001f));
	const FQuat Same = HawkeyeLedge::AlignFrames(FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f));
	TestTrue(TEXT("A frame already there is not turned"), Same.Equals(FQuat::Identity, 0.001f));
	const FQuat Skew = HawkeyeLedge::AlignFrames(FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f));
	TestTrue(TEXT("A quarter turn round up carries Y onto X"), Skew.RotateVector(FVector(0.f, 1.f, 0.f)).Equals(FVector(1.f, 0.f, 0.f), 0.001f));
	TestTrue(TEXT("and Z onto Y, the second direction"), Skew.RotateVector(FVector(0.f, 0.f, 1.f)).Equals(FVector(0.f, 1.f, 0.f), 0.001f));

	// The swing: a positive angle takes the feet away from the wall (toward the normal), turning about the hands' line.
	const FQuat Swing = HawkeyeLedge::SwingTurn(Normal, 20.f);
	const FVector Feet = Swing.RotateVector(FVector(0.f, 0.f, -100.f));
	TestTrue(TEXT("Swung out, the feet are further along the normal"), Feet.X > 30.0);
	TestEqual(TEXT("and still 94 cm down or so"), static_cast<float>(Feet.Z), -100.f * FMath::Cos(FMath::DegreesToRadians(20.f)), 0.1f);
	TestTrue(TEXT("The swing's own axis (the hands' line) does not move"), Swing.RotateVector(FVector(0.f, 1.f, 0.f)).Equals(FVector(0.f, 1.f, 0.f), 0.001f));

	// A ball held off the wall's plane.
	const double PlaneD = 100.0;
	const FVector Ball = HawkeyeLedge::BallOnPlane(FVector(140.f, 7.f, 700.f), Normal, PlaneD, 5.f);
	TestEqual(TEXT("A ball 40 cm out is brought to 5 cm off the face"), static_cast<float>(Ball.X), 105.f, 0.001f);
	TestEqual(TEXT("keeping its height and its place along the wall"), static_cast<float>(Ball.Z), 700.f, 0.001f);
	TestEqual(TEXT("and along"), static_cast<float>(Ball.Y), 7.f, 0.001f);
	return true;
}

#endif
