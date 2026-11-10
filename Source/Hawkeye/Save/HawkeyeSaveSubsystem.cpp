#include "Save/HawkeyeSaveSubsystem.h"

#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "Boss/BossFightSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Chase/ChaseSubsystem.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Crime/CrimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flashback/FlashbackDefinition.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionSubsystem.h"
#include "Phone/PhoneSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/InventoryComponent.h"
#include "SpudCustomSaveInfo.h"
#include "SpudState.h"
#include "SpudSubsystem.h"
#include "UObject/SoftObjectPath.h"

const TCHAR* UHawkeyeSaveSubsystem::DefaultSlotName = TEXT("HawkeyeCampaign");

namespace HawkeyeSave
{
	static const TCHAR* GlobalName = TEXT("HawkeyeCampaign");
	static const TCHAR* VersionKey = TEXT("Version");
	static const TCHAR* MissionKey = TEXT("Mission");
	static const TCHAR* ReasonKey = TEXT("Reason");
}

UHawkeyeSaveSubsystem* UHawkeyeSaveSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UHawkeyeSaveSubsystem>() : nullptr;
}

UHawkeyeSaveSubsystem::UHawkeyeSaveSubsystem()
{
	// A default subobject, so a subsystem made by a test (never Initialized) has one too.
	Campaign = CreateDefaultSubobject<UHawkeyeCampaignState>(TEXT("HawkeyeCampaign"));
}

void UHawkeyeSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<USpudSubsystem>();
	Super::Initialize(Collection);

	if (USpudSubsystem* Spud = GetSpud())
	{
		Spud->AddPersistentGlobalObjectWithName(Campaign, HawkeyeSave::GlobalName);
		Spud->PostLoadGame.AddDynamic(this, &UHawkeyeSaveSubsystem::HandleSpudPostLoad);
		Spud->PostSaveGame.AddDynamic(this, &UHawkeyeSaveSubsystem::HandleSpudPostSave);
	}
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UHawkeyeSaveSubsystem::HandlePreLoadMap);
}

void UHawkeyeSaveSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	if (USpudSubsystem* Spud = GetSpud())
	{
		Spud->RemovePersistentGlobalObject(Campaign);
		Spud->PostLoadGame.RemoveDynamic(this, &UHawkeyeSaveSubsystem::HandleSpudPostLoad);
		Spud->PostSaveGame.RemoveDynamic(this, &UHawkeyeSaveSubsystem::HandleSpudPostSave);
	}
	Super::Deinitialize();
}

USpudSubsystem* UHawkeyeSaveSubsystem::GetSpud() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<USpudSubsystem>() : nullptr;
}

UWorld* UHawkeyeSaveSubsystem::GetGameWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World && World->IsGameWorld() ? World : nullptr;
}

FString UHawkeyeSaveSubsystem::GetSlotName() const
{
	if (!SlotNameOverride.IsEmpty())
	{
		return SlotNameOverride;
	}
	// An automation pass completes objectives and enters the safehouse, and each of those saves;
	// none of that may land in the player's own campaign.
	return GIsAutomationTesting ? FString::Printf(TEXT("%sAutomation"), DefaultSlotName) : FString(DefaultSlotName);
}

bool UHawkeyeSaveSubsystem::HasSave() const
{
	return IFileManager::Get().FileExists(*USpudSubsystem::GetSaveGameFilePath(GetSlotName()));
}

bool UHawkeyeSaveSubsystem::IsLoading() const
{
	return bLoadInFlight || bRestorePending;
}

// --- Saving -----------------------------------------------------------------------------------------

FString UHawkeyeSaveSubsystem::GetSaveRefusal(UWorld* World) const
{
	const USpudSubsystem* Spud = GetSpud();
	if (!World || !Spud)
	{
		return TEXT("no game world");
	}
	if (!Spud->IsIdle() || IsLoading())
	{
		return TEXT("a save or load is already under way");
	}
	if (SceneReturn.bInScene || SceneReturn.bReturnPending)
	{
		// The slot holds the district; a scene saved over it could never be returned from.
		return TEXT("a playable scene is running");
	}
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
	if (!Missions || !Missions->GetCurrentMission())
	{
		return TEXT("no mission is running");
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	const AHawkeyeCharacter* Player = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	const UHealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
	if (!Player || !Health)
	{
		return TEXT("no player");
	}
	if (!Health->IsAlive() || Player->IsDowned())
	{
		return TEXT("the player is down");
	}
	return FString();
}

void UHawkeyeSaveSubsystem::CaptureCampaign(UWorld* World)
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
	const UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	Campaign->Version = UHawkeyeCampaignState::CurrentVersion;
	Campaign->MissionPath = Missions ? FSoftObjectPath(Missions->GetCurrentMission()).ToString() : FString();
	Campaign->CompletedObjectives = Tracker ? Tracker->GetCompletedObjectiveIds() : TArray<FName>();

	const APlayerController* PC = World->GetFirstPlayerController();
	const AHawkeyeCharacter* Player = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	Campaign->ControlledCharacter = Player ? Player->GetCharacterName().ToString() : FString();

	// The phone's texts still on their delay go into the save with the seconds they have left, and
	// an objective whose autosave this is may not have reached the phone yet.
	if (UPhoneSubsystem* Phone = World->GetSubsystem<UPhoneSubsystem>())
	{
		Phone->CatchUpTriggers();
		Phone->MirrorToCampaign();
	}
	if (const UChallengeSubsystem* Challenges = World->GetSubsystem<UChallengeSubsystem>())
	{
		Challenges->MirrorToCampaign();
	}
	if (const UCrimeSubsystem* Crimes = World->GetSubsystem<UCrimeSubsystem>())
	{
		Crimes->MirrorToCampaign();
	}
	if (const UChaseSubsystem* Chase = World->GetSubsystem<UChaseSubsystem>())
	{
		Chase->MirrorToCampaign();
	}
	if (const UBossFightSubsystem* Boss = World->GetSubsystem<UBossFightSubsystem>())
	{
		Boss->MirrorToCampaign();
	}
}

bool UHawkeyeSaveSubsystem::SaveCampaign(const FString& Reason)
{
	UWorld* World = GetGameWorld();
	const FString Refusal = GetSaveRefusal(World);
	if (!Refusal.IsEmpty())
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: save (%s) refused: %s."), *GetName(), *Reason, *Refusal);
		return false;
	}

	CaptureCampaign(World);
	USpudSubsystem* Spud = GetSpud();
	USpudCustomSaveInfo* Info = Spud->CreateCustomSaveInfo();
	Info->SetInt(HawkeyeSave::VersionKey, Campaign->Version);
	Info->SetString(HawkeyeSave::MissionKey, Campaign->MissionPath);
	Info->SetString(HawkeyeSave::ReasonKey, Reason);

	const UMissionDefinition* Mission = UMissionSubsystem::Get(World)->GetCurrentMission();
	const FText Title = FText::Format(NSLOCTEXT("Hawkeye", "SaveTitle", "{0} ({1})"),
		Mission->MissionName, FText::FromString(Reason));

	// Synchronous and without a thumbnail, so the save is on disk before this returns: an objective
	// autosave must land before OnMissionComplete clears the quiver.
	bLastSaveSucceeded = false;
	Spud->SaveGame(GetSlotName(), Title, /*bTakeScreenshot=*/false, Info);
	if (!bLastSaveSucceeded)
	{
		UE_LOG(LogHawkeye, Error, TEXT("%s: SPUD could not write slot %s (%s)."), *GetName(), *GetSlotName(), *Reason);
		OnCampaignSaved.Broadcast(Reason, false);
		return false;
	}

	++SaveCount;
	AutosaveClock.Reset();
	UE_LOG(LogHawkeye, Log, TEXT("%s: saved the campaign to %s (%s): %s, %d objective(s) done, playing %s."),
		*GetName(), *GetSlotName(), *Reason, *Campaign->MissionPath, Campaign->CompletedObjectives.Num(),
		*Campaign->ControlledCharacter);
	OnCampaignSaved.Broadcast(Reason, true);
	return true;
}

void UHawkeyeSaveSubsystem::HandleSpudPostSave(const FString& SlotName, bool bSuccess)
{
	if (SlotName == GetSlotName())
	{
		bLastSaveSucceeded = bSuccess;
	}
}

void UHawkeyeSaveSubsystem::AdvanceAutosave(float DeltaSeconds, bool bCanSave)
{
	if (Campaign && DeltaSeconds > 0.f)
	{
		Campaign->PlayTimeSeconds += DeltaSeconds;
	}
	if (AutosaveClock.Advance(DeltaSeconds, bCanSave))
	{
		SaveCampaign(TEXT("roaming"));
	}
}

// --- Loading ----------------------------------------------------------------------------------------

EHawkeyeLoadDecision UHawkeyeSaveSubsystem::DecideLoad(bool bSaveExists, int32 SavedVersion, const FString& InMissionPath) const
{
	const EHawkeyeLoadDecision Decision = UHawkeyeCampaignState::EvaluateLoad(bSaveExists, SavedVersion, InMissionPath);
	switch (Decision)
	{
	case EHawkeyeLoadDecision::VersionMismatch:
		UE_LOG(LogHawkeye, Warning, TEXT("%s: slot %s is save version %d, this build reads %d and has no migration; "
			"starting a new game."), *GetName(), *GetSlotName(), SavedVersion, UHawkeyeCampaignState::CurrentVersion);
		break;
	case EHawkeyeLoadDecision::StaleMission:
		UE_LOG(LogHawkeye, Error, TEXT("%s: slot %s is for mission '%s', which no longer exists; starting a new game."),
			*GetName(), *GetSlotName(), *InMissionPath);
		break;
	case EHawkeyeLoadDecision::NoSave:
		UE_LOG(LogHawkeye, Log, TEXT("%s: no save in slot %s; starting a new game."), *GetName(), *GetSlotName());
		break;
	default:
		break;
	}
	return Decision;
}

bool UHawkeyeSaveSubsystem::ReadSaveHeader(int32& OutVersion, FString& OutMissionPath) const
{
	OutVersion = INDEX_NONE;
	OutMissionPath.Reset();
	const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*USpudSubsystem::GetSaveGameFilePath(GetSlotName())));
	if (!Reader)
	{
		return false;
	}
	USpudSaveGameInfo* Info = NewObject<USpudSaveGameInfo>();
	const bool bRead = USpudState::LoadSaveInfoFromArchive(*Reader, *Info);
	Reader->Close();
	if (!bRead || !Info->CustomInfo)
	{
		return false;
	}
	int Version = INDEX_NONE;
	Info->CustomInfo->GetInt(HawkeyeSave::VersionKey, Version);
	Info->CustomInfo->GetString(HawkeyeSave::MissionKey, OutMissionPath);
	OutVersion = Version;
	return true;
}

bool UHawkeyeSaveSubsystem::LoadCampaign()
{
	if (IsLoading())
	{
		return false;
	}

	// The header alone says whether this save can be used; an unreadable one counts as another version.
	int32 SavedVersion = INDEX_NONE;
	FString SavedMission;
	const bool bExists = HasSave();
	if (bExists)
	{
		ReadSaveHeader(SavedVersion, SavedMission);
	}
	if (DecideLoad(bExists, SavedVersion, SavedMission) != EHawkeyeLoadDecision::Load)
	{
		StartNewGame();
		return false;
	}

	USpudSubsystem* Spud = GetSpud();
	if (!Spud || !GetGameWorld() || !Spud->IsIdle())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: cannot load now (no world, or SPUD busy)."), *GetName());
		return false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: loading %s (%s)."), *GetName(), *GetSlotName(), *SavedMission);
	bLoadInFlight = true;
	bRestorePending = true;
	AutosaveClock.Reset();
	Spud->LoadGame(GetSlotName());
	return true;
}

void UHawkeyeSaveSubsystem::StartNewGame()
{
	UWorld* World = GetGameWorld();
	USpudSubsystem* Spud = GetSpud();
	Campaign->ResetCampaign();
	SessionBossHeard.Reset();
	AutosaveClock.Reset();
	bRestorePending = false;
	bLoadInFlight = false;
	bNewGameAutosavePending = true;
	bBootMenuConsumed = true;
	if (!World || !Spud)
	{
		return;
	}
	Spud->NewGame(/*bCheckServerOnly=*/false, /*bAfterLevelLoad=*/true);
	const FString Level = UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString=*/true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: new game; reopening %s."), *GetName(), *Level);
	UGameplayStatics::OpenLevel(World, FName(*Level));
}

bool UHawkeyeSaveSubsystem::ConsumeNewGameAutosave()
{
	const bool bPending = bNewGameAutosavePending;
	bNewGameAutosavePending = false;
	return bPending;
}

void UHawkeyeSaveSubsystem::HandlePreLoadMap(const FString& MapName)
{
	// SPUD keeps level state across every travel unless told otherwise. Only its own loads should
	// restore anything: a restart, a test reopening the map, or a new game must start clean.
	USpudSubsystem* Spud = GetSpud();
	if (Spud && !Spud->IsLoadingGame())
	{
		Spud->NewGame(/*bCheckServerOnly=*/false, /*bAfterLevelLoad=*/true);
	}
}

void UHawkeyeSaveSubsystem::ApplyPendingRestore(UWorld* World)
{
	if (!bRestorePending || !World)
	{
		return;
	}
	bRestorePending = false;

	UMissionSubsystem* Missions = UMissionSubsystem::Get(World);
	UMissionTracker* Tracker = Missions ? Missions->GetTracker() : nullptr;
	if (!Tracker)
	{
		return;
	}
	const FString Running = FSoftObjectPath(Missions->GetCurrentMission()).ToString();
	if (Running != Campaign->MissionPath)
	{
		// A save from another chapter's map: start the saved chapter instead of the map's own.
		UMissionDefinition* Saved = LoadObject<UMissionDefinition>(nullptr, *Campaign->MissionPath);
		if (!Saved || !Missions->StartMission(Saved))
		{
			UE_LOG(LogHawkeye, Error, TEXT("%s: could not start saved mission '%s'."), *GetName(), *Campaign->MissionPath);
			return;
		}
	}
	const int32 Restored = Tracker->RestoreCompletedObjectives(Campaign->CompletedObjectives);
	UE_LOG(LogHawkeye, Log, TEXT("%s: restored %d completed objective(s) of %s."), *GetName(), Restored, *Campaign->MissionPath);
}

void UHawkeyeSaveSubsystem::HandleSpudPostLoad(const FString& SlotName, bool bSuccess)
{
	if (!bLoadInFlight || SlotName != GetSlotName())
	{
		return;
	}
	bLoadInFlight = false;
	UWorld* World = GetGameWorld();

	if (!bSuccess)
	{
		UE_LOG(LogHawkeye, Error, TEXT("%s: SPUD failed to load %s; starting a new game."), *GetName(), *SlotName);
		bRestorePending = false;
		OnCampaignLoaded.Broadcast(false);
		StartNewGame();
		return;
	}

	// Dying inside an interior loads the entry save: the district is back and the interior is over.
	if (SceneReturn.bInScene && SceneReturn.bInterior)
	{
		SceneReturn = FHawkeyeSceneReturn();
	}
	// Dying (or Restart) in a flashback's playable scene loads the district's entry save too: the scene
	// is over as if its mission had completed, or every later save would be refused as "in a scene".
	else if (SceneReturn.bInScene)
	{
		SceneReturn.Leave();
		UE_LOG(LogHawkeye, Log, TEXT("%s: the playable scene ended with a load; back in the district."), *GetName());
	}
	// The map had no game mode to hand the objectives to (a test map, say): apply them here.

	ApplyPendingRestore(World);
	FinishRestoredWorld(World);
	UE_LOG(LogHawkeye, Log, TEXT("%s: loaded %s."), *GetName(), *SlotName);
	OnCampaignLoaded.Broadcast(true);
}

void UHawkeyeSaveSubsystem::FinishRestoredWorld(UWorld* World)
{
	AHawkeyePlayerController* PC = World ? Cast<AHawkeyePlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC)
	{
		return;
	}

	// The game mode spawns Kate for the player; if the save was made playing Clint, swap back.
	const AHawkeyeCharacter* Current = Cast<AHawkeyeCharacter>(PC->GetPawn());
	if (Current && !Campaign->ControlledCharacter.IsEmpty()
		&& Current->GetCharacterName().ToString() != Campaign->ControlledCharacter)
	{
		const bool bOverride = PC->bAllowSwitchingOverride;
		PC->bAllowSwitchingOverride = true;
		PC->SwitchCharacter();
		PC->bAllowSwitchingOverride = bOverride;
	}

	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, FadeInSeconds, FLinearColor::Black, false, false);
	}
	ApplyPendingSceneReturn(World);
}

// --- Playable scenes --------------------------------------------------------------------------------

bool UHawkeyeSaveSubsystem::EnterPlayableScene(UFlashbackDefinition* Flashback)
{
	UWorld* World = GetGameWorld();
	if (!World || !Flashback || !Flashback->HasPlayableScene())
	{
		return false;
	}
	SceneReturn = FHawkeyeSceneReturn();
	SceneReturn.ReturnPointLabel = Flashback->ReturnPointLabel;
	SceneReturn.ReturnMap = World->GetOutermost()->GetName();
	SceneReturn.bSavedOnEntry = SaveCampaign(TEXT("playable scene"));
	if (!SceneReturn.bSavedOnEntry)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: the district could not be saved before %s; the way back opens it fresh."),
			*GetName(), *Flashback->PlayableScene.ToString());
	}
	SceneReturn.bInScene = true;
	UE_LOG(LogHawkeye, Log, TEXT("%s: entering playable scene %s (back to %s at %s)."), *GetName(),
		*Flashback->PlayableScene.ToString(), *SceneReturn.ReturnMap, *SceneReturn.ReturnPointLabel.ToString());
	UGameplayStatics::OpenLevelBySoftObjectPtr(World, Flashback->PlayableScene);
	return true;
}

void UHawkeyeSaveSubsystem::ReturnFromPlayableScene()
{
	if (!SceneReturn.Leave())
	{
		return;
	}
	TravelBackFromScene();
}

bool UHawkeyeSaveSubsystem::EnterInterior(const TSoftObjectPtr<UWorld>& Interior, FName ReturnPointLabel)
{
	UWorld* World = GetGameWorld();
	if (!World || Interior.IsNull())
	{
		return false;
	}
	const FString District = World->GetOutermost()->GetName();
	const bool bSaved = SaveCampaign(TEXT("interior"));
	if (!bSaved)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: the district could not be saved before %s; the way out opens it fresh."),
			*GetName(), *Interior.ToString());
	}
	CarryQuiver(World);
	SceneReturn.BeginInterior(District, ReturnPointLabel, bSaved);
	UE_LOG(LogHawkeye, Log, TEXT("%s: entering interior %s (back to %s at %s)."), *GetName(), *Interior.ToString(),
		*SceneReturn.ReturnMap, *SceneReturn.ReturnPointLabel.ToString());
	UGameplayStatics::OpenLevelBySoftObjectPtr(World, Interior);
	return true;
}

bool UHawkeyeSaveSubsystem::ReturnFromInterior(FName ReturnPointOverride)
{
	if (!IsInInterior() || !SceneReturn.Leave(ReturnPointOverride))
	{
		return false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: leaving the interior for %s."), *GetName(), *SceneReturn.ReturnPointLabel.ToString());
	CarryQuiver(GetGameWorld());
	TravelBackFromScene();
	return true;
}

void UHawkeyeSaveSubsystem::CarryQuiver(UWorld* World)
{
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AHawkeyeCharacter* Player = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	const UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	bCarryingQuiver = Inventory != nullptr;
	if (!Inventory)
	{
		return;
	}
	CarriedBow = Inventory->GetBow();
	CarriedArrows = Inventory->GetArrowSlots();
	CarriedActiveSlot = Inventory->GetActiveArrowSlot();
}

bool UHawkeyeSaveSubsystem::RestoreCarriedQuiver(UWorld* World)
{
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AHawkeyeCharacter* Player = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	UInventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	if (!bCarryingQuiver || !Inventory)
	{
		return false;
	}
	bCarryingQuiver = false;
	Inventory->RestoreQuiver(CarriedBow, CarriedArrows, CarriedActiveSlot);
	UE_LOG(LogHawkeye, Log, TEXT("%s: carried the quiver through the door (%s, %d slot(s))."), *GetName(),
		*GetNameSafe(CarriedBow), CarriedArrows.Num());
	CarriedBow = nullptr;
	CarriedArrows.Reset();
	return true;
}

void UHawkeyeSaveSubsystem::BeginInteriorWithoutEntry(const FString& District, FName ReturnPointLabel)
{
	SceneReturn.BeginInterior(District, ReturnPointLabel, /*bSaved=*/false);
	SceneReturn.bFadeInOnArrival = false;
	UE_LOG(LogHawkeye, Log, TEXT("%s: interior opened directly; its exits lead to %s at %s."), *GetName(), *District,
		*ReturnPointLabel.ToString());
}

bool UHawkeyeSaveSubsystem::ConsumeInteriorArrivalFade()

{
	const bool bFade = SceneReturn.bInScene && SceneReturn.bFadeInOnArrival;
	SceneReturn.bFadeInOnArrival = false;
	return bFade;
}

void UHawkeyeSaveSubsystem::TravelBackFromScene()
{
	int32 SavedVersion = INDEX_NONE;
	FString SavedMission;
	const bool bExists = SceneReturn.bSavedOnEntry && HasSave() && ReadSaveHeader(SavedVersion, SavedMission);
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s over; back to %s."), *GetName(),
		SceneReturn.bInterior ? TEXT("interior") : TEXT("playable scene"), *SceneReturn.ReturnMap);
	if (bExists && DecideLoad(true, SavedVersion, SavedMission) == EHawkeyeLoadDecision::Load && LoadCampaign())
	{
		return;
	}
	if (UWorld* World = GetGameWorld(); World && !SceneReturn.ReturnMap.IsEmpty())
	{
		UGameplayStatics::OpenLevel(World, FName(*SceneReturn.ReturnMap));
	}
}

void UHawkeyeSaveSubsystem::ApplyPendingSceneReturn(UWorld* World)
{
	if (!SceneReturn.bReturnPending || !World || IsLoading())
	{
		return;
	}
	SceneReturn.bReturnPending = false;
	AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(World->GetFirstPlayerController());
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const TArray<FHawkeyeReturnCandidate> Candidates = FHawkeyeSceneReturn::GatherCandidates(World);
	const int32 Index = FHawkeyeSceneReturn::SelectReturnPoint(Candidates, SceneReturn.ReturnPointLabel);
	if (Pawn && Index != INDEX_NONE)
	{
		const FTransform& Point = Candidates[Index].Transform;
		const FRotator Facing(0.f, Point.Rotator().Yaw, 0.f);
		Pawn->TeleportTo(Point.GetLocation(), Facing);
		PC->SetControlRotation(FRotator(-10.f, Facing.Yaw, 0.f));
		UE_LOG(LogHawkeye, Log, TEXT("%s: back from the scene at %s."), *GetName(), *SceneReturn.ReturnPointLabel.ToString());
	}
	else if (!SceneReturn.ReturnPointLabel.IsNone())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no actor labelled or tagged %s to return to; keeping the saved position."),
			*GetName(), *SceneReturn.ReturnPointLabel.ToString());
	}
	// Out of an interior: the quiver as it was inside, not as the entry save had it.
	RestoreCarriedQuiver(World);
	// A flashback's scene hands the chapter's end sequence back to the controller; an interior does not.
	const bool bResume = SceneReturn.ShouldResumeMissionFlow();

	SceneReturn.bInterior = false;
	if (PC && bResume)
	{
		PC->HandleReturnedFromScene();
	}
}

// --- Safehouses -------------------------------------------------------------------------------------

bool UHawkeyeSaveSubsystem::DiscoverSafehouse(FName SafehouseId)
{
	if (SafehouseId.IsNone() || Campaign->DiscoveredSafehouses.Contains(SafehouseId))
	{
		return false;
	}
	Campaign->DiscoveredSafehouses.Add(SafehouseId);
	UE_LOG(LogHawkeye, Log, TEXT("%s: safehouse %s discovered."), *GetName(), *SafehouseId.ToString());
	return true;
}

bool UHawkeyeSaveSubsystem::IsSafehouseDiscovered(FName SafehouseId) const
{
	return Campaign && Campaign->DiscoveredSafehouses.Contains(SafehouseId);
}

TArray<FName> UHawkeyeSaveSubsystem::GetDiscoveredSafehouses() const
{
	return Campaign ? Campaign->DiscoveredSafehouses : TArray<FName>();
}

void UHawkeyeSaveSubsystem::SetLastSafehouse(FName SafehouseId)
{
	if (Campaign && !SafehouseId.IsNone())
	{
		Campaign->LastSafehouse = SafehouseId;
	}
}

FName UHawkeyeSaveSubsystem::GetLastSafehouse() const
{
	return Campaign ? Campaign->LastSafehouse : NAME_None;
}

bool UHawkeyeSaveSubsystem::NotePlace(FName PlaceId)
{
	if (!Campaign || PlaceId.IsNone() || Campaign->NoticedPlaces.Contains(PlaceId))
	{
		return false;
	}
	Campaign->NoticedPlaces.Add(PlaceId);
	return true;
}

TArray<FName> UHawkeyeSaveSubsystem::GetNoticedPlaces() const
{
	return Campaign ? Campaign->NoticedPlaces : TArray<FName>();
}

int32 UHawkeyeSaveSubsystem::GetHintShowCount(FName HintId) const
{
	const int32 Index = Campaign ? Campaign->HintIds.IndexOfByKey(HintId) : INDEX_NONE;
	return Index != INDEX_NONE && Campaign->HintShowCounts.IsValidIndex(Index) ? Campaign->HintShowCounts[Index] : 0;
}

void UHawkeyeSaveSubsystem::SetHintShowCount(FName HintId, int32 Count)
{
	if (!Campaign || HintId.IsNone())
	{
		return;
	}
	// The two arrays stay the same length: a save with a short count array (hand-edited, an old build) pads with 0.
	Campaign->HintShowCounts.SetNumZeroed(Campaign->HintIds.Num());
	int32 Index = Campaign->HintIds.IndexOfByKey(HintId);
	if (Index == INDEX_NONE)
	{
		Index = Campaign->HintIds.Add(HintId);
		Campaign->HintShowCounts.Add(0);
	}
	Campaign->HintShowCounts[Index] = FMath::Max(Count, 0);
}

// --- Main menu --------------------------------------------------------------------------------------

bool UHawkeyeSaveSubsystem::ConsumeBootMenu()
{
	if (bBootMenuConsumed)
	{
		return false;
	}
	bBootMenuConsumed = true;

	// Automation drives the game itself; a menu pausing the first world would only get in its way.
	const TCHAR* CommandLine = FCommandLine::Get();
	const bool bAutomation = (FCString::Stristr(CommandLine, TEXT("RunTests")) != nullptr || GIsAutomationTesting)
		&& !AHawkeyePlayerController::bAutomationPlaysAsPlayer;
	if (FParse::Param(CommandLine, TEXT("NoMainMenu")) || bAutomation)
	{
		return false;
	}
	return !IsLoading();
}
