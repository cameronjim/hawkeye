#include "Combat/TakedownComponent.h"

#include "Hawkeye.h"
#include "Combat/Takedownable.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/MeleeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Mission/ChapterEventSubsystem.h"
#include "TimerManager.h"

UTakedownComponent::UTakedownComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

FTransform UTakedownComponent::ComputePairedSpot(const FVector& VictimLocation, const FRotator& VictimRotation, float Gap, float AttackerZ)
{
	const FRotator Facing(0.f, VictimRotation.Yaw, 0.f);
	const FVector Behind = VictimLocation - Facing.Vector() * Gap;
	return FTransform(Facing, FVector(Behind.X, Behind.Y, AttackerZ));
}

EHawkeyeTakedownVariant UTakedownComponent::ChooseVariant(bool bAttackerClip, bool bVictimClip, bool bSpotClear)
{
	return bAttackerClip && bVictimClip && bSpotClear ? EHawkeyeTakedownVariant::Paired : EHawkeyeTakedownVariant::Plain;
}

bool UTakedownComponent::ShouldEndPairedEarly(bool bVictimValid, bool bVictimLimp)
{
	return !bVictimValid || bVictimLimp;
}

bool UTakedownComponent::IsPairedSpotClear(const AActor& Target, const FTransform& Spot, FName* OutWhy) const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}
	// The way there: the strike magnetism's own checks (a sight line, a walkable floor all the way).
	if (const UMeleeComponent* Melee = Owner->FindComponentByClass<UMeleeComponent>())
	{
		if (!Melee->IsStrikePathClear(Target, Spot.GetLocation(), OutWhy))
		{
			return false;
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyePairedTakedown), false, Owner);
	Params.AddIgnoredActor(&Target);
	FCollisionObjectQueryParams Static;
	Static.AddObjectTypesToQuery(ECC_WorldStatic);
	Static.AddObjectTypesToQuery(ECC_WorldDynamic);
	// Her body on the spot: nothing solid where her capsule will stand.
	float Radius = 34.f;
	float HalfHeight = 88.f;
	Owner->GetSimpleCollisionCylinder(Radius, HalfHeight);
	if (World->OverlapAnyTestByObjectType(Spot.GetLocation() + FVector(0.f, 0.f, 5.f), FQuat::Identity, Static,
			FCollisionShape::MakeCapsule(Radius * 0.9f, HalfHeight * 0.9f), Params))
	{
		if (OutWhy)
		{
			*OutWhy = FName(TEXT("no room behind him"));
		}
		return false;
	}
	// His fall: he is lowered onto his back behind where he stood and she drags him there, so a body's length and more
	// behind him must be open at hip height and floored (not a ledge). Pawns (her, him, anyone near) do not count.
	const FVector Feet = Target.GetActorLocation() - FVector(0.f, 0.f, Target.GetSimpleCollisionHalfHeight());
	const FVector Back = -FRotator(0.f, Target.GetActorRotation().Yaw, 0.f).Vector();
	auto FirstSolid = [&](const TArray<FHitResult>& Hits) -> const FHitResult*
	{
		for (const FHitResult& Hit : Hits)
		{
			if (Hit.bBlockingHit && !(Hit.GetActor() && Hit.GetActor()->IsA<APawn>()))
			{
				return &Hit;
			}
		}
		return nullptr;
	};
	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(Hits, Feet + FVector(0.f, 0.f, 45.f), Feet + Back * PairedFallClearance + FVector(0.f, 0.f, 45.f),
		FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(20.f), Params);
	if (FirstSolid(Hits))
	{
		if (OutWhy)
		{
			*OutWhy = FName(TEXT("a wall where he falls"));
		}
		return false;
	}
	for (const float Along : { 0.4f, 0.8f, 1.0f })
	{
		const FVector Ground = Feet + Back * PairedFallClearance * Along;
		Hits.Reset();
		World->LineTraceMultiByChannel(Hits, Ground + FVector(0.f, 0.f, 40.f), Ground - FVector(0.f, 0.f, 40.f), ECC_Visibility, Params);
		if (!FirstSolid(Hits))
		{
			if (OutWhy)
			{
				*OutWhy = FName(TEXT("no floor where he falls"));
			}
			return false;
		}
	}
	return true;
}

bool UTakedownComponent::TryPairedTakedown(AActor* Target, float& OutSeconds)
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	UAnimMontage* Montage = UCombatAnimSet::Resolve(AnimSet, ECombatAnimRole::TakedownAttacker);
	USkeletalMeshComponent* Mesh = Owner ? Owner->GetMesh() : nullptr;
	if (!Owner || !Mesh || !Target)
	{
		return false;
	}
	const FTransform Spot = ComputePairedSpot(Target->GetActorLocation(), Target->GetActorRotation(), PairedGap,
		Owner->GetActorLocation().Z);
	FName Why = NAME_None;
	const bool bClear = Montage && IsPairedSpotClear(*Target, Spot, &Why);
	// The victim is asked last: once he answers he is already playing his half.
	if (ChooseVariant(Montage != nullptr, true, bClear) != EHawkeyeTakedownVariant::Paired)
	{
		if (Montage)
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: paired takedown on %s refused (%s): plain."), *Owner->GetName(), *Target->GetName(),
				*Why.ToString());
		}
		return false;
	}
	const float VictimSeconds = ITakedownable::Execute_BeginPairedTakedown(Target, Owner);
	if (ChooseVariant(true, VictimSeconds > 0.f, true) != EHawkeyeTakedownVariant::Paired)
	{
		return false;
	}
	// The capsule goes to the spot at once; her body eases there from where it was drawn.
	const FTransform MeshWorld = Mesh->GetComponentTransform();
	const FTransform MeshRelative = Mesh->GetRelativeTransform();
	if (UCharacterMovementComponent* Movement = Owner->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	Owner->SetActorLocationAndRotation(Spot.GetLocation(), Spot.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	Mesh->SetWorldTransform(MeshWorld);
	EaseFrom = Mesh->GetRelativeTransform();
	EaseTo = MeshRelative;
	EaseElapsed = 0.f;
	bEasing = true;
	UAnimInstance* Instance = HawkeyeCombatAnim::Play(Mesh, Montage);
	if (!Instance)
	{
		Mesh->SetRelativeTransform(MeshRelative);
		bEasing = false;
		return false;
	}
	PairedInstance = Instance;
	PairedMontage = Montage;
	PairedVictim = Target;
	OutSeconds = FMath::Max(Montage->GetPlayLength(), VictimSeconds);
	SetComponentTickEnabled(true);
	UE_LOG(LogHawkeye, Log, TEXT("%s: paired takedown on %s (%s, %.2f s)."), *Owner->GetName(), *Target->GetName(), *Montage->GetName(),
		OutSeconds);
	return true;
}

void UTakedownComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Owner ? Owner->GetMesh() : nullptr;
	if (bEasing && Mesh)
	{
		EaseElapsed += DeltaTime;
		const float Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(EaseElapsed / PairedEaseSeconds, 0.f, 1.f));
		FTransform Eased;
		Eased.Blend(EaseFrom, EaseTo, Alpha);
		Mesh->SetRelativeTransform(Eased);
		bEasing = Alpha < 1.f;
	}
	if (bIsPerformingTakedown && LastVariant == EHawkeyeTakedownVariant::Paired)
	{
		const AActor* Victim = PairedVictim.Get();
		const USkeletalMeshComponent* VictimMesh = Victim ? Victim->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		if (ShouldEndPairedEarly(IsValid(Victim), VictimMesh && VictimMesh->IsSimulatingPhysics()
				&& GetWorld()->GetTimeSeconds() - PairedStartedAt < LockSeconds - 0.05f && !bVictimHandedOver))
		{
			UE_LOG(LogHawkeye, Log, TEXT("%s: paired takedown ended early (the victim is gone or already down)."), *GetNameSafe(Owner));
			EndTakedown();
		}
	}
	if (!bEasing && !bIsPerformingTakedown)
	{
		SetComponentTickEnabled(false);
	}
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

	// The paired version when both halves can play and the spot behind him is clear, else as before; every rule
	// (who, when, the noise, the event, the kill) is the same either way.
	float PairedSeconds = 0.f;
	LastVariant = TryPairedTakedown(Target, PairedSeconds) ? EHawkeyeTakedownVariant::Paired : EHawkeyeTakedownVariant::Plain;
	LockSeconds = LastVariant == EHawkeyeTakedownVariant::Paired ? PairedSeconds : TakedownSeconds;
	PairedStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bVictimHandedOver = false;

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
	if (World && LockSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(
			TakedownTimerHandle, this, &UTakedownComponent::EndTakedown, LockSeconds, false);
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
	if (LastVariant == EHawkeyeTakedownVariant::Paired)
	{
		// Cut short (the victim gone): her half stops; at its natural end it has already blended out.
		HawkeyeCombatAnim::Stop(PairedInstance.Get(), PairedMontage, 0.2f);
		bVictimHandedOver = true;
	}
	PairedInstance = nullptr;
	PairedMontage = nullptr;
	PairedVictim = nullptr;
}
