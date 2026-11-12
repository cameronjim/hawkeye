#include "Combat/CombatReadability.h"
#include "Combat/MeleeComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Combat readability (claude-docs/gameplay-semantics.md, "Combat readability"): the marker on the assist's
 * pick and its fade, the fight camera's thresholds and blend, the heavy's hit stop and camera punch, and
 * the telegraph glyph's growth.
 */
namespace HawkeyeCombatReadabilityTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static AThugCharacter* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& At)
	{
		AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), At, FRotator(0.f, 180.f, 0.f)));
		if (Thug)
		{
			Thug->Weapon = EThugWeapon::Fists;
		}
		return Thug;
	}

	static AHawkeyeAimTestCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector,
			FRotator::ZeroRotator));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTargetMarkerLifetime, "Hawkeye.Melee.TargetMarkerLifetime", HawkeyeCombatReadabilityTest::Flags)

bool FHawkeyeTargetMarkerLifetime::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatReadabilityTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Him = SpawnThug(TestWorld, FVector(250.f, 0.f, 0.f));
	AThugCharacter* Other = SpawnThug(TestWorld, FVector(0.f, 250.f, 0.f));
	if (!Him || !Other)
	{
		AddError(TEXT("Failed to spawn the thugs."));
		return false;
	}

	FHawkeyeTargetMarker Marker;
	TestEqual(TEXT("The fade is 0.3 s"), Marker.FadeSeconds, 0.3f);
	TestEqual(TEXT("Nobody marked: nothing shows"), Marker.GetAlpha(), 0.f);
	Marker.Mark(Him);
	TestTrue(TEXT("Marked"), Marker.GetTarget() == Him);
	Marker.Advance(1.f, true);
	TestEqual(TEXT("Full for as long as the swing runs"), Marker.GetAlpha(), 1.f);
	Marker.Advance(0.15f, false);
	TestEqual(TEXT("Half gone 0.15 s after the swing"), Marker.GetAlpha(), 0.5f, 0.001f);
	Marker.Advance(0.1f, true);
	TestEqual(TEXT("A fade that has started runs on (a new swing marks afresh)"), Marker.GetAlpha(), 1.f / 6.f, 0.001f);
	Marker.Advance(0.06f, false);
	TestNull(TEXT("Gone 0.3 s after the swing"), Marker.GetTarget());
	TestEqual(TEXT("And shows nothing"), Marker.GetAlpha(), 0.f);

	Marker.Mark(Him);
	Marker.Advance(0.2f, false);
	Marker.Mark(Other);
	TestTrue(TEXT("A new pick takes the marker"), Marker.GetTarget() == Other);
	TestEqual(TEXT("Full again"), Marker.GetAlpha(), 1.f);
	Marker.Mark(nullptr);
	TestEqual(TEXT("A swing at nobody clears it"), Marker.GetAlpha(), 0.f);

	Marker.Mark(Him);
	Him->Destroy();
	Marker.Advance(0.01f, true);
	TestNull(TEXT("A removed thug takes his marker with him"), Marker.GetTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeTargetMarkerOnKate, "Hawkeye.Melee.TargetMarkerFollowsTheAssist", HawkeyeCombatReadabilityTest::Flags)

bool FHawkeyeTargetMarkerOnKate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatReadabilityTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* Thug = SpawnThug(TestWorld, FVector(250.f, 0.f, 0.f));
	if (!Kate || !Thug)
	{
		AddError(TEXT("Failed to spawn Kate and a thug."));
		return false;
	}
	TestNull(TEXT("Nothing marked before she swings"), Kate->GetMeleeTargetMarker().GetTarget());
	TestTrue(TEXT("The light starts"), Kate->StartLightAttack());
	TestTrue(TEXT("The assist's pick is marked"), Kate->GetMeleeTargetMarker().GetTarget() == Thug);
	TestEqual(TEXT("Full while the swing runs"), Kate->GetMeleeTargetMarker().GetAlpha(), 1.f);
	Kate->GetMeleeComponent()->AdvanceAttack(1.f);
	TestFalse(TEXT("The swing is over"), Kate->GetMeleeComponent()->IsAttacking());
	// Her whole Tick, as the game runs it (AActor's is public; hers is protected).
	static_cast<AActor*>(Kate)->Tick(0.15f);
	TestEqual(TEXT("Fading after it"), Kate->GetMeleeTargetMarker().GetAlpha(), 0.5f, 0.01f);
	static_cast<AActor*>(Kate)->Tick(0.2f);
	TestNull(TEXT("Gone 0.3 s after"), Kate->GetMeleeTargetMarker().GetTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFightCameraRules, "Hawkeye.Camera.FightCameraThresholds", HawkeyeCombatReadabilityTest::Flags)

bool FHawkeyeFightCameraRules::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatReadability;
	const FHawkeyeFightCameraSettings Settings;
	TestEqual(TEXT("9 m"), Settings.Radius, 900.f);
	TestEqual(TEXT("15 m for the far ones"), Settings.FarRadius, 1500.f);
	TestEqual(TEXT("Two thugs make a fight"), Settings.MinThugs, 2);
	TestTrue(TEXT("The boom lengthens 60 to 80 cm"), Settings.ExtraArmLength >= 60.f && Settings.ExtraArmLength <= 80.f);
	TestTrue(TEXT("The lens tips a few degrees"), Settings.PitchDrop >= 2.f && Settings.PitchDrop <= 6.f);

	const FVector Her = FVector::ZeroVector;
	TestEqual(TEXT("At 500 and 890 both count"),
		CountEngaged(Her, { FVector(500.f, 0.f, 0.f), FVector(0.f, -890.f, 50.f) }, Settings.Radius, Settings.MaxHeight, Settings.FarRadius), 2);
	TestEqual(TEXT("A gunner at 1400 counts with one at 800"),
		CountEngaged(Her, { FVector(800.f, 0.f, 0.f), FVector(1400.f, 0.f, 0.f) }, Settings.Radius, Settings.MaxHeight, Settings.FarRadius), 2);
	TestEqual(TEXT("At 1510 he does not"),
		CountEngaged(Her, { FVector(800.f, 0.f, 0.f), FVector(1510.f, 0.f, 0.f) }, Settings.Radius, Settings.MaxHeight, Settings.FarRadius), 1);
	TestEqual(TEXT("Two far ones with nobody within 9 m are not a fight"),
		CountEngaged(Her, { FVector(950.f, 0.f, 0.f), FVector(1400.f, 0.f, 0.f) }, Settings.Radius, Settings.MaxHeight, Settings.FarRadius), 0);
	TestEqual(TEXT("Nor one on the roof above"),
		CountEngaged(Her, { FVector(100.f, 0.f, 0.f), FVector(100.f, 0.f, 400.f) }, Settings.Radius, Settings.MaxHeight, Settings.FarRadius), 1);
	TestFalse(TEXT("One is not a fight"), IsFight(1, Settings));
	TestTrue(TEXT("Two are"), IsFight(2, Settings));
	TestTrue(TEXT("Three are"), IsFight(3, Settings));

	TestEqual(TEXT("Half in after 0.25 s"), AdvanceFightAlpha(0.f, true, 0.25f, Settings), 0.5f, 0.001f);
	TestEqual(TEXT("All in after 0.5 s"), AdvanceFightAlpha(0.5f, true, 0.25f, Settings), 1.f, 0.001f);
	TestEqual(TEXT("Half out after 0.5 s"), AdvanceFightAlpha(1.f, false, 0.5f, Settings), 0.5f, 0.001f);
	TestEqual(TEXT("All out after 1 s"), AdvanceFightAlpha(0.5f, false, 0.5f, Settings), 0.f, 0.001f);

	float Arm = 0.f;
	float Pitch = 0.f;
	ComputeFightOffsets(1.f, 0.f, 0.f, Settings, Arm, Pitch);
	TestEqual(TEXT("All in: the whole extra"), Arm, Settings.ExtraArmLength, 0.001f);
	TestEqual(TEXT("And the whole tip"), Pitch, Settings.PitchDrop, 0.001f);
	ComputeFightOffsets(0.5f, 0.f, 0.f, Settings, Arm, Pitch);
	TestEqual(TEXT("Half way, smoothstepped, half"), Arm, Settings.ExtraArmLength * 0.5f, 0.001f);
	ComputeFightOffsets(1.f, 1.f, 0.f, Settings, Arm, Pitch);
	TestEqual(TEXT("Aiming: the bow's camera is left alone"), Arm + Pitch, 0.f, 0.001f);
	ComputeFightOffsets(1.f, 0.f, 1.f, Settings, Arm, Pitch);
	TestEqual(TEXT("A finisher's push-in takes over"), Arm + Pitch, 0.f, 0.001f);

	// The heavy's punch: 2.5 cm and back over 0.1 s, a third with reduce camera shake.
	TestEqual(TEXT("Punch peaks half way"), ComputeCameraPunch(0.05f, 0.1f, 2.5f, 1.f), 2.5f, 0.001f);
	TestEqual(TEXT("Reduced shake: 30%"), ComputeCameraPunch(0.05f, 0.1f, 2.5f, 0.3f), 0.75f, 0.001f);
	TestEqual(TEXT("Nothing after it"), ComputeCameraPunch(0.11f, 0.1f, 2.5f, 1.f), 0.f);
	TestEqual(TEXT("Nothing before it"), ComputeCameraPunch(-1.f, 0.1f, 2.5f, 1.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFightCameraOnKate, "Hawkeye.Camera.FightCameraWidensForTwoThugs", HawkeyeCombatReadabilityTest::Flags)

bool FHawkeyeFightCameraOnKate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatReadabilityTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = SpawnKate(TestWorld);
	AThugCharacter* One = SpawnThug(TestWorld, FVector(300.f, 0.f, 0.f));
	AThugCharacter* Two = SpawnThug(TestWorld, FVector(0.f, 450.f, 0.f));
	if (!Kate || !One || !Two || !Kate->GetCameraBoom())
	{
		AddError(TEXT("Failed to spawn Kate and the thugs."));
		return false;
	}
	const float Hip = Kate->ComputeCameraTargets(false).ArmLength;
	Kate->TestTickAim(0.1f);
	TestEqual(TEXT("Calm thugs: no fight"), Kate->GetEngagedThugCount(), 0);
	TestEqual(TEXT("The hip boom"), Kate->GetCameraBoom()->TargetArmLength, Hip, 0.5f);

	One->SetAlertState(EThugAlertState::Alerted);
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Kate->TestTickAim(0.1f);
	}
	TestEqual(TEXT("One alerted thug is not a fight"), Kate->GetFightCameraAlpha(), 0.f);

	Two->SetAlertState(EThugAlertState::Alerted);
	for (int32 Step = 0; Step < 7; ++Step)
	{
		Kate->TestTickAim(0.1f);
	}
	TestEqual(TEXT("Two alerted within 9 m"), Kate->GetEngagedThugCount(), 2);
	TestEqual(TEXT("All in by 0.5 s (and a recount)"), Kate->GetFightCameraAlpha(), 1.f, 0.001f);
	TestEqual(TEXT("The boom is 70 cm longer"), Kate->GetCameraBoom()->TargetArmLength, Hip + Kate->GetFightCameraSettings().ExtraArmLength, 0.5f);

	Two->SetAlertState(EThugAlertState::Calm);
	for (int32 Step = 0; Step < 6; ++Step)
	{
		Kate->TestTickAim(0.1f);
	}
	TestTrue(TEXT("On its way out, not yet back after 0.6 s"), Kate->GetFightCameraAlpha() > 0.f && Kate->GetFightCameraAlpha() < 0.6f);
	for (int32 Step = 0; Step < 6; ++Step)
	{
		Kate->TestTickAim(0.1f);
	}
	TestEqual(TEXT("Back over 1 s"), Kate->GetFightCameraAlpha(), 0.f);
	TestEqual(TEXT("The hip boom again"), Kate->GetCameraBoom()->TargetArmLength, Hip, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHitReadRules, "Hawkeye.Melee.HitStopAndTelegraphGlyph", HawkeyeCombatReadabilityTest::Flags)

bool FHawkeyeHitReadRules::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatReadability;
	const AHawkeyeCharacter* Kate = GetDefault<AHawkeyeCharacter>();
	TestEqual(TEXT("A light (weight 0.3): 62 ms of hit-stop"), Kate->GetMeleeHitStopSeconds(false), 0.062f, 0.001f);
	TestEqual(TEXT("A heavy (weight 1): 90 ms"), Kate->GetMeleeHitStopSeconds(true), 0.09f, 0.001f);

	const FHawkeyeTelegraphLook Start = ComputeTelegraphLook(0.f, 0.6f);
	const FHawkeyeTelegraphLook End = ComputeTelegraphLook(0.6f, 0.6f, 0.f);
	TestEqual(TEXT("The glyph starts at its size"), Start.Scale, 1.f, 0.001f);
	TestEqual(TEXT("And grows 30% by the hit (reduce flashing: no pulse)"), End.Scale, 1.3f, 0.001f);
	TestEqual(TEXT("Without the pulse it is solid"), End.Alpha, 1.f, 0.001f);
	float MaxScale = 0.f;
	for (float T = 0.f; T <= 0.6f; T += 0.01f)
	{
		MaxScale = FMath::Max(MaxScale, ComputeTelegraphLook(T, 0.6f).Scale);
	}
	TestTrue(TEXT("It pulses on top of the growth"), MaxScale > 1.35f);

	// The stack sits on his head (y down: bigger is lower): the bar's bottom on the anchor, the glyph 4 px over its top.
	TestEqual(TEXT("No bar: the glyph sits at his head"), ComputeGlyphBottom(300.f, false, 5.f), 300.f, 0.001f);
	TestEqual(TEXT("A bar: the glyph sits 4 px over its top"), ComputeGlyphBottom(300.f, true, 5.f), 291.f, 0.001f);
	TestEqual(TEXT("The gap is the caller's"), ComputeGlyphBottom(300.f, true, 5.f, 2.f), 293.f, 0.001f);
	TestEqual(TEXT("A negative gap never pulls the glyph onto the bar"), ComputeGlyphBottom(300.f, true, 5.f, -3.f), 295.f, 0.001f);
	// Never overlapping: wherever his head is on screen, the glyph's bottom is over the bar's top (anchor less its height).
	for (float Anchor = -50.f; Anchor <= 1200.f; Anchor += 37.f)
	{
		const float Bottom = ComputeGlyphBottom(Anchor, true, 5.f);
		if (Bottom > Anchor - 5.f)
		{
			AddError(FString::Printf(TEXT("At anchor %.0f the glyph's bottom %.1f is on the bar (its top %.1f)."), Anchor, Bottom,
				Anchor - 5.f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeOffScreenCue, "Hawkeye.Melee.OffScreenTelegraphCue", HawkeyeCombatReadabilityTest::Flags)

bool FHawkeyeOffScreenCue::RunTest(const FString& Parameters)
{
	using namespace HawkeyeCombatReadability;
	const FVector2D Size(1280.f, 720.f);
	TestFalse(TEXT("Over his head in the middle of the screen: no cue"), IsOffScreen(true, FVector2D(640.f, 300.f), Size));
	TestTrue(TEXT("Behind the lens: the cue"), IsOffScreen(false, FVector2D(640.f, 300.f), Size));
	TestTrue(TEXT("Off the right edge: the cue"), IsOffScreen(true, FVector2D(1400.f, 300.f), Size));
	TestTrue(TEXT("Hard on the top edge: the cue"), IsOffScreen(true, FVector2D(640.f, 10.f), Size));

	// Round her, as seen from above: ahead at the top, behind at the bottom, right at the right.
	const FVector2D Centre(640.f, 400.f);
	const FVector2D Radii(200.f, 170.f);
	const FVector2D Behind = ComputeOffScreenCuePosition(180.f, Centre, Radii);
	TestEqual(TEXT("Behind her: under her"), Behind.Y, 570.0, 0.1);
	TestEqual(TEXT("Behind her: in line with her"), Behind.X, 640.0, 0.1);
	TestEqual(TEXT("To her right: right of her"), ComputeOffScreenCuePosition(90.f, Centre, Radii).X, 840.0, 0.1);
	TestEqual(TEXT("To her left: left of her"), ComputeOffScreenCuePosition(-90.f, Centre, Radii).X, 440.0, 0.1);
	const FVector2D BackRight = ComputeOffScreenCuePosition(135.f, Centre, Radii);
	TestTrue(TEXT("Behind and to the right: lower right"), BackRight.X > Centre.X && BackRight.Y > Centre.Y);
	return true;
}

#endif
