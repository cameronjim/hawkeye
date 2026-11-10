#include "Combat/BowComponent.h"

#include "Camera/CameraComponent.h"
#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "CollisionQueryParams.h"
#include "AIController.h"
#include "Combat/AimAssist.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/ArrowProjectile.h"
#include "Combat/BowDefinition.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/HealthComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/WeaponComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Mission/ChapterEventSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/GrappleComponent.h"
#include "Player/InventoryComponent.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/ThugAIController.h"

namespace HawkeyeBow
{
	static const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* ShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
}

const FName UBowComponent::DrawParameter(TEXT("Draw"));

UBowComponent::UBowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the body has animated, so the bow sits on this frame's hand and spine, not last frame's.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	DefaultProjectileClass = AArrowProjectile::StaticClass();
}

void UBowComponent::BeginPlay()
{
	Super::BeginPlay();

	// Per-owner seed so two archers never scatter identically.
	SpreadStream.Initialize(*GetNameSafe(GetOwner()));

	if (UInventoryComponent* Inventory = FindInventory())
	{
		Inventory->OnInventoryChanged.AddDynamic(this, &UBowComponent::HandleInventoryChanged);
	}
	ApplyHandsIK();
	RefreshBowVisual();
}

void UBowComponent::ApplyHandsIK()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!HandsIKClass || !Body || Body->GetPostProcessAnimBPClassToBeUsed() == HandsIKClass)
	{
		return;
	}
	// Not a full re-initialise: the main AnimBP keeps its state and only the post-process instance
	// is created. The override replaces the mesh asset's own post-process AnimBP; ABP_BowIK_Post
	// runs the sample's one inside itself so nothing is lost.
	Body->SetOverridePostProcessAnimBP(HandsIKClass, /*ReinitAnimInstances=*/false);
	Body->InitializeAnimScriptInstance(/*bForceReinit=*/false);
	UE_LOG(LogHawkeye, Log, TEXT("%s: bow hands post-process %s on %s (instance %s)."), *GetNameSafe(Character),
		*GetNameSafe(HandsIKClass.Get()), *GetNameSafe(Body), *GetNameSafe(Body->GetPostProcessInstance()));
}

UHawkeyeBowIKAnimInstance* UBowComponent::GetHandsIKInstance() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	return Body ? Cast<UHawkeyeBowIKAnimInstance>(Body->GetPostProcessInstance()) : nullptr;
}

FRotator UBowComponent::ComputeHandsAimRotation(const FVector& From) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FRotator::ZeroRotator;
	}
	if (bHasAimOverride && !(AimOverridePoint - From).IsNearlyZero())
	{
		return (AimOverridePoint - From).Rotation();
	}
	const APawn* Pawn = Cast<APawn>(Owner);
	if (Pawn && Pawn->GetController())
	{
		const FRotator View = Pawn->GetControlRotation();
		return FRotator(FRotator::NormalizeAxis(View.Pitch), View.Yaw, 0.f);
	}
	return FRotator(0.f, Owner->GetActorRotation().Yaw, 0.f);
}

void UBowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UInventoryComponent* Inventory = FindInventory())
	{
		Inventory->OnInventoryChanged.RemoveDynamic(this, &UBowComponent::HandleInventoryChanged);
	}
	UHawkeyeAudioSubsystem::StopLoop(DrawLoop, TEXT("bow draw"));
	Super::EndPlay(EndPlayReason);
}

void UBowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bDrawing)
	{
		const float Fraction = GetDrawFraction();
		OnDrawChanged.Broadcast(Fraction);
		if (DrawLoop)
		{
			DrawLoop->SetFloatParameter(DrawParameter, Fraction);
		}
	}
	UpdateBowClip();
	UpdateBowVisual();
}

bool UBowComponent::IsBowClipPlaying() const
{
	return BowClip && HawkeyeCombatAnim::IsPlaying(BowClipInstance.Get(), BowClip);
}

float UBowComponent::GetBowClipWeight() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	const UAnimInstance* Post = Body ? Body->GetPostProcessInstance() : nullptr;
	return Post ? FMath::Clamp(Post->GetSlotMontageGlobalWeight(HawkeyeCombatAnim::UpperBodySlot), 0.f, 1.f) : 0.f;
}

ECombatAnimRole UBowComponent::NextBowClip(ECombatAnimRole Current, bool bDrawing, bool bClipAtEnd)
{
	if (!bClipAtEnd)
	{
		return ECombatAnimRole::None;
	}
	if (bDrawing && Current == ECombatAnimRole::BowDraw)
	{
		// At the anchor: hold the aim until the release (the aim clip loops).
		return ECombatAnimRole::BowAimIdle;
	}
	if (!bDrawing && Current == ECombatAnimRole::BowFire)
	{
		return ECombatAnimRole::BowNock;
	}
	return ECombatAnimRole::None;
}

bool UBowComponent::KeepsBowInHand(bool bRaised, bool bHolsterWhenIdle, float ClipWeight)
{
	return bRaised || !bHolsterWhenIdle || ClipWeight > ClipKeepsBowWeight;
}

bool UBowComponent::PlayBowClip(ECombatAnimRole Role)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	UAnimMontage* Montage = UCombatAnimSet::Resolve(AnimSet, Role);
	if (!Montage || !Body)
	{
		return false;
	}
	// The clip's own pace: the manifest trims it and bakes its rate into the montage.
	UAnimInstance* Instance = HawkeyeCombatAnim::Play(Body, Montage, 1.f);
	if (!Instance)
	{
		return false;
	}
	BowClip = Montage;
	BowClipInstance = Instance;
	BowClipRole = static_cast<uint8>(Role);
	return true;
}

void UBowComponent::StopBowClip(float BlendOutSeconds)
{
	HawkeyeCombatAnim::Stop(BowClipInstance.Get(), BowClip, BlendOutSeconds);
	BowClip = nullptr;
	BowClipInstance = nullptr;
	BowClipRole = 0;
}

void UBowComponent::UpdateBowClip()
{
	if (!BowClip)
	{
		return;
	}
	const ECombatAnimRole Role = static_cast<ECombatAnimRole>(BowClipRole);
	const UAnimInstance* Instance = BowClipInstance.Get();
	// BowDraw holds its last frame (no auto blend-out), so "at its end" is its position, not whether it plays.
	const bool bAtEnd = !IsBowClipPlaying() || !Instance
		|| Instance->Montage_GetPosition(BowClip) >= BowClip->GetPlayLength() - 0.01f;
	const ECombatAnimRole Next = NextBowClip(Role, bDrawing, bAtEnd);
	if (Next == ECombatAnimRole::None)
	{
		return;
	}
	if (!PlayBowClip(Next) && Next == ECombatAnimRole::BowNock)
	{
		// No nock clip: the fire clip blends itself out.
		BowClip = nullptr;
		BowClipRole = 0;
	}
}

const TSoftObjectPtr<UNiagaraSystem>& UBowComponent::GetImpactVfx(EHawkeyeArrowSurface Surface) const
{
	switch (Surface)
	{
	case EHawkeyeArrowSurface::Wood:
		return ImpactWoodVfx;
	case EHawkeyeArrowSurface::Flesh:
		return HitSparkVfx;
	default:
		return ImpactStoneVfx;
	}
}

const TSoftObjectPtr<USoundBase>& UBowComponent::GetImpactSound(EHawkeyeArrowSurface Surface) const
{
	switch (Surface)
	{
	case EHawkeyeArrowSurface::Wood:
		return ImpactWoodSound;
	case EHawkeyeArrowSurface::Flesh:
		return ImpactFleshSound;
	default:
		return ImpactStoneSound;
	}
}

void UBowComponent::PlayPickupSound() const
{
	if (const AActor* Owner = GetOwner())
	{
		UHawkeyeAudioSubsystem::PlayAt(this, PickupSound, Owner->GetActorLocation(), TEXT("pickup"));
	}
}

double UBowComponent::GetNowSeconds() const
{
	if (bUseTestTime)
	{
		return TestTimeOverride;
	}
	const UWorld* World = GetWorld();
	return World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
}

void UBowComponent::SetTestTimeSeconds(double InSeconds)
{
	bUseTestTime = true;
	TestTimeOverride = InSeconds;
}

void UBowComponent::ClearTestTime()
{
	if (!bUseTestTime)
	{
		return;
	}
	const double Left = FollowThroughUntilSeconds - TestTimeOverride;
	bUseTestTime = false;
	FollowThroughUntilSeconds = Left > 0.0 ? GetNowSeconds() + Left : -1.0;
}

UInventoryComponent* UBowComponent::FindInventory() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UInventoryComponent>() : nullptr;
}

UBowDefinition* UBowComponent::GetBow() const
{
	const UInventoryComponent* Inventory = FindInventory();
	return Inventory ? Inventory->GetBow() : OwnBow.Get();
}

float UBowComponent::GetDrawElapsed() const
{
	return bDrawing ? static_cast<float>(FMath::Max(0.0, GetNowSeconds() - DrawStartSeconds)) * FMath::Max(DrawRate, 0.1f) : 0.f;
}

float UBowComponent::GetDrawFraction() const
{
	const UBowDefinition* Bow = GetBow();
	return (bDrawing && Bow) ? Bow->ComputeDrawFraction(GetDrawElapsed()) : 0.f;
}

float UBowComponent::GetCurrentSpreadDegrees() const
{
	const UBowDefinition* Bow = GetBow();
	return Bow ? Bow->ComputeSpread(GetDrawFraction()) : 0.f;
}

bool UBowComponent::IsBowRaised() const
{
	return bDrawing || GetNowSeconds() < FollowThroughUntilSeconds;
}

bool UBowComponent::IsBowInHand() const
{
	return KeepsBowInHand(IsBowRaised(), bHolsterWhenIdle, GetBowClipWeight());
}

bool UBowComponent::IsInPerfectWindow() const
{
	const UBowDefinition* Bow = GetBow();
	return bDrawing && Bow && Bow->IsPerfectRelease(GetDrawElapsed());
}

// --- Draw -------------------------------------------------------------------------------------------

bool UBowComponent::StartDraw()
{
	AActor* Owner = GetOwner();
	if (bDrawing || !GetBow() || !Owner)
	{
		return false;
	}
	if (const UHealthComponent* Health = Owner->FindComponentByClass<UHealthComponent>())
	{
		if (!Health->IsAlive())
		{
			return false;
		}
	}

	bDrawing = true;
	DrawStartSeconds = GetNowSeconds();
	if (AHawkeyeCharacter* Character = Cast<AHawkeyeCharacter>(Owner))
	{
		// Drawing is aiming: camera in over the shoulder, strafing, walking slower.
		Character->NotifyBowDrawStarted();
	}
	OnDrawChanged.Broadcast(0.f);
	// At the clip's own pace, not stretched to the draw time: the draw fraction counts on its own clock.
	PlayBowClip(ECombatAnimRole::BowDraw);
	UpdateBowVisual();
	UHawkeyeAudioSubsystem::StopLoop(DrawLoop, TEXT("bow draw"));
	DrawLoop = UHawkeyeAudioSubsystem::PlayAttached(DrawSound, Owner->GetRootComponent(), TEXT("bow draw"));
	if (DrawLoop)
	{
		DrawLoop->SetFloatParameter(DrawParameter, 0.f);
	}
	return true;
}

void UBowComponent::EndDraw()
{
	bDrawing = false;
	UHawkeyeAudioSubsystem::StopLoop(DrawLoop, TEXT("bow draw"));
	OnDrawChanged.Broadcast(0.f);
	if (AHawkeyeCharacter* Character = Cast<AHawkeyeCharacter>(GetOwner()))
	{
		Character->NotifyBowDrawEnded();
	}
	UpdateBowVisual();
}

void UBowComponent::CancelDraw()
{
	if (!bDrawing)
	{
		return;
	}
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: draw let down at %.0f%%."), *GetNameSafe(GetOwner()), GetDrawFraction() * 100.f);
	EndDraw();
	StopBowClip(ClipLetDownSeconds);
}

bool UBowComponent::ReleaseDraw()
{
	if (!bDrawing)
	{
		return false;
	}

	const UBowDefinition* Bow = GetBow();
	const float Elapsed = GetDrawElapsed();
	const float Fraction = Bow ? Bow->ComputeDrawFraction(Elapsed) : 0.f;
	EndDraw();

	if (!Bow || Bow->IsBelowMinDraw(Fraction))
	{
		// Too early: the string is let down and the arrow stays on it.
		UE_LOG(LogHawkeye, Log, TEXT("%s: released at %.0f%% draw, below %.0f%%; cancelled."), *GetNameSafe(GetOwner()),
			Fraction * 100.f, Bow ? Bow->MinDrawFraction * 100.f : 0.f);
		StopBowClip(ClipLetDownSeconds);
		return false;
	}
	const bool bFired = FireArrow(Elapsed);
	if (bFired && PlayBowClip(ECombatAnimRole::BowFire))
	{
		// A shot on a clip follows through until its fire clip starts to blend out, so the IK never
		// raises the arm again while the clip lets it down.
		FollowThroughUntilSeconds = FMath::Min(FollowThroughUntilSeconds,
			GetNowSeconds() + ComputeClipFollowThrough(BowClip->GetPlayLength(), BowClip->BlendOut.GetBlendTime()));
	}
	else
	{
		StopBowClip(ClipLetDownSeconds);
	}
	return bFired;
}

// --- Firing -----------------------------------------------------------------------------------------

bool UBowComponent::FireGrapple(UArrowDefinition* Arrow)
{
	UGrappleComponent* Grapple = GetOwner() ? GetOwner()->FindComponentByClass<UGrappleComponent>() : nullptr;
	const bool bFired = Grapple && Grapple->TryFire();
	if (bFired)
	{
		OnArrowFired.Broadcast(Arrow);
	}
	else
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: grapple arrow released with no anchor a press would fire at; nothing fired."),
			*GetNameSafe(GetOwner()));
	}
	return bFired;
}

bool UBowComponent::FireArrow(float Elapsed)
{
	UInventoryComponent* Inventory = FindInventory();
	UBowDefinition* Bow = GetBow();
	// No quiver (an AI archer): his own arrow type, never spent.
	UArrowDefinition* Arrow = Inventory ? Inventory->GetActiveArrow() : OwnArrow.Get();
	AActor* Owner = GetOwner();
	if (!Arrow || !Bow || !Owner)
	{
		return false;
	}
	if (Arrow->OnHitEffect == EArrowHitEffect::Grapple)
	{
		return FireGrapple(Arrow);
	}

	const int32 Slot = Inventory ? Inventory->GetActiveArrowSlot() : Arrow->Slot;
	if (Inventory && !Inventory->ConsumeArrow(Slot))
	{
		// TODO(stage3): the empty-quiver click sound.
		UE_LOG(LogHawkeye, Log, TEXT("%s: no %s left in slot %d."), *GetNameSafe(Owner), *GetNameSafe(Arrow), Slot);
		return false;
	}

	const float Fraction = Bow->ComputeDrawFraction(Elapsed);
	const bool bPerfect = Bow->IsPerfectRelease(Elapsed);
	const float Damage = Bow->ComputeReleaseDamage(Arrow->Damage, Fraction, bPerfect);
	const float Spread = Bow->ComputeSpread(Fraction);
	const float Speed = Bow->ComputeSpeed(Fraction);

	const FVector Start = GetArrowSpawnLocation();
	FVector Aim = (ComputeAimPoint() - Start).GetSafeNormal();
	if (Aim.IsNearlyZero())
	{
		Aim = Owner->GetActorForwardVector();
	}
	Aim = ApplyReleaseBend(Start, Aim, Speed);
	const FVector Direction = UWeaponComponent::ApplyConeSpread(Aim, Spread, SpreadStream);

	if (AArrowProjectile* Projectile = SpawnArrowProjectile(Arrow, DefaultProjectileClass, Direction))
	{
		Projectile->InitArrow(Arrow, Bow, Damage, Owner, this);
		Projectile->LaunchWithVelocity(Direction * Speed);
	}
	UHawkeyeAudioSubsystem::PlayAt(this, ReleaseSound, Start, TEXT("bow release"));
	UHawkeyeVfxSubsystem::SpawnAt(this, ReleaseVfx, Start, Direction.Rotation(), UHawkeyeVfxSubsystem::BowReleaseEvent);

	if (bPerfect)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: perfect release (%.2f s, window %.2f-%.2f s): +%.0f%% damage."),
			*GetNameSafe(Owner), Elapsed, Bow->FullDrawSeconds, Bow->FullDrawSeconds + Bow->PerfectWindowSeconds,
			Bow->PerfectBonus * 100.f);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: loosed %s at %.0f%% draw: %.0f cm/s, spread %.2f deg, damage %.1f; %d left."),
		*GetNameSafe(Owner), *GetNameSafe(Arrow), Fraction * 100.f, Speed, Spread, Damage,
		Inventory ? Inventory->GetArrowCount(Slot) : -1);
	++ArrowsLoosed;

	FollowThroughUntilSeconds = GetNowSeconds() + FollowThroughSeconds;
	OnArrowFired.Broadcast(Arrow);
	if (const APawn* Shooter = Cast<APawn>(Owner); Shooter && Shooter->IsPlayerControlled())
	{
		UChapterEventSubsystem::NotifyEvent(this, UHawkeyeChapterEventRules::ArrowLoosed(Arrow));
	}
	return true;
}

FVector UBowComponent::ApplyReleaseBend(const FVector& Start, const FVector& Aim, float Speed) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!bAimAssist || !Pawn || Cast<AAIController>(Pawn->GetController()))
	{
		return Aim;
	}
	const FHawkeyeAimAssistTuning Tuning = UHawkeyeAimAssist::GetTuningFor(this);
	if (Tuning.BendMaxDegrees <= 0.f || Tuning.MagnetismConeDegrees <= 0.f)
	{
		return Aim;
	}
	TArray<FHawkeyeAimAssistCandidate> Candidates;
	UHawkeyeAimAssist::GatherCandidates(GetWorld(), Start, UHawkeyeAimAssist::MaxRange, Pawn, Candidates);
	for (const FHawkeyeAimAssistCandidate& Candidate : Candidates)
	{
		if (UHawkeyeAimAssist::IsLineOnBody(Start, Aim, Candidate))
		{
			// Her own aim is on someone (a head included): the arrow goes where she put it.
			return Aim;
		}
	}
	const int32 Pick = UHawkeyeAimAssist::FindBestIndex(Start, Aim, Candidates, Tuning.MagnetismConeDegrees,
		UHawkeyeAimAssist::MaxRange);
	if (!Candidates.IsValidIndex(Pick))
	{
		return Aim;
	}
	const FHawkeyeAimAssistCandidate& Target = Candidates[Pick];
	const UWorld* World = GetWorld();
	const FVector Lead = AThugAIController::ComputeLeadAimPoint(Start, Target.Chest, Target.Velocity, Speed,
		World ? World->GetGravityZ() : -980.f);
	const FVector Bent = UHawkeyeAimAssist::BendToward(Aim, Lead - Start, Tuning.BendMaxDegrees);
	++BentReleases;
	UE_LOG(LogHawkeye, Log, TEXT("%s: aim assist bends the release %.2f deg toward %s (up to %.1f)."), *GetNameSafe(Pawn),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Aim.GetSafeNormal(), Bent), -1.f, 1.f))),
		*GetNameSafe(Target.Actor.Get()), Tuning.BendMaxDegrees);
	return Bent;
}

AArrowProjectile* UBowComponent::SpawnArrowProjectile(UArrowDefinition* Arrow, TSubclassOf<AArrowProjectile> FallbackClass,
	const FVector& Direction) const
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	TSubclassOf<AArrowProjectile> Class = (Arrow && Arrow->ProjectileClass) ? Arrow->ProjectileClass : FallbackClass;
	if (!World || !Owner || !Class)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Owner;
	Params.Instigator = Cast<APawn>(Owner);
	return World->SpawnActor<AArrowProjectile>(Class, GetArrowSpawnLocation(), Direction.Rotation(), Params);
}

FVector UBowComponent::GetArrowSpawnLocation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	const ACharacter* Character = Cast<ACharacter>(Owner);
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	const UBowDefinition* Bow = GetBow();
	if (Body && Bow && !Bow->HandSocket.IsNone() && Body->DoesSocketExist(Bow->HandSocket))
	{
		return Body->GetSocketLocation(Bow->HandSocket);
	}
	return Owner->GetActorTransform().TransformPosition(ArrowLaunchOffset);
}

FVector UBowComponent::ComputeAimPoint() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return FVector::ZeroVector;
	}

	if (bHasAimOverride)
	{
		return AimOverridePoint;
	}

	FVector ViewLocation;
	FVector ViewForward;
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Owner);
	if (const UCameraComponent* Camera = Hawkeye ? Hawkeye->GetFollowCamera() : nullptr)
	{
		ViewLocation = Camera->GetComponentLocation();
		ViewForward = Camera->GetForwardVector();
	}
	else
	{
		FRotator ViewRotation;
		Owner->GetActorEyesViewPoint(ViewLocation, ViewRotation);
		ViewForward = ViewRotation.Vector();
	}

	// Start level with the character along the view, so nothing between the lens and her back
	// (a railing the arm pulled in past) becomes the target.
	const float ToCharacter = FMath::Max(0.f, FVector::DotProduct(Owner->GetActorLocation() - ViewLocation, ViewForward));
	const FVector Start = ViewLocation + ViewForward * ToCharacter;
	const FVector End = ViewLocation + ViewForward * AimTraceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeBowAim), /*bTraceComplex=*/false, Owner);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_HawkeyeWeapon, Params))
	{
		return Hit.ImpactPoint;
	}
	return End;
}

void UBowComponent::SetAimOverride(const FVector& Point)
{
	bHasAimOverride = true;
	AimOverridePoint = Point;
}

void UBowComponent::NotifyArrowHit(AActor* HitActor, float Damage, bool bHeadshot)
{
	OnHit.Broadcast(HitActor, Damage, bHeadshot);
}

void UBowComponent::HandleInventoryChanged()
{
	if (bDrawing && !GetBow())
	{
		CancelDraw();
	}
	RefreshBowVisual();
}

// --- Visual -----------------------------------------------------------------------------------------

void UBowComponent::RefreshBowVisual()
{
	UBowDefinition* Bow = GetBow();
	if (Bow == VisualBow && (BowMesh != nullptr) == (Bow != nullptr))
	{
		return;
	}

	for (UStaticMeshComponent* Existing : { BowMesh.Get(), StringUpper.Get(), StringLower.Get(), NockedShaft.Get(), NockedNock.Get() })
	{
		if (Existing)
		{
			Existing->DestroyComponent();
		}
	}
	BowMesh = nullptr;
	StringUpper = nullptr;
	StringLower = nullptr;
	NockedShaft = nullptr;
	NockedNock = nullptr;
	VisualBow = Bow;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!Bow || !Body || !GetWorld())
	{
		return;
	}

	auto MakePart = [Character, Body](USceneComponent* Parent, FName Socket, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Character);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetupAttachment(Parent ? Parent : Body, Socket);
		Part->RegisterComponent();
		return Part;
	};

	// Soft: the definition does not pull its mesh into memory until a bow is actually carried.
	BowMesh = MakePart(Body, HolsterBone, Bow->BowMesh.IsNull() ? nullptr : Bow->BowMesh.LoadSynchronous());
	BowMesh->SetCastShadow(true);

	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, HawkeyeBow::CylinderPath);
	StringUpper = MakePart(BowMesh, NAME_None, Cylinder);
	StringLower = MakePart(BowMesh, NAME_None, Cylinder);
	UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, HawkeyeBow::ShapeMaterialPath);
	for (UStaticMeshComponent* String : { StringUpper.Get(), StringLower.Get() })
	{
		String->SetCastShadow(false);
		if (ShapeMaterial)
		{
			String->SetMaterial(0, ShapeMaterial);
			if (UMaterialInstanceDynamic* Tint = String->CreateDynamicMaterialInstance(0))
			{
				Tint->SetVectorParameterValue(TEXT("Color"), StringColor);
			}
		}
	}

	// The arrow on the string: the same pale shaft and purple nock as a loosed one.
	NockedShaft = MakePart(BowMesh, NAME_None, Cylinder);
	NockedNock = MakePart(BowMesh, NAME_None, Cylinder);
	// An archer's own arrow type may carry its own colours (Trickshot's black shafts).
	const bool bOwnColors = !FindInventory() && OwnArrow && OwnArrow->bOverrideColors;
	const TPair<UStaticMeshComponent*, FLinearColor> ArrowParts[] = {
		{ NockedShaft.Get(), bOwnColors ? OwnArrow->ShaftColor : FLinearColor(0.6f, 0.55f, 0.45f) },
		{ NockedNock.Get(), bOwnColors ? OwnArrow->NockColor : FLinearColor(0.45f, 0.1f, 0.75f) },
	};
	for (const TPair<UStaticMeshComponent*, FLinearColor>& Part : ArrowParts)
	{
		Part.Key->SetCastShadow(false);
		Part.Key->SetVisibility(false);
		UMaterialInterface* PartMaterial = ShapeMaterial;
		if (Part.Key == NockedNock.Get())
		{
			// The glowing nock loosed arrows wear, when it has been built.
			const TSoftObjectPtr<UMaterialInterface> Glow{ FSoftObjectPath(AArrowProjectile::DefaultNockMaterialPath) };
			if (UMaterialInterface* Loaded = Glow.LoadSynchronous())
			{
				PartMaterial = Loaded;
			}
		}
		if (PartMaterial)
		{
			Part.Key->SetMaterial(0, PartMaterial);
			if (UMaterialInstanceDynamic* Tint = Part.Key->CreateDynamicMaterialInstance(0))
			{
				Tint->SetVectorParameterValue(TEXT("Color"), Part.Value);
			}
		}
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s: bow %s shown (mesh %s) on %s."), *GetNameSafe(Character), *GetNameSafe(Bow),
		*GetNameSafe(BowMesh->GetStaticMesh()), *HolsterBone.ToString());
	UpdateBowVisual();
}

void UBowComponent::PlaceString(UStaticMeshComponent* String, const FVector& Tip, const FVector& Nock) const
{
	if (!String)
	{
		return;
	}
	// The engine cylinder is 100 cm along Z about its centre.
	const FVector Span = Nock - Tip;
	const float Length = FMath::Max(Span.Size(), 0.1f);
	String->SetWorldLocationAndRotation((Tip + Nock) * 0.5f, FRotationMatrix::MakeFromZ(Span).Rotator());
	String->SetWorldScale3D(FVector(StringThickness / 100.f, StringThickness / 100.f, Length / 100.f));
}

FName UBowComponent::FindGripSocket(const USkeletalMeshComponent& Body, bool bWithHandsIK) const
{
	const UBowDefinition* Bow = GetBow();
	if (Bow && !Bow->HandSocket.IsNone() && Body.DoesSocketExist(Bow->HandSocket))
	{
		return Bow->HandSocket;
	}
	// An archer's bow names no socket (his clip-driven hand used to hang at his hip); with hands IK
	// the hand is where the bow should be, so the plain hand bone does.
	if (bWithHandsIK && Body.DoesSocketExist(HandsIK.GripFallbackBone))
	{
		return HandsIK.GripFallbackBone;
	}
	return HolsterBone;
}

void UBowComponent::PlaceBowWithHandsIK(const USkeletalMeshComponent& Body, const UHawkeyeBowIKAnimInstance& Hands,
	FName Grip, FVector& OutLocation, FQuat& OutRotation) const
{
	// The hand is already on the grip (IK ran this frame, before this tick); the bow turns from
	// hanging in the palm to the aim as the bow hand blends up.
	const FVector Hand = Body.GetSocketLocation(Grip);
	const FVector Anchor = Body.DoesSocketExist(HandsIK.AnchorBone) ? Body.GetSocketLocation(HandsIK.AnchorBone) : Hand;
	const FRotator Aim = ComputeHandsAimRotation(Anchor);
	const FQuat Resting = FRotator(RestPitchDegrees, GetOwner()->GetActorRotation().Yaw, DrawnCantDegrees).Quaternion();
	const FQuat Raised = FRotator(Aim.Pitch, Aim.Yaw, DrawnCantDegrees).Quaternion();
	OutRotation = FQuat::Slerp(Resting, Raised, FMath::SmoothStep(0.f, 1.f, Hands.GetBowAlpha()));
	OutLocation = Hand + OutRotation.RotateVector(HandGripOffset);
}

void UBowComponent::PlaceBowHeldOut(const USkeletalMeshComponent& Body, FName Grip, FVector& OutLocation,
	FQuat& OutRotation) const
{
	// Up from the hand and round to the aim in the first part of the draw, then held there while
	// the string comes back, and for the follow-through after the shot.
	const ACharacter* Character = CastChecked<ACharacter>(GetOwner());
	const FRotator Aim = Character->GetControlRotation();
	const float Draw = GetDrawFraction();
	const bool bRaised = IsBowRaised();
	const float Raise = !bRaised ? 0.f
		: bDrawing ? FMath::SmoothStep(0.f, 1.f, FMath::Min(1.f, Draw / RaiseByDrawFraction)) : 1.f;
	const FQuat Lowered = bRaised ? FRotator(LoweredPitchDegrees, Aim.Yaw, 0.f).Quaternion()
		: FRotator(RestPitchDegrees, Character->GetActorRotation().Yaw, DrawnCantDegrees).Quaternion();
	const FQuat Raised = FRotator(Aim.Pitch, Aim.Yaw, DrawnCantDegrees).Quaternion();
	OutRotation = FQuat::Slerp(Lowered, Raised, Raise);
	const FVector Hand = Body.GetSocketLocation(Grip) + OutRotation.RotateVector(HandGripOffset);
	const FVector Held = Body.GetSocketLocation(DrawShoulderBone) + Raised.RotateVector(DrawnGripOffset);
	OutLocation = FMath::Lerp(Hand, Held, Raise);
}

void UBowComponent::UpdateBowVisual()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (!BowMesh || !Body || !GetBow())
	{
		return;
	}

	const UHawkeyeBowIKAnimInstance* Hands = GetHandsIKInstance();
	const bool bInHand = IsBowInHand();
	const FName Socket = bInHand ? FindGripSocket(*Body, Hands != nullptr) : HolsterBone;
	if (BowMesh->GetAttachSocketName() != Socket)
	{
		BowMesh->AttachToComponent(Body, FAttachmentTransformRules::KeepWorldTransform, Socket);
	}

	// Positions come from the bones; orientation from the actor and the aim, so the bow never
	// depends on how a bone's axes happen to be authored.
	FVector Location;
	FQuat Rotation;
	if (bInHand && Socket != HolsterBone && Hands)
	{
		PlaceBowWithHandsIK(*Body, *Hands, Socket, Location, Rotation);
	}
	else if (bInHand)
	{
		PlaceBowHeldOut(*Body, Socket, Location, Rotation);
	}
	else
	{
		// Flat across the back: the bow's X (where an arrow would go) to the actor's right, then
		// tipped HolsterRollDegrees about the actor's forward axis so it lies diagonally.
		const FRotator ActorRotation = Character->GetActorRotation();
		Rotation = ActorRotation.Quaternion()
			* FQuat(FVector::ForwardVector, FMath::DegreesToRadians(HolsterRollDegrees))
			* FRotator(0.f, 90.f, 0.f).Quaternion();
		Location = Body->GetSocketLocation(HolsterBone) + ActorRotation.RotateVector(HolsterOffset);
	}
	BowMesh->SetWorldLocationAndRotation(Location, Rotation);
	PlaceStringAndArrow(*Body, Hands);

	// The body hides from its own camera when a wall pulls the lens in; the bow goes with it.
	const bool bHidden = Body->bOwnerNoSee;
	for (UStaticMeshComponent* Part : { BowMesh.Get(), StringUpper.Get(), StringLower.Get(), NockedShaft.Get(), NockedNock.Get() })
	{
		if (Part && Part->bOwnerNoSee != bHidden)
		{
			Part->SetOwnerNoSee(bHidden);
		}
	}
}

void UBowComponent::PlaceStringAndArrow(const USkeletalMeshComponent& Body, const UHawkeyeBowIKAnimInstance* Hands)
{
	const FTransform BowTransform = BowMesh->GetComponentTransform();
	FVector Nock = BowTransform.TransformPosition(FVector(StringTip.X - StringPullAtFullDraw * GetDrawFraction(), 0.f, 0.f));
	FVector Along = BowTransform.GetUnitAxis(EAxis::X);
	const float StringHand = Hands && IsBowInHand() ? Hands->GetDrawAlpha() : 0.f;
	if (StringHand > 0.f)
	{
		// The string goes where the string hand is, so the fingers are always on it; the arrow runs
		// from there forward through the grip.
		const FName Socket = Body.DoesSocketExist(HandsIK.StringHandSocket) ? HandsIK.StringHandSocket : FName(TEXT("hand_r"));
		if (Body.DoesSocketExist(Socket))
		{
			Nock = FMath::Lerp(Nock, Body.GetSocketLocation(Socket), StringHand);
			const FVector ToGrip = BowTransform.GetLocation() - Nock;
			Along = ToGrip.SizeSquared() > 1.f ? ToGrip.GetSafeNormal() : Along;
		}
	}
	PlaceString(StringUpper, BowTransform.TransformPosition(StringTip), Nock);
	PlaceString(StringLower, BowTransform.TransformPosition(FVector(StringTip.X, StringTip.Y, -StringTip.Z)), Nock);

	// The arrow on the string, from the nock forward through the grip, while drawing.
	const bool bShowArrow = bDrawing && NockedShaft && NockedNock;
	if (bShowArrow)
	{
		NockedArrowTip = Nock + Along * NockedArrowLength;
		PlaceString(NockedShaft, Nock, Nock + Along * NockedArrowLength);
		NockedShaft->SetWorldScale3D(FVector(0.015f, 0.015f, NockedArrowLength / 100.f));
		PlaceString(NockedNock, Nock - Along * 1.f, Nock + Along * 4.f);
		NockedNock->SetWorldScale3D(FVector(0.025f, 0.025f, 0.05f));
	}
	for (UStaticMeshComponent* Part : { NockedShaft.Get(), NockedNock.Get() })
	{
		if (Part && Part->IsVisible() != bShowArrow)
		{
			Part->SetVisibility(bShowArrow);
		}
	}
}
