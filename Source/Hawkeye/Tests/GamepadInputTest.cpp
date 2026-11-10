#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Settings/HawkeyeSettings.h"
#include "Settings/HawkeyeSettingsSave.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeGamepadInputTest
{
	/** Never the real slot: a test run must not change what the player set. */
	static const TCHAR* TestSlot = TEXT("HawkeyeGamepadAutomationTest");

	static UHawkeyeSettingsSubsystem* MakeSubsystem()
	{
		UGameInstance* Outer = NewObject<UGameInstance>(GEngine);
		UHawkeyeSettingsSubsystem* Subsystem = NewObject<UHawkeyeSettingsSubsystem>(Outer);
		if (Subsystem)
		{
			Subsystem->SlotNameOverride = TestSlot;
		}
		return Subsystem;
	}

	static void ClearTestSlot()
	{
		if (UGameplayStatics::DoesSaveGameExist(TestSlot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStickLookScalesByDeltaTimeAndSensitivity,
	"Hawkeye.Character.StickLookScalesByDeltaTimeAndSensitivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeStickLookScalesByDeltaTimeAndSensitivity::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
		AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Kate)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	TestFalse(TEXT("No subsystem in a test world"), Kate->TestHasSettingsStickSensitivity());
	TestEqual(TEXT("Fallback StickSensitivity default is 1.0"), Kate->TestStickSensitivity(), 1.f);
	TestEqual(TEXT("Default yaw rate"), Kate->TestStickYawDegreesPerSecond(), 180.f);
	TestEqual(TEXT("Default pitch rate"), Kate->TestStickPitchDegreesPerSecond(), 120.f);

	// Full deflection: value^1.5 of 1.0 is still 1.0, so this reads the raw rate * delta.
	const FVector2D FullDeltaHalfSecond = Kate->ComputeStickLookDelta(FVector2D(1.f, 1.f), 0.5f, false);
	TestEqual(TEXT("Full stick yaw scales by delta time"),
		static_cast<float>(FullDeltaHalfSecond.X), Kate->TestStickYawDegreesPerSecond() * 0.5f);
	TestEqual(TEXT("Full stick pitch scales by delta time"),
		static_cast<float>(FullDeltaHalfSecond.Y), Kate->TestStickPitchDegreesPerSecond() * 0.5f);

	// Doubling delta time doubles the output; halving the raw input roughly quarters it (the
	// ease curve is value^1.5, so 0.5^1.5 =~ 0.3536, not 0.5).
	const FVector2D HalfInput = Kate->ComputeStickLookDelta(FVector2D(0.5f, 0.f), 1.f, false);
	const float Eased = FMath::Pow(0.5f, 1.5f);
	TestEqual(TEXT("The ease curve is value^1.5, not linear"),
		static_cast<float>(HalfInput.X), Kate->TestStickYawDegreesPerSecond() * Eased);

	// Negative input eases the same way, sign preserved.
	const FVector2D NegativeInput = Kate->ComputeStickLookDelta(FVector2D(-1.f, 0.f), 1.f, false);
	TestEqual(TEXT("A full stick left produces a negative yaw delta"),
		static_cast<float>(NegativeInput.X), -Kate->TestStickYawDegreesPerSecond());

	// Aiming halves the rate, same as AimLookMultiplier does for the mouse.
	const FVector2D AimingDelta = Kate->ComputeStickLookDelta(FVector2D(1.f, 0.f), 1.f, true);
	TestEqual(TEXT("Aiming applies AimStickRateMultiplier"),
		static_cast<float>(AimingDelta.X), Kate->TestStickYawDegreesPerSecond() * Kate->TestAimStickRateMultiplier());

	// Zero delta time (e.g. a paused first tick) must never divide by zero or return garbage.
	const FVector2D ZeroDelta = Kate->ComputeStickLookDelta(FVector2D(1.f, 1.f), 0.f, false);
	TestEqual(TEXT("Zero delta time yields zero movement"), static_cast<float>(ZeroDelta.X), 0.f);
	TestEqual(TEXT("Zero delta time yields zero movement (Y)"), static_cast<float>(ZeroDelta.Y), 0.f);

	// Settings drive the multiplier once a subsystem broadcasts.
	FHawkeyeSettings Settings;
	Settings.StickSensitivity = 2.f;
	Kate->TestApplySettings(Settings);
	TestTrue(TEXT("The subsystem's StickSensitivity is now in use"), Kate->TestHasSettingsStickSensitivity());

	const FVector2D DoubledSensitivity = Kate->ComputeStickLookDelta(FVector2D(1.f, 0.f), 1.f, false);
	TestEqual(TEXT("StickSensitivity 2.0 doubles the yaw delta"),
		static_cast<float>(DoubledSensitivity.X), Kate->TestStickYawDegreesPerSecond() * 2.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeStickLookUpIsUpByDefaultAndInverted,
	"Hawkeye.Character.StickLookUpIsUpByDefaultAndInverted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeStickLookUpIsUpByDefaultAndInverted::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
		AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Kate)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	TestFalse(TEXT("Not inverted by default"), Kate->TestInvertStickY());

	// Raw stick +Y ("stick pushed up") must produce a positive pitch delta (AddControllerPitchInput
	// of a positive value looks up, per the mouse path below), never the inverted opposite.
	const FVector2D Up = Kate->ComputeStickLookDelta(FVector2D(0.f, 1.f), 1.f, false);
	TestTrue(TEXT("Stick up looks up by default"), Up.Y > 0.f);

	FHawkeyeSettings Inverted;
	Inverted.bInvertStickY = true;
	Kate->TestApplySettings(Inverted);
	TestTrue(TEXT("bInvertStickY took effect"), Kate->TestInvertStickY());

	const FVector2D UpInverted = Kate->ComputeStickLookDelta(FVector2D(0.f, 1.f), 1.f, false);
	TestTrue(TEXT("Stick up looks down once inverted"), UpInverted.Y < 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeMouseLookUpIsUpByDefaultAndInverted,
	"Hawkeye.Character.MouseLookUpIsUpByDefaultAndInverted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeMouseLookUpIsUpByDefaultAndInverted::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
		AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Kate)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	TestFalse(TEXT("Not inverted by default"), Kate->TestInvertMouseY());

	// IA_Look's NegateY modifier already turns "mouse moved up" (raw Mouse2D Y negative) into a
	// positive Y here; ComputeLookDelta must pass it straight through by default.
	const FVector2D Up = Kate->ComputeLookDelta(FVector2D(0.f, 1.f), false);
	TestTrue(TEXT("Mouse up (post-modifier positive Y) looks up by default"), Up.Y > 0.f);

	FHawkeyeSettings Inverted;
	Inverted.bInvertMouseY = true;
	Kate->TestApplySettings(Inverted);
	TestTrue(TEXT("bInvertMouseY took effect"), Kate->TestInvertMouseY());

	const FVector2D UpInverted = Kate->ComputeLookDelta(FVector2D(0.f, 1.f), false);
	TestTrue(TEXT("Mouse up looks down once inverted"), UpInverted.Y < 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsInvertYFlagsRoundTrip,
	"Hawkeye.Settings.InvertYFlagsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsInvertYFlagsRoundTrip::RunTest(const FString& Parameters)
{
	HawkeyeGamepadInputTest::ClearTestSlot();
	UHawkeyeSettingsSubsystem* Writer = HawkeyeGamepadInputTest::MakeSubsystem();
	if (!Writer)
	{
		AddError(TEXT("Could not create the settings subsystem."));
		return false;
	}

	TestFalse(TEXT("bInvertMouseY defaults false"), Writer->GetInvertMouseY());
	TestFalse(TEXT("bInvertStickY defaults false"), Writer->GetInvertStickY());

	Writer->SetInvertMouseY(true);
	Writer->SetInvertStickY(true);
	TestTrue(TEXT("bInvertMouseY set"), Writer->GetInvertMouseY());
	TestTrue(TEXT("bInvertStickY set"), Writer->GetInvertStickY());

	UHawkeyeSettingsSubsystem* Reader = HawkeyeGamepadInputTest::MakeSubsystem();
	Reader->Load();
	TestTrue(TEXT("bInvertMouseY round-trips through the slot"), Reader->GetInvertMouseY());
	TestTrue(TEXT("bInvertStickY round-trips through the slot"), Reader->GetInvertStickY());

	HawkeyeGamepadInputTest::ClearTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsStickSensitivityRoundTrips,
	"Hawkeye.Settings.StickSensitivityRoundTrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsStickSensitivityRoundTrips::RunTest(const FString& Parameters)
{
	HawkeyeGamepadInputTest::ClearTestSlot();
	UHawkeyeSettingsSubsystem* Settings = HawkeyeGamepadInputTest::MakeSubsystem();
	if (!Settings)
	{
		AddError(TEXT("Could not create the settings subsystem."));
		return false;
	}

	TestEqual(TEXT("The default is 1.0"), Settings->GetStickSensitivity(), 1.f);

	Settings->SetStickSensitivity(0.f);
	TestEqual(TEXT("Zero clamps up to the minimum"),
		Settings->GetStickSensitivity(), UHawkeyeSettingsSubsystem::MinStickSensitivity);

	Settings->SetStickSensitivity(50.f);
	TestEqual(TEXT("A huge value clamps down to the maximum"),
		Settings->GetStickSensitivity(), UHawkeyeSettingsSubsystem::MaxStickSensitivity);

	Settings->SetStickSensitivity(1.75f);
	TestEqual(TEXT("A value inside the range is kept"), Settings->GetStickSensitivity(), 1.75f);

	// Round-trips alongside LookSensitivity through the same slot.
	Settings->SetLookSensitivity(0.3f);
	TestTrue(TEXT("The slot exists after a change"),
		UGameplayStatics::DoesSaveGameExist(HawkeyeGamepadInputTest::TestSlot, 0));

	UHawkeyeSettingsSubsystem* Reader = HawkeyeGamepadInputTest::MakeSubsystem();
	Reader->Load();
	TestEqual(TEXT("StickSensitivity round-trips through the slot"), Reader->GetStickSensitivity(), 1.75f);
	TestEqual(TEXT("LookSensitivity round-trips alongside it"), Reader->GetLookSensitivity(), 0.3f);

	HawkeyeGamepadInputTest::ClearTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsOldVersionMigratesToDefaults,
	"Hawkeye.Settings.OldVersionMigratesToDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsOldVersionMigratesToDefaults::RunTest(const FString& Parameters)
{
	HawkeyeGamepadInputTest::ClearTestSlot();

	// A save written before StickSensitivity existed: Version 1, no notion of the new field.
	UHawkeyeSettingsSave* Stale =
		Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!Stale)
	{
		AddError(TEXT("Could not create a save object."));
		return false;
	}

	Stale->Settings.LookSensitivity = 0.9f;
	Stale->Settings.Version = 1;
	UGameplayStatics::SaveGameToSlot(Stale, HawkeyeGamepadInputTest::TestSlot, 0);

	TestTrue(TEXT("CurrentVersion moved on from the stale save's version"),
		FHawkeyeSettings::CurrentVersion != 1);

	// A version we do not understand must never reach the player's sliders.
	AddExpectedError(TEXT("is version"), EAutomationExpectedErrorFlags::Contains, 0);

	UHawkeyeSettingsSubsystem* Settings = HawkeyeGamepadInputTest::MakeSubsystem();
	Settings->Load();
	TestEqual(TEXT("A stale version yields the default look sensitivity"),
		Settings->GetLookSensitivity(), FHawkeyeSettings().LookSensitivity);
	TestEqual(TEXT("A stale version yields the default stick sensitivity"),
		Settings->GetStickSensitivity(), FHawkeyeSettings().StickSensitivity);

	HawkeyeGamepadInputTest::ClearTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeCharacterTakedownBeforeInteract,
	"Hawkeye.Character.TakedownBeforeInteract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeCharacterTakedownBeforeInteract::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
		AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Kate)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	// Takedown and Interact share the gamepad's Y face button. Input_Takedown is bound first
	// (see SetupPlayerInputComponent) and records whether it executed one; Input_Interact must
	// consume that flag and back off rather than also interacting on the same press.
	Kate->TestSetTookDownThisPress(true);
	Kate->TestFireInputInteract();
	TestFalse(TEXT("Interact consumes and clears a takedown that just landed"), Kate->TestTookDownThisPress());

	// With no takedown pending this press, Interact runs its own logic normally (and safely,
	// with no focused interactable nearby).
	Kate->TestSetTookDownThisPress(false);
	Kate->TestFireInputInteract();
	TestFalse(TEXT("The flag stays clear when no takedown ran"), Kate->TestTookDownThisPress());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
