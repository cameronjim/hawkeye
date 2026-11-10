#include "Mission/ChapterEventSubsystem.h"

#include "Hawkeye.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Player/InventoryComponent.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"

// --- Rules ---------------------------------------------------------------------------------------------------

const FName UHawkeyeChapterEventRules::Parry(TEXT("parry"));

TArray<int32> UHawkeyeChapterEventRules::Match(const TArray<FHawkeyeChapterEvent>& Events, FName Event, const TArray<FName>& Fired)
{
	TArray<int32> Out;
	if (Event.IsNone() || Fired.Contains(Event))
	{
		return Out;
	}
	for (int32 Index = 0; Index < Events.Num(); ++Index)
	{
		if (Events[Index].Event == Event)
		{
			Out.Add(Index);
		}
	}
	return Out;
}

FName UHawkeyeChapterEventRules::ObjectiveCompleted(FName ObjectiveId)
{
	return FName(*FString::Printf(TEXT("objective_completed:%s"), *ObjectiveId.ToString()));
}

FName UHawkeyeChapterEventRules::ObjectiveStarted(FName ObjectiveId)
{
	return FName(*FString::Printf(TEXT("objective_started:%s"), *ObjectiveId.ToString()));
}

FName UHawkeyeChapterEventRules::ArrowLoosed(const UObject* Arrow)
{
	return Arrow ? FName(*FString::Printf(TEXT("arrow_loosed:%s"), *Arrow->GetName())) : NAME_None;
}

// --- Subsystem -----------------------------------------------------------------------------------------------

UChapterEventSubsystem* UChapterEventSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UChapterEventSubsystem>() : nullptr;
}

void UChapterEventSubsystem::NotifyEvent(const UObject* WorldContextObject, FName Event)
{
	if (UChapterEventSubsystem* Events = Get(WorldContextObject))
	{
		Events->Notify(Event);
	}
}

bool UChapterEventSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UChapterEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UMissionSubsystem>();
	Super::Initialize(Collection);
}

void UChapterEventSubsystem::Deinitialize()
{
	OnEventFired.Clear();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UChapterEventSubsystem::HandleCampaignLoaded);
	}
	Super::Deinitialize();
}

void UChapterEventSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	BindMissions();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UChapterEventSubsystem::HandleCampaignLoaded);
	}
}

void UChapterEventSubsystem::BindMissions()
{
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->OnMissionStarted.AddUniqueDynamic(this, &UChapterEventSubsystem::HandleMissionStarted);
		Missions->OnObjectiveUpdated.AddUniqueDynamic(this, &UChapterEventSubsystem::HandleObjectiveUpdated);
	}
}

void UChapterEventSubsystem::HandleMissionStarted(UMissionDefinition* Mission)
{
	// A fresh run of the chapter: nothing has fired. A load puts back what had (HandleCampaignLoaded).
	Fired.Reset();
	SpokenLines.Reset();
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
	LastCurrentObjective = Current ? Current->ObjectiveId : NAME_None;
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (Current && !(Save && Save->IsLoading()))
	{
		Notify(UHawkeyeChapterEventRules::ObjectiveStarted(Current->ObjectiveId));
	}
}

void UChapterEventSubsystem::HandleObjectiveUpdated(UMissionObjective* Objective, int32 /*ObjectiveIndex*/)
{
	if (!Objective || !Objective->IsCompleted())
	{
		return;
	}
	Notify(UHawkeyeChapterEventRules::ObjectiveCompleted(Objective->ObjectiveId));
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
	const FName CurrentId = Current ? Current->ObjectiveId : NAME_None;
	if (CurrentId != LastCurrentObjective)
	{
		LastCurrentObjective = CurrentId;
		if (Current)
		{
			Notify(UHawkeyeChapterEventRules::ObjectiveStarted(CurrentId));
		}
	}
}

int32 UChapterEventSubsystem::Notify(FName Event)
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionDefinition* Mission = Missions ? Missions->GetCurrentMission() : nullptr;
	if (!Mission || Mission->Events.Num() == 0)
	{
		return 0;
	}
	const TArray<int32> Matches = UHawkeyeChapterEventRules::Match(Mission->Events, Event, Fired);
	if (Matches.Num() == 0)
	{
		return 0;
	}
	Fired.Add(Event);
	UInventoryComponent* Inventory = FindInventory();
	UDialogueSubsystem* Dialogue = bSkipLines ? nullptr : UDialogueSubsystem::Get(this);
	int32 Lines = 0;
	int32 Arrows = 0;
	for (const int32 Index : Matches)
	{
		const FHawkeyeChapterEvent& Entry = Mission->Events[Index];
		for (const FName Line : Entry.Lines)
		{
			SpokenLines.Add(Line);
			++Lines;
			if (Dialogue)
			{
				Dialogue->PlayLine(Line);
			}
		}
		for (const FHawkeyeArrowGrant& Grant : Entry.Arrows)
		{
			UArrowDefinition* Arrow = Grant.Arrow.LoadSynchronous();
			if (Inventory && Arrow && Grant.Count > 0)
			{
				Arrows += Inventory->AddArrows(Arrow, Grant.Count);
			}
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: chapter event %s in %s: %d line(s), %d arrow(s) granted."), *GetName(), *Event.ToString(),
		*Mission->GetName(), Lines, Arrows);
	OnEventFired.Broadcast(Event);
	return Matches.Num();
}

UInventoryComponent* UChapterEventSubsystem::FindInventory() const
{
	if (InventoryOverride.IsValid())
	{
		return InventoryOverride.Get();
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	return Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr;
}

void UChapterEventSubsystem::MirrorToCampaign() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr)
	{
		Campaign->FiredChapterEvents = Fired;
	}
}

void UChapterEventSubsystem::RestoreFromCampaign()
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr)
	{
		Fired = Campaign->FiredChapterEvents;
	}
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
	LastCurrentObjective = Current ? Current->ObjectiveId : NAME_None;
}

void UChapterEventSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		RestoreFromCampaign();
	}
}
