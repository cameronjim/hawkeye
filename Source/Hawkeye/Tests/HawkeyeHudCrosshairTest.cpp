#include "Misc/AutomationTest.h"
#include "UI/HawkeyeHudWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The third-person reticle's rules, without building Slate: hidden at the hip, a small dot while
 * aiming, white on a hit and back again. Where it lands on screen is checked by eye.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHudReticleOnlyWhileAiming, "Hawkeye.Hud.ReticleOnlyWhileAiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHudReticleOnlyWhileAiming::RunTest(const FString& Parameters)
{
	UHawkeyeHudWidget* Hud = NewObject<UHawkeyeHudWidget>();

	TestFalse(TEXT("No reticle at the hip"), Hud->IsReticleVisible());

	Hud->SetReticleAiming(true);
	TestTrue(TEXT("Aiming shows it"), Hud->IsReticleVisible());

	Hud->SetReticleAiming(false);
	TestFalse(TEXT("And lowering hides it again"), Hud->IsReticleVisible());

	return true;
}

/** A 4 pixel dot. A later tweak that quietly grows it into a target is the regression to catch. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHudReticleSize, "Hawkeye.Hud.ReticleSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHudReticleSize::RunTest(const FString& Parameters)
{
	UHawkeyeHudWidget* Hud = NewObject<UHawkeyeHudWidget>();
	TestEqual(TEXT("The dot is 4 pixels"), Hud->GetReticleSize(), 4.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHudHitMarkerFlash, "Hawkeye.Hud.HitMarkerFlash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHudHitMarkerFlash::RunTest(const FString& Parameters)
{
	UHawkeyeHudWidget* Hud = NewObject<UHawkeyeHudWidget>();

	const FLinearColor Resting = Hud->GetReticleColor();
	TestNotEqual(TEXT("At rest the dot is not white"), Resting, FLinearColor::White);

	Hud->FlashHitMarker();
	TestEqual(TEXT("A hit turns it white"), Hud->GetReticleColor(), FLinearColor::White);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeHudDebugLineOffByDefault, "Hawkeye.Hud.DebugLineOffByDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeHudDebugLineOffByDefault::RunTest(const FString& Parameters)
{
	UHawkeyeHudWidget* Hud = NewObject<UHawkeyeHudWidget>();
	TestTrue(TEXT("hawkeye.DebugMovement is off unless asked for"), Hud->GetMovementDebugText().IsEmpty());
	return true;
}

#endif
