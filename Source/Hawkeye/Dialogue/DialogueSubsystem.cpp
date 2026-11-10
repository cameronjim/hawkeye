#include "Dialogue/DialogueSubsystem.h"

#include "Hawkeye.h"
#include "HawkeyeGameMode.h"
#include "HawkeyePlayerController.h"
#include "Components/AudioComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Settings/HawkeyeNarrativeSettings.h"
#include "Sound/SoundBase.h"
#include "UI/HawkeyeHudWidget.h"

UDialogueSubsystem* UDialogueSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDialogueSubsystem>() : nullptr;
}

bool UDialogueSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Queue = NewObject<UDialogueQueue>(this, TEXT("DialogueQueue"));
	if (const UHawkeyeNarrativeSettings* Settings = GetDefault<UHawkeyeNarrativeSettings>())
	{
		Queue->LoadLines(Settings->DialogueTable.LoadSynchronous());
		Queue->LoadSequences(Settings->DialogueSequenceTable.LoadSynchronous());
	}
}

void UDialogueSubsystem::Deinitialize()
{
	OnLineStarted.Clear();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.RemoveDynamic(this, &UDialogueSubsystem::HandleCampaignLoaded);
	}
	if (VoiceAudio)
	{
		VoiceAudio->Stop();
		VoiceAudio = nullptr;
	}
	Queue = nullptr;
	Super::Deinitialize();
}

void UDialogueSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RestoreFromCampaign();
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->OnCampaignLoaded.AddUniqueDynamic(this, &UDialogueSubsystem::HandleCampaignLoaded);
	}
}

TStatId UDialogueSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDialogueSubsystem, STATGROUP_Tickables);
}

bool UDialogueSubsystem::IsInCombat() const
{
	return CombatOverride.IsSet() ? CombatOverride.GetValue() : AHawkeyeGameMode::IsWorldInCombat(GetWorld());
}

bool UDialogueSubsystem::PlayLine(FName RowName)
{
	if (!Queue || !Queue->PlayLine(RowName))
	{
		return false;
	}
	// Starts now when nothing else is speaking.
	Step(0.f);
	return true;
}

bool UDialogueSubsystem::PlaySequence(FName SequenceName, bool bEvenIfPlayed)
{
	if (!Queue || !Queue->PlaySequence(SequenceName, bEvenIfPlayed))
	{
		return false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("Dialogue: sequence %s queued."), *SequenceName.ToString());
	Step(0.f);
	return true;
}

void UDialogueSubsystem::Tick(float DeltaTime)
{
	if (Queue && Queue->IsBusy())
	{
		Step(DeltaTime);
	}
}

void UDialogueSubsystem::Step(float DeltaSeconds)
{
	const FName Started = Queue->Advance(DeltaSeconds, IsInCombat());
	if (Started.IsNone())
	{
		return;
	}
	PresentRow(Started);
	if (!Queue->GetCurrentSequence().IsNone() && Queue->HasPlayedSequence(Queue->GetCurrentSequence()))
	{
		MirrorToCampaign();
	}
}

void UDialogueSubsystem::PresentRow(FName Row)
{
	const FHawkeyeDialogueLine* Line = Queue->FindLine(Row);
	if (!Line)
	{
		return;
	}
	USoundBase* Audio = Line->Audio.LoadSynchronous();
	float Seconds = Queue->GetLineSeconds(*Line);
	if (Audio && Line->DurationSeconds <= 0.f && Audio->GetDuration() > 0.f && Audio->GetDuration() < INDEFINITELY_LOOPING_DURATION)
	{
		Seconds = Audio->GetDuration();
		Queue->SetCurrentLineSeconds(Seconds);
	}
	UE_LOG(LogHawkeye, Log, TEXT("Dialogue %s %s: \"%s\"%s"), *Row.ToString(), *Line->Speaker.ToString(),
		*Line->Text.ToString(), Audio ? TEXT(" (audio)") : TEXT(""));
	PresentLine(Line->Speaker, Line->Text, Seconds, Audio);
	OnLineStarted.Broadcast(Row, Line->Speaker, Line->Text);
}

void UDialogueSubsystem::PresentLine(FName Speaker, const FText& Text, float Seconds, USoundBase* Audio)
{
	if (UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(this))
	{
		Hud->ShowSubtitle(FText::FromName(Speaker), Text, Seconds);
	}
	if (!Audio)
	{
		return;
	}
	if (VoiceAudio)
	{
		VoiceAudio->Stop();
	}
	VoiceAudio = UGameplayStatics::SpawnSound2D(this, Audio);
}

void UDialogueSubsystem::MirrorToCampaign() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr; Campaign && Queue)
	{
		Campaign->PlayedDialogueSequences = Queue->ExportPlayed();
	}
}

void UDialogueSubsystem::RestoreFromCampaign()
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (const UHawkeyeCampaignState* Campaign = Save ? Save->GetCampaignState() : nullptr; Campaign && Queue)
	{
		Queue->ImportPlayed(Campaign->PlayedDialogueSequences);
	}
}

void UDialogueSubsystem::HandleCampaignLoaded(bool bSuccess)
{
	if (bSuccess)
	{
		RestoreFromCampaign();
	}
}
