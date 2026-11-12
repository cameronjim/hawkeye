#include "World/LuckyCharacter.h"

#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Hawkeye.h"
#include "Kismet/GameplayStatics.h"

namespace HawkeyeLucky
{
	/** Below this ground speed (cm/s) Kate counts as standing still. */
	static constexpr float StillSpeed = 15.f;
	/** How often a following Lucky asks the navmesh again (s): she keeps moving. */
	static constexpr float RepathInterval = 0.5f;
	/** The slowest the walk and trot play while he speeds up or slows down. */
	static constexpr float MinMovingRate = 0.35f;
}

ALuckyCharacter::ALuckyCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;

	// A retriever about 58 cm at the shoulder and 95 cm long: a short capsule, the body at its bottom.
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(28.f, 36.f);
	// The world holds him up; nothing else touches him (Kate, thugs, arrows, the camera, sight lines).
	Capsule->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Capsule->SetCanEverAffectNavigation(false);

	USkeletalMeshComponent* Body = GetMesh();
	Body->SetRelativeLocation(FVector(0.f, 0.f, -36.f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->SetCanEverAffectNavigation(false);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 240.f, 0.f);
	Movement->MaxWalkSpeed = TrotClipSpeed * TrotRate;
	Movement->MaxAcceleration = 300.f;
	Movement->BrakingDecelerationWalking = 300.f;
	bUseControllerRotationYaw = false;

	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	BodyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Lucky/SK_Lucky.SK_Lucky")));
	IdleClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Lucky/A_Lucky_Idle.A_Lucky_Idle")));
	WalkClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Lucky/A_Lucky_Walk.A_Lucky_Walk")));
	TrotClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Lucky/A_Lucky_Trot.A_Lucky_Trot")));
	SitClip = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Characters/Lucky/A_Lucky_Sit.A_Lucky_Sit")));
}

void ALuckyCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Loaded here, synchronously: a soft reference is never assumed resident in the standalone game.
	USkeletalMesh* LoadedMesh = BodyMesh.LoadSynchronous();
	IdleClip.LoadSynchronous();
	WalkClip.LoadSynchronous();
	TrotClip.LoadSynchronous();
	SitClip.LoadSynchronous();
	bHasBody = LoadedMesh != nullptr;
	if (!bHasBody)
	{
		UE_LOG(LogHawkeye, Display,
			TEXT("%s: no %s on this clone (import_bodies.py makes it from the asset packs); Lucky is hidden."),
			*GetName(), *BodyMesh.ToString());
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		SetActorTickEnabled(false);
		return;
	}
	GetMesh()->SetSkeletalMeshAsset(LoadedMesh);
	State = ELuckyState::Sit; // anything but Idle, so the next line plays the clip
	SetLuckyState(ELuckyState::Idle);
}

void ALuckyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bThinking)
	{
		UpdateClipRate();
		return;
	}
	APawn* Kate = FindKate();
	if (!Kate)
	{
		return;
	}
	UpdateStillTime(Kate, DeltaSeconds);
	const FVector Gap = Kate->GetActorLocation() - GetActorLocation();
	const ELuckyState Next = FLuckyRules::ChooseState(Follow, State, Gap.Size2D(), Gap.Z, KateStillSeconds);
	SetLuckyState(Next);
	ApplyMovement(Kate);
	UpdateClipRate();
}

void ALuckyCharacter::SetThinkingEnabled(bool bEnabled)
{
	bThinking = bEnabled;
	if (!bEnabled)
	{
		if (AAIController* Brain = Cast<AAIController>(GetController()))
		{
			Brain->StopMovement();
		}
		GetCharacterMovement()->StopMovementImmediately();
	}
}

void ALuckyCharacter::SetLuckyState(ELuckyState NewState)
{
	const bool bChanged = NewState != State;
	State = NewState;
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(SpeedFor(State), 1.f);
	UAnimSequence* Clip = ClipFor(State);
	if (!bHasBody || !Clip || (!bChanged && PlayingClip == Clip))
	{
		return;
	}
	PlayingClip = Clip;
	// Every clip loops: Sit is a seated idle (he drops straight into it), not a sit-down.
	GetMesh()->PlayAnimation(Clip, /*bLooping=*/true);
	RepathSeconds = 0.f;
}

float ALuckyCharacter::SpeedFor(ELuckyState InState) const
{
	switch (InState)
	{
		case ELuckyState::Walk:
			return WalkClipSpeed * WalkRate;
		case ELuckyState::Trot:
			return TrotClipSpeed * TrotRate;
		default:
			return 0.f;
	}
}

APawn* ALuckyCharacter::FindKate() const
{
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

void ALuckyCharacter::UpdateStillTime(const APawn* Kate, float DeltaSeconds)
{
	KateStillSeconds = Kate->GetVelocity().Size2D() < HawkeyeLucky::StillSpeed ? KateStillSeconds + DeltaSeconds : 0.f;
}

void ALuckyCharacter::ApplyMovement(APawn* Kate)
{
	AAIController* Brain = Cast<AAIController>(GetController());
	if (!Brain)
	{
		return;
	}
	if (!FLuckyRules::IsMoving(State))
	{
		if (Brain->GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			Brain->StopMovement();
		}
		return;
	}
	RepathSeconds -= GetWorld()->GetDeltaSeconds();
	if (RepathSeconds > 0.f && Brain->GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		return;
	}
	RepathSeconds = HawkeyeLucky::RepathInterval;
	// Stops a little short of StopDistance so the rules see him arrive.
	Brain->MoveToActor(Kate, Follow.StopDistance * 0.8f, /*bStopOnOverlap=*/true, /*bUsePathfinding=*/true,
		/*bCanStrafe=*/false, nullptr, /*bAllowPartialPath=*/true);
}

void ALuckyCharacter::UpdateClipRate()
{
	if (!bHasBody)
	{
		return;
	}
	float Rate = 1.f;
	const float Speed = GetVelocity().Size2D();
	if (State == ELuckyState::Walk)
	{
		Rate = FLuckyRules::PlayRateFor(Speed, WalkClipSpeed, HawkeyeLucky::MinMovingRate, WalkRate);
	}
	else if (State == ELuckyState::Trot)
	{
		Rate = FLuckyRules::PlayRateFor(Speed, TrotClipSpeed, HawkeyeLucky::MinMovingRate, TrotRate);
	}
	GetMesh()->SetPlayRate(Rate);
}

UAnimSequence* ALuckyCharacter::ClipFor(ELuckyState InState) const
{
	switch (InState)
	{
		case ELuckyState::Walk:
			return WalkClip.Get();
		case ELuckyState::Trot:
			return TrotClip.Get();
		case ELuckyState::Sit:
			return SitClip.Get() ? SitClip.Get() : IdleClip.Get();
		default:
			return IdleClip.Get();
	}
}
