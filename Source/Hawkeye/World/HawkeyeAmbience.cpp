#include "World/HawkeyeAmbience.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioMath.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "World/TimeOfDaySubsystem.h"

AHawkeyeAmbience::AHawkeyeAmbience()
{
	PrimaryActorTick.bCanEverTick = true;
	// The main menu pauses the district under it on a fresh boot; the city still hums behind it.
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	DayStreetSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Ambient/MS_Amb_StreetDay.MS_Amb_StreetDay")));
}

void AHawkeyeAmbience::BeginPlay()
{
	Super::BeginPlay();
	bBedsStarted = false;
}

void AHawkeyeAmbience::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UHawkeyeAudioSubsystem::StopLoop(Wind, TEXT("ambience wind"));
	UHawkeyeAudioSubsystem::StopLoop(Street, TEXT("ambience street"));
	UHawkeyeAudioSubsystem::StopLoop(DayStreet, TEXT("ambience street day"));
	Super::EndPlay(EndPlayReason);
}

void AHawkeyeAmbience::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBedsStarted)
	{
		bBedsStarted = true;
		// Non-spatial beds: attached for their lifetime, but the sounds carry no attenuation.
		Wind = UHawkeyeAudioSubsystem::PlayAttached(WindSound, RootComponent, TEXT("ambience wind"));
		// Each street bed starts at its share for the time of day, so the other is never heard before the first mix.
		const float StartDayWeight = FMath::Clamp(UTimeOfDaySubsystem::GetPresetInForce(this).DayBedWeight, 0.f, 1.f);
		Street = UHawkeyeAudioSubsystem::PlayAttached(StreetSound, RootComponent, TEXT("ambience street"), 1.f - StartDayWeight);
		DayStreet = UHawkeyeAudioSubsystem::PlayAttached(DayStreetSound, RootComponent, TEXT("ambience street day"),
			StartDayWeight);
		SinceUpdate = UpdateSeconds;
	}
	SinceUpdate += DeltaSeconds;
	if (SinceUpdate >= UpdateSeconds)
	{
		UpdateMix();
	}
}

void AHawkeyeAmbience::UpdateMix()
{
	SinceUpdate = 0.f;
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	ListenerHeight = Pawn->GetActorLocation().Z - GetActorLocation().Z;
	float WindVolume = 1.f;
	float StreetVolume = 1.f;
	HawkeyeAudioMath::ComputeAmbienceMix(ListenerHeight, WindVolume, StreetVolume);
	// The street level splits between the night bed and the day bed; a change of the time of day crossfades
	// the two over BedCrossfadeSeconds rather than the height fade's FadeSeconds.
	const float DayWeight = FMath::Clamp(UTimeOfDaySubsystem::GetPresetInForce(this).DayBedWeight, 0.f, 1.f);
	float BedFade = FadeSeconds;
	if (AppliedDayWeight < 0.f)
	{
		BedFade = 0.f;
	}
	else if (!FMath::IsNearlyEqual(AppliedDayWeight, DayWeight))
	{
		BedFade = HawkeyeAudioMath::BedCrossfadeSeconds;
		UE_LOG(LogHawkeye, Log, TEXT("%s: street beds crossfading to %s over %.1f s."), *GetName(),
			DayWeight >= 0.5f ? TEXT("day") : TEXT("night"), BedFade);
	}
	AppliedDayWeight = DayWeight;
	HawkeyeAudioMath::ComputeStreetBeds(StreetVolume, DayWeight, NightStreetVolume, DayStreetVolume);
	if (Wind)
	{
		Wind->AdjustVolume(FadeSeconds, WindVolume);
	}
	if (Street)
	{
		Street->AdjustVolume(BedFade, NightStreetVolume);
	}
	if (DayStreet)
	{
		DayStreet->AdjustVolume(BedFade, DayStreetVolume);
	}
}
