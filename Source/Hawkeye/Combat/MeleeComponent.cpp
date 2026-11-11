#include "Combat/MeleeComponent.h"

#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/AnimNotifyState_ComboWindow.h"
#include "Combat/AnimNotifyState_HitWindow.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/HealthComponent.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "MotionWarpingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "World/ThugCharacter.h"

UMeleeComponent::UMeleeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMeleeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceAttack(DeltaTime);
}

bool UMeleeComponent::CanStartAttack() const
{
	return Phase == EMeleePhase::Idle || (bComboWindowOpen && Phase == EMeleePhase::Recover);
}

bool UMeleeComponent::StartAttack(const FHawkeyeMeleeAttack& Attack)
{
	if (!CanStartAttack())
	{
		return false;
	}

	// Chaining out of a combo window: the old clip's windows are forgotten, and the clip itself is cut
	// by the new one (or blended out below if this swing has none).
	UAnimMontage* OldMontage = CurrentMontage;
	const TWeakObjectPtr<UAnimInstance> OldInstance = MontageInstance;
	ResetClipState();
	++SwingSerial;

	CurrentAttack = Attack;
	SwingDirection = PendingDirection;
	PendingDirection = FVector::ZeroVector;
	SwingTarget = PendingTarget;
	PendingTarget = nullptr;
	ContactPlan = FHawkeyeContactPlan();
	bContactWhiff = false;
	bApproachDone = false;
	bLimbSweep = false;
	LastLimbPoints.Reset();
	Phase = EMeleePhase::Windup;
	PhaseRemaining = Attack.WindupSeconds;
	SwingStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	UE_LOG(LogHawkeye, Log, TEXT("%s: %s swing winds up (%.2f s, %.0f damage)."),
		*GetNameSafe(GetOwner()), *Attack.Name.ToString(), Attack.WindupSeconds, Attack.Damage);
	if (Attack.WindupSeconds >= TelegraphMinWindup && GetOwner())
	{
		UHawkeyeAudioSubsystem::PlayAt(this, WindupSound, GetOwner()->GetActorLocation(), TEXT("melee telegraph"));
	}

	OnAttackWindup.Broadcast(Attack.Name, Attack.WindupSeconds);
	LastLungeDistance = 0.f;
	float HitDelaySeconds = Attack.WindupSeconds;
	const bool bClip = TryPlaySwingClip(Attack, HitDelaySeconds);
	if (!bClip && OldMontage)
	{
		HawkeyeCombatAnim::Stop(OldInstance.Get(), OldMontage, 0.15f);
	}
	if (!bClipMovesOwner)
	{
		ApplyLunge(Attack, HitDelaySeconds);
	}

	// A zero wind-up lands on the frame it starts, not a tick later.
	if (!bHitFromNotify && PhaseRemaining <= 0.f)
	{
		AdvanceAttack(0.f);
	}
	return true;
}

UAnimMontage* UMeleeComponent::PickSwingVariant(const FHawkeyeMeleeAttack& Attack)
{
	PickedMontage = nullptr;
	PickedVariantIndex = -1;
	const int32 Count = AnimSet ? AnimSet->GetVariantCount(Attack.AnimRole) : 0;
	if (Count <= 0)
	{
		return nullptr;
	}
	const int32 Index = UCombatAnimSet::PickNextVariant(VariantCursors.FindOrAdd(Attack.AnimRole), Count);
	UAnimMontage* Montage = AnimSet->ResolveVariant(Attack.AnimRole, Index);
	if (!Montage && Index > 0)
	{
		// A variant that does not load gives way to the role's own clip.
		Montage = AnimSet->ResolveVariant(Attack.AnimRole, 0);
	}
	PickedMontage = Montage;
	PickedVariantIndex = Montage ? Index : -1;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s variant %d of %d for %s: %s."), *GetNameSafe(GetOwner()), *Attack.Name.ToString(),
		Index + 1, Count, *UEnum::GetValueAsString(Attack.AnimRole), *GetNameSafe(Montage));
	return Montage;
}

bool UMeleeComponent::TryPlaySwingClip(const FHawkeyeMeleeAttack& Attack, float& OutHitDelaySeconds)
{
	UAnimMontage* Montage = PickSwingVariant(Attack);
	if (bForceNotifyTimingForTest)
	{
		bForceNotifyTimingForTest = false;
		bHitFromNotify = true;
		bComboFromNotify = true;
		ClipTimeRemaining = 5.f;
		return true;
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Montage || !Mesh)
	{
		return false;
	}

	float HitStart = 0.f;
	float HitEnd = 0.f;
	float ComboStart = 0.f;
	float ComboEnd = 0.f;
	const bool bHasHit = HawkeyeCombatAnim::FindNotifyWindow(Montage, UAnimNotifyState_HitWindow::StaticClass(), HitStart, HitEnd);
	const bool bHasCombo = HawkeyeCombatAnim::FindNotifyWindow(Montage, UAnimNotifyState_ComboWindow::StaticClass(), ComboStart,
		ComboEnd);
	const HawkeyeCombatAnim::FHitFit Fit = (bFitClipToWindup && bHasHit)
		? HawkeyeCombatAnim::FitHitToWindup(HitStart, Attack.WindupSeconds, ClipFitMinRate, ClipFitMaxRate)
		: HawkeyeCombatAnim::FHitFit();
	const float Rate = Fit.Rate;

	CurrentMontage = Montage;
	bClipMovesOwner = HawkeyeCombatAnim::DrivesRootMotion(Mesh, Montage);
	UpdateWarpTarget();
	UAnimInstance* Instance = HawkeyeCombatAnim::Play(Mesh, Montage, Rate, Fit.StartAtSeconds);
	if (!Instance)
	{
		ResetClipState();
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: %s clip %s did not play; procedural swing."), *GetNameSafe(GetOwner()),
			*Attack.Name.ToString(), *GetNameSafe(Montage));
		return false;
	}
	MontageInstance = Instance;
	if (bHasHit)
	{
		OutHitDelaySeconds = Fit.HoldSeconds + FMath::Max(HitStart - Fit.StartAtSeconds, 0.f) / FMath::Max(Rate, 0.05f);
	}
	bHitFromNotify = bHasHit;
	bComboFromNotify = bHasCombo;
	ClipRate = Rate;
	ClipHoldRemaining = Fit.HoldSeconds;
	if (ClipHoldRemaining > 0.f)
	{
		// The wind-up is shorter than the telegraph: he squares up in the clip's first frame, then swings.
		Instance->Montage_Pause(Montage);
	}
	ClipTimeRemaining = Fit.HoldSeconds + (Montage->GetPlayLength() - Fit.StartAtSeconds) / Rate + ClipTimeoutPadding;

	FOnMontageEnded Ended = FOnMontageEnded::CreateWeakLambda(this,
		[this, Serial = SwingSerial](UAnimMontage*, bool bInterrupted)
		{
			if (Serial == SwingSerial)
			{
				NotifyMontageEnded(bInterrupted);
			}
		});
	Instance->Montage_SetEndDelegate(Ended, Montage);
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s plays %s at %.2fx from %.2f s, held %.2f s (hit %s, combo %s, %s)."),
		*GetNameSafe(GetOwner()), *Attack.Name.ToString(), *Montage->GetName(), Rate, Fit.StartAtSeconds, Fit.HoldSeconds,
		bHasHit ? TEXT("from the clip") : TEXT("on the timer"),
		bHasCombo ? TEXT("from the clip") : TEXT("on the timer"), bClipMovesOwner ? TEXT("root motion") : TEXT("lunge"));
	return true;
}

UMotionWarpingComponent* UMeleeComponent::FindOrAddMotionWarping() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (UMotionWarpingComponent* Existing = Owner->FindComponentByClass<UMotionWarpingComponent>())
	{
		return Existing;
	}
	// The sample's character already has one (its traversal warps); a second would warp every clip twice.
	UMotionWarpingComponent* Created = NewObject<UMotionWarpingComponent>(Owner, TEXT("CombatMotionWarping"));
	Created->RegisterComponent();
	return Created;
}

void UMeleeComponent::UpdateWarpTarget()
{
	const AActor* Owner = GetOwner();
	if (!bClipMovesOwner || !Owner)
	{
		return;
	}
	UMotionWarpingComponent* Warp = FindOrAddMotionWarping();
	if (!Warp)
	{
		return;
	}
	const AActor* Target = SwingTarget.Get();
	if (!Target || bContactWhiff)
	{
		Warp->RemoveWarpTarget(WarpTargetName);
		return;
	}
	// The root lands where the striking limb meets his chosen region (its contact distance from that bone), turned onto
	// it; inside that distance it only turns her. Out of the caps the clip plays its own motion and whiffs.
	const FHawkeyeContactPlan Plan = PlanCurrentContact();
	ContactPlan = Plan;
	if (!Plan.bReachable)
	{
		MarkContactWhiff(TEXT("warp"));
		Warp->RemoveWarpTarget(WarpTargetName);
		return;
	}
	Warp->AddOrUpdateWarpTargetFromLocationAndRotation(WarpTargetName, Plan.StandPoint, Plan.Facing.Rotation());
}

FVector UMeleeComponent::ComputeContactPoint(const AActor& Target, const FHawkeyeStrikeContact& InContact) const
{
	const AThugCharacter* Thug = Cast<AThugCharacter>(&Target);
	const bool bDown = Thug && (Thug->IsKnockedDown() || Thug->IsLimp());
	const EHawkeyeContactRegion Region = UHawkeyeHitContactRules::SelectContactRegion(InContact.Region, bDown);
	const ACharacter* Character = Cast<ACharacter>(&Target);
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (bDown || !Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		return Target.GetActorLocation();
	}
	const FName Bone = UHawkeyeHitContactRules::SelectRegionBone(Region,
		[Mesh](FName Name) { return Mesh->GetBoneIndex(Name) != INDEX_NONE; });
	return Bone.IsNone() ? Target.GetActorLocation() : Mesh->GetBoneLocation(Bone);
}

FHawkeyeContactPlan UMeleeComponent::PlanCurrentContact() const
{
	const AActor* Owner = GetOwner();
	const AActor* Target = SwingTarget.Get();
	if (!Owner || !Target)
	{
		return FHawkeyeContactPlan();
	}
	// A bigger body (a boss drawn 1.15 times a thug) lands its blow from further off.
	const float Scale = FMath::Max(Owner->GetActorScale3D().X, 0.1f);
	const FHawkeyeStrikeContact& C = CurrentAttack.Contact;
	return UHawkeyeHitContactRules::PlanContact(Owner->GetActorLocation(), Owner->GetActorForwardVector(),
		ComputeContactPoint(*Target, C), C.ContactDistance * Scale, C.MaxSlide, C.MaxTurnDegrees);
}

void UMeleeComponent::MarkContactWhiff(const TCHAR* Where)
{
	if (bContactWhiff)
	{
		return;
	}
	bContactWhiff = true;
	++ContactWhiffCount;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s %s refused: %s (a %.0f cm slide, %.0f degrees round, caps %.0f cm and %.0f); it whiffs."),
		*GetNameSafe(GetOwner()), *CurrentAttack.Name.ToString(), Where, *ContactPlan.WhiffReason.ToString(), ContactPlan.Slide,
		ContactPlan.TurnDegrees, CurrentAttack.Contact.MaxSlide, CurrentAttack.Contact.MaxTurnDegrees);
}

void UMeleeComponent::UpdateApproach()
{
	if (bApproachDone || Phase != EMeleePhase::Windup || bAssistSwings || bClipMovesOwner || CurrentAttack.LungeDistance > 0.f
		|| !SwingTarget.IsValid() || PhaseRemaining > CurrentAttack.Contact.ApproachSeconds)
	{
		return;
	}
	bApproachDone = true;
	ContactPlan = PlanCurrentContact();
	if (!ContactPlan.bReachable)
	{
		// She is gone (a dodge, a step back): no lunge after her, the swing goes through the air.
		MarkContactWhiff(TEXT("slide"));
		return;
	}
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const float Seconds = FMath::Max(FMath::Min(PhaseRemaining, CurrentAttack.Contact.ApproachSeconds), 0.02f);
	if (!Movement || ContactPlan.Slide < 1.f || !Movement->IsMovingOnGround())
	{
		return;
	}
	// A root motion force like the lunge: exactly the slide, ending on the hit, stopping dead; her capsule stops it short.
	TSharedPtr<FRootMotionSource_ConstantForce> Slide = MakeShared<FRootMotionSource_ConstantForce>();
	Slide->InstanceName = FName(TEXT("MeleeContactSlide"));
	Slide->AccumulateMode = ERootMotionAccumulateMode::Override;
	Slide->Priority = 4;
	Slide->Force = ContactPlan.Facing * (ContactPlan.Slide / Seconds);
	Slide->Duration = Seconds;
	Slide->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Slide->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Movement->ApplyRootMotionSource(Slide);
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: %s slides %.0f cm into contact over %.2f s."), *GetNameSafe(GetOwner()),
		*CurrentAttack.Name.ToString(), ContactPlan.Slide, Seconds);
}

bool UMeleeComponent::GatherStrikePoints(TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		return false;
	}
	for (const FName Bone : UHawkeyeHitContactRules::LimbBones(CurrentAttack.Contact.Limbs))
	{
		if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
		{
			OutPoints.Add(Mesh->GetBoneLocation(Bone));
		}
	}
	// A held weapon sweeps along its length: its middle and its far end, beyond the hand.
	const AThugCharacter* Thug = Cast<AThugCharacter>(GetOwner());
	const UStaticMeshComponent* Held = Thug ? Thug->GetHeldWeaponComponent() : nullptr;
	if (Held && Held->IsVisible() && CurrentAttack.Contact.Limbs != EHawkeyeStrikeLimb::Feet && OutPoints.Num() > 0)
	{
		const FVector Hand = OutPoints[0];
		const FVector Middle = Held->Bounds.Origin;
		const FVector Out = (Middle - Hand).GetSafeNormal();
		OutPoints.Add(Middle);
		OutPoints.Add(Middle + Out * FMath::Max(Held->Bounds.SphereRadius * 0.8f, 10.f));
	}
	return OutPoints.Num() > 0;
}

bool UMeleeComponent::IsStrikeCandidate(const AActor* Candidate) const
{
	if (!Candidate || Candidate == GetOwner() || (!IgnoreTag.IsNone() && Candidate->ActorHasTag(IgnoreTag)))
	{
		return false;
	}
	const UHealthComponent* Health = Candidate->FindComponentByClass<UHealthComponent>();
	return Health && Health->IsAlive();
}

AActor* UMeleeComponent::FindTargetByLimbs(FVector& OutPoint, FVector& OutVelocity)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	TArray<FVector> Points;
	if (!Owner || !World || !GatherStrikePoints(Points))
	{
		return nullptr;
	}
	const double Now = World->GetTimeSeconds();
	const float Dt = FMath::Max(static_cast<float>(Now - LastLimbTime), 1.f / 120.f);
	const bool bHaveLast = LastLimbPoints.Num() == Points.Num();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeMeleeLimbs), /*bTraceComplex=*/false, Owner);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	const float Radius = CurrentAttack.Contact.StrikeRadius * FMath::Max(Owner->GetActorScale3D().X, 0.1f);

	// The limb nearest the target goes first, so a hit names the fist that threw it.
	const AActor* Aim = SwingTarget.Get();
	TArray<int32> Order;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Order.Add(Index);
	}
	if (Aim)
	{
		const FVector At = Aim->GetActorLocation();
		Order.Sort([&Points, &At](int32 A, int32 B) { return FVector::DistSquared(Points[A], At) < FVector::DistSquared(Points[B], At); });
	}

	AActor* Found = nullptr;
	for (const int32 Index : Order)
	{
		const FVector Start = bHaveLast ? LastLimbPoints[Index] : Points[Index];
		const FVector End = Points[Index];
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(Hits, Start, End + (Start.Equals(End) ? FVector(0.f, 0.f, 0.1f) : FVector::ZeroVector),
			FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);
		for (const FHitResult& Hit : Hits)
		{
			AActor* Candidate = Hit.GetActor();
			// The body, not the capsule round it (34 cm of air about a 15 cm torso): a hit on the capsule counts only for
			// someone with no physics bodies to meet.
			const ACharacter* Struck = Cast<ACharacter>(Candidate);
			const USkeletalMeshComponent* Body = Struck ? Struck->GetMesh() : nullptr;
			// (A body on the floor in a ragdoll is no longer a pawn to the query: there the capsule still counts.)
			const bool bHasBody = Body && Body->GetPhysicsAsset() && Body->Bodies.Num() > 0 && !Body->IsSimulatingPhysics()
				&& Body->GetCollisionObjectType() == ECC_Pawn && Body->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
			if (bHasBody && Hit.GetComponent() != Body)
			{
				continue;
			}
			if (IsStrikeCandidate(Candidate))
			{
				Found = Candidate;
				OutPoint = Hit.bStartPenetrating ? End : FVector(Hit.ImpactPoint);
				OutVelocity = (End - Start) / Dt;
				break;
			}
		}
		if (Found)
		{
			break;
		}
	}
	LastLimbPoints = Points;
	LastLimbTime = Now;
	return Found;
}

FHawkeyeStrikeImpact UMeleeComponent::BuildImpact(const AActor& Target, const FVector* LimbPoint, const FVector& LimbVelocity) const
{
	FHawkeyeStrikeImpact Impact;
	Impact.bValid = true;
	Impact.Source = EHawkeyeImpactSource::Strike;
	Impact.Weight = CurrentAttack.Contact.Weight;
	const AActor* Owner = GetOwner();
	const FVector From = Owner ? Owner->GetActorLocation() : Target.GetActorLocation() - GetSwingDirection() * 100.f;
	Impact.Direction = UHawkeyeHitContactRules::ComputeStrikeDirection(LimbVelocity, Target.GetActorLocation() - From, 150.f);
	if (LimbPoint)
	{
		Impact.Point = *LimbPoint;
		return Impact;
	}
	// The capsule's sweep found him: the blow lands on the front of his body toward the striker, at the height of the
	// striking limb nearest him (a jab at his face, a kick at his ribs), else at the region the attack aims at.
	TArray<FVector> Points;
	float Radius = 0.f;
	float HalfHeight = 0.f;
	Target.GetSimpleCollisionCylinder(Radius, HalfHeight);
	FVector Aim = ComputeContactPoint(Target, CurrentAttack.Contact);
	if (GatherStrikePoints(Points))
	{
		const int32 Nearest = UHawkeyeHitContactRules::PickStrikeLimb(Points, Target.GetActorLocation());
		if (Points.IsValidIndex(Nearest))
		{
			Aim.Z = FMath::Clamp(Points[Nearest].Z, Target.GetActorLocation().Z - HalfHeight * 0.6f,
				Target.GetActorLocation().Z + HalfHeight * 0.85f);
		}
	}
	const FVector Toward = (From - Target.GetActorLocation()).GetSafeNormal2D();
	Impact.Point = FVector(Target.GetActorLocation().X, Target.GetActorLocation().Y, Aim.Z) + Toward * Radius * 0.5f;
	return Impact;
}

void UMeleeComponent::DeliverImpact(AActor& Target, const FHawkeyeStrikeImpact& Impact, bool bTargetAlive)
{
	AActor* Owner = GetOwner();
	UHitReactionComponent* Mine = Owner ? Owner->FindComponentByClass<UHitReactionComponent>() : nullptr;
	UHitReactionComponent* Theirs = Target.FindComponentByClass<UHitReactionComponent>();
	// Both bodies hold a few frames (their own time only: the world, the tokens and the boss's clock run on), except
	// anyone winding up a swing, whose telegraph is a promise (a boss swinging through a light).
	if (Mine && UHawkeyeHitContactRules::ShouldHitStop(true, false))
	{
		Mine->BeginHitStop(UHawkeyeHitContactRules::ComputeHitStopSeconds(Impact.Weight, Mine->HitStop));
	}
	const UMeleeComponent* TheirMelee = Target.FindComponentByClass<UMeleeComponent>();
	if (Theirs && UHawkeyeHitContactRules::ShouldHitStop(bTargetAlive, TheirMelee && TheirMelee->IsWindingUp()))
	{
		Theirs->BeginHitStop(UHawkeyeHitContactRules::ComputeHitStopSeconds(Impact.Weight, Theirs->HitStop));
	}
	if (Theirs && bTargetAlive)
	{
		Theirs->PlayHitReaction(Impact);
	}
}

void UMeleeComponent::ResetClipState()
{
	CurrentMontage = nullptr;
	MontageInstance = nullptr;
	bHitFromNotify = false;
	bComboFromNotify = false;
	bHitWindowOpen = false;
	bComboWindowOpen = false;
	bSwingResolved = false;
	bClipMovesOwner = false;
	ClipTimeRemaining = 0.f;
	ClipHoldRemaining = 0.f;
	ClipRate = 1.f;
}

bool UMeleeComponent::IsCurrentClip(const UAnimSequenceBase* Animation) const
{
	return !CurrentMontage || !Animation || Animation == CurrentMontage;
}

void UMeleeComponent::NotifyHitWindowBegin(const UAnimSequenceBase* Animation)
{
	if (!bHitFromNotify || Phase != EMeleePhase::Windup || !IsCurrentClip(Animation))
	{
		return;
	}
	Phase = EMeleePhase::Recover;
	PhaseRemaining = CurrentAttack.RecoverSeconds;
	bHitWindowOpen = true;
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s hit window opens %.2f s into the swing (wind-up %.2f s)."), *GetNameSafe(GetOwner()),
		*CurrentAttack.Name.ToString(), GetWorld() ? GetWorld()->GetTimeSeconds() - SwingStartTime : 0.0,
		CurrentAttack.WindupSeconds);
	if (const AActor* Owner = GetOwner())
	{
		UHawkeyeAudioSubsystem::PlayAt(this, SwingSound, Owner->GetActorLocation(), TEXT("melee swing"));
	}
	bSwingResolved = ResolveSweep();
}

void UMeleeComponent::NotifyHitWindowEnd(const UAnimSequenceBase* Animation)
{
	if (!bHitWindowOpen || !IsCurrentClip(Animation))
	{
		return;
	}
	bHitWindowOpen = false;
	if (!bSwingResolved)
	{
		bSwingResolved = true;
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: %s swing hit nothing."), *GetNameSafe(GetOwner()), *CurrentAttack.Name.ToString());
		OnAttackMissed.Broadcast(CurrentAttack.Name);
	}
}

void UMeleeComponent::NotifyComboWindowBegin(const UAnimSequenceBase* Animation)
{
	if (!bComboFromNotify || Phase == EMeleePhase::Idle || bComboWindowOpen || !IsCurrentClip(Animation))
	{
		return;
	}
	// A window that opens before the hit window closed settles the hit first: the chain needs to know.
	if (bHitWindowOpen)
	{
		NotifyHitWindowEnd(Animation);
	}
	if (Phase == EMeleePhase::Windup)
	{
		return;
	}
	bComboWindowOpen = true;
	OnComboWindowChanged.Broadcast(true);
}

void UMeleeComponent::NotifyComboWindowEnd(const UAnimSequenceBase* Animation)
{
	if (IsCurrentClip(Animation))
	{
		CloseComboWindow();
	}
}

void UMeleeComponent::CloseComboWindow()
{
	if (!bComboWindowOpen)
	{
		return;
	}
	bComboWindowOpen = false;
	OnComboWindowChanged.Broadcast(false);
}

void UMeleeComponent::NotifyMontageEnded(bool bInterrupted)
{
	if (Phase == EMeleePhase::Idle)
	{
		ResetClipState();
		return;
	}
	if (bHitFromNotify || bComboFromNotify)
	{
		EndClipSwing(bInterrupted);
		return;
	}
	CurrentMontage = nullptr;
	MontageInstance = nullptr;
}

void UMeleeComponent::EndClipSwing(bool bInterrupted)
{
	if (Phase == EMeleePhase::Windup && bHitFromNotify)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s clip ended before its hit window (%s)."), *GetNameSafe(GetOwner()),
			*CurrentAttack.Name.ToString(), bInterrupted ? TEXT("cut") : TEXT("no window reached"));
	}
	if (bHitWindowOpen)
	{
		NotifyHitWindowEnd(nullptr);
	}
	const bool bWasComboOpen = bComboWindowOpen;
	const bool bChainedByClip = bComboFromNotify;
	ResetClipState();
	Phase = EMeleePhase::Idle;
	PhaseRemaining = 0.f;
	// Listeners holding the chain open for this clip's window let go, whether or not it ever opened.
	if (bWasComboOpen || bChainedByClip)
	{
		OnComboWindowChanged.Broadcast(false);
	}
}

bool UMeleeComponent::AdvanceClipSwing(float DeltaSeconds)
{
	if (ClipHoldRemaining > 0.f)
	{
		ClipHoldRemaining -= FMath::Max(DeltaSeconds, 0.f);
		if (ClipHoldRemaining <= 0.f)
		{
			// Resume where it would be had the hold ended mid-tick, so the hit keeps the telegraph's time.
			if (UAnimInstance* Instance = MontageInstance.Get())
			{
				Instance->Montage_Resume(CurrentMontage);
				Instance->Montage_SetPosition(CurrentMontage, Instance->Montage_GetPosition(CurrentMontage) - ClipHoldRemaining * ClipRate);
			}
			ClipHoldRemaining = 0.f;
		}
	}
	const bool bWindupFromClip = Phase == EMeleePhase::Windup && bHitFromNotify;
	const bool bRecoverFromClip = Phase == EMeleePhase::Recover && bComboFromNotify;
	if (bWindupFromClip)
	{
		// The notify lands the hit, but the telegraph's clock still runs: the parry window (Hard ignores its first
		// 0.1 s) and GetTelegraphElapsed read it. A fitted thug clip opens its hit window as this reaches 0.
		PhaseRemaining = FMath::Max(PhaseRemaining - FMath::Max(DeltaSeconds, 0.f), 0.f);
	}
	if (bHitWindowOpen && !bSwingResolved)
	{
		bSwingResolved = ResolveSweep();
	}
	if (!bWindupFromClip && !bRecoverFromClip)
	{
		return false;
	}
	ClipTimeRemaining -= FMath::Max(DeltaSeconds, 0.f);
	if (ClipTimeRemaining <= 0.f)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s clip %s never closed its windows; the swing ends."), *GetNameSafe(GetOwner()),
			*CurrentAttack.Name.ToString(), *GetNameSafe(CurrentMontage));
		HawkeyeCombatAnim::Stop(MontageInstance.Get(), CurrentMontage, 0.15f);
		EndClipSwing(true);
	}
	return true;
}

void UMeleeComponent::CancelAttack()
{
	if (Phase == EMeleePhase::Windup)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s swing interrupted."), *GetNameSafe(GetOwner()), *CurrentAttack.Name.ToString());
	}
	if (CurrentMontage)
	{
		HawkeyeCombatAnim::Stop(MontageInstance.Get(), CurrentMontage, 0.15f);
	}
	const bool bChainedByClip = bComboFromNotify && Phase != EMeleePhase::Idle;
	const bool bWasComboOpen = bComboWindowOpen;
	ResetClipState();
	++SwingSerial;
	Phase = EMeleePhase::Idle;
	PhaseRemaining = 0.f;
	if (bWasComboOpen || bChainedByClip)
	{
		OnComboWindowChanged.Broadcast(false);
	}
}

void UMeleeComponent::AdvanceAttack(float DeltaSeconds)
{
	if (Phase == EMeleePhase::Idle)
	{
		return;
	}
	// The assist keeps the warp on him while he moves in the wind-up.
	if (bAssistSwings && bClipMovesOwner && Phase == EMeleePhase::Windup && SwingTarget.IsValid())
	{
		UpdateWarpTarget();
	}
	UpdateApproach();
	// A clip-timed swing's limbs are followed through the wind-up, so the hit window's first sweep starts where they were.
	if (bHitFromNotify && Phase == EMeleePhase::Windup && GetWorld())
	{
		TArray<FVector> Points;
		if (GatherStrikePoints(Points))
		{
			LastLimbPoints = MoveTemp(Points);
			LastLimbTime = GetWorld()->GetTimeSeconds();
		}
	}
	if (AdvanceClipSwing(DeltaSeconds))
	{
		return;
	}

	PhaseRemaining -= DeltaSeconds;
	if (Phase == EMeleePhase::Windup && PhaseRemaining <= KINDA_SMALL_NUMBER)
	{
		// Whatever the wind-up overshot comes off the recovery, so a big step lands on time.
		const float Overshoot = -FMath::Min(PhaseRemaining, 0.f);
		Phase = EMeleePhase::Recover;
		PhaseRemaining = CurrentAttack.RecoverSeconds - Overshoot;
		Strike();
	}
	if (Phase == EMeleePhase::Recover && PhaseRemaining <= KINDA_SMALL_NUMBER)
	{
		Phase = EMeleePhase::Idle;
		PhaseRemaining = 0.f;
	}
}

FVector UMeleeComponent::GetSwingDirection() const
{
	if (!SwingDirection.IsNearlyZero())
	{
		return SwingDirection;
	}
	return GetOwner() ? GetOwner()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
}

AActor* UMeleeComponent::FindTarget(const FHawkeyeMeleeAttack& Attack) const
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	const FVector Start = Owner->GetActorLocation();
	const FVector End = Start + GetSwingDirection() * Attack.Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeMelee), /*bTraceComplex=*/false, Owner);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(Attack.Radius), Params);

	for (const FHitResult& Hit : Hits)
	{
		AActor* Candidate = Hit.GetActor();
		if (!Candidate || Candidate == Owner || (!IgnoreTag.IsNone() && Candidate->ActorHasTag(IgnoreTag)))
		{
			continue;
		}
		const UHealthComponent* Health = Candidate->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsAlive())
		{
			return Candidate;
		}
	}
	return nullptr;
}

void UMeleeComponent::Strike()
{
	AActor* Owner = GetOwner();
	if (Owner)
	{
		UHawkeyeAudioSubsystem::PlayAt(this, SwingSound, Owner->GetActorLocation(), TEXT("melee swing"));
	}
	if (!ResolveSweep())
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: %s swing hit nothing."), *GetNameSafe(Owner), *CurrentAttack.Name.ToString());
		OnAttackMissed.Broadcast(CurrentAttack.Name);
	}
}

AActor* UMeleeComponent::FindForgivenTarget() const
{
	AActor* Target = SwingTarget.Get();
	const AActor* Owner = GetOwner();
	// Out of the warp's caps the swing whiffs honestly: nothing forgives it.
	if (!bAssistSwings || bContactWhiff || !Target || !Owner || Target == Owner || (!IgnoreTag.IsNone() && Target->ActorHasTag(IgnoreTag)))
	{
		return nullptr;
	}
	const UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>();
	if (!Health || !Health->IsAlive() || FMath::Abs(Target->GetActorLocation().Z - Owner->GetActorLocation().Z) > 200.f)
	{
		return nullptr;
	}
	float TargetRadius = 0.f;
	float TargetHalfHeight = 0.f;
	Target->GetSimpleCollisionCylinder(TargetRadius, TargetHalfHeight);
	return UHawkeyeMeleeRules::IsAssistHitForgiven(Owner->GetActorLocation(), GetSwingDirection(), Target->GetActorLocation(),
		TargetRadius, CurrentAttack.Range + CurrentAttack.Radius, AssistReachScale, AssistForgiveDegrees) ? Target : nullptr;
}

bool UMeleeComponent::ResolveSweep()
{
	AActor* Owner = GetOwner();
	// A clip's swing is found by its striking limbs across the hit window (a fist meets the body or it does not); the
	// procedural swing (no clip, no bones) keeps the sweep from the capsule.
	FVector LimbPoint = FVector::ZeroVector;
	FVector LimbVelocity = FVector::ZeroVector;
	TArray<FVector> Probe;
	bLimbSweep = bHitFromNotify && GatherStrikePoints(Probe);
	AActor* Target = bLimbSweep ? FindTargetByLimbs(LimbPoint, LimbVelocity) : FindTarget(CurrentAttack);
	const bool bLimbHit = bLimbSweep && Target != nullptr;
	if (!Target)
	{
		Target = FindForgivenTarget();
		if (Target)
		{
			++ForgivenHitCount;
			UE_LOG(LogHawkeye, Log, TEXT("%s: %s assist: the sweep missed %s at %.0f cm, but he was the target at the press; it lands."),
				*GetNameSafe(Owner), *CurrentAttack.Name.ToString(), *GetNameSafe(Target),
				Owner ? FVector::Dist2D(Owner->GetActorLocation(), Target->GetActorLocation()) : 0.f);
		}
	}
	UHealthComponent* Health = Target ? Target->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!Health)
	{
		return false;
	}

	// A knockdown replaces the stagger: the body going over is the reaction. The heavy's shield takes a
	// light strike from the front outright; a heavy gets through it (and breaks his guard instead).
	AThugCharacter* Thug = Cast<AThugCharacter>(Target);
	if (Thug && Owner && !CurrentAttack.bKnockdown
		&& Thug->TryBlock(Owner, Owner->GetActorLocation() - Thug->GetActorLocation(), CurrentAttack.Name.ToString()))
	{
		++BlockedCount;
		OnAttackMissed.Broadcast(CurrentAttack.Name);
		return true;
	}
	const bool bKnockdown = CurrentAttack.bKnockdown && Thug;
	// The struck body hears of the blow before the damage, so a death it causes throws the ragdoll from where it landed
	// and a stagger plays the reaction for the side it came from.
	const FHawkeyeStrikeImpact Impact = BuildImpact(*Target, bLimbHit ? &LimbPoint : nullptr, LimbVelocity);
	UHitReactionComponent* TargetReaction = Target->FindComponentByClass<UHitReactionComponent>();
	if (TargetReaction)
	{
		TargetReaction->SetPendingImpact(Impact);
	}
	const float Dealt = Health->ApplyMeleeDamage(CurrentAttack.Damage, Owner, CurrentAttack.bStagger && !bKnockdown);
	if (Dealt <= 0.f)
	{
		if (TargetReaction)
		{
			TargetReaction->ClearPendingImpact();
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: %s swing on %s did nothing (invulnerable or dead)."),
			*GetNameSafe(Owner), *CurrentAttack.Name.ToString(), *GetNameSafe(Target));
		OnAttackMissed.Broadcast(CurrentAttack.Name);
		return true;
	}

	if (bKnockdown && Health->IsAlive())
	{
		Thug->Knockdown(Owner);
	}
	if (CurrentAttack.KnockbackDistance > 0.f && Health->IsAlive())
	{
		ApplyKnockback(Target, CurrentAttack);
	}
	LastImpact = Impact;
	DeliverImpact(*Target, Impact, Health->IsAlive());
	if (TargetReaction)
	{
		TargetReaction->ClearPendingImpact();
	}

	const bool bHeavySound = CurrentAttack.bKnockdown && !HeavyHitSound.IsNull();
	UHawkeyeAudioSubsystem::PlayAt(this, bHeavySound ? HeavyHitSound : HitSound, Impact.Point,
		bHeavySound ? TEXT("melee heavy hit") : TEXT("melee hit"));
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s swing hit %s for %.1f (health %.1f), %s at %s%s."),
		*GetNameSafe(Owner), *CurrentAttack.Name.ToString(), *GetNameSafe(Target), Dealt, Health->GetCurrentHealth(),
		bLimbHit ? TEXT("its limb met him") : TEXT("the capsule sweep"),
		*FString::Printf(TEXT("%.0f cm up"), Impact.Point.Z - Target->GetActorLocation().Z),
		Owner ? *FString::Printf(TEXT(", %.0f cm apart"), FVector::Dist2D(Owner->GetActorLocation(), Target->GetActorLocation())) : TEXT(""));
	OnAttackLanded.Broadcast(Target, Dealt, CurrentAttack.Name);
	return true;
}

void UMeleeComponent::ApplyLunge(const FHawkeyeMeleeAttack& Attack, float HitDelaySeconds)
{
	float Distance = Attack.LungeDistance;
	float Seconds = Attack.LungeSeconds;
	const AActor* Target = SwingTarget.Get();
	if (Target && GetOwner())
	{
		ContactPlan = PlanCurrentContact();
		if (!ContactPlan.bReachable)
		{
			// Out of the caps: her own step, through the air.
			MarkContactWhiff(TEXT("lunge"));
		}
		else if (bAssistSwings)
		{
			// Close the gap: the step ends at the attack's contact distance from the region it aims at (the plan's
			// slide), and is over a little before the hit so the blow comes from the end of it.
			Distance = FMath::Max(Attack.LungeDistance, ContactPlan.Slide);
			if (Distance > Attack.LungeDistance + KINDA_SMALL_NUMBER)
			{
				Seconds = FMath::Max(Attack.LungeSeconds, 0.8f * HitDelaySeconds);
			}
		}
		else if (Attack.LungeDistance > 0.f)
		{
			// A boss's lunge stops at the contact distance instead of running on into her.
			Distance = FMath::Min(Attack.LungeDistance, ContactPlan.Slide);
		}
	}
	LastLungeDistance = Distance;

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || Distance <= 0.f || !Movement->IsMovingOnGround() || !GetWorld())
	{
		return;
	}

	// A root motion force rather than a launch: it overrides the gait speed the sample's graph
	// writes, and ends exactly LungeSeconds later without sliding on.
	TSharedPtr<FRootMotionSource_ConstantForce> Lunge = MakeShared<FRootMotionSource_ConstantForce>();
	Lunge->InstanceName = FName(TEXT("MeleeLunge"));
	Lunge->AccumulateMode = ERootMotionAccumulateMode::Override;
	Lunge->Priority = 4;
	Lunge->Force = GetSwingDirection() * (Distance / FMath::Max(Seconds, 0.01f));
	Lunge->Duration = Seconds;
	Lunge->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Lunge->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Movement->ApplyRootMotionSource(Lunge);
}

FVector UMeleeComponent::ComputeKnockbackForce(const FVector& From, const FVector& To, float Distance, float Seconds)
{
	FVector Away = (To - From).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}
	return Away * (FMath::Max(Distance, 0.f) / FMath::Max(Seconds, 0.01f));
}

void UMeleeComponent::ApplyKnockback(AActor* Target, const FHawkeyeMeleeAttack& Attack) const
{
	const AActor* Owner = GetOwner();
	ACharacter* Character = Cast<ACharacter>(Target);
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Owner || !Movement || Attack.KnockbackDistance <= 0.f)
	{
		return;
	}
	// A root motion force, like the lunge and the dodge: it beats whatever gait the target's graph asks
	// for, covers exactly the distance, and stops dead at the end.
	TSharedPtr<FRootMotionSource_ConstantForce> Shove = MakeShared<FRootMotionSource_ConstantForce>();
	Shove->InstanceName = FName(TEXT("MeleeKnockback"));
	Shove->AccumulateMode = ERootMotionAccumulateMode::Override;
	Shove->Priority = 6;
	Shove->Force = ComputeKnockbackForce(Owner->GetActorLocation(), Target->GetActorLocation(), Attack.KnockbackDistance,
		Attack.KnockbackSeconds);
	Shove->Duration = Attack.KnockbackSeconds;
	Shove->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Shove->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Movement->ApplyRootMotionSource(Shove);
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s knocks %s back %.0f cm."), *GetNameSafe(Owner), *Attack.Name.ToString(),
		*GetNameSafe(Target), Attack.KnockbackDistance);
}
