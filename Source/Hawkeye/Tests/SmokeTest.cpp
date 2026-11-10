#include "Components/LightComponent.h"
#include "Crime/Civilian.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeSpot.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "World/CityLedgeSpawner.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Boots /Game/Maps/L_District_EastVillage for real and asserts the level is playable: the
 * Rooftops chapter is running with its four objectives, the four Tracksuit thugs are placed, the
 * ledge spawner has put out ledges and grapple anchors, there is a PlayerStart and the pawn is an
 * AHawkeyeCharacter, and navigation data exists.
 *
 * Deliberately shallow - it answers "does the game boot", not "is the game correct" - but it is
 * the one thing the unit tests cannot cover. Hawkeye.Lap.EastVillage plays the level through.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSmokeLoadEastVillage, "Hawkeye.Smoke.LoadEastVillage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeSmoke
{
	static const TCHAR* MapPath = TEXT("/Game/Maps/L_District_EastVillage");

	/** DA_CH01_NightShift (Act 1's opening chase): chase_thief, cross_gap, catch_thief. */
	static const TCHAR* ExpectedObjectives[] = { TEXT("chase_thief"), TEXT("cross_gap"), TEXT("catch_thief") };

	/** Two patrolling the street, the heavy on his own patrol, the RoofPair on the cross_block roof. */
	static constexpr int32 ExpectedThugs = 5;
	static constexpr int32 ExpectedArchers = 2;

	/** The game world the map was opened into, or null. */
	static UWorld* FindGameWorld()
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
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeAssertEastVillagePlayable, FHawkeyeSmokeLoadEastVillage*, Test);

bool FHawkeyeAssertEastVillagePlayable::Update()
{
	UWorld* World = HawkeyeSmoke::FindGameWorld();
	if (!World)
	{
		Test->AddError(TEXT("No game world after opening L_District_EastVillage."));
		return true;
	}

	// --- mission ------------------------------------------------------------------------------
	const UMissionSubsystem* Missions = World->GetSubsystem<UMissionSubsystem>();
	if (!Missions)
	{
		Test->AddError(TEXT("The level has no UMissionSubsystem."));
	}
	else
	{
		const UMissionDefinition* Mission = Missions->GetCurrentMission();
		Test->TestNotNull(TEXT("The game mode started a mission"), Mission);
		if (Mission)
		{
			Test->TestEqual(TEXT("The mission is Night Shift"), Mission->MissionName.ToString(), FString(TEXT("Night Shift")));
			Test->TestFalse(TEXT("Night Shift names its chase"), Mission->Chase.IsNull());
			Test->TestFalse(TEXT("Act 1 has no partner on screen"), Mission->bPartnerPresent);
			Test->TestFalse(TEXT("...and no switching"), Mission->bAllowSwitching);

			const TArray<UMissionObjective*> Objectives = Missions->GetActiveObjectives();
			TArray<FString> Ids;
			for (const UMissionObjective* Objective : Objectives)
			{
				Ids.Add(Objective ? Objective->ObjectiveId.ToString() : FString(TEXT("<null>")));
			}
			TArray<FString> Expected;
			for (const TCHAR* Id : HawkeyeSmoke::ExpectedObjectives)
			{
				Expected.Add(Id);
			}
			Test->TestEqual(TEXT("Night Shift has its three objectives, in order"),
				FString::Join(Ids, TEXT(",")), FString::Join(Expected, TEXT(",")));
			Test->TestNotNull(TEXT("And a current objective to show on the HUD"), Missions->GetCurrentObjective());
		}
	}

	// --- world actors -------------------------------------------------------------------------
	int32 ThugCount = 0;
	int32 ArcherCount = 0;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		// Barney's archers (BP_Archer, tag ArcherPair) are placed beside chapter 1's five (the heavy is the fifth).
		(It->ActorHasTag(FName(TEXT("ArcherPair"))) ? ArcherCount : ThugCount)++;
	}
	Test->TestEqual(TEXT("Five thugs are placed"), ThugCount, HawkeyeSmoke::ExpectedThugs);
	Test->TestEqual(TEXT("And the two archers facing the find_arrow roof"), ArcherCount, HawkeyeSmoke::ExpectedArchers);

	// The street crimes' fourteen to sixteen spots, each holding its crimes, and the classes they spawn (cooked).
	int32 SpotCount = 0;
	int32 SpotsWithoutCrimes = 0;
	int32 MissingClasses = 0;
	int32 AlleySpots = 0;
	for (TActorIterator<ACrimeSpot> It(World); It; ++It)
	{
		++SpotCount;
		AlleySpots += It->bAlley ? 1 : 0;
		SpotsWithoutCrimes += It->Crimes.ContainsByPredicate([](const UCrimeDefinition* Crime) { return Crime != nullptr; }) ? 0 : 1;
		for (const UCrimeDefinition* Crime : It->Crimes)
		{
			for (const FCrimeRosterEntry& Entry : Crime ? Crime->Roster : TArray<FCrimeRosterEntry>())
			{
				MissingClasses += Entry.ThugClass.LoadSynchronous() ? 0 : 1;
			}
			MissingClasses += Crime && Crime->Type == ECrimeType::Mugging && !Crime->VictimClass.LoadSynchronous() ? 1 : 0;
		}
	}
	// Eight corners, four roofs, and two to four alleys (or, on a block without them, two park-path spots).
	Test->TestTrue(FString::Printf(TEXT("Fourteen to sixteen crime spots are placed (%d, %d in alleys)"), SpotCount, AlleySpots),
		SpotCount >= 14 && SpotCount <= 16);
	Test->TestEqual(TEXT("Each holds its crimes"), SpotsWithoutCrimes, 0);
	Test->TestEqual(TEXT("And every thug and victim class they spawn loads"), MissingClasses, 0);

	// Both safehouses, registered for fast travel under their own ids and placeholder names.
	const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(World);
	const ASafehouse* First = Safehouses ? Safehouses->FindSafehouse(TEXT("ch01_east_7th")) : nullptr;
	const ASafehouse* Second = Safehouses ? Safehouses->FindSafehouse(TEXT("avenue_b")) : nullptr;
	Test->TestEqual(TEXT("Two safehouses are registered"), Safehouses ? Safehouses->GetSafehouses().Num() : 0, 2);
	Test->TestTrue(TEXT("The first is [Safehouse 1]"), First && First->GetDisplayName().ToString() == TEXT("[Safehouse 1]"));
	Test->TestTrue(TEXT("The second is [Safehouse 2]"), Second && Second->GetDisplayName().ToString() == TEXT("[Safehouse 2]"));
	Test->TestTrue(TEXT("At least 250 m apart"), First && Second
		&& FVector::Dist2D(First->GetActorLocation(), Second->GetActorLocation()) >= 25000.f);

	int32 PlayerStartCount = 0;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		++PlayerStartCount;
	}
	Test->TestTrue(TEXT("There is a PlayerStart"), PlayerStartCount >= 1);

	// --- lighting -----------------------------------------------------------------------------
	// Nothing in the generated district is built lighting, so a Static light renders as a black
	// surface until someone builds it. Every light has to be Movable.
	int32 StaticLightCount = 0;
	for (TObjectIterator<ULightComponent> It; It; ++It)
	{
		const ULightComponent* Light = *It;
		if (Light && Light->GetWorld() == World && Light->Mobility == EComponentMobility::Static)
		{
			++StaticLightCount;
			Test->AddError(FString::Printf(TEXT("Static light component %s in the district."), *Light->GetPathName()));
		}
	}
	Test->TestEqual(TEXT("No light in the district has Static mobility"), StaticLightCount, 0);

	// --- player -------------------------------------------------------------------------------
	const APlayerController* PC = World->GetFirstPlayerController();
	Test->TestNotNull(TEXT("There is a player controller"), PC);
	if (PC)
	{
		const AHawkeyeCharacter* Player = Cast<AHawkeyeCharacter>(PC->GetPawn());
		Test->TestNotNull(TEXT("The pawn is an AHawkeyeCharacter"), Player);
		if (Player)
		{
			Test->TestNotNull(TEXT("The camera sits on a spring arm"), Player->GetCameraBoom());
		}
	}

	return true;
}

/**
 * The ledge spawner puts every anchor and the ledges near the player out at BeginPlay and the
 * rest a few milliseconds a frame, so this polls until both counts are above zero.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FHawkeyeAssertLedgesSpawned, FHawkeyeSmokeLoadEastVillage*, Test, float, SecondsLeft);

bool FHawkeyeAssertLedgesSpawned::Update()
{
	UWorld* World = HawkeyeSmoke::FindGameWorld();
	if (!World)
	{
		Test->AddError(TEXT("No game world while waiting for the ledge spawner."));
		return true;
	}

	const ACityLedgeSpawner* Spawner = nullptr;
	int32 SpawnerCount = 0;
	for (TActorIterator<ACityLedgeSpawner> It(World); It; ++It)
	{
		Spawner = Spawner ? Spawner : *It;
		++SpawnerCount;
	}
	if (!Spawner)
	{
		Test->AddError(TEXT("L_District_EastVillage has no ACityLedgeSpawner."));
		return true;
	}

	const int32 Ledges = Spawner->GetSpawnedLedgeCount();
	const int32 Anchors = Spawner->GetSpawnedAnchorCount();
	if (Ledges > 0 && Anchors > 0)
	{
		Test->AddInfo(FString::Printf(TEXT("Ledge spawner: %d ledge(s), %d anchor(s) out, complete=%d."),
			Ledges, Anchors, Spawner->IsSpawnComplete() ? 1 : 0));
		Test->TestEqual(TEXT("Exactly one ledge spawner"), SpawnerCount, 1);
		Test->TestTrue(TEXT("The ledge spawner spawned ledges"), true);
		Test->TestTrue(TEXT("The ledge spawner spawned grapple anchors"), true);
		return true;
	}

	SecondsLeft -= FApp::GetDeltaTime();
	if (SecondsLeft > 0.f)
	{
		return false;
	}

	Test->TestTrue(TEXT("The ledge spawner spawned ledges"), Ledges > 0);
	Test->TestTrue(TEXT("The ledge spawner spawned grapple anchors"), Anchors > 0);
	return true;
}

/**
 * Navigation. Runtime generation is Dynamic (Config/DefaultEngine.ini) and asynchronous, so this
 * polls rather than asserting on one frame. Without it every thug MoveTo fails.
 */
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
	FHawkeyeAssertEastVillageNavigation, FHawkeyeSmokeLoadEastVillage*, Test, float, SecondsLeft);

bool FHawkeyeAssertEastVillageNavigation::Update()
{
	UWorld* World = HawkeyeSmoke::FindGameWorld();
	if (!World)
	{
		Test->AddError(TEXT("No game world while waiting for navigation data."));
		return true;
	}

	const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
	const bool bHasNavData = NavSystem && NavSystem->GetDefaultNavDataInstance() != nullptr;

	bool bHasRecastNavMesh = false;
	for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
	{
		bHasRecastNavMesh = true;
		break;
	}

	if (bHasNavData && bHasRecastNavMesh)
	{
		Test->TestTrue(TEXT("The level has navigation data"), true);
		return true;
	}

	SecondsLeft -= FApp::GetDeltaTime();
	if (SecondsLeft > 0.f)
	{
		return false;
	}

	Test->TestNotNull(TEXT("A navigation system exists"), NavSystem);
	Test->TestTrue(TEXT("GetDefaultNavDataInstance() is not null"), bHasNavData);
	Test->TestTrue(TEXT("An ARecastNavMesh was generated for the district"), bHasRecastNavMesh);
	return true;
}

bool FHawkeyeSmokeLoadEastVillage::RunTest(const FString& Parameters)
{
	AutomationOpenMap(HawkeyeSmoke::MapPath);

	// Long enough for BeginPlay, the game mode's StartMission and the spawner's first pass.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeAssertEastVillagePlayable(this));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeAssertLedgesSpawned(this, 15.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeAssertEastVillageNavigation(this, 15.f));

	return true;
}

#endif
