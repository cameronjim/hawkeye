#include "Combat/HitReactionComponent.h"

#include "Hawkeye.h"
#include "Misc/App.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Player/HawkeyeCharacter.h"

UHitReactionComponent::UHitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

USkeletalMeshComponent* UHitReactionComponent::GetBodyMesh() const
{
	if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(GetOwner()))
	{
		// A look with bodies of its own (Kate's Sparrow) reacts itself; one without (Clint's) is posed from the
		// driver after physics, so the driver's bodies react and the look shows it.
		USkeletalMeshComponent* Seen = Hawkeye->GetBodyMesh();
		return Seen && Seen->GetPhysicsAsset() ? Seen : Hawkeye->GetMesh();
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

void UHitReactionComponent::UpdateTickEnabled()
{
	const bool bNeed = HitStopRemaining > 0.f || bReactionQueued || bReacting || (bKnockout && !bKnockoutSettled);
	if (IsComponentTickEnabled() != bNeed)
	{
		SetComponentTickEnabled(bNeed);
	}
}

void UHitReactionComponent::BeginHitStop(float Seconds)
{
	AActor* Owner = GetOwner();
	if (!Owner || Seconds <= 0.f)
	{
		return;
	}
	// The world's time is what stops (AHawkeyeCharacter::ApplyTimeWarp, started by whoever landed the blow); this only
	// remembers it, in real seconds, so the body's reaction waits for it.
	HitStopRemaining = FMath::Max(HitStopRemaining, Seconds);
	LastHitStopSeconds = Seconds;
	++HitStopCount;
	UpdateTickEnabled();
}

void UHitReactionComponent::EndHitStop()
{
	HitStopRemaining = 0.f;
}

void UHitReactionComponent::PlayHitReaction(const FHawkeyeStrikeImpact& Impact)
{
	if (!Impact.bValid)
	{
		return;
	}
	LastImpact = Impact;
	QueuedImpact = Impact;
	bReactionQueued = true;
	const UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr;
	// A guard (a boss's incoming damage scaled under 1, not a scripted 0) takes the blow with less of the body.
	bReactionGuarding = Health && Health->IncomingDamageScale > 0.f && Health->IncomingDamageScale < 1.f;
	if (HitStopRemaining <= 0.f)
	{
		StartQueuedReaction();
	}
	UpdateTickEnabled();
}

bool UHitReactionComponent::PrepareMesh(USkeletalMeshComponent& Mesh)
{
	if (!Mesh.GetPhysicsAsset() || !Mesh.GetSkeletalMeshAsset())
	{
		return false;
	}
	if (Mesh.GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		// Kate's look has no collision; its bodies may simulate but must never touch anything.
		Mesh.SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh.SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	}
	if (!PhysicalAnimation || ReactionMesh.Get() != &Mesh)
	{
		if (!PhysicalAnimation)
		{
			PhysicalAnimation = NewObject<UPhysicalAnimationComponent>(GetOwner(), TEXT("HitPhysicalAnimation"));
			PhysicalAnimation->RegisterComponent();
		}
		PhysicalAnimation->SetSkeletalMeshComponent(&Mesh);
		ReactionMesh = &Mesh;
	}
	return Mesh.Bodies.Num() > 0;
}

FName UHitReactionComponent::FindStruckBody(const USkeletalMeshComponent& Mesh, const FHawkeyeStrikeImpact& Impact) const
{
	if (!Impact.Bone.IsNone() && Mesh.GetBodyInstance(Impact.Bone))
	{
		return Impact.Bone;
	}
	FName Best = NAME_None;
	float BestDistance = TNumericLimits<float>::Max();
	for (const FBodyInstance* Body : Mesh.Bodies)
	{
		if (!Body || !Body->BodySetup.IsValid())
		{
			continue;
		}
		const FName Bone = Body->BodySetup->BoneName;
		if (Bone == PelvisBone || Mesh.GetBoneIndex(Bone) == INDEX_NONE)
		{
			continue;
		}
		const float Distance = FVector::DistSquared(Mesh.GetBoneLocation(Bone), Impact.Point);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Bone;
		}
	}
	return Best;
}

TArray<FName> UHitReactionComponent::FindTopBodiesBelow(const USkeletalMeshComponent& Mesh, FName Bone) const
{
	TArray<FName> Top;
	for (const FBodyInstance* Body : Mesh.Bodies)
	{
		const FName Name = Body && Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
		if (Name.IsNone() || (Name != Bone && !Mesh.BoneIsChildOf(Name, Bone)))
		{
			continue;
		}
		bool bBodyAbove = false;
		for (FName Parent = Mesh.GetParentBone(Name); !Parent.IsNone() && !bBodyAbove; Parent = Mesh.GetParentBone(Parent))
		{
			bBodyAbove = Mesh.GetBodyInstance(Parent) != nullptr;
		}
		if (!bBodyAbove)
		{
			Top.Add(Name);
		}
	}
	return Top;
}

bool UHitReactionComponent::IsPelvisNearFloor(const USkeletalMeshComponent& Mesh) const
{
	const UWorld* World = GetWorld();
	if (!World || Mesh.GetBoneIndex(PelvisBone) == INDEX_NONE)
	{
		return true;
	}
	const FVector Pelvis = Mesh.GetBoneLocation(PelvisBone);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeKnockoutFloor), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, Pelvis, Pelvis - FVector(0.f, 0.f, Knockout.GroundedHeight), ECC_Visibility, Params);
}

bool UHitReactionComponent::IsRagdollOwned(const USkeletalMeshComponent& Mesh) const
{
	// The pelvis simulating is a knockdown or a death. Not "the root body simulating": on a physics asset with no pelvis
	// body (Kate's Sparrow) the root body is spine_01, the reaction's own chain, which once cancelled every reaction of hers
	// on its first tick and left her chest simulating for good (the melee feel audit, 2026-11-12).
	return Mesh.GetBodyInstance(PelvisBone) != nullptr && Mesh.IsSimulatingPhysics(PelvisBone);
}

FName UHitReactionComponent::FindChainRoot(const USkeletalMeshComponent& Mesh, FName Bone) const
{
	FName Current = Bone;
	for (int32 Guard = 0; Guard < 64 && !Current.IsNone(); ++Guard)
	{
		const FName Parent = Mesh.GetParentBone(Current);
		if (Parent.IsNone() || Parent == PelvisBone)
		{
			return Parent.IsNone() ? NAME_None : Current;
		}
		Current = Parent;
	}
	return NAME_None;
}

void UHitReactionComponent::StartQueuedReaction()
{
	bReactionQueued = false;
	USkeletalMeshComponent* Mesh = GetBodyMesh();
	const UHealthComponent* Health = GetOwner() ? GetOwner()->FindComponentByClass<UHealthComponent>() : nullptr;
	if (bReacting)
	{
		FinishReaction();
	}
	++ReactionCount;
	if (!bPhysicalReaction)
	{
		return;
	}
	// A body already simulating (a knockdown, a get-up, a death) is the ragdoll's: the reaction leaves it alone.
	if (!Mesh || (Health && !Health->IsAlive()) || IsRagdollOwned(*Mesh) || !PrepareMesh(*Mesh))
	{
		return;
	}
	const FName Struck = FindStruckBody(*Mesh, QueuedImpact);
	const FName Root = Struck.IsNone() ? NAME_None : FindChainRoot(*Mesh, Struck);
	if (Root.IsNone())
	{
		return;
	}

	FPhysicalAnimationData Motors;
	Motors.bIsLocalSimulation = true;
	Motors.OrientationStrength = Reaction.OrientationStrength;
	Motors.AngularVelocityStrength = Reaction.AngularVelocityStrength;
	Motors.PositionStrength = 0.f;
	Motors.VelocityStrength = 0.f;
	PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(Root, Motors, /*bIncludeSelf=*/true);
	Mesh->SetAllBodiesBelowSimulatePhysics(Root, true, /*bIncludeSelf=*/true);
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(Root, 0.f, false, /*bIncludeSelf=*/true);
	// Never a body with no body above it: a component follows its root body while that simulates, and on Kate's Sparrow
	// (no pelvis body) the topmost spine body is the root, inside the chain. Every punch she took carried her whole look
	// off her capsule and left it there, about a metre further each time, until she was gone from the street (the melee
	// feel audit, 2026-11-12, kate_after_his_punches.png). That body stays on the animation; the ones under it react.
	KeptKinematic = FindTopBodiesBelow(*Mesh, Root);
	for (const FName Bone : KeptKinematic)
	{
		if (FBodyInstance* Body = Mesh->GetBodyInstance(Bone))
		{
			Body->SetInstanceSimulatePhysics(false);
			Body->PhysicsBlendWeight = 0.f;
		}
	}
	const FVector Kick = UHawkeyeHitContactRules::ComputeReactionImpulse(QueuedImpact.Direction, QueuedImpact.Weight,
		bReactionGuarding, Reaction);
	// The struck body's velocity, set on the body itself: AddImpulse refuses a mesh whose collision is query only (every
	// thug's driver, CharacterMesh), and dropped every thug's kick until 2026-11-12 (the melee feel audit).
	if (!KeptKinematic.Contains(Struck))
	{
		if (FBodyInstance* Body = Mesh->GetBodyInstance(Struck))
		{
			Body->SetLinearVelocity(Kick, /*bAddToCurrent=*/true);
		}
	}
	else
	{
		// The struck body is held on the animation: the bodies hanging straight off it (her neck, her arms) take the kick.
		for (FBodyInstance* Body : Mesh->Bodies)
		{
			const FName Name = Body && Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
			FName Above = Name.IsNone() ? NAME_None : Mesh->GetParentBone(Name);
			while (!Above.IsNone() && !Mesh->GetBodyInstance(Above))
			{
				Above = Mesh->GetParentBone(Above);
			}
			if (Above == Struck)
			{
				Body->SetLinearVelocity(Kick, /*bAddToCurrent=*/true);
			}
		}
	}

	bReacting = true;
	ReactionElapsed = 0.f;
	ReactionBlend = 0.f;
	ReactionRoot = Root;
	ReactionBone = Struck;
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: hit reaction on %s (chain from %s), %.0f cm/s%s."), *GetNameSafe(GetOwner()),
		*Struck.ToString(), *Root.ToString(), Kick.Size(), bReactionGuarding ? TEXT(", guarding") : TEXT(""));
}

void UHitReactionComponent::FinishReaction()
{
	USkeletalMeshComponent* Mesh = ReactionMesh.Get();
	// A ragdoll that took the body over mid-reaction (the pelvis simulating) keeps it.
	if (bReacting && Mesh && !ReactionRoot.IsNone() && !IsRagdollOwned(*Mesh))
	{
		Mesh->SetAllBodiesBelowPhysicsBlendWeight(ReactionRoot, 0.f, false, true);
		Mesh->SetAllBodiesBelowSimulatePhysics(ReactionRoot, false, true);
		if (PhysicalAnimation)
		{
			PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(ReactionRoot, FPhysicalAnimationData(), true);
		}
	}
	bReacting = false;
	ReactionBlend = 0.f;
	ReactionElapsed = 0.f;
	ReactionRoot = NAME_None;
}

void UHitReactionComponent::CancelReaction()
{
	bReactionQueued = false;
	if (bReacting)
	{
		// Leave the bodies as they are: whoever cancels (a knockdown, a death) is about to simulate all of them.
		if (PhysicalAnimation && !ReactionRoot.IsNone())
		{
			PhysicalAnimation->ApplyPhysicalAnimationSettingsBelow(ReactionRoot, FPhysicalAnimationData(), true);
		}
		bReacting = false;
		ReactionBlend = 0.f;
		ReactionRoot = NAME_None;
	}
	UpdateTickEnabled();
}

void UHitReactionComponent::BeginKnockout(USkeletalMeshComponent* Ragdoll, bool bBlendFromClip)
{
	CancelReaction();
	if (!Ragdoll || !Ragdoll->IsSimulatingPhysics())
	{
		return;
	}
	KnockoutMesh = Ragdoll;
	bKnockout = true;
	bKnockoutSettled = false;
	KnockoutElapsed = 0.f;
	KnockoutStillSeconds = 0.f;
	KnockoutGroundedAt = -1.f;
	KnockoutBoneVelocity = FVector::ZeroVector;
	KnockoutBodyVelocity = FVector::ZeroVector;

	for (FBodyInstance* Body : Ragdoll->Bodies)
	{
		if (Body)
		{
			Body->LinearDamping = Knockout.LinearDamping;
			Body->AngularDamping = Knockout.AngularDamping;
			Body->UpdateDampingProperties();
		}
	}

	const FHawkeyeStrikeImpact Impact = PendingImpact;
	if (Impact.bValid)
	{
		UHawkeyeHitContactRules::ComputeKnockoutImpulse(Impact.Direction, Impact.Weight, Impact.Source, Knockout,
			KnockoutBoneVelocity, KnockoutBodyVelocity);
		// The whole body keeps what it had (a run, a shove) plus the throw; the struck bone takes its own on top.
		Ragdoll->SetAllPhysicsLinearVelocity(KnockoutBodyVelocity, /*bAddToCurrent=*/true);
		const FName Struck = FindStruckBody(*Ragdoll, Impact);
		if (!Struck.IsNone())
		{
			Ragdoll->AddImpulse(KnockoutBoneVelocity, Struck, /*bVelChange=*/true);
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: knocked out on %s, the bone thrown %.0f cm/s, the body %.0f cm/s (weight %.2f, %s)."),
			*GetNameSafe(GetOwner()), *Struck.ToString(), KnockoutBoneVelocity.Size(), KnockoutBodyVelocity.Size(), Impact.Weight,
			Impact.Source == EHawkeyeImpactSource::Arrow ? TEXT("arrow") : (Impact.Source == EHawkeyeImpactSource::Finisher
				? TEXT("finisher") : TEXT("strike")));
	}

	// From the clip's pose into the ragdoll's, not a snap: the bodies simulate at once, what shows blends over.
	bKnockoutBlending = bBlendFromClip && Knockout.BlendInSeconds > 0.f;
	if (bKnockoutBlending)
	{
		Ragdoll->SetEnablePhysicsBlending(false);
		Ragdoll->SetAllBodiesPhysicsBlendWeight(0.f);
	}
	UpdateTickEnabled();
}

void UHitReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// World time, not the owner's: the hit-stop dilates the owner, and the reaction and the stop are measured outside it.
	const UWorld* World = GetWorld();
	const float Step = World ? FMath::Max(World->GetDeltaSeconds(), 0.f) : FMath::Max(DeltaTime, 0.f);

	if (HitStopRemaining > 0.f)
	{
		// Real time: the stop slows the world's.
		HitStopRemaining -= FMath::Max(static_cast<float>(FApp::GetDeltaTime()), 0.f);
		if (HitStopRemaining <= 0.f)
		{
			EndHitStop();
		}
	}
	if (bReactionQueued && HitStopRemaining <= 0.f)
	{
		StartQueuedReaction();
	}
	if (bReacting)
	{
		USkeletalMeshComponent* Mesh = ReactionMesh.Get();
		if (!Mesh || IsRagdollOwned(*Mesh))
		{
			CancelReaction();
		}
		else
		{
			ReactionElapsed += Step;
			ReactionBlend = UHawkeyeHitContactRules::ComputeReactionBlend(ReactionElapsed, QueuedImpact.Weight, bReactionGuarding,
				Reaction);
			Mesh->SetAllBodiesBelowPhysicsBlendWeight(ReactionRoot, ReactionBlend, false, true);
				for (const FName Bone : KeptKinematic)
				{
					if (FBodyInstance* Body = Mesh->GetBodyInstance(Bone))
					{
						Body->PhysicsBlendWeight = 0.f;
					}
				}
			if (ReactionElapsed >= Reaction.RecoverSeconds)
			{
				FinishReaction();
			}
		}
	}
	if (bKnockout && !bKnockoutSettled)
	{
		USkeletalMeshComponent* Ragdoll = KnockoutMesh.Get();
		KnockoutElapsed += Step;
		if (!Ragdoll || !Ragdoll->IsSimulatingPhysics())
		{
			bKnockoutSettled = true;
		}
		else
		{
			if (bKnockoutBlending)
			{
				const float Alpha = FMath::Clamp(KnockoutElapsed / Knockout.BlendInSeconds, 0.f, 1.f);
				Ragdoll->SetAllBodiesPhysicsBlendWeight(FMath::SmoothStep(0.f, 1.f, Alpha));
				if (Alpha >= 1.f)
				{
					bKnockoutBlending = false;
					Ragdoll->SetEnablePhysicsBlending(true);
				}
			}
			// Down, the damping climbs: the body lands with weight and stops instead of rolling on. Not in the air: a body
			// off a roof falls at full speed.
			if (KnockoutGroundedAt < 0.f && KnockoutElapsed >= Knockout.GroundedAfterSeconds && IsPelvisNearFloor(*Ragdoll))
			{
				KnockoutGroundedAt = KnockoutElapsed;
			}
			const float SecondsGrounded = KnockoutGroundedAt < 0.f ? -1.f : KnockoutElapsed - KnockoutGroundedAt;
			if (SecondsGrounded >= 0.f && SecondsGrounded <= Knockout.GroundedBlendSeconds + Step)
			{
				const FVector2D Damping = UHawkeyeHitContactRules::ComputeKnockoutDamping(SecondsGrounded, Knockout);
				for (FBodyInstance* Body : Ragdoll->Bodies)
				{
					if (Body)
					{
						Body->LinearDamping = Damping.X;
						Body->AngularDamping = Damping.Y;
						Body->UpdateDampingProperties();
					}
				}
			}
			const float Speed = Ragdoll->GetPhysicsLinearVelocity(PelvisBone).Size();
			KnockoutStillSeconds = Speed < Knockout.SettleSpeed ? KnockoutStillSeconds + Step : 0.f;
			if (!bKnockoutBlending
				&& (KnockoutStillSeconds >= Knockout.SettleHoldSeconds || KnockoutElapsed >= Knockout.SettleMaxSeconds))
			{
				// Still: asleep, so a corpse costs nothing and never twitches.
				Ragdoll->PutAllRigidBodiesToSleep();
				bKnockoutSettled = true;
				UE_LOG(LogHawkeye, Log, TEXT("%s: the body settles %.1f s after the knockout."), *GetNameSafe(GetOwner()),
					KnockoutElapsed);
			}
		}
	}
	UpdateTickEnabled();
}

void UHitReactionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HitStopRemaining > 0.f)
	{
		EndHitStop();
	}
	Super::EndPlay(EndPlayReason);
}
