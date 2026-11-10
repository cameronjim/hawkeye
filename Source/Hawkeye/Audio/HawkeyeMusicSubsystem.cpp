#include "Audio/HawkeyeMusicSubsystem.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Combat/BossPhaseComponent.h"
#include "Combat/HealthComponent.h"
#include "Components/AudioComponent.h"
#include "Crime/CrimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Sound/SoundBase.h"
#include "World/ThugCharacter.h"
#include "World/TimeOfDaySubsystem.h"

namespace
{
	const FName PadGainName(TEXT("PadGain"));
	const FName PulseGainName(TEXT("PulseGain"));
	const FName PercGainName(TEXT("PercGain"));
	const FName MotifGainName(TEXT("MotifGain"));
	const FName DroneGainName(TEXT("DroneGain"));

	/** Dead, limp (ragdolled or fallen over for good) or gone. A knockdown he gets up from is not down. */
	bool IsThugDown(const AThugCharacter* Thug)
	{
		if (!IsValid(Thug))
		{
			return true;
		}
		const UHealthComponent* Health = Thug->GetHealthComponent();
		return (Health && Health->IsDead()) || Thug->IsLimp();
	}
}

UHawkeyeMusicSubsystem::UHawkeyeMusicSubsystem()
{
	ScoreSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Music/MS_Music_Score.MS_Music_Score")));
	WinSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Music/MS_Music_Win.MS_Music_Win")));
}

UHawkeyeMusicSubsystem* UHawkeyeMusicSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = (GEngine && WorldContext)
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UHawkeyeMusicSubsystem>() : nullptr;
}

bool UHawkeyeMusicSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UHawkeyeMusicSubsystem::Deinitialize()
{
	UHawkeyeAudioSubsystem::StopLoop(Score, TEXT("music score"));
	WinSting = nullptr;
	Engaged.Reset();
	Super::Deinitialize();
}

TStatId UHawkeyeMusicSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHawkeyeMusicSubsystem, STATGROUP_Tickables);
}

void UHawkeyeMusicSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// A pause screen (map, inventory, pause menu and settings, safehouse menu, phone, results card, flashback)
	// pauses the world; the director stands still with it, and the score and the sting hold where they are.
	SetScorePaused(World->IsPaused());
	if (bScorePaused || !World->HasBegunPlay())
	{
		return;
	}
	if (!bScoreTried)
	{
		StartScore();
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	Advance(GatherInputs(PC ? PC->GetPawn() : nullptr), DeltaTime);
}

bool UHawkeyeMusicSubsystem::IsTickableWhenPaused() const
{
	// Ticks under a pause only to notice it and to hold the sound; the rules do not run.
	return true;
}

void UHawkeyeMusicSubsystem::SetScorePaused(bool bPaused)
{
	if (bPaused == bScorePaused)
	{
		return;
	}
	bScorePaused = bPaused;
	if (IsValid(Score))
	{
		Score->SetPaused(bPaused);
	}
	if (IsValid(WinSting))
	{
		WinSting->SetPaused(bPaused);
	}
	UE_LOG(LogHawkeye, Log, TEXT("Music: %s (a pause screen %s, state %s)."), bPaused ? TEXT("paused") : TEXT("resumed"),
		bPaused ? TEXT("opened") : TEXT("closed"), HawkeyeMusic::GetStateName(Director.GetState()));
}

void UHawkeyeMusicSubsystem::StartScore()
{
	bScoreTried = true;
	// Not spatial and not attached: the score follows no one. Play2D also keeps it going under the pause menu.
	Score = UHawkeyeAudioSubsystem::Play2D(this, ScoreSound, TEXT("music score"));
	bPushedOnce = false;
	if (Score)
	{
		PushLayers();
	}
}

FHawkeyeMusicInputs UHawkeyeMusicSubsystem::GatherInputs(const APawn* Player)
{
	FHawkeyeMusicInputs Inputs;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Inputs;
	}
	const FVector PlayerLocation = Player ? Player->GetActorLocation() : FVector::ZeroVector;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		AThugCharacter* Thug = *It;
		if (Thug->GetAlertState() != EThugAlertState::Alerted || IsThugDown(Thug))
		{
			continue;
		}
		++Inputs.AlertedStanding;
		if (Player)
		{
			Inputs.NearestAlertedCm = FMath::Min(Inputs.NearestAlertedCm,
				static_cast<float>(FVector::Dist(Thug->GetActorLocation(), PlayerLocation)));
		}
		Inputs.bBossAlerted |= Thug->IsArcher() || Thug->FindComponentByClass<UBossPhaseComponent>() != nullptr;
		Inputs.bHeavyAlerted |= Thug->IsHeavy();
		Engaged.AddUnique(Thug);
	}
	for (const TWeakObjectPtr<AThugCharacter>& Thug : Engaged)
	{
		if (IsThugDown(Thug.Get()))
		{
			++Inputs.EngagedDown;
		}
		else
		{
			++Inputs.EngagedStanding;
		}
	}

	const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(Player);
	Inputs.bPlayerDowned = Kate && Kate->IsDowned();
	if (const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(this); Crimes && Player && Crimes->IsCrimeActive()
		&& !Crimes->IsCrimePaused())
	{
		Inputs.bCrimeNearby = FVector::Dist(Crimes->GetCrimePoint(), PlayerLocation) <= HawkeyeMusic::CrimeNearbyCm;
	}
	const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	Inputs.bChallengeRunning = Challenges && Challenges->IsRunning();
	const UTimeOfDaySubsystem* TimeOfDay = UTimeOfDaySubsystem::Get(this);
	Inputs.bNight = !TimeOfDay || TimeOfDay->GetApplied() == EHawkeyeTimeOfDay::Night;
	return Inputs;
}

void UHawkeyeMusicSubsystem::Advance(const FHawkeyeMusicInputs& Inputs, float DeltaSeconds)
{
	if (Director.Step(Inputs, DeltaSeconds))
	{
		++TransitionCount;
		const EHawkeyeMusicState State = Director.GetState();
		const FString Nearest = Inputs.AlertedStanding > 0 ? FString::Printf(TEXT("%.1f m"), Inputs.NearestAlertedCm / 100.f)
			: FString(TEXT("-"));
		UE_LOG(LogHawkeye, Log,
			TEXT("Music: %s -> %s (alerted %d, nearest %s, boss %s, heavy %s, this fight %d up / %d down, crime near %s, downed %s, #%d)."),
			HawkeyeMusic::GetStateName(Director.GetPreviousState()), HawkeyeMusic::GetStateName(State), Inputs.AlertedStanding,
			*Nearest,
			Inputs.bBossAlerted ? TEXT("yes") : TEXT("no"), Inputs.bHeavyAlerted ? TEXT("yes") : TEXT("no"), Inputs.EngagedStanding, Inputs.EngagedDown,
			Inputs.bCrimeNearby ? TEXT("yes") : TEXT("no"), Inputs.bPlayerDowned ? TEXT("yes") : TEXT("no"), TransitionCount);
		if (State == EHawkeyeMusicState::Win)
		{
			WinSting = UHawkeyeAudioSubsystem::Play2D(this, WinSound, TEXT("music win"));
		}
	}
	if (Director.GetState() == EHawkeyeMusicState::Roam)
	{
		// The fight is over; the next one counts its own thugs.
		Engaged.Reset();
	}
	const EHawkeyeMusicState State = Director.GetState();
	const bool bFast = State == EHawkeyeMusicState::Win || State == EHawkeyeMusicState::Downed;
	Layers = HawkeyeMusic::AdvanceLayers(Layers, HawkeyeMusic::GetLayersFor(State, Inputs), DeltaSeconds, bFast);
	PushLayers();
}

void UHawkeyeMusicSubsystem::PushLayers()
{
	if (!IsValid(Score))
	{
		return;
	}
	const TPair<FName, float> Gains[] = {
		{ PadGainName, Layers.Pad },
		{ PulseGainName, Layers.Pulse },
		{ PercGainName, Layers.Perc },
		{ MotifGainName, Layers.Motif },
		{ DroneGainName, Layers.Drone },
	};
	const float Pushed[] = { PushedLayers.Pad, PushedLayers.Pulse, PushedLayers.Perc, PushedLayers.Motif, PushedLayers.Drone };
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Gains)); ++Index)
	{
		if (!bPushedOnce || !FMath::IsNearlyEqual(Pushed[Index], Gains[Index].Value, 0.001f))
		{
			Score->SetFloatParameter(Gains[Index].Key, Gains[Index].Value);
		}
	}
	PushedLayers = Layers;
	bPushedOnce = true;
}
