#include "Phone/PhoneSubsystem.h"
#include "Dialogue/DialogueSubsystem.h"

#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/HawkeyeNarrativeSettings.h"
#include "UI/HawkeyeHudWidget.h"
#include "UI/HawkeyeObjectiveWidget.h"

UPhoneSubsystem* UPhoneSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UPhoneSubsystem>() : nullptr;
}

bool UPhoneSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPhoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UMissionSubsystem>();
	Super::Initialize(Collection);

	Inbox = NewObject<UPhoneInbox>(this, TEXT("PhoneInbox"));
	const UHawkeyeNarrativeSettings* Settings = GetDefault<UHawkeyeNarrativeSettings>();
	if (const UDataTable* Table = Settings ? Settings->MessageTable.LoadSynchronous() : nullptr)
	{
		Inbox->LoadTable(Table);
	}
}

void UPhoneSubsystem::Deinitialize()
{
	OnMessageArrived.Clear();
	OnUnreadChanged.Clear();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UPhoneSubsystem::HandleCampaignLoaded);
	}
	Inbox = nullptr;
	Super::Deinitialize();
}

void UPhoneSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	RestoreFromCampaign();
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->OnMissionStarted.AddUniqueDynamic(this, &UPhoneSubsystem::HandleMissionStarted);
		Missions->OnObjectiveUpdated.AddUniqueDynamic(this, &UPhoneSubsystem::HandleObjectiveUpdated);
		// The game mode starts the mission in its own BeginPlay, which may already have run.
		if (UMissionDefinition* Running = Missions->GetCurrentMission())
		{
			HandleMissionStarted(Running);
		}
	}
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UPhoneSubsystem::HandleCampaignLoaded);
	}
}

TStatId UPhoneSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPhoneSubsystem, STATGROUP_Tickables);
}

bool UPhoneSubsystem::IsRestoring() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	return Save && Save->IsLoading();
}

void UPhoneSubsystem::HandleMissionStarted(UMissionDefinition* Mission)
{
	if (!Inbox || !Mission)
	{
		return;
	}
	ChapterId = Mission->GetFName();
	if (IsRestoring())
	{
		// The loaded game has already had its chapter-start texts; remember where it stands.
		const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
		const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
		LastCurrentObjectiveId = Current ? Current->ObjectiveId : NAME_None;
		return;
	}
	// A message arrives once per campaign, so a restarted chapter does not text anyone twice.
	Inbox->HandleTrigger(EHawkeyeMessageTrigger::ChapterStart, NAME_None, ChapterId);
	LastCurrentObjectiveId = NAME_None;
	NoteCurrentObjective();
	MirrorToCampaign();
}

void UPhoneSubsystem::HandleObjectiveUpdated(UMissionObjective* Objective, int32 /*ObjectiveIndex*/)
{
	if (!Inbox || !Objective || IsRestoring())
	{
		return;
	}
	if (Objective->IsCompleted())
	{
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveCompleted, Objective->ObjectiveId, ChapterId);
	}
	NoteCurrentObjective();
	// A level change before the next save keeps what is now on its delay.
	MirrorToCampaign();
}

void UPhoneSubsystem::NoteCurrentObjective()
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
	const FName CurrentId = Current ? Current->ObjectiveId : NAME_None;
	if (CurrentId == LastCurrentObjectiveId)
	{
		return;
	}
	LastCurrentObjectiveId = CurrentId;
	if (!CurrentId.IsNone())
	{
		Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveStarted, CurrentId, ChapterId);
	}
}

int32 UPhoneSubsystem::NotifyEvent(FName EventName)
{
	const int32 Count = Inbox && !EventName.IsNone() ? Inbox->HandleTrigger(EHawkeyeMessageTrigger::Event, EventName, ChapterId) : 0;
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: event %s scheduled %d message(s)."), *GetNameSafe(this), *EventName.ToString(), Count);
	if (Count > 0)
	{
		MirrorToCampaign();
	}
	return Count;
}

void UPhoneSubsystem::Tick(float DeltaTime)
{
	if (!Inbox || Inbox->GetScheduledCount() == 0)
	{
		return;
	}
	const TArray<FName> Arrived = Inbox->Advance(DeltaTime);
	if (Arrived.IsEmpty())
	{
		return;
	}
	for (const FName Id : Arrived)
	{
		AnnounceArrival(Id);
	}
	MirrorToCampaign();
	OnUnreadChanged.Broadcast(Inbox->GetUnreadCount());
}

void UPhoneSubsystem::AnnounceArrival(FName Id)
{
	const FHawkeyePhoneMessage* Row = Inbox->FindMessage(Id);
	if (!Row)
	{
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("Phone: %s from %s arrived (%d unread)."), *Id.ToString(), *Row->Sender.ToString(),
		Inbox->GetUnreadCount());
	if (!Row->bRead)
	{
		UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(this);
		if (UHawkeyeObjectiveWidget* Toasts = Hud ? Hud->GetObjectiveMarker() : nullptr)
		{
			Toasts->PushToast(Row->Sender, UPhoneInbox::FirstLine(Row->Text), EHawkeyeUISound::Toast);
		}
	}
	if (!Row->CallSequence.IsNone())
	{
		// A call: the voices play as a sequence (it waits out a fight like any walk-and-talk).
		if (UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this))
		{
			const bool bQueued = Dialogue->PlaySequence(Row->CallSequence);
			UE_LOG(LogHawkeye, Log, TEXT("Phone: %s is a call; sequence %s %s."), *Id.ToString(), *Row->CallSequence.ToString(),
				bQueued ? TEXT("queued") : TEXT("not queued (unknown or already played)"));
		}
	}
	OnMessageArrived.Broadcast(Id);
}

int32 UPhoneSubsystem::ReadThread(const FString& Sender)
{
	const int32 Count = Inbox ? Inbox->MarkThreadRead(Sender) : 0;
	if (Count > 0)
	{
		MirrorToCampaign();
		OnUnreadChanged.Broadcast(Inbox->GetUnreadCount());
	}
	return Count;
}

void UPhoneSubsystem::MirrorToCampaign() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (Inbox && Campaign)
	{
		Inbox->Export(Campaign->ReceivedMessages, Campaign->ReadMessages);
		Inbox->ExportPending(Campaign->PendingMessages, Campaign->PendingMessageSeconds);
	}
}

int32 UPhoneSubsystem::CatchUpTriggers()
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Inbox || IsRestoring() || !Missions || !Missions->GetCurrentMission())
	{
		return 0;
	}
	int32 Count = 0;
	for (const UMissionObjective* Objective : Missions->GetActiveObjectives())
	{
		if (Objective && Objective->IsCompleted())
		{
			Count += Inbox->HandleTrigger(EHawkeyeMessageTrigger::ObjectiveCompleted, Objective->ObjectiveId, ChapterId);
		}
	}
	return Count;
}

void UPhoneSubsystem::RestoreFromCampaign()
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr;
	if (Inbox && Campaign)
	{
		Inbox->Import(Campaign->ReceivedMessages, Campaign->ReadMessages);
		Inbox->ImportPending(Campaign->PendingMessages, Campaign->PendingMessageSeconds);
		OnUnreadChanged.Broadcast(Inbox->GetUnreadCount());
	}
}

void UPhoneSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		RestoreFromCampaign();
	}
}
