#include "Combat/ArrowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HawkeyeGameMode.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionTracker.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeAutosaveClock.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "SpudCustomSaveInfo.h"
#include "SpudState.h"
#include "SpudSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "UI/HawkeyeMainMenuWidget.h"
#include "World/Safehouse.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The save rules from claude-docs/gameplay-semantics.md, "Save data": what Continue does with a
 * save it cannot use, the roaming autosave clock, the safehouse restock, the menu's Continue, and
 * the pieces a load restores (objectives, a character's health, a dead thug). The full SPUD round
 * trip through a real map is Hawkeye.Save.RoundTrip (SaveRoundTripTest.cpp), run in -game.
 */
namespace HawkeyeSaveTest
{
	/** Never the player's slot. */
	static const TCHAR* TestSlot = TEXT("HawkeyeCampaignAutomationTest");

	/** Exists in every build without touching project content. */
	static const TCHAR* ExistingPackagePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	static UHawkeyeSaveSubsystem* MakeSubsystem()
	{
		// UGameInstanceSubsystem declares ClassWithin = UGameInstance, so a package outer ensures.
		UGameInstance* Outer = NewObject<UGameInstance>(GEngine);
		UHawkeyeSaveSubsystem* Save = NewObject<UHawkeyeSaveSubsystem>(Outer);
		Save->SlotNameOverride = TestSlot;
		return Save;
	}

	static FString TestSlotFile()
	{
		return USpudSubsystem::GetSaveGameFilePath(TestSlot);
	}

	static void DeleteTestSlot()
	{
		IFileManager::Get().Delete(*TestSlotFile(), false, true, true);
	}

	/** A SPUD save file holding only its header: the version and mission Continue reads. */
	static bool WriteHeaderOnlySave(int32 Version, const FString& MissionPath)
	{
		USpudState* State = NewObject<USpudState>();
		USpudCustomSaveInfo* Info = NewObject<USpudCustomSaveInfo>();
		Info->SetInt(TEXT("Version"), Version);
		Info->SetString(TEXT("Mission"), MissionPath);
		State->SetTitle(FText::FromString(TEXT("automation")));
		State->SetTimestamp(FDateTime::Now());
		State->SetCustomSaveInfo(Info);
		const TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*TestSlotFile()));
		if (!Writer)
		{
			return false;
		}
		State->SaveToArchive(*Writer);
		return Writer->Close();
	}

	static UArrowDefinition* MakeArrow(UObject* Outer, int32 Slot, int32 Cap)
	{
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Outer);
		Arrow->Slot = Slot;
		Arrow->Cap = Cap;
		return Arrow;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSaveVersionMismatchStartsNewGame, "Hawkeye.Save.VersionMismatchStartsNewGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSaveVersionMismatchStartsNewGame::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSaveTest;
	const int32 Current = UHawkeyeCampaignState::CurrentVersion;
	TestEqual(TEXT("An older version with no migration is a mismatch"),
		UHawkeyeCampaignState::EvaluateLoad(true, Current - 1, ExistingPackagePath), EHawkeyeLoadDecision::VersionMismatch);
	TestEqual(TEXT("A newer version is a mismatch"),
		UHawkeyeCampaignState::EvaluateLoad(true, Current + 1, ExistingPackagePath), EHawkeyeLoadDecision::VersionMismatch);
	TestEqual(TEXT("A current save of an existing mission loads"),
		UHawkeyeCampaignState::EvaluateLoad(true, Current, ExistingPackagePath), EHawkeyeLoadDecision::Load);
	TestEqual(TEXT("No file is no save"), UHawkeyeCampaignState::EvaluateLoad(false, Current, ExistingPackagePath),
		EHawkeyeLoadDecision::NoSave);

	DeleteTestSlot();
	if (!TestTrue(TEXT("Wrote a save from another version"), WriteHeaderOnlySave(Current + 1, ExistingPackagePath)))
	{
		return false;
	}
	UHawkeyeSaveSubsystem* Save = MakeSubsystem();
	Save->GetCampaignState()->CompletedObjectives = { TEXT("reach_roof") };
	TestTrue(TEXT("The slot has a save"), Save->HasSave());

	AddExpectedMessage(TEXT("has no migration"), EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("Continue does not load it"), Save->LoadCampaign());
	TestTrue(TEXT("A new game is under way instead"), Save->IsTravelPending());
	TestEqual(TEXT("The campaign state is fresh"), Save->GetCampaignState()->CompletedObjectives.Num(), 0);
	TestTrue(TEXT("The new game gets its first autosave"), Save->ConsumeNewGameAutosave());
	DeleteTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSaveStaleMissionStartsNewGame, "Hawkeye.Save.StaleMissionStartsNewGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSaveStaleMissionStartsNewGame::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSaveTest;
	const FString Gone = TEXT("/Game/Missions/DA_NoSuchChapter.DA_NoSuchChapter");
	TestFalse(TEXT("The mission asset does not exist"), UHawkeyeCampaignState::DoesMissionExist(Gone));
	TestFalse(TEXT("An empty mission path does not exist"), UHawkeyeCampaignState::DoesMissionExist(FString()));
	TestEqual(TEXT("A current save of a missing mission is stale"),
		UHawkeyeCampaignState::EvaluateLoad(true, UHawkeyeCampaignState::CurrentVersion, Gone), EHawkeyeLoadDecision::StaleMission);

	DeleteTestSlot();
	if (!TestTrue(TEXT("Wrote a save of a missing mission"), WriteHeaderOnlySave(UHawkeyeCampaignState::CurrentVersion, Gone)))
	{
		return false;
	}
	UHawkeyeSaveSubsystem* Save = MakeSubsystem();
	Save->GetCampaignState()->DiscoveredSafehouses = { TEXT("ch01_east_7th") };

	AddExpectedError(TEXT("which no longer exists"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Continue does not load it"), Save->LoadCampaign());
	TestTrue(TEXT("A new game is under way instead"), Save->IsTravelPending());
	TestEqual(TEXT("The campaign state is fresh"), Save->GetCampaignState()->DiscoveredSafehouses.Num(), 0);
	DeleteTestSlot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSaveAutosaveRespectsCombat, "Hawkeye.Save.AutosaveRespectsCombat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSaveAutosaveRespectsCombat::RunTest(const FString& Parameters)
{
	FHawkeyeAutosaveClock Clock;
	TestEqual(TEXT("Every 60 s by default"), Clock.IntervalSeconds, 60.f);
	TestFalse(TEXT("59 s of roaming is not yet due"), Clock.Advance(59.f, true));
	TestFalse(TEXT("A fight never saves"), Clock.Advance(30.f, false));
	TestEqual(TEXT("The fight paused the clock rather than resetting it"), Clock.GetRoamingSeconds(), 59.f);
	TestTrue(TEXT("The first calm second after it saves"), Clock.Advance(1.f, true));
	TestEqual(TEXT("And starts the next interval"), Clock.GetRoamingSeconds(), 0.f);
	TestFalse(TEXT("A zero step does nothing"), Clock.Advance(0.f, true));
	Clock.Advance(40.f, true);
	Clock.Reset();
	TestFalse(TEXT("Another save (objective, safehouse) restarts the interval"), Clock.Advance(59.f, true));

	// The game mode's combat gate: an alerted living thug is a fight, a calm or dead one is not.
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Thug spawned"), Thug))
	{
		return false;
	}
	TestFalse(TEXT("A calm thug is no fight"), AHawkeyeGameMode::IsWorldInCombat(TestWorld.Get()));
	Thug->SetAlertState(EThugAlertState::Alerted);
	TestTrue(TEXT("An alerted thug is a fight"), AHawkeyeGameMode::IsWorldInCombat(TestWorld.Get()));
	Thug->GetHealthComponent()->ApplyDamage(10000.f, nullptr);
	TestFalse(TEXT("A dead one is not"), AHawkeyeGameMode::IsWorldInCombat(TestWorld.Get()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSaveSafehouseRefillCaps, "Hawkeye.Save.SafehouseRefillCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSaveSafehouseRefillCaps::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSaveTest;
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>();
	UArrowDefinition* Standard = MakeArrow(Inventory, 1, 30);
	UArrowDefinition* Grapple = MakeArrow(Inventory, 2, 6);
	UArrowDefinition* Putty = MakeArrow(Inventory, 4, 3);
	Inventory->StandardArrowDefinition = Standard;
	Inventory->Clear();
	Inventory->AddArrows(Standard, 5);
	Inventory->AddArrows(Grapple, 6);
	Inventory->AddArrows(Putty, 0);

	TestEqual(TEXT("Refill adds what each slot is short of its cap"), ASafehouse::RefillArrows(Inventory), 25 + 0 + 3);
	TestEqual(TEXT("Standard arrows to 30"), Inventory->GetArrowCount(1), 30);
	TestEqual(TEXT("Grapple arrows stay at 6"), Inventory->GetArrowCount(2), 6);
	TestEqual(TEXT("Putty arrows to 3, from empty"), Inventory->GetArrowCount(4), 3);
	TestTrue(TEXT("A slot never filled stays empty"), Inventory->IsArrowSlotEmpty(5));
	TestEqual(TEXT("A second refill adds nothing"), ASafehouse::RefillArrows(Inventory), 0);
	TestEqual(TEXT("No inventory, nothing to refill"), ASafehouse::RefillArrows(nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSaveMenuContinueEnabled, "Hawkeye.Save.MenuContinueEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSaveMenuContinueEnabled::RunTest(const FString& Parameters)
{
	using namespace HawkeyeSaveTest;
	DeleteTestSlot();
	UHawkeyeSaveSubsystem* Save = MakeSubsystem();
	UHawkeyeMainMenuWidget* Menu = NewObject<UHawkeyeMainMenuWidget>(Save->GetGameInstance());

	Menu->RefreshFromSave(Save);
	TestFalse(TEXT("No save: Continue is greyed out"), Menu->IsContinueEnabled());

	if (!TestTrue(TEXT("Wrote a save"), WriteHeaderOnlySave(UHawkeyeCampaignState::CurrentVersion, ExistingPackagePath)))
	{
		return false;
	}
	Menu->RefreshFromSave(Save);
	TestTrue(TEXT("A save: Continue is enabled"), Menu->IsContinueEnabled());

	DeleteTestSlot();
	Menu->RefreshFromSave(Save);
	TestFalse(TEXT("The save gone: greyed out again"), Menu->IsContinueEnabled());
	Menu->RefreshFromSave(nullptr);
	TestFalse(TEXT("No save subsystem: greyed out"), Menu->IsContinueEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSaveRestoreObjectivesSilently, "Hawkeye.Save.RestoreObjectivesSilently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSaveRestoreObjectivesSilently::RunTest(const FString& Parameters)
{
	UMissionDefinition* Mission = NewObject<UMissionDefinition>();
	for (const TCHAR* Id : { TEXT("reach_roof"), TEXT("cross_block"), TEXT("find_arrow") })
	{
		UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
		Objective->ObjectiveId = FName(Id);
		Mission->Objectives.Add(Objective);
	}
	Mission->bEnforceOrder = true;

	UMissionTracker* Tracker = NewObject<UMissionTracker>();
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Tracker->OnObjectiveUpdated.AddDynamic(Listener, &UHawkeyeTestListener::HandleObjectiveUpdated);
	Tracker->OnMissionComplete.AddDynamic(Listener, &UHawkeyeTestListener::HandleMissionComplete);
	Tracker->StartMission(Mission);

	AddExpectedMessage(TEXT("is not in mission"), EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("Two saved objectives restored, the unknown one skipped"),
		Tracker->RestoreCompletedObjectives({ TEXT("reach_roof"), TEXT("cross_block"), TEXT("no_such_objective") }), 2);
	TestEqual(TEXT("No OnObjectiveUpdated for old news"), Listener->ObjectiveUpdatedCount, 0);
	TestEqual(TEXT("The save reads them back in order"), Tracker->GetCompletedObjectiveIds(),
		TArray<FName>({ TEXT("reach_roof"), TEXT("cross_block") }));
	TestEqual(TEXT("The HUD's current objective moves on"), Tracker->GetCurrentObjective()->ObjectiveId, FName(TEXT("find_arrow")));

	TestEqual(TEXT("Restoring the last one completes the mission"), Tracker->RestoreCompletedObjectives({ TEXT("find_arrow") }), 1);
	TestTrue(TEXT("The mission is complete"), Tracker->IsMissionComplete());
	TestEqual(TEXT("Without an end card"), Listener->MissionCompleteCount, 0);
	TestEqual(TEXT("A repeat restore changes nothing"), Tracker->RestoreCompletedObjectives({ TEXT("find_arrow") }), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSaveCharacterAndThugRestore, "Hawkeye.Save.CharacterAndThugRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSaveCharacterAndThugRestore::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
	AThugCharacter* Thug = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(600.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Kate spawned"), Kate) || !TestNotNull(TEXT("Thug spawned"), Thug))
	{
		return false;
	}

	Kate->CharacterName = FText::FromString(TEXT("Kate"));
	TestEqual(TEXT("SPUD knows her by name, not by her spawned FName"), Kate->OverrideName_Implementation(), FString(TEXT("Kate")));
	UHealthComponent* Health = Kate->GetHealthComponent();
	Health->ApplyDamage(20.f, nullptr);
	const float Saved = Health->GetCurrentHealth();
	Kate->SpudPreStore_Implementation(nullptr);
	Health->ApplyDamage(50.f, nullptr);
	Kate->SpudPostRestore_Implementation(nullptr);
	TestEqual(TEXT("Health comes back to what was saved"), Health->GetCurrentHealth(), Saved);

	// A thug dead at the save goes down again on load, as a death the group objective can count.
	Thug->GetHealthComponent()->ApplyDamage(10000.f, nullptr);
	Thug->SpudPreStore_Implementation(nullptr);
	AThugCharacter* Reloaded = Cast<AThugCharacter>(
		TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(900.f, 0.f, 100.f), FRotator::ZeroRotator));
	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Reloaded->GetHealthComponent()->OnDeath.AddDynamic(Listener, &UHawkeyeTestListener::HandleDeath);
	Reloaded->RestoreAsDead();
	TestFalse(TEXT("The restored thug is dead"), Reloaded->GetHealthComponent()->IsAlive());
	TestTrue(TEXT("And down"), Reloaded->IsLimp());
	TestEqual(TEXT("His death is broadcast once"), Listener->DeathCount, 1);
	Reloaded->RestoreAsDead();
	TestEqual(TEXT("A second restore does nothing"), Listener->DeathCount, 1);
	return true;
}

#endif
