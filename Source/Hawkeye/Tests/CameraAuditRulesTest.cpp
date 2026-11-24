#include "Misc/AutomationTest.h"
#include "Tests/CameraAudit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The camera audit's rules on synthetic frames (Tests/CameraAudit.h): each fires on what it is for, stays quiet on what a
 * player's camera legitimately does, and is silenced by a flagged cut or scripted beat where it should be.
 */
namespace HawkeyeCameraAuditTest
{
	using namespace HawkeyeCameraAudit;

	/** A calm frame: Kate at the origin, the lens 350 behind her and 150 up, looking along +X, 1/60 s after the last. */
	static FAuditFrame Calm(double Time)
	{
		FAuditFrame Frame;
		Frame.Time = Time;
		Frame.Delta = 1.f / 60.f;
		Frame.Pivot = FVector(0.f, 0.f, 90.f);
		Frame.Lens = FVector(-350.f, 0.f, 150.f);
		Frame.View = FRotator(0.f, 0.f, 0.f);
		Frame.Mode = TEXT("Walking");
		Frame.FloorUnderKate = 0.f;
		Frame.FloorUnderLens = 0.f;
		return Frame;
	}

	static int32 Count(const TArray<FViolation>& Violations, ERule Rule)
	{
		int32 Total = 0;
		for (const FViolation& Violation : Violations)
		{
			Total += Violation.Rule == Rule ? 1 : 0;
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCameraAuditRules, "Hawkeye.Camera.AuditRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCameraAuditRules::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCameraAudit;
	using namespace HawkeyeCameraAuditTest;

	{
		FRuleBook Rules;
		TArray<FViolation> Found;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Rules.Evaluate(Calm(Frame / 60.0), Found);
		}
		TestEqual(TEXT("A calm camera is no violation"), Found.Num(), 0);
	}

	{
		// Floors: 25 cm is clear, 10 is not; under her and under it.
		FRuleBook Rules;
		TArray<FViolation> Found;
		FAuditFrame Frame = Calm(0.0);
		Frame.Lens.Z = 25.f;
		Rules.Evaluate(Frame, Found);
		TestEqual(TEXT("25 cm above both floors is clear"), Found.Num(), 0);
		Frame = Calm(1.0 / 60.0);
		Frame.Lens.Z = 10.f;
		Rules.Evaluate(Frame, Found);
		TestEqual(TEXT("10 cm above the floor under her"), Count(Found, ERule::FloorUnderKate), 1);
		TestEqual(TEXT("and under the lens"), Count(Found, ERule::FloorUnderLens), 1);
		Frame = Calm(2.0 / 60.0);
		Frame.Lens.Z = -30.f;
		Frame.FloorUnderKate = HawkeyeCameraRules::NoFloor;
		Rules.Evaluate(Frame, Found);
		TestEqual(TEXT("In the ground is worse; with no floor under her only the lens's counts"), Count(Found, ERule::FloorUnderKate), 1);
		TestEqual(TEXT("(the lens one fires again)"), Count(Found, ERule::FloorUnderLens), 2);
		TestTrue(TEXT("and is ranked worse"), Found.Last().Severity > Found[0].Severity);
	}

	{
		FRuleBook Rules;
		TArray<FViolation> Found;
		FAuditFrame Frame = Calm(0.0);
		Frame.bLensOverlap = true;
		Frame.NearPlaneHits = 2;
		Rules.Evaluate(Frame, Found);
		TestEqual(TEXT("A lens inside geometry"), Count(Found, ERule::LensOverlap), 1);
		TestEqual(TEXT("and a near plane through it"), Count(Found, ERule::NearPlane), 1);
	}

	{
		// A lens that jumps 300 cm in a frame; a cut allows it; an orbit round her does not count.
		FRuleBook Rules;
		TArray<FViolation> Found;
		Rules.Evaluate(Calm(0.0), Found);
		FAuditFrame Jump = Calm(1.0 / 60.0);
		Jump.Lens.X = -50.f;
		Rules.Evaluate(Jump, Found);
		TestEqual(TEXT("300 cm in a frame is a jump"), Count(Found, ERule::LensJump), 1);

		FRuleBook CutRules;
		TArray<FViolation> CutFound;
		CutRules.Evaluate(Calm(0.0), CutFound);
		Jump.bCut = true;
		CutRules.Evaluate(Jump, CutFound);
		TestEqual(TEXT("on a cut it is allowed"), Count(CutFound, ERule::LensJump), 0);

		// The view swings 9 degrees round her (540 deg/s) and the lens goes round with it: not a jump.
		FRuleBook OrbitRules;
		TArray<FViolation> OrbitFound;
		OrbitRules.Evaluate(Calm(0.0), OrbitFound);
		FAuditFrame Orbit = Calm(1.0 / 60.0);
		Orbit.View = FRotator(0.f, 9.f, 0.f);
		Orbit.Lens = Orbit.Pivot - Orbit.View.Vector() * 350.f + FVector(0.f, 0.f, 60.f);
		OrbitRules.Evaluate(Orbit, OrbitFound);
		TestEqual(TEXT("A lens carried round her by the view is not a jump"), Count(OrbitFound, ERule::LensJump), 0);
		TestEqual(TEXT("and 540 degrees a second is not too fast a turn"), Count(OrbitFound, ERule::AngularSpeed), 0);
	}

	{
		// The view turning 20 degrees in a frame is 1200 degrees a second.
		FRuleBook Rules;
		TArray<FViolation> Found;
		Rules.Evaluate(Calm(0.0), Found);
		FAuditFrame Spin = Calm(1.0 / 60.0);
		Spin.View = FRotator(0.f, 20.f, 0.f);
		Spin.Lens = Spin.Pivot - Spin.View.Vector() * 350.f + FVector(0.f, 0.f, 60.f);
		Rules.Evaluate(Spin, Found);
		TestEqual(TEXT("1200 degrees a second is a violation"), Count(Found, ERule::AngularSpeed), 1);

		FRuleBook BeatRules;
		TArray<FViolation> BeatFound;
		BeatRules.Evaluate(Calm(0.0), BeatFound);
		Spin.bScriptedBeat = true;
		BeatRules.Evaluate(Spin, BeatFound);
		TestEqual(TEXT("but not in a scripted beat"), Count(BeatFound, ERule::AngularSpeed), 0);

		FRuleBook CutRules;
		TArray<FViolation> CutFound;
		CutRules.Evaluate(Calm(0.0), CutFound);
		Spin.bScriptedBeat = false;
		Spin.bCut = true;
		CutRules.Evaluate(Spin, CutFound);
		TestEqual(TEXT("nor on a cut"), Count(CutFound, ERule::AngularSpeed), 0);

		// A half turn in one frame, the thing the old bot did.
		FRuleBook FlipRules;
		TArray<FViolation> FlipFound;
		FlipRules.Evaluate(Calm(0.0), FlipFound);
		FAuditFrame Flip = Calm(1.0 / 60.0);
		Flip.View = FRotator(0.f, 180.f, 0.f);
		FlipRules.Evaluate(Flip, FlipFound);
		TestEqual(TEXT("A yaw flip is caught"), Count(FlipFound, ERule::AngularSpeed), 1);
	}

	{
		// The body hide: two toggles inside a second is a violation, one each side of it is not.
		FRuleBook Rules;
		TArray<FViolation> Found;
		FAuditFrame Frame = Calm(0.0);
		Rules.Evaluate(Frame, Found);
		Frame = Calm(0.1);
		Frame.bBodyHidden = true;
		Rules.Evaluate(Frame, Found);
		TestEqual(TEXT("One hide is fine"), Count(Found, ERule::BodyToggle), 0);
		Frame = Calm(0.5);
		Frame.bBodyHidden = false;
		Rules.Evaluate(Frame, Found);
		TestEqual(TEXT("and shown again within the second is the flicker"), Count(Found, ERule::BodyToggle), 1);

		FRuleBook Slow;
		TArray<FViolation> SlowFound;
		Frame = Calm(0.0);
		Slow.Evaluate(Frame, SlowFound);
		Frame = Calm(0.1);
		Frame.bBodyHidden = true;
		Slow.Evaluate(Frame, SlowFound);
		Frame = Calm(1.5);
		Frame.bBodyHidden = false;
		Slow.Evaluate(Frame, SlowFound);
		TestEqual(TEXT("Hidden for more than a second is not"), Count(SlowFound, ERule::BodyToggle), 0);
	}

	TestEqual(TEXT("Nothing is running, so no violations are counted"), GetViolationCount(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
