#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeSprintToggle.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeSettingsWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/HawkeyeSettingsSave.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The sprint toggle (gameplay-semantics.md, "Movement"; 2026-09-29 after "sprint on controller should be pressing the
 * left joystick once and the character continues to sprint the whole time"): one L3 press sprints until a second
 * press, the stick centred past 0.6 s, an aim, a crouch, a slide's end or a menu. Shift stays a hold unless the
 * setting says Toggle; the setting's Hold or Toggle applies to both.
 */
namespace HawkeyeSprintToggleTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static AHawkeyeAimTestCharacter* Spawn(const FHawkeyeTestWorld& TestWorld)
	{
		AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
			TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
		if (Kate && Kate->GetCharacterMovement())
		{
			Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		return Kate;
	}

	/** Seconds of frames at 60 Hz, the stick pushed or not. */
	static void Run(AHawkeyeAimTestCharacter* Kate, float Seconds, bool bStickPushed)
	{
		for (float T = 0.f; T < Seconds - 1e-4f; T += 1.f / 60.f)
		{
			Kate->TestTickSprintToggle(1.f / 60.f, bStickPushed);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSprintToggleStateMachine, "Hawkeye.Sprint.ToggleStateMachine", HawkeyeSprintToggleTest::Flags)

bool FHawkeyeSprintToggleStateMachine::RunTest(const FString& Parameters)
{
	FHawkeyeSprintToggle Toggle;
	TestEqual(TEXT("Centre time 0.6 s"), Toggle.CentreSeconds, 0.6f);
	TestFalse(TEXT("Starts off"), Toggle.IsOn());
	TestTrue(TEXT("A press turns it on"), Toggle.Press());

	// Turns and short stops: the stick back in the dead zone for 0.5 s, then pushed again.
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		TestEqual(TEXT("A 0.5 s stop keeps it"), Toggle.Tick(1.f / 60.f, true), EHawkeyeSprintStop::None);
	}
	TestTrue(TEXT("Still on after a 0.5 s stop"), Toggle.IsOn());
	Toggle.Tick(1.f / 60.f, false);
	TestEqual(TEXT("Pushing the stick again restarts the clock"), Toggle.GetCentredSeconds(), 0.f);
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Toggle.Tick(1.f / 60.f, true);
	}
	TestTrue(TEXT("A second 0.5 s stop keeps it too"), Toggle.IsOn());

	// Centred past 0.6 s: off, on the frame it passes.
	EHawkeyeSprintStop Why = EHawkeyeSprintStop::None;
	int32 Frames = 0;
	while (Toggle.IsOn() && Frames < 200)
	{
		Why = Toggle.Tick(1.f / 60.f, true);
		++Frames;
	}
	TestEqual(TEXT("The stick centred turns it off"), Why, EHawkeyeSprintStop::StickCentred);
	TestTrue(TEXT("Just past 0.6 s in all"), Frames + 30 >= 36 && Frames + 30 <= 38);
	TestEqual(TEXT("Off stays off"), Toggle.Tick(1.f, true), EHawkeyeSprintStop::None);

	TestTrue(TEXT("On again"), Toggle.Press());
	TestFalse(TEXT("A second press turns it off"), Toggle.Press());
	TestFalse(TEXT("Off"), Toggle.IsOn());
	TestTrue(TEXT("On"), Toggle.Press());
	TestTrue(TEXT("Stop reports it was on"), Toggle.Stop());
	TestFalse(TEXT("And a second stop that it was not"), Toggle.Stop());

	// Which presses toggle.
	TestTrue(TEXT("Default: a pad toggles"), FHawkeyeSprintToggle::UsesToggle(EHawkeyeSprintMode::Default, true));
	TestFalse(TEXT("Default: keys hold"), FHawkeyeSprintToggle::UsesToggle(EHawkeyeSprintMode::Default, false));
	TestFalse(TEXT("Hold: a pad holds"), FHawkeyeSprintToggle::UsesToggle(EHawkeyeSprintMode::Hold, true));
	TestFalse(TEXT("Hold: keys hold"), FHawkeyeSprintToggle::UsesToggle(EHawkeyeSprintMode::Hold, false));
	TestTrue(TEXT("Toggle: a pad toggles"), FHawkeyeSprintToggle::UsesToggle(EHawkeyeSprintMode::Toggle, true));
	TestTrue(TEXT("Toggle: keys toggle"), FHawkeyeSprintToggle::UsesToggle(EHawkeyeSprintMode::Toggle, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSprintToggleOnKate, "Hawkeye.Sprint.PadPressSprintsUntilItEnds", HawkeyeSprintToggleTest::Flags)

bool FHawkeyeSprintToggleOnKate::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSprintToggleTest;
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Spawn(TestWorld);
	if (!TestNotNull(TEXT("Kate"), Kate))
	{
		return false;
	}
	TestEqual(TEXT("Default mode"), Kate->GetSprintMode(), EHawkeyeSprintMode::Default);

	// L3 once: she sprints with the button up, through a turn and a short stop.
	Kate->PressSprint(/*bFromGamepad=*/true);
	Kate->ReleaseSprint();
	TestTrue(TEXT("Sprinting after the press is let go"), Kate->IsSprinting() && Kate->IsSprintToggled());
	Run(Kate, 2.f, true);
	Run(Kate, 0.4f, false);
	Run(Kate, 1.f, true);
	TestTrue(TEXT("Still sprinting after a 0.4 s stop"), Kate->IsSprinting());
	Run(Kate, 0.7f, false);
	TestFalse(TEXT("The stick centred for 0.7 s ends it"), Kate->IsSprinting() || Kate->IsSprintToggled());

	// A second press.
	Kate->PressSprint(true);
	Run(Kate, 0.5f, true);
	Kate->PressSprint(true);
	TestFalse(TEXT("A second press ends it"), Kate->IsSprinting());

	// Aim.
	Kate->PressSprint(true);
	Kate->PressAim();
	TestFalse(TEXT("An aim ends it"), Kate->IsSprinting() || Kate->IsSprintToggled());
	TestTrue(TEXT("And the aim is up (a held sprint refuses it; a toggled one gives way)"), Kate->IsAiming());
	Kate->ReleaseAim();

	// Crouch while standing (too slow to slide): a crouch, and the sprint is over.
	Kate->PressSprint(true);
	Kate->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Kate->PressCrouch();
	TestFalse(TEXT("A crouch ends it"), Kate->IsSprinting() || Kate->IsSprintToggled());
	Kate->ReleaseCrouch();
	Kate->PressCrouch();
	Kate->ReleaseCrouch();

	// Crouch at speed: a slide, still toggled through it, over when the slide ends.
	Kate->UnCrouch();
	Kate->GetCharacterMovement()->bWantsToCrouch = false;
	Kate->PressSprint(true);
	Kate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Kate->GetCharacterMovement()->Velocity = FVector(700.f, 0.f, 0.f);
	Kate->PressCrouch();
	TestTrue(TEXT("A crouch at a sprint slides"), Kate->IsSliding());
	TestTrue(TEXT("The toggle holds through the slide"), Kate->IsSprintToggled());
	Kate->TestTickSlide(Kate->TestSlideSeconds() + 0.05f);
	TestFalse(TEXT("The slide is over"), Kate->IsSliding());
	TestFalse(TEXT("And so is the sprint"), Kate->IsSprinting() || Kate->IsSprintToggled());

	// A menu (the controller calls this on every pause-style screen).
	Kate->PressSprint(true);
	TestTrue(TEXT("A menu stops it"), Kate->StopSprintToggle(EHawkeyeSprintStop::Menu));
	TestFalse(TEXT("Not sprinting under the menu"), Kate->IsSprinting());

	// Keys on the default: a hold.
	Kate->PressSprint(/*bFromGamepad=*/false);
	TestTrue(TEXT("Shift sprints"), Kate->IsSprinting());
	TestFalse(TEXT("As a hold"), Kate->IsSprintToggled());
	Kate->ReleaseSprint();
	TestFalse(TEXT("Letting Shift go stops"), Kate->IsSprinting());

	// The setting set explicitly applies to both.
	Kate->TestSetSprintMode(EHawkeyeSprintMode::Toggle);
	Kate->PressSprint(false);
	Kate->ReleaseSprint();
	TestTrue(TEXT("Toggle: Shift toggles"), Kate->IsSprinting() && Kate->IsSprintToggled());
	Kate->PressSprint(false);
	TestFalse(TEXT("And a second Shift press ends it"), Kate->IsSprinting());
	Kate->TestSetSprintMode(EHawkeyeSprintMode::Hold);
	Kate->PressSprint(true);
	TestFalse(TEXT("Hold: L3 holds"), Kate->IsSprintToggled());
	Kate->ReleaseSprint();
	TestFalse(TEXT("And letting L3 go stops"), Kate->IsSprinting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSprintModeSaved, "Hawkeye.Sprint.ModeIsSavedAndMigrated", HawkeyeSprintToggleTest::Flags)

bool FHawkeyeSprintModeSaved::RunTest(const FString& Parameters)
{
	static const TCHAR* TestSlot = TEXT("HawkeyeSettingsAutomation_Sprint");
	auto ClearSlot = []()
	{
		if (UGameplayStatics::DoesSaveGameExist(TestSlot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
		}
	};
	auto MakeSettings = []()
	{
		UHawkeyeSettingsSubsystem* Settings = NewObject<UHawkeyeSettingsSubsystem>(NewObject<UGameInstance>(GEngine));
		Settings->SlotNameOverride = TestSlot;
		return Settings;
	};
	TestTrue(TEXT("Settings version 8 or later (8 added the sprint row)"), FHawkeyeSettings::CurrentVersion >= 8);
	ClearSlot();
	UHawkeyeSettingsSubsystem* Writer = MakeSettings();
	Writer->Load();
	TestEqual(TEXT("A fresh slot is Default"), Writer->GetSettings().SprintMode, EHawkeyeSprintMode::Default);
	Writer->SetSprintMode(EHawkeyeSprintMode::Toggle);
	UHawkeyeSettingsSubsystem* Reader = MakeSettings();
	Reader->Load();
	TestEqual(TEXT("Toggle survives the save"), Reader->GetSettings().SprintMode, EHawkeyeSprintMode::Toggle);

	// A version 7 save (before the row) keeps what the player set and comes up Default.
	ClearSlot();
	UHawkeyeSettingsSave* Old = Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!TestNotNull(TEXT("A save object"), Old))
	{
		return false;
	}
	Old->Settings.LookSensitivity = 0.33f;
	Old->Settings.AimAssist = EHawkeyeAimAssist::Strong;
	Old->Settings.SprintMode = EHawkeyeSprintMode::Default;
	Old->Settings.Version = 7;
	UGameplayStatics::SaveGameToSlot(Old, TestSlot, 0);
	UHawkeyeSettingsSubsystem* Migrated = MakeSettings();
	Migrated->Load();
	TestEqual(TEXT("Version 7 keeps the sensitivity"), Migrated->GetLookSensitivity(), 0.33f);
	TestEqual(TEXT("And the aim assist"), Migrated->GetStoredSettings().AimAssist, EHawkeyeAimAssist::Strong);
	TestEqual(TEXT("And gets the Default sprint"), Migrated->GetStoredSettings().SprintMode, EHawkeyeSprintMode::Default);
	TestEqual(TEXT("Stamped the current version"), Migrated->GetStoredSettings().Version, FHawkeyeSettings::CurrentVersion);
	ClearSlot();

	// The row's words.
	TestEqual(TEXT("Default reads"), FHawkeyeSprintToggle::GetModeName(EHawkeyeSprintMode::Default).ToString(),
		FString(TEXT("Hold on keys, toggle on pad")));
	TestEqual(TEXT("Hold reads"), FHawkeyeSprintToggle::GetModeName(EHawkeyeSprintMode::Hold).ToString(), FString(TEXT("Hold")));
	TestEqual(TEXT("Toggle reads"), FHawkeyeSprintToggle::GetModeName(EHawkeyeSprintMode::Toggle).ToString(), FString(TEXT("Toggle")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
