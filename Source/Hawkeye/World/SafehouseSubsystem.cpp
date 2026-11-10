#include "World/SafehouseSubsystem.h"

#include "AIController.h"
#include "Camera/PlayerCameraManager.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Crime/CrimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Hawkeye.h"
#include "HawkeyeGameMode.h"
#include "Combat/HealthComponent.h"
#include "HawkeyePlayerController.h"
#include "HAL/PlatformTime.h"
#include "Mission/MissionSubsystem.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "World/Safehouse.h"

const FName USafehouseSubsystem::MarkerSource(TEXT("Safehouse"));

USafehouseSubsystem* USafehouseSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<USafehouseSubsystem>() : nullptr;
}

bool USafehouseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USafehouseSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TravelTimer);
	}
	Safehouses.Reset();
	Super::Deinitialize();
}

// --- The district's safehouses ---------------------------------------------------------------------

void USafehouseSubsystem::RegisterSafehouse(ASafehouse* Safehouse)
{
	if (IsValid(Safehouse))
	{
		Safehouses.AddUnique(Safehouse);
	}
}

void USafehouseSubsystem::UnregisterSafehouse(ASafehouse* Safehouse)
{
	Safehouses.RemoveAll([Safehouse](const TWeakObjectPtr<ASafehouse>& Weak) { return !Weak.IsValid() || Weak.Get() == Safehouse; });
	if (Marked.Get() == Safehouse)
	{
		ClearSafehouseMarker();
	}
}

TArray<ASafehouse*> USafehouseSubsystem::GetSafehouses() const
{
	TArray<ASafehouse*> Out;
	for (const TWeakObjectPtr<ASafehouse>& Weak : Safehouses)
	{
		if (ASafehouse* Safehouse = Weak.Get())
		{
			Out.Add(Safehouse);
		}
	}
	Out.Sort([](const ASafehouse& A, const ASafehouse& B) { return A.SafehouseId.LexicalLess(B.SafehouseId); });
	return Out;
}

ASafehouse* USafehouseSubsystem::FindSafehouse(FName SafehouseId) const
{
	for (ASafehouse* Safehouse : GetSafehouses())
	{
		if (Safehouse->SafehouseId == SafehouseId)
		{
			return Safehouse;
		}
	}
	return nullptr;
}

TArray<FHawkeyeSafehouseEntry> USafehouseSubsystem::MakeEntries(const TArray<FName>& Discovered) const
{
	TArray<FHawkeyeSafehouseEntry> Out;
	for (const ASafehouse* Safehouse : GetSafehouses())
	{
		FHawkeyeSafehouseEntry& Entry = Out.AddDefaulted_GetRef();
		Entry.SafehouseId = Safehouse->SafehouseId;
		Entry.DisplayName = Safehouse->GetDisplayName();
		Entry.Location = Safehouse->GetActorLocation();
		Entry.bDiscovered = Discovered.Contains(Safehouse->SafehouseId);
	}
	return Out;
}

TArray<FHawkeyeSafehouseEntry> USafehouseSubsystem::GetEntries() const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	return MakeEntries(Save ? Save->GetDiscoveredSafehouses() : TArray<FName>());
}

TArray<FHawkeyeSafehouseEntry> USafehouseSubsystem::BuildTravelList(const TArray<FHawkeyeSafehouseEntry>& All, FName CurrentId,
	const TArray<FName>& Discovered)
{
	TArray<FHawkeyeSafehouseEntry> Out;
	for (const FHawkeyeSafehouseEntry& Entry : All)
	{
		if (Entry.SafehouseId.IsNone() || Entry.SafehouseId == CurrentId)
		{
			continue;
		}
		FHawkeyeSafehouseEntry& Row = Out.Add_GetRef(Entry);
		Row.bDiscovered = Discovered.Contains(Entry.SafehouseId);
	}
	Out.Sort([](const FHawkeyeSafehouseEntry& A, const FHawkeyeSafehouseEntry& B) { return A.SafehouseId.LexicalLess(B.SafehouseId); });
	return Out;
}

ASafehouse* USafehouseSubsystem::FindNearest(const FVector& From, const TArray<FName>& Discovered) const
{
	ASafehouse* Nearest = nullptr;
	float Best = TNumericLimits<float>::Max();
	for (ASafehouse* Safehouse : GetSafehouses())
	{
		const float Distance = FVector::DistSquared(Safehouse->GetActorLocation(), From);
		if (Discovered.Contains(Safehouse->SafehouseId) && Distance < Best)
		{
			Best = Distance;
			Nearest = Safehouse;
		}
	}
	return Nearest;
}

ASafehouse* USafehouseSubsystem::FindNearestDiscovered(const FVector& From) const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	return FindNearest(From, Save ? Save->GetDiscoveredSafehouses() : TArray<FName>());
}

ASafehouse* USafehouseSubsystem::FindNearestAny(const FVector& From) const
{
	TArray<FName> All;
	for (const ASafehouse* Safehouse : GetSafehouses())
	{
		All.Add(Safehouse->SafehouseId);
	}
	return FindNearest(From, All);
}

ASafehouse* USafehouseSubsystem::FindUnnoticed(const FVector& From, float Radius, const TArray<FName>& Discovered,
	const TArray<FName>& Noticed) const
{
	ASafehouse* Nearest = nullptr;
	float Best = FMath::Square(Radius);
	for (ASafehouse* Safehouse : GetSafehouses())
	{
		const float Distance = FVector::DistSquared(Safehouse->GetActorLocation(), From);
		if (Distance <= Best && !Discovered.Contains(Safehouse->SafehouseId) && !Noticed.Contains(Safehouse->SafehouseId))
		{
			Best = Distance;
			Nearest = Safehouse;
		}
	}
	return Nearest;
}

FText USafehouseSubsystem::GetMarkerName(const ASafehouse* Safehouse, bool bDiscovered)
{
	if (!Safehouse)
	{
		return FText::GetEmpty();
	}
	return bDiscovered ? Safehouse->GetDisplayName() : NSLOCTEXT("Hawkeye", "SafehouseUnknown", "[Unknown safehouse]");
}

bool USafehouseSubsystem::IsDiscoveredNow(const ASafehouse* Safehouse) const
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	return Safehouse && Save && Save->IsSafehouseDiscovered(Safehouse->SafehouseId);
}

FText USafehouseSubsystem::GetMarkerNameNow(const ASafehouse* Safehouse) const
{
	return GetMarkerName(Safehouse, IsDiscoveredNow(Safehouse));
}

// --- Fast travel -----------------------------------------------------------------------------------

FText USafehouseSubsystem::GetTravelRefusal(bool bCrimeActive, bool bChallengeRunning, bool bTravelling, bool bInFight)
{
	return bCrimeActive || bChallengeRunning || bTravelling || bInFight
		? NSLOCTEXT("Hawkeye", "FastTravelRefused", "[Can't fast travel now]") : FText::GetEmpty();
}

FText USafehouseSubsystem::GetTravelRefusalNow() const
{
	const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(this);
	const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AHawkeyeCharacter* Player = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	const bool bDown = Player && (Player->IsDowned() || !Player->GetHealthComponent()->IsAlive());
	return GetTravelRefusal(Crimes && Crimes->IsCrimeActive(), Challenges && Challenges->IsRunning(), bTravelling,
		AHawkeyeGameMode::IsWorldInCombat(World) || bDown);
}

bool USafehouseSubsystem::PlaceAtArrival(ASafehouse* Destination, APawn* Player, APawn* Partner)
{
	if (!IsValid(Destination) || !IsValid(Player))
	{
		return false;
	}
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		APawn* Pawn = Slot == 0 ? Player : Partner;
		if (!IsValid(Pawn))
		{
			continue;
		}
		const FTransform Arrival = Destination->GetArrivalTransform(Slot);
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		const float HalfHeight = Character && Character->GetCapsuleComponent()
			? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		if (AAIController* Brain = Cast<AAIController>(Pawn->GetController()))
		{
			Brain->StopMovement();
		}
		// Nudged clear of anything in the way when it can be; put there regardless when it cannot.
		const FVector Spot = Arrival.GetLocation() + FVector(0.f, 0.f, HalfHeight + 2.f);
		if (!Pawn->TeleportTo(Spot, Arrival.Rotator(), /*bIsATest=*/false, /*bNoCheck=*/false))
		{
			Pawn->TeleportTo(Spot, Arrival.Rotator(), /*bIsATest=*/false, /*bNoCheck=*/true);
		}
		if (Character && Character->GetCharacterMovement())
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
	}
	return true;
}

bool USafehouseSubsystem::BeginFastTravel(AHawkeyePlayerController* PC, ASafehouse* Destination)
{
	const UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	UWorld* World = GetWorld();
	if (!World || !IsValid(PC) || !PC->GetPawn() || !IsValid(Destination) || !Save
		|| !Save->IsSafehouseDiscovered(Destination->SafehouseId))
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: fast travel to %s refused: not a discovered safehouse, or no player."), *GetName(),
			*GetNameSafe(Destination));
		return false;
	}
	const FText Refusal = GetTravelRefusalNow();
	if (!Refusal.IsEmpty())
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: fast travel to %s refused (crime, challenge, fight, down or a travel under way)."), *GetName(),
			*Destination->SafehouseId.ToString());
		return false;
	}
	bTravelling = true;
	TravelPC = PC;
	TravelDestination = Destination;
	TravelStartedAt = FPlatformTime::Seconds();
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FadeOutSeconds, FLinearColor::Black, false, /*bHoldWhenFinished=*/true);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: fast travel to %s."), *GetName(), *Destination->SafehouseId.ToString());
	World->GetTimerManager().SetTimer(TravelTimer, this, &USafehouseSubsystem::HandleFadedOut, FadeOutSeconds, false);
	return true;
}

void USafehouseSubsystem::HandleFadedOut()
{
	AHawkeyePlayerController* PC = TravelPC.Get();
	ASafehouse* Destination = TravelDestination.Get();
	APawn* Player = PC ? PC->GetPawn() : nullptr;
	const AHawkeyePartnerController* Partner = PC ? PC->FindPartnerController() : nullptr;
	if (!Player || !Destination)
	{
		bTravelling = false;
		ReleasePlayer(PC);
		return;
	}

	// The boom's lag would sweep the camera across the district; off for the hold, back for the fade in.
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Player);
	if (Hawkeye && Hawkeye->GetCameraBoom())
	{
		Hawkeye->GetCameraBoom()->bEnableCameraLag = false;
	}
	PlaceAtArrival(Destination, Player, Partner ? Partner->GetPawn() : nullptr);
	PC->SetControlRotation(FRotator(-10.f, Destination->GetArrivalTransform(0).Rotator().Yaw, 0.f));

	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		Save->SetLastSafehouse(Destination->SafehouseId);
		Save->SaveCampaign(TEXT("fast travel"));
	}
	if (Marked.Get() == Destination)
	{
		ClearSafehouseMarker();
	}
	GetWorld()->GetTimerManager().SetTimer(TravelTimer, this, &USafehouseSubsystem::HandleHoldDone, FMath::Max(HoldSeconds, 0.01f), false);
}

void USafehouseSubsystem::HandleHoldDone()
{
	AHawkeyePlayerController* PC = TravelPC.Get();
	const AHawkeyeCharacter* Hawkeye = PC ? Cast<AHawkeyeCharacter>(PC->GetPawn()) : nullptr;
	if (Hawkeye && Hawkeye->GetCameraBoom())
	{
		Hawkeye->GetCameraBoom()->bEnableCameraLag = true;
	}
	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, FadeInSeconds, FLinearColor::Black, false, false);
	}
	GetWorld()->GetTimerManager().SetTimer(TravelTimer, this, &USafehouseSubsystem::HandleFadedIn, FadeInSeconds, false);
}

void USafehouseSubsystem::HandleFadedIn()
{
	AHawkeyePlayerController* PC = TravelPC.Get();
	const FName Destination = TravelDestination.IsValid() ? TravelDestination->SafehouseId : NAME_None;
	LastTravelSeconds = static_cast<float>(FPlatformTime::Seconds() - TravelStartedAt);
	bTravelling = false;
	ReleasePlayer(PC);
	UE_LOG(LogHawkeye, Log, TEXT("%s: fast travel to %s done in %.2f s."), *GetName(), *Destination.ToString(), LastTravelSeconds);
	OnFastTravelFinished.Broadcast(Destination, LastTravelSeconds);
}

void USafehouseSubsystem::ReleasePlayer(AHawkeyePlayerController* PC)
{
	if (PC)
	{
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}
	TravelPC.Reset();
	TravelDestination.Reset();
}

// --- The safehouse marker --------------------------------------------------------------------------

ASafehouse* USafehouseSubsystem::MarkNearestSafehouse(const FVector& From)
{
	// Found or not: the marker is how you find one in the first place.
	ASafehouse* Nearest = FindNearestAny(From);
	return MarkSafehouse(Nearest) ? Nearest : nullptr;
}

bool USafehouseSubsystem::MarkSafehouse(ASafehouse* Safehouse)
{
	UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	if (!Safehouse || !Missions)
	{
		return false;
	}
	Marked = Safehouse;
	Missions->SetSecondaryMarkers(MarkerSource, { Safehouse->GetActorLocation() + FVector(0.f, 0.f, MarkerUp) });
	UE_LOG(LogHawkeye, Log, TEXT("%s: marked safehouse %s (%s)."), *GetName(), *Safehouse->SafehouseId.ToString(),
		IsDiscoveredNow(Safehouse) ? TEXT("found") : TEXT("not found yet"));
	return true;
}

void USafehouseSubsystem::ClearSafehouseMarker()
{
	Marked.Reset();
	if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
	{
		Missions->ClearSecondaryMarkers(MarkerSource);
	}
}
