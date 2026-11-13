#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HawkeyePlayerController.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Hawkeye.Screenshot.Settings: UI/settings.png, the pause menu's Settings screen, taken on
 * L_District_EastVillage. A look-at-it tool, not an assertion beyond "the screen opened". It
 * needs a real RHI, so it is an explicit no-op in the normal -nullrhi suite:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -ExecCmds="Automation RunTests Hawkeye.Screenshot.Settings; Quit"
 *       -unattended -nosplash -nop4 -stdout
 *
 * ClientContext as well as EditorContext, so the same pass can be run inside the standalone
 * game: an editor render once looked right while the game, which loads assets rather than
 * finding them resident, did not.
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot; Quit"
 */
static constexpr EAutomationTestFlags HawkeyeScreenshotFlags =
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotSettings, "Hawkeye.Screenshot.Settings",
	HawkeyeScreenshotFlags)

/** Where the UI shots land. Absolute, because FScreenshotRequest does not resolve /Game paths. */
static FString UiScreenshotPath(const FString& FileName)
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("UI") / FileName);
}

/** The game world the map was opened into, or null. */
static UWorld* FindScreenshotWorld()
{
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
		{
			return Context.World();
		}
	}
	return nullptr;
}

/** Open the pause menu and then the settings screen, the way the player would. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
	FHawkeyeOpenSettingsScreen, FAutomationTestBase*, Test);

bool FHawkeyeOpenSettingsScreen::Update()
{
	UWorld* World = FindScreenshotWorld();
	AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC)
	{
		Test->AddError(TEXT("No AHawkeyePlayerController to open the settings screen with."));
		return true;
	}

	PC->TogglePause();
	PC->OpenSettings();

	if (!PC->IsSettingsOpen())
	{
		Test->AddError(TEXT("OpenSettings did nothing; check SettingsWidgetClass on BP_HawkeyePlayerController."));
	}
	return true;
}

/** Ask for one screenshot under Saved/Screenshots/UI. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FHawkeyeTakeUiShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeTakeUiShot::Update()
{
	const FString FullPath = UiScreenshotPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

/** True when this process cannot render, in which case the screenshot tests do nothing. */
static bool SkipWithoutRHI(FAutomationTestBase& Test)
{
	// -nullrhi cannot render, so the normal suite runs these as explicit no-ops rather than
	// failures. FApp::CanEverRender() reads the -nullrhi switch off the command line, which is
	// exactly the condition we care about. Nothing here asserts; the PNGs are the output.
	if (FApp::CanEverRender())
	{
		return false;
	}
	Test.AddWarning(TEXT("Skipped (no RHI): the screenshots. Re-run without -nullrhi to capture them."));
	return true;
}

bool FHawkeyeScreenshotSettings::RunTest(const FString& Parameters)
{
	if (SkipWithoutRHI(*this))
	{
		return true;
	}

	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	// Long enough for the editor's own billboards to stop drawing over the game view.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeOpenSettingsScreen(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTakeUiShot(this, TEXT("settings.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
