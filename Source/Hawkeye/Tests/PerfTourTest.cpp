#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HawkeyeGameMode.h"
#include "HawkeyePlayerController.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The parts of a performance pass the laps don't cover (claude-docs/infrastructure.md, "Performance"):
 * standing on the block, the world map open, the inventory open, and a death reload with no save (the
 * level reopened, as RestartMission does), each held long enough for a -HawkeyePerfLog window.
 * Standalone game only; asserts nothing but that the screens opened and the level came back:
 *
 *   UnrealEditor-Cmd.exe Hawkeye.uproject -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -log
 *       -HawkeyePerfLog=2 -HawkeyeHitchMs=50 -ExecCmds="Automation RunTests Hawkeye.Perf.Tour; Quit"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyePerfTour, "Hawkeye.Perf.Tour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyePerfTour
{
	static const TCHAR* MapPath = TEXT("/Game/Maps/L_District_EastVillage");
	static constexpr float PhaseSeconds = 8.f;

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
}

bool FHawkeyePerfTour::RunTest(const FString& Parameters)
{
	using namespace HawkeyePerfTour;
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the perf tour. Run it from the standalone game (-game, no -nullrhi)."));
		return true;
	}
	AutomationOpenMap(MapPath);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { AHawkeyeGameMode::SetPerfPhase(TEXT("idle")); return true; }));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(PhaseSeconds));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		AHawkeyePlayerController* PC = FindController();
		AHawkeyeGameMode::SetPerfPhase(TEXT("map"));
		if (PC)
		{
			PC->SetMapOpen(true);
		}
		TestTrue(TEXT("The map opened"), PC && PC->IsMapOpen());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(PhaseSeconds));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		AHawkeyePlayerController* PC = FindController();
		if (PC)
		{
			PC->SetMapOpen(false);
		}
		AHawkeyeGameMode::SetPerfPhase(TEXT("between"));
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		AHawkeyePlayerController* PC = FindController();
		AHawkeyeGameMode::SetPerfPhase(TEXT("inventory"));
		if (PC)
		{
			PC->SetInventoryOpen(true);
		}
		TestTrue(TEXT("The inventory opened"), PC && PC->IsInventoryOpen());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(PhaseSeconds));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this]()
	{
		AHawkeyePlayerController* PC = FindController();
		if (PC)
		{
			PC->SetInventoryOpen(false);
		}
		// A death with no save to go back to: the level reopens.
		AHawkeyeGameMode* GameMode = FindWorld() ? Cast<AHawkeyeGameMode>(FindWorld()->GetAuthGameMode()) : nullptr;
		AHawkeyeGameMode::SetPerfPhase(TEXT("reload"));
		if (!TestNotNull(TEXT("A Hawkeye game mode"), GameMode))
		{
			return true;
		}
		GameMode->RestartMission(0.f);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		const AHawkeyeGameMode* GameMode = FindWorld() ? Cast<AHawkeyeGameMode>(FindWorld()->GetAuthGameMode()) : nullptr;
		return GameMode && GameMode->GetPlayableSeconds() >= 0.f;
	}, [this]()
	{
		AddError(TEXT("The level did not come back within 30 s of the reload."));
		return true;
	}, 30.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { AHawkeyeGameMode::SetPerfPhase(TEXT("after reload")); return true; }));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(PhaseSeconds));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { AHawkeyeGameMode::SetPerfPhase(FString()); return true; }));
	return true;
}

#endif
