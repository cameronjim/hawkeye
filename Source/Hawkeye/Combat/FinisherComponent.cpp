#include "Combat/FinisherComponent.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeRules.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Kismet/GameplayStatics.h"
#include "World/ThugCharacter.h"

UFinisherComponent::UFinisherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UFinisherComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceFinisher(DeltaTime);
}

void UFinisherComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bActive)
	{
		EndFinisher();
	}
	Super::EndPlay(EndPlayReason);
}

AThugCharacter* UFinisherComponent::FindTarget() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}
	AThugCharacter* Best = nullptr;
	float BestDistance = BIG_NUMBER;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		if (!UHawkeyeMeleeRules::IsFinisherTarget(*It, Owner->GetActorLocation(), Range))
		{
			continue;
		}
		const float Distance = FVector::Dist2D(It->GetActorLocation(), Owner->GetActorLocation());
		if (Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool UFinisherComponent::TryFinisher(bool bBowOut)
{
	if (bActive)
	{
		return false;
	}
	AThugCharacter* Target = FindTarget();
	return Target && StartFinisher(Target, bBowOut ? EHawkeyeFinisherStyle::Bow : EHawkeyeFinisherStyle::Strike);
}

bool UFinisherComponent::StartFinisher(AThugCharacter* Target, EHawkeyeFinisherStyle InStyle)
{
	AActor* Owner = GetOwner();
	if (bActive || !Owner || !UHawkeyeMeleeRules::IsFinisherTarget(Target, Owner->GetActorLocation(), Range))
	{
		return false;
	}
	bActive = true;
	bStruck = false;
	bSlowing = false;
	bSlowDone = false;
	Elapsed = 0.f;
	Style = InStyle;
	Victim = Target;
	++FinisherCount;

	// He stays where he is until the blow: whatever stagger he had is stretched to cover the lunge.
	Target->HoldStagger(StrikeAtSeconds + 0.1f);

	UHealthComponent* Health = Owner->FindComponentByClass<UHealthComponent>();
	bOwnsInvulnerability = Health && !Health->IsInvulnerable();
	if (bOwnsInvulnerability)
	{
		Health->SetInvulnerable(true);
	}

	const FVector To = (Target->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
	if (!To.IsNearlyZero())
	{
		Owner->SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}
	Lunge(*Target);

	UE_LOG(LogHawkeye, Log, TEXT("%s: %s finisher on %s from %.0f cm."), *GetNameSafe(Owner),
		Style == EHawkeyeFinisherStyle::Bow ? TEXT("bow") : TEXT("strike"), *GetNameSafe(Target),
		FVector::Dist2D(Target->GetActorLocation(), Owner->GetActorLocation()));
	OnFinisherStarted.Broadcast(Target, Style);

	// A zero-length lead-in strikes at once.
	AdvanceFinisher(0.f);
	return true;
}

void UFinisherComponent::AdvanceFinisher(float DeltaSeconds)
{
	if (!bActive)
	{
		return;
	}
	const float Step = FMath::Max(DeltaSeconds, 0.f);
	Elapsed += Step;

	if (bSlowing)
	{
		// The step is dilated game time; the slow motion is measured in real time.
		SlowRealRemaining -= Step / FMath::Max(SlowDilation, 0.05f);
		if (SlowRealRemaining <= 0.f)
		{
			EndSlow();
		}
	}
	else if (!bSlowDone && Elapsed >= SlowAtSeconds)
	{
		BeginSlow();
	}
	if (!bStruck && Elapsed >= StrikeAtSeconds)
	{
		Strike();
	}
	if (Elapsed >= DurationSeconds)
	{
		EndFinisher();
	}
}

float UFinisherComponent::GetCameraPushAlpha() const
{
	if (!bActive)
	{
		return 0.f;
	}
	const float In = FMath::Clamp(Elapsed / CameraInSeconds, 0.f, 1.f);
	const float Out = FMath::Clamp((DurationSeconds - Elapsed) / CameraOutSeconds, 0.f, 1.f);
	return FMath::SmoothStep(0.f, 1.f, FMath::Min(In, Out));
}

FVector UFinisherComponent::ComputeImpulse(const FVector& Forward, EHawkeyeFinisherStyle InStyle, float Speed, float Lift)
{
	const FVector Facing = Forward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : Forward.GetSafeNormal2D();
	// The sweep goes left to right across her, so the bow throws him out to her right.
	const FVector Along = InStyle == EHawkeyeFinisherStyle::Bow ? FVector::CrossProduct(FVector::UpVector, Facing) : Facing;
	return Along * Speed + FVector(0.f, 0.f, Lift);
}

void UFinisherComponent::Lunge(const AActor& Target) const
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return;
	}
	const FVector To = Target.GetActorLocation() - Character->GetActorLocation();
	const float Distance = FMath::Max(0.f, To.Size2D() - StandOffDistance);
	if (Distance <= 1.f)
	{
		return;
	}
	TSharedPtr<FRootMotionSource_ConstantForce> Step = MakeShared<FRootMotionSource_ConstantForce>();
	Step->InstanceName = FName(TEXT("FinisherLunge"));
	Step->AccumulateMode = ERootMotionAccumulateMode::Override;
	Step->Priority = 7;
	Step->Force = To.GetSafeNormal2D() * (Distance / LungeSeconds);
	Step->Duration = LungeSeconds;
	Step->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Step->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Movement->ApplyRootMotionSource(Step);
}

void UFinisherComponent::Strike()
{
	bStruck = true;
	AActor* Owner = GetOwner();
	AThugCharacter* Target = Victim.Get();
	if (!Owner || !Target)
	{
		return;
	}
	const FVector Impulse = ComputeImpulse(Owner->GetActorForwardVector(), Style, ImpulseSpeed, ImpulseLift);
	const bool bKilled = Target->ReceiveFinisher(Owner, Impulse);
	UHawkeyeAudioSubsystem::PlayAt(this, StrikeSound, Target->GetActorLocation(), TEXT("finisher"));
	UE_LOG(LogHawkeye, Log, TEXT("%s: finisher lands on %s (%s), thrown %s."), *GetNameSafe(Owner), *GetNameSafe(Target),
		bKilled ? TEXT("killed") : TEXT("already down"), *Impulse.ToCompactString());
	OnFinisherStruck.Broadcast(Target);
}

void UFinisherComponent::BeginSlow()
{
	UWorld* World = GetWorld();
	if (!World || SlowRealSeconds <= 0.f)
	{
		bSlowDone = true;
		return;
	}
	bSlowing = true;
	SlowRealRemaining = SlowRealSeconds;
	UGameplayStatics::SetGlobalTimeDilation(World, SlowDilation);
}

void UFinisherComponent::EndSlow()
{
	if (!bSlowing)
	{
		return;
	}
	bSlowing = false;
	bSlowDone = true;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
	}
}

void UFinisherComponent::EndFinisher()
{
	EndSlow();
	bActive = false;
	if (bOwnsInvulnerability)
	{
		bOwnsInvulnerability = false;
		if (UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr)
		{
			Health->SetInvulnerable(false);
		}
	}
	Victim = nullptr;
}
