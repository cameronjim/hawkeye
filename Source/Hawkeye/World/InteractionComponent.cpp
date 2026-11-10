#include "World/InteractionComponent.h"

#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Combat/TakedownComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "UI/HawkeyeHudWidget.h"
#include "World/Interactable.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	// So the first frame already has a prompt rather than a blank one.
	RefreshFocus();
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TimeSinceRefresh += DeltaTime;
	if (TimeSinceRefresh < RefreshSeconds)
	{
		return;
	}

	TimeSinceRefresh = 0.f;
	RefreshFocus();
}

void UInteractionComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (AController* OwnerController = Pawn->GetController())
		{
			OwnerController->GetPlayerViewPoint(OutLocation, OutRotation);
			return;
		}
	}

	Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
}

AActor* UInteractionComponent::TraceForInteractable() const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return nullptr;
	}
	// Down or dead, nothing offers itself: no prompt, and E does nothing (a chapter end examined from the
	// ground ran its sequence with her down and its autosave refused).
	if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Owner);
		Hawkeye && (Hawkeye->IsDowned() || (Hawkeye->GetHealthComponent() && !Hawkeye->GetHealthComponent()->IsAlive())))
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	GetViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractRange;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HawkeyeInteractionFocus), /*bTraceComplex=*/false, Owner);
	QueryParams.AddIgnoredActor(Owner);

	// A sphere sweep rather than a line: doors and small pickups are easy to miss otherwise.
	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(
		Hits, ViewLocation, TraceEnd, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(TraceRadius), QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!IsValid(Candidate) || !Candidate->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
		{
			continue;
		}

		if (!IInteractable::Execute_CanInteract(Candidate, Owner))
		{
			continue;
		}

		// Hits come back sorted along the sweep, so the first valid one is the nearest.
		return Candidate;
	}

	// Standing in an interactable's zone counts as looking at it: in third person the camera trace
	// starts metres behind the character and often ends before the thing she is standing at.
	TArray<AActor*> Overlapping;
	Owner->GetOverlappingActors(Overlapping);
	for (AActor* Candidate : Overlapping)
	{
		if (IsValid(Candidate) && Candidate->GetClass()->ImplementsInterface(UInteractable::StaticClass())
			&& IInteractable::Execute_CanInteract(Candidate, Owner))
		{
			return Candidate;
		}
	}

	return nullptr;
}

void UInteractionComponent::RefreshFocus()
{
	SetFocusedActor(TraceForInteractable());
	UpdateHudPrompt();
}

void UInteractionComponent::SetFocusedActor(AActor* NewFocus)
{
	if (FocusedActor == NewFocus)
	{
		return;
	}

	FocusedActor = NewFocus;
	OnFocusedInteractableChanged.Broadcast(NewFocus);
}

FText UInteractionComponent::GetFocusPrompt() const
{
	if (!IsValid(FocusedActor))
	{
		return FText::GetEmpty();
	}

	return IInteractable::Execute_GetInteractPrompt(FocusedActor);
}

void UInteractionComponent::UpdateHudPrompt()
{
	if (!bDrivesHudPrompt)
	{
		return;
	}

	UHawkeyeHudWidget* Hud = AHawkeyePlayerController::GetHawkeyeHudFor(this);
	if (!Hud)
	{
		return;
	}

	FText Prompt = GetFocusPrompt();

	// Nothing to interact with: offer the takedown instead, so one line covers both verbs.
	if (Prompt.IsEmpty())
	{
		UTakedownComponent* Takedown = GetOwner() ? GetOwner()->FindComponentByClass<UTakedownComponent>() : nullptr;
		if (Takedown && Takedown->FindTakedownTarget())
		{
			Prompt = NSLOCTEXT("Hawkeye", "TakedownPrompt", "[F] Take down");
		}
	}

	Hud->SetPrompt(Prompt);
}

bool UInteractionComponent::TryInteract()
{
	// The player may have turned since the last refresh tick.
	RefreshFocus();

	AActor* Target = FocusedActor;
	AActor* Owner = GetOwner();
	if (!IsValid(Target) || !Owner)
	{
		return false;
	}

	IInteractable::Execute_Interact(Target, Owner);
	UE_LOG(LogHawkeye, Verbose, TEXT("%s interacted with %s."), *GetNameSafe(Owner), *Target->GetName());

	// Interacting usually changes the prompt (a door that is now open, a pickup that is gone).
	RefreshFocus();
	return true;
}
