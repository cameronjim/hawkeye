// The grapple trip's body pose and the landing looks: AHawkeyeCharacter's share of them
// (claude-docs/gameplay-semantics.md, "Traversal body pose"). Its own file so it stays clear of the character's other
// work. Everything here is visual: it reads the movement and the grapple, plays montages and hands numbers to the
// bow-IK anim instance; it never writes Velocity, the capsule or the movement mode.

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/BowIKAnimInstance.h"
#include "Combat/CombatAnimSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Hawkeye.h"
#include "Player/GrappleComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMovementComponent.h"
#include "World/GrappleAnchor.h"

namespace HawkeyeTraversalPoseDriver
{
	/** Where the rope is taken to leave her from, up from the capsule centre (the shoulder's height), cm. */
	static constexpr float RopeOriginHeight = 55.f;
}

FTraversalPoseInput AHawkeyeCharacter::BuildTraversalPoseInput(float DeltaSeconds) const
{
	FTraversalPoseInput Input;
	Input.DeltaSeconds = DeltaSeconds;
	Input.ActorRotation = GetActorQuat();
	Input.Velocity = GetVelocity();
	const UHawkeyeMovementComponent* Movement = Cast<UHawkeyeMovementComponent>(GetCharacterMovement());
	if (!Movement)
	{
		return Input;
	}
	Input.bZipping = Movement->IsZipping();
	Input.bHopping = Movement->IsZipHopping();
	Input.bRedirecting = Movement->IsZipRedirecting();
	Input.bFalling = Movement->IsFalling();
	Input.bGrounded = Movement->IsMovingOnGround();
	Input.ZipLineSpeed = Movement->GetZipLineSpeed();
	Input.HopFraction = Movement->GetZipHopFraction();
	Input.bFallClipPlays = ActiveFallLoop != nullptr;
	if (Input.bZipping)
	{
		// From the capsule and not from her hand: the hand is what the pose places, so reading it back would chase
		// itself.
		const FVector From = GetActorLocation() + FVector(0.f, 0.f, HawkeyeTraversalPoseDriver::RopeOriginHeight);
		const AGrappleAnchor* Anchor = GrappleComponent ? GrappleComponent->GetZipAnchor() : nullptr;
		if (Anchor)
		{
			Input.RopeDirection = (Anchor->GetMarkerLocation() - From).GetSafeNormal();
		}
		else if (TraversalRopeOverride.IsSet())
		{
			Input.RopeDirection = (TraversalRopeOverride.GetValue() - From).GetSafeNormal();
		}
	}
	return Input;
}

void AHawkeyeCharacter::StopFallLoop(float BlendOutSeconds)
{
	if (ActiveFallLoop)
	{
		const USkeletalMeshComponent* Body = GetMesh();
		HawkeyeCombatAnim::Stop(Body ? Body->GetAnimInstance() : nullptr, ActiveFallLoop, BlendOutSeconds);
		ActiveFallLoop = nullptr;
	}
}

void AHawkeyeCharacter::UpdateTraversalClips(float /*DeltaSeconds*/)
{
	// The fall after letting go of the line plays the sample's fall loop under the body pose's reaction to how fast she
	// drops.
	const bool bWantsLoop = TraversalPoseState.GetPhase() == ETraversalPhase::Fall && !IsRolling() &&
		UHawkeyeBowIKAnimInstance::IsTraversalPoseEnabled();
	if (!bWantsLoop)
	{
		StopFallLoop(0.15f);
		return;
	}
	USkeletalMeshComponent* Body = GetMesh();
	UAnimInstance* Instance = Body ? Body->GetAnimInstance() : nullptr;
	if (!IsValid(Instance) || (ActiveFallLoop && Instance->Montage_IsPlaying(ActiveFallLoop)))
	{
		return;
	}
	ActiveFallLoop = UCombatAnimSet::Resolve(CombatAnimSet, ECombatAnimRole::FallLoop);
	if (ActiveFallLoop && !HawkeyeCombatAnim::Play(Body, ActiveFallLoop))
	{
		ActiveFallLoop = nullptr;
	}
}

void AHawkeyeCharacter::BeginLandingPose(float FallHeight)
{
	const FHawkeyeTraversalPoseSettings& Settings = TraversalPoseSettings;
	float Impact = FMath::Max(FallPeakSpeed, FMath::Max(0.f, -static_cast<float>(GetVelocity().Z)));
	if (Impact <= 0.f && FallHeight > 0.f)
	{
		// No speed was tracked (a landing told of a height only): the speed a free fall of that height reaches.
		Impact = FTraversalPoseRules::ImpactSpeedFromHeight(FallHeight, Settings.Gravity);
	}
	FallPeakSpeed = 0.f;
	StopFallLoop(0.1f);

	// A hop or a step is the sample's to land; a release from a zip, a fall that rolls or stumbles or dips, or a real
	// drop is ours.
	const ETraversalPhase Phase = TraversalPoseState.GetPhase();
	const bool bAfterZip = Phase == ETraversalPhase::Release || Phase == ETraversalPhase::Fall;
	const bool bOurs = bAfterZip || LandingState != EHawkeyeLanding::None || FallHeight >= LandingPoseMinHeight;
	if (!bOurs || !UHawkeyeBowIKAnimInstance::IsTraversalPoseEnabled())
	{
		return;
	}

	const float Ground = static_cast<float>(GetVelocity().Size2D());
	const ELandingClip Clip =
		FTraversalPoseRules::ClassifyLanding(Impact, Ground, IsRolling(), IsStumbling(), Settings);
	bool bClipPlays = false;
	const UCombatAnimSet* Set = CombatAnimSet;
	const ECombatAnimRole Role = UCombatAnimSet::LandingRoleFor(Clip);
	if (Set && Role != ECombatAnimRole::None && Set->GetVariantCount(Role) > 0)
	{
		const int32 Index = UCombatAnimSet::PickNextVariant(LandingVariantCursor, Set->GetVariantCount(Role));
		if (UAnimMontage* Montage = Set->ResolveVariant(Role, Index))
		{
			// The roll and the stumble take the time the landing rules give them; the others play as they were made.
			const float Fit =
				Clip == ELandingClip::Roll ? RollSeconds : (Clip == ELandingClip::Stumble ? StumbleSeconds : 0.f);
			const float Rate = Fit > 0.f ? HawkeyeCombatAnim::FitRate(Montage->GetPlayLength(), Fit) : 1.f;
			bClipPlays = HawkeyeCombatAnim::Play(GetMesh(), Montage, Rate) != nullptr;
		}
	}
	if (Clip == ELandingClip::Roll)
	{
		bRollClipPlaying = bClipPlays;
	}
	TraversalPoseState.BeginLanding(Clip, Impact, bClipPlays, Settings);
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: landing pose %s at %.0f cm/s impact, %.0f cm/s along the ground, %s"),
		*GetNameSafe(this), *UEnum::GetValueAsString(Clip), Impact, Ground,
		bClipPlays ? TEXT("a clip") : TEXT("the legs' absorb"));
}
