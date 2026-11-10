#include "Combat/TakedownComponent.h"

#include "Hawkeye.h"
#include "Combat/Takedownable.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Mission/ChapterEventSubsystem.h"
#include "TimerManager.h"

UTakedownComponent::UTakedownComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTakedownComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TakedownTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

bool UTakedownComponent::IsBehindTarget(const FVector& AttackerLocation, const FVector& TargetLocation, const FVector& TargetForward, float MaxAngleDegrees)
{
	const FVector TargetToAttacker = (AttackerLocation - TargetLocation).GetSafeNormal2D();
	const FVector Forward = TargetForward.GetSafeNormal2D();
	if (TargetToAttacker.IsNearlyZero() || Forward.IsNearlyZero())
	{
		return false;
	}

	// Angle between the target's BACKWARD direction and the direction to the attacker.
	const float Dot = FVector::DotProduct(-Forward, TargetToAttacker);
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));

	// KINDA_SMALL_NUMBER of slack so a target placed exactly at MaxAngleDegrees counts as behind.
	return AngleDegrees <= MaxAngleDegrees + KINDA_SMALL_NUMBER;
}

bool UTakedownComponent::IsValidTakedownTarget(const AActor* Target, const FVector& AttackerLocation) const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Target) || Target == Owner)
	{
		return false;
	}

	if (!TargetTag.IsNone() && !Target->ActorHasTag(TargetTag))
	{
		return false;
	}

	if (!Target->GetClass()->ImplementsInterface(UTakedownable::StaticClass()))
	{
		return false;
	}

	if (FVector::DistSquared(AttackerLocation, Target->GetActorLocation()) > FMath::Square(Range))
	{
		return false;
	}

	if (!IsBehindTarget(AttackerLocation, Target->GetActorLocation(), Target->GetActorForwardVector(), MaxAngleDegrees))
	{
		return false;
	}

	// Thugs veto this while Alerted; bAlertedThugsAreValid ignores the veto.
	if (!bAlertedThugsAreValid &&
		!ITakedownable::Execute_CanBeTakenDown(const_cast<AActor*>(Target), const_cast<AActor*>(Owner)))
	{
		return false;
	}

	return true;
}

AActor* UTakedownComponent::FindTakedownTarget() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	const FVector Start = Owner->GetActorLocation();
	const FVector End = Start + Owner->GetActorForwardVector() * Range;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HawkeyeTakedown), /*bTraceComplex=*/false, Owner);
	QueryParams.AddIgnoredActor(Owner);

	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(
		Hits, Start, End, FQuat::Identity, TraceChannel, FCollisionShape::MakeSphere(TakedownRadius), QueryParams);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		DrawDebugCapsule(World, (Start + End) * 0.5f, (End - Start).Size() * 0.5f + TakedownRadius, TakedownRadius,
			FRotationMatrix::MakeFromZ(End - Start).ToQuat(), FColor::Yellow, false, 2.f);
	}
#endif

	AActor* BestTarget = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (const FHitResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!IsValidTakedownTarget(Candidate, Start))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(Start, Candidate->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestTarget = Candidate;
		}
	}

	return BestTarget;
}

bool UTakedownComponent::TryTakedown()
{
	if (bIsPerformingTakedown)
	{
		return false;
	}

	AActor* Target = FindTakedownTarget();
	if (!Target)
	{
		return false;
	}

	bIsPerformingTakedown = true;

	const FVector BodyAt = Target->GetActorLocation();
	ITakedownable::Execute_OnTakedown(Target, GetOwner());
	OnTakedownPerformed.Broadcast(Target);
	// The player's stealth takedown is a chapter event (the grotto's "Naughty list.").
	if (const APawn* Owner = Cast<APawn>(GetOwner()); Owner && Owner->IsPlayerControlled())
	{
		UChapterEventSubsystem::NotifyEvent(this, UHawkeyeChapterEventRules::Takedown);
	}
	// The body drop: anyone near enough, with nothing solid in the way, comes to look.
	if (APawn* Attacker = Cast<APawn>(GetOwner()); Attacker && TakedownNoiseLoudness > 0.f)
	{
		Attacker->MakeNoise(TakedownNoiseLoudness, Attacker, BodyAt);
	}

	UE_LOG(LogHawkeye, Verbose, TEXT("Takedown performed on %s."), *Target->GetName());

	UWorld* World = GetWorld();
	if (World && TakedownSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(
			TakedownTimerHandle, this, &UTakedownComponent::EndTakedown, TakedownSeconds, false);
	}
	else
	{
		EndTakedown();
	}

	return true;
}

void UTakedownComponent::EndTakedown()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TakedownTimerHandle);
	}

	bIsPerformingTakedown = false;
}
