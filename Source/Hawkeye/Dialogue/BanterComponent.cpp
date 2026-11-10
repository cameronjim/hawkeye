#include "Dialogue/BanterComponent.h"

#include "Hawkeye.h"
#include "Combat/HealthComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HawkeyePlayerController.h"
#include "Mission/MissionSubsystem.h"
#include "Partner/HawkeyePartnerController.h"
#include "UI/HawkeyeHudWidget.h"

UBanterComponent::UBanterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
	NextSpeaker = FirstSpeaker;
	Stream.Initialize(0x4b617465);
}

void UBanterComponent::BeginPlay()
{
	Super::BeginPlay();

	NextSpeaker = FirstSpeaker;
	Stream.GenerateNewSeed();
	if (DialogueTable && DialogueTable->GetRowStruct() == FHawkeyeDialogueLine::StaticStruct())
	{
		Lines.Reset();
		DialogueTable->ForeachRow<FHawkeyeDialogueLine>(TEXT("UBanterComponent"),
			[this](const FName& /*Key*/, const FHawkeyeDialogueLine& Row) { Lines.Add(Row); });
	}
	ResetRoamClock();
	UE_LOG(LogHawkeye, Log, TEXT("%s: %d banter line(s) from %s."), *GetNameSafe(GetOwner()), Lines.Num(),
		*GetNameSafe(DialogueTable));
}

int32 UBanterComponent::PickLineIndex(const TArray<FHawkeyeDialogueLine>& InLines, EHawkeyeBanterSituation Situation,
	FName Speaker, int32 AvoidIndex, FRandomStream& InStream)
{
	TArray<int32> Candidates;
	for (int32 Index = 0; Index < InLines.Num(); ++Index)
	{
		if (InLines[Index].Situation == Situation && InLines[Index].Speaker == Speaker)
		{
			Candidates.Add(Index);
		}
	}
	if (Candidates.Num() > 1)
	{
		Candidates.Remove(AvoidIndex);
	}
	return Candidates.IsEmpty() ? INDEX_NONE : Candidates[InStream.RandRange(0, Candidates.Num() - 1)];
}

bool UBanterComponent::PlaySituation(EHawkeyeBanterSituation Situation)
{
	// A scripted line or a walk-and-talk sequence has the floor; banter would talk over it.
	UDialogueSubsystem* Dialogue = UDialogueSubsystem::Get(this);
	if (Dialogue && Dialogue->IsBusy())
	{
		return false;
	}
	FName Speaker = NextSpeaker.IsNone() ? FirstSpeaker : NextSpeaker;
	int32 Index = PickLineIndex(Lines, Situation, Speaker, LastIndex, Stream);
	if (Index == INDEX_NONE)
	{
		// Whoever has something to say for it says it.
		Speaker = Speaker == FirstSpeaker ? SecondSpeaker : FirstSpeaker;
		Index = PickLineIndex(Lines, Situation, Speaker, LastIndex, Stream);
	}
	if (Index == INDEX_NONE)
	{
		return false;
	}

	LastIndex = Index;
	LastSpeaker = Speaker;
	LastText = Lines[Index].Text;
	NextSpeaker = Speaker == FirstSpeaker ? SecondSpeaker : FirstSpeaker;
	const UWorld* World = GetWorld();
	LastLineSeconds = World ? World->GetTimeSeconds() : 0.0;
	ResetRoamClock();

	UE_LOG(LogHawkeye, Log, TEXT("Banter (%s) %s: \"%s\""), *UEnum::GetValueAsString(Situation), *Speaker.ToString(),
		*LastText.ToString());
	if (Dialogue)
	{
		Dialogue->PresentLine(Speaker, LastText, SubtitleSeconds, Lines[Index].Audio.LoadSynchronous());
	}
	else if (const AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwner()))
	{
		if (UHawkeyeHudWidget* Hud = PC->GetHawkeyeHud())
		{
			Hud->ShowSubtitle(FText::FromName(Speaker), LastText, SubtitleSeconds);
		}
	}
	OnBanterLine.Broadcast(Speaker, Situation, LastText);
	return true;
}

void UBanterComponent::ResetRoamClock()
{
	RoamSecondsLeft = Stream.FRandRange(MinRoamSeconds, FMath::Max(MinRoamSeconds, MaxRoamSeconds));
}

void UBanterComponent::TriggerEvent(EHawkeyeBanterSituation Situation)
{
	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() - LastLineSeconds < MinEventGapSeconds)
	{
		return;
	}
	PlaySituation(Situation);
}

void UBanterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	UWorld* World = GetWorld();
	if (!PC || !World || !PC->GetPawn() || Lines.IsEmpty())
	{
		return;
	}
	TActorIterator<AHawkeyePartnerController> PartnerIt(World);
	// Two speakers or none: a benched partner (a chapter without him) has no lines either.
	if (!PartnerIt || !PartnerIt->GetPartner())
	{
		return;
	}

	WatchEvents();
	if (bWasInFight)
	{
		return;
	}
	RoamSecondsLeft -= DeltaTime;
	if (RoamSecondsLeft <= 0.f)
	{
		PlaySituation(EHawkeyeBanterSituation::IdleRoam);
	}
}

void UBanterComponent::WatchEvents()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		return;
	}

	bool bInFight = false;
	for (TActorIterator<AHawkeyePartnerController> It(World); It; ++It)
	{
		bInFight |= It->IsInFight();
	}
	const bool bFightEnded = bWasInFight && !bInFight;
	bWasInFight = bInFight;
	if (bFightEnded)
	{
		TriggerEvent(EHawkeyeBanterSituation::AfterFight);
		return;
	}

	if (const UHealthComponent* Health = Pawn->FindComponentByClass<UHealthComponent>())
	{
		const float Fraction = Health->GetHealthPercent();
		if (bLowHealthArmed && Health->IsAlive() && Fraction < LowHealthFraction)
		{
			bLowHealthArmed = false;
			TriggerEvent(EHawkeyeBanterSituation::LowHealth);
			return;
		}
		bLowHealthArmed |= Fraction > LowHealthRearmFraction;
	}

	FVector Objective;
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (Missions && Missions->GetCurrentObjectiveLocation(Objective)
		&& FVector::Dist(Objective, Pawn->GetActorLocation()) < ObjectiveNearDistance
		&& !Objective.Equals(AnnouncedObjective, 1.f) && !bInFight)
	{
		AnnouncedObjective = Objective;
		TriggerEvent(EHawkeyeBanterSituation::ObjectiveNear);
	}
}
