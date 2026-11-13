#include "HawkeyePlayerController.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/InventoryComponent.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"

#include "Tests/HawkeyeShots.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Reference shots of the hotbar and the inventory screen, written to Saved/Screenshots/UI/.
 * Look-at-them tools, not assertions, like the room shots in ScreenshotTest.cpp:
 *
 *   Hawkeye.Screenshot.Hotbar    hotbar.png    the HUD quiver: standard arrows (30) and grapple (6/6)
 *   Hawkeye.Screenshot.Inventory inventory.png the Tab screen over a paused game
 *
 * Both are taken on L_District_EastVillage, where Kate spawns at the PlayerStart. The bow is a
 * stand-in definition made in the test until DA_Bow_Kate exists (TODO(stage2): load that).
 * Needs a real RHI, so it is an explicit no-op in the normal -nullrhi suite. Run it from the
 * standalone game (claude-docs/testing.md section 2b):
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended
 *       -nosplash -log -ExecCmds="Automation RunTests Hawkeye.Screenshot.Hotbar; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotHotbar, "Hawkeye.Screenshot.Hotbar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotInventory, "Hawkeye.Screenshot.Inventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter)

namespace HawkeyeUiShot
{
	/** DA_Bow_Kate and the arrows. Loaded, because this shot is about content. */
	static const TCHAR* BowPath = TEXT("/Game/Blueprints/Weapons/DA_Bow_Kate.DA_Bow_Kate");
	static const TCHAR* StandardPath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Standard.DA_Arrow_Standard");
	static const TCHAR* GrapplePath = TEXT("/Game/Blueprints/Weapons/DA_Arrow_Grapple.DA_Arrow_Grapple");

	/** The district the shots are taken on; the pawn spawns at its PlayerStart. */
	static const TCHAR* MapPath = TEXT("/Game/Maps/L_District_EastVillage");

	static FString UiPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("UI") / FileName);
	}

	static UWorld* FindWorld()
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

	static AHawkeyePlayerController* FindController()
	{
		UWorld* World = FindWorld();
		return World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	static UInventoryComponent* FindInventory()
	{
		const AHawkeyePlayerController* PC = FindController();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	}

	/** True when this process cannot render, in which case the shots are skipped. */
	static bool SkipWithoutRHI(FAutomationTestBase& Test)
	{
		if (FApp::CanEverRender())
		{
			return false;
		}
		Test.AddWarning(TEXT("Skipped (no RHI): the UI screenshots. Re-run without -nullrhi to capture them."));
		return true;
	}
}

/**
 * DA_Bow_Kate with 30 standard and 6 grapple arrows, so the hotbar shows two filled slots.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeDrawBow, FAutomationTestBase*, Test);

bool FHawkeyeDrawBow::Update()
{
	UInventoryComponent* Inventory = HawkeyeUiShot::FindInventory();
	if (!Inventory)
	{
		Test->AddError(TEXT("No inventory on the player pawn; check BP_HawkeyeCharacter."));
		return true;
	}

	// Back at the PlayerStart looking through Kate's own camera. When the group runs in one
	// process the map is not reopened, so an earlier shot may have moved, hidden or left her.
	AHawkeyePlayerController* PC = HawkeyeUiShot::FindController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn)
	{
		TActorIterator<APlayerStart> Start(Pawn->GetWorld());
		if (Start)
		{
			Pawn->TeleportTo(Start->GetActorLocation(), Start->GetActorRotation(), false, true);
			PC->SetControlRotation(Start->GetActorRotation());
		}
		Pawn->SetActorHiddenInGame(false);
		PC->SetViewTarget(Pawn);
	}

	UBowDefinition* Bow = LoadObject<UBowDefinition>(nullptr, HawkeyeUiShot::BowPath);
	UArrowDefinition* Standard = LoadObject<UArrowDefinition>(nullptr, HawkeyeUiShot::StandardPath);
	UArrowDefinition* Grapple = LoadObject<UArrowDefinition>(nullptr, HawkeyeUiShot::GrapplePath);
	if (!Bow || !Standard || !Grapple)
	{
		Test->AddError(TEXT("DA_Bow_Kate or the DA_Arrow_* assets did not load; run Tools\\create-content.ps1."));
		return true;
	}
	FHawkeyeQuiverSlot StandardSlot;
	StandardSlot.Arrow = Standard;
	StandardSlot.Count = 30;
	FHawkeyeQuiverSlot GrappleSlot;
	GrappleSlot.Arrow = Grapple;
	GrappleSlot.Count = 6;
	Inventory->ApplyStartingQuiver(Bow, { StandardSlot, GrappleSlot });

	if (!Inventory->HasBow() || Inventory->IsArrowSlotEmpty(2))
	{
		Test->AddError(TEXT("The quiver should hold the bow, standard and grapple arrows."));
	}
	return true;
}

/** Open the Tab screen the way the player would. */
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeOpenInventoryScreen, FAutomationTestBase*, Test);

bool FHawkeyeOpenInventoryScreen::Update()
{
	AHawkeyePlayerController* PC = HawkeyeUiShot::FindController();
	if (!PC)
	{
		Test->AddError(TEXT("No AHawkeyePlayerController to open the inventory with."));
		return true;
	}

	PC->ToggleInventory();
	if (!PC->IsInventoryOpen())
	{
		Test->AddError(TEXT("ToggleInventory did nothing; check InventoryWidgetClass on BP_HawkeyePlayerController."));
	}
	return true;
}

/** Ask for one screenshot under Saved/Screenshots/UI, with the UI drawn. */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FHawkeyeTakeInventoryUiShot, FAutomationTestBase*, Test, FString, FileName);

bool FHawkeyeTakeInventoryUiShot::Update()
{
	const FString FullPath = HawkeyeUiShot::UiPath(FileName);
	HawkeyeShots::Request(Test, FullPath, /*bShowUI=*/true);
	return true;
}

bool FHawkeyeScreenshotHotbar::RunTest(const FString& Parameters)
{
	if (HawkeyeUiShot::SkipWithoutRHI(*this))
	{
		return true;
	}

	AutomationOpenMap(HawkeyeUiShot::MapPath);
	// Long enough for the editor's own billboards to stop drawing over the game view.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));

	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeDrawBow(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTakeInventoryUiShot(this, TEXT("hotbar.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

bool FHawkeyeScreenshotInventory::RunTest(const FString& Parameters)
{
	if (HawkeyeUiShot::SkipWithoutRHI(*this))
	{
		return true;
	}

	AutomationOpenMap(HawkeyeUiShot::MapPath);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));

	// Something to list: the bow and two arrow types.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeDrawBow(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeOpenInventoryScreen(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeTakeInventoryUiShot(this, TEXT("inventory.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));

	// Fails the test for any capture that did not reach the disk.
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
