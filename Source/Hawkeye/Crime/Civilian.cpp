#include "Crime/Civilian.h"

#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Hawkeye.h"
#include "NavigationSystem.h"

namespace HawkeyeCivilian
{
	/** The thanks line floats this far above his capsule's centre, cm. */
	static constexpr float ThanksHeight = 125.f;
	/** He counts as there once within this of where he ran to, cm. */
	static constexpr float ArrivedRadius = 150.f;
}

ACivilian::ACivilian()
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	ThanksText = NSLOCTEXT("Hawkeye", "CivilianThanks", "[thank you]");
	GetCapsuleComponent()->InitCapsuleSize(34.f, 96.f);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = 150.f;
		Movement->bOrientRotationToMovement = true;
		Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
	}
	bUseControllerRotationYaw = false;

	ThanksLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ThanksLabel"));
	ThanksLabel->SetupAttachment(GetCapsuleComponent());
	ThanksLabel->SetRelativeLocation(FVector(0.f, 0.f, HawkeyeCivilian::ThanksHeight));
	ThanksLabel->SetHorizontalAlignment(EHTA_Center);
	ThanksLabel->SetVerticalAlignment(EVRTA_TextCenter);
	ThanksLabel->SetWorldSize(24.f);
	ThanksLabel->SetTextRenderColor(FColor(255, 240, 210));
	ThanksLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThanksLabel->SetHiddenInGame(true);
}

void ACivilian::BeginPlay()
{
	Super::BeginPlay();
	if (const USkeletalMeshComponent* Body = GetMesh())
	{
		MeshMount = Body->GetRelativeTransform();
	}
	ThanksLabel->SetText(ThanksText);
	UpdateAnimation();
}

void ACivilian::SetCowering(bool bInCowering)
{
	if (bCowering == bInCowering)
	{
		return;
	}
	bCowering = bInCowering;
	UpdatePose();
}

void ACivilian::TakeHit(AActor* By)
{
	if (bFreed)
	{
		return;
	}
	++HitsTaken;
	FlinchRemaining = FlinchSeconds;
	UE_LOG(LogHawkeye, Log, TEXT("%s: hit by %s (%d)."), *GetName(), *GetNameSafe(By), HitsTaken);
}

void ACivilian::Free(AActor* Rescuer)
{
	if (bFreed)
	{
		return;
	}
	bFreed = true;
	SetCowering(false);
	ThanksLabel->SetText(ThanksText);
	ThanksLabel->SetHiddenInGame(false);
	ThanksRemaining = ThanksSeconds;

	// Away from whoever freed him (or on the way he faces), onto the navmesh where there is one.
	FVector Away = Rescuer ? (GetActorLocation() - Rescuer->GetActorLocation()).GetSafeNormal2D() : GetActorForwardVector();
	if (Away.IsNearlyZero())
	{
		Away = GetActorForwardVector();
	}
	FleeGoal = GetActorLocation() + Away * FleeDistance;
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation OnNav;
		if (Nav->ProjectPointToNavigation(FleeGoal, OnNav, FVector(400.f, 400.f, 400.f)))
		{
			FleeGoal = OnNav.Location;
		}
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = FleeSpeed;
	}
	if (AAIController* Brain = Cast<AAIController>(GetController()))
	{
		Brain->MoveToLocation(FleeGoal, HawkeyeCivilian::ArrivedRadius, /*bStopOnOverlap=*/true, /*bUsePathfinding=*/true,
			/*bProjectDestinationToNavigation=*/false, /*bCanStrafe=*/false);
		bFleeing = true;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: freed by %s after %d hit(s); running to %s."), *GetName(), *GetNameSafe(Rescuer), HitsTaken,
		*FleeGoal.ToCompactString());
	UpdatePose();
}

void ACivilian::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FlinchRemaining = FMath::Max(0.f, FlinchRemaining - DeltaSeconds);
	if (ThanksRemaining > 0.f)
	{
		ThanksRemaining = FMath::Max(0.f, ThanksRemaining - DeltaSeconds);
		ThanksLabel->SetHiddenInGame(ThanksRemaining <= 0.f);
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - ThanksLabel->GetComponentLocation();
			ThanksLabel->SetWorldRotation(FRotator(0.f, ToCamera.Rotation().Yaw, 0.f));
		}
	}
	if (bFleeing && FVector::Dist2D(GetActorLocation(), FleeGoal) <= HawkeyeCivilian::ArrivedRadius)
	{
		bFleeing = false;
	}
	UpdatePose();
	UpdateAnimation();
}

void ACivilian::UpdatePose()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
	{
		return;
	}
	const float Flinch = FlinchSeconds > 0.f ? FlinchRemaining / FlinchSeconds : 0.f;
	const float Lean = (bCowering ? CowerLeanDegrees : 0.f) + Flinch * FlinchDegrees;
	const float Drop = bCowering ? CowerDrop : 0.f;
	// Pitch the mount forward in the capsule's frame: nose (and head) down.
	const FQuat Hunch = FRotator(-Lean, 0.f, 0.f).Quaternion();
	Body->SetRelativeLocationAndRotation(MeshMount.GetLocation() - FVector(0.f, 0.f, Drop),
		Hunch * MeshMount.GetRotation());
}

void ACivilian::UpdateAnimation()
{
	USkeletalMeshComponent* Body = GetMesh();
	UAnimSequence* Wanted = bFleeing && GetVelocity().Size2D() > 50.f && RunAnim ? RunAnim.Get() : IdleAnim.Get();
	if (!Body || !Wanted || Wanted == CurrentAnim)
	{
		return;
	}
	CurrentAnim = Wanted;
	Body->PlayAnimation(Wanted, /*bLooping=*/true);
}
