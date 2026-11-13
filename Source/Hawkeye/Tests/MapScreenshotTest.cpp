#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HawkeyePlayerController.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "Tests/HawkeyeShots.h"
#include "Tests/MapScreenshots.h"
#include "UI/HawkeyeMapWidget.h"
#include "World/CityMapData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeMapShots
{
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

	static AHawkeyePlayerController* FindController(UWorld* World)
	{
		return World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	static FString ShotPath(const FString& FileName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Kate") / FileName);
	}
}

void HawkeyeAddMapShots(FAutomationTestBase* Test)
{
	using namespace HawkeyeMapShots;
	// Kate at the PlayerStart, as a new game begins, then the map.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		UWorld* World = FindWorld();
		AHawkeyePlayerController* PC = FindController(World);
		AHawkeyeCharacter* Kate = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
		if (PC && Kate && Kate->GetCharacterName().ToString() != TEXT("Kate"))
		{
			PC->bAllowSwitchingOverride = true;
			PC->SwitchCharacter();
			Kate = Cast<AHawkeyeCharacter>(PC->GetPawn());
		}
		TActorIterator<APlayerStart> StartIt(World);
		if (!PC || !Kate || !StartIt)
		{
			Test->AddError(TEXT("map_open.png: no Kate or no PlayerStart."));
			return true;
		}
		Kate->TeleportTo(StartIt->GetActorLocation(), StartIt->GetActorRotation());
		PC->SetControlRotation(StartIt->GetActorRotation());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		AHawkeyePlayerController* PC = FindController(FindWorld());
		if (!PC)
		{
			return true;
		}
		PC->SetMapOpen(true);
		Test->TestTrue(TEXT("map_open.png: the map opens"), PC->IsMapOpen());
		Test->TestTrue(TEXT("map_open.png: the game is paused under it"), PC->IsPaused());
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		const AHawkeyePlayerController* PC = FindController(FindWorld());
		UHawkeyeMapWidget* Map = PC ? PC->GetMapWidget() : nullptr;
		if (!Map || !Map->GetMapData())
		{
			Test->AddError(TEXT("map_open.png: no map widget or no map data."));
			return true;
		}
		// The cursor on the first safehouse in id order (the far one, avenue_b), so the shot shows a hover label too.
		Test->TestTrue(TEXT("map_open.png: a safehouse to hover"), Map->MoveCursorToIcon(EHawkeyeMapIconKind::Safehouse));
		int32 Counts[8] = {};
		for (const FHawkeyeMapIcon& Icon : Map->GetIcons())
		{
			++Counts[FMath::Min(static_cast<int32>(Icon.Kind), 7)];
		}
		Test->AddInfo(FString::Printf(TEXT("map_open.png: %d footprints, %d icons: player %d, partner %d, objective %d, safehouses %d, "
			"pedestals %d, crime %d, doors %d, markers %d; hovering %d."), Map->GetMapData()->Footprints.Num(), Map->GetIcons().Num(),
			Counts[0], Counts[1], Counts[2], Counts[3], Counts[4], Counts[5], Counts[6], Counts[7], Map->GetHoveredIndex()));
		Test->TestEqual(TEXT("map_open.png: both safehouses"), Counts[static_cast<int32>(EHawkeyeMapIconKind::Safehouse)], 2);
		Test->TestTrue(TEXT("map_open.png: the pedestals"), Counts[static_cast<int32>(EHawkeyeMapIconKind::Challenge)] >= 3);
		Test->TestEqual(TEXT("map_open.png: Kate"), Counts[static_cast<int32>(EHawkeyeMapIconKind::Player)], 1);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.3f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test]()
	{
		HawkeyeShots::Request(Test, ShotPath(TEXT("map_open.png")), true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]()
	{
		if (AHawkeyePlayerController* PC = FindController(FindWorld()))
		{
			PC->SetMapOpen(false);
		}
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
}

/** The map shot on its own. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeScreenshotMap, "Hawkeye.Screenshot.Map",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeScreenshotMap::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Skipped (no RHI): the map screenshot."));
		return true;
	}
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	HawkeyeAddMapShots(this);
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeWaitForShots(this));
	return true;
}

#endif
