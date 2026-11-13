#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Settings/HawkeyeSettings.h"
#include "Settings/HawkeyeSettingsSave.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeSettingsTest
{
	/** Never the real slot: a test run must not change what the player set. */
	static const TCHAR* TestSlot = TEXT("HawkeyeSettingsAutomationTest");

	static UHawkeyeSettingsSubsystem* MakeSubsystem(bool bUseTestSlot = true)
	{
		// UGameInstanceSubsystem declares ClassWithin = UGameInstance, so a package outer ensures.
		UGameInstance* Outer = NewObject<UGameInstance>(GEngine);
		UHawkeyeSettingsSubsystem* Subsystem = NewObject<UHawkeyeSettingsSubsystem>(Outer);
		if (Subsystem && bUseTestSlot)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsClampsSensitivity, "Hawkeye.Settings.ClampsSensitivity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsClampsSensitivity::RunTest(const FString& Parameters)
{
	HawkeyeSettingsTest::ClearTestSlot();
	UHawkeyeSettingsSubsystem* Settings = HawkeyeSettingsTest::MakeSubsystem();
	if (!Settings)
	{
		AddError(TEXT("Could not create the settings subsystem."));
		return false;
	}

	TestEqual(TEXT("The default is 0.2"), Settings->GetLookSensitivity(), 0.2f);

	Settings->SetLookSensitivity(0.f);
	TestEqual(TEXT("Zero clamps up to the minimum"),
		Settings->GetLookSensitivity(), UHawkeyeSettingsSubsystem::MinLookSensitivity);

	Settings->SetLookSensitivity(-5.f);
	TestEqual(TEXT("A negative value clamps up to the minimum"),
		Settings->GetLookSensitivity(), UHawkeyeSettingsSubsystem::MinLookSensitivity);

	Settings->SetLookSensitivity(12.f);
	TestEqual(TEXT("A huge value clamps down to the maximum"),
		Settings->GetLookSensitivity(), UHawkeyeSettingsSubsystem::MaxLookSensitivity);

	Settings->SetLookSensitivity(0.35f);
	TestEqual(TEXT("A value inside the range is kept"), Settings->GetLookSensitivity(), 0.35f);

	HawkeyeSettingsTest::ClearTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsBroadcastsOnce, "Hawkeye.Settings.BroadcastsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsBroadcastsOnce::RunTest(const FString& Parameters)
{
	HawkeyeSettingsTest::ClearTestSlot();
	UHawkeyeSettingsSubsystem* Settings = HawkeyeSettingsTest::MakeSubsystem();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	if (!Settings || !Listener)
	{
		AddError(TEXT("Could not create the settings subsystem or the listener."));
		return false;
	}

	Settings->OnSettingsChanged.AddDynamic(Listener, &UHawkeyeTestListener::HandleSettingsChanged);

	Settings->SetLookSensitivity(0.5f);
	TestEqual(TEXT("One change, one broadcast"), Listener->SettingsChangedCount, 1);
	TestEqual(TEXT("And it carried the new value"), Listener->LastLookSensitivity, 0.5f);

	// A slider fires on every pixel of drag, most of which land on the same value.
	Settings->SetLookSensitivity(0.5f);
	TestEqual(TEXT("Setting the same value again broadcasts nothing"), Listener->SettingsChangedCount, 1);

	Settings->SetLookSensitivity(0.6f);
	TestEqual(TEXT("A real change broadcasts again"), Listener->SettingsChangedCount, 2);

	Settings->OnSettingsChanged.RemoveDynamic(Listener, &UHawkeyeTestListener::HandleSettingsChanged);
	HawkeyeSettingsTest::ClearTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsRoundTripsThroughTheSlot, "Hawkeye.Settings.RoundTripsThroughTheSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsRoundTripsThroughTheSlot::RunTest(const FString& Parameters)
{
	HawkeyeSettingsTest::ClearTestSlot();

	UHawkeyeSettingsSubsystem* Writer = HawkeyeSettingsTest::MakeSubsystem();
	if (!Writer)
	{
		AddError(TEXT("Could not create the writing settings subsystem."));
		return false;
	}

	// SetLookSensitivity saves immediately; that is the behaviour being asserted.
	Writer->SetLookSensitivity(0.42f);
	TestTrue(TEXT("The slot exists after a change"),
		UGameplayStatics::DoesSaveGameExist(HawkeyeSettingsTest::TestSlot, 0));

	UHawkeyeSettingsSubsystem* Reader = HawkeyeSettingsTest::MakeSubsystem();
	Reader->Load();
	TestEqual(TEXT("A fresh subsystem loads the stored value"), Reader->GetLookSensitivity(), 0.42f);

	HawkeyeSettingsTest::ClearTestSlot();

	UHawkeyeSettingsSubsystem* AfterDelete = HawkeyeSettingsTest::MakeSubsystem();
	AfterDelete->Load();
	TestEqual(TEXT("With no slot on disk the defaults come back"), AfterDelete->GetLookSensitivity(), 0.2f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsVersionMismatchYieldsDefaults,
	"Hawkeye.Settings.VersionMismatchYieldsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsVersionMismatchYieldsDefaults::RunTest(const FString& Parameters)
{
	HawkeyeSettingsTest::ClearTestSlot();

	UHawkeyeSettingsSave* Stale =
		Cast<UHawkeyeSettingsSave>(UGameplayStatics::CreateSaveGameObject(UHawkeyeSettingsSave::StaticClass()));
	if (!Stale)
	{
		AddError(TEXT("Could not create a save object."));
		return false;
	}

	Stale->Settings.LookSensitivity = 0.9f;
	Stale->Settings.Version = FHawkeyeSettings::CurrentVersion + 7;
	UGameplayStatics::SaveGameToSlot(Stale, HawkeyeSettingsTest::TestSlot, 0);

	// A version we do not understand must never reach the player's mouse.
	AddExpectedError(TEXT("is version"), EAutomationExpectedErrorFlags::Contains, 0);

	UHawkeyeSettingsSubsystem* Settings = HawkeyeSettingsTest::MakeSubsystem();
	Settings->Load();
	TestEqual(TEXT("A version mismatch yields defaults, not the stale value"),
		Settings->GetLookSensitivity(), 0.2f);

	HawkeyeSettingsTest::ClearTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSettingsDriveLookDelta, "Hawkeye.Settings.DriveLookDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSettingsDriveLookDelta::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(TestWorld.SpawnActor(
		AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!Kate)
	{
		AddError(TEXT("Could not spawn the test character."));
		return false;
	}

	// No game instance subsystem in a test world, so the character's own property is the source.
	TestFalse(TEXT("No subsystem was found"), Kate->TestHasSettingsSensitivity());
	const float Fallback = Kate->TestLookSensitivity();
	TestEqual(TEXT("The C++ fallback default is 0.2"), Fallback, 0.2f);
	TestEqual(TEXT("Hip look scales by the fallback"),
		static_cast<float>(Kate->ComputeLookDelta(FVector2D(10.f, 0.f), false).X), 10.f * Fallback);

	FHawkeyeSettings Settings;
	Settings.LookSensitivity = 0.5f;
	Kate->TestApplySettings(Settings);

	TestTrue(TEXT("The subsystem value is now in use"), Kate->TestHasSettingsSensitivity());
	TestEqual(TEXT("Hip look scales by the setting, not the property"),
		static_cast<float>(Kate->ComputeLookDelta(FVector2D(10.f, 0.f), false).X), 5.f);
	TestEqual(TEXT("Pitch uses the same scale"),
		static_cast<float>(Kate->ComputeLookDelta(FVector2D(0.f, -4.f), false).Y), -2.f);
	TestEqual(TEXT("Aiming applies AimLookMultiplier on top"),
		static_cast<float>(Kate->ComputeLookDelta(FVector2D(10.f, 0.f), true).X), 5.f * Kate->TestAimLookMultiplier());

	// A second broadcast wins: the slider is live while the pause menu is open.
	Settings.LookSensitivity = 0.1f;
	Kate->TestApplySettings(Settings);
	TestEqual(TEXT("A later change takes effect immediately"),
		static_cast<float>(Kate->ComputeLookDelta(FVector2D(10.f, 0.f), false).X), 1.f);

	return true;
}

#endif
