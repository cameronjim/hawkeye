#include "Combat/BowIKAnimInstance.h"
#include "Combat/HitRecoil.h"

#include "Animation/BlendSpace.h"
#include "Combat/BowComponent.h"
#include "Combat/MeleeRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Player/HawkeyeCharacter.h"

static TAutoConsoleVariable<int32> CVarHawkeyeZipPose(TEXT("hawkeye.ZipPose"), 1,
	TEXT("1 (default) draws the grapple trip's body pose (the hang, the launch, the landings' legs) over the sample's "
		 "pose; "
		 "0 leaves the sample's pose alone, for before and after shots."),
	ECVF_Default);

bool UHawkeyeBowIKAnimInstance::IsTraversalPoseEnabled()
{
	return CVarHawkeyeZipPose.GetValueOnAnyThread() != 0;
}

FBowHandIKTargets UHawkeyeBowIKAnimInstance::ComputeHandTargets(const FTransform& AimFrame, float DrawFraction,
	const FBowHandIKSettings& Settings)
{
	const float Draw = FMath::Clamp(DrawFraction, 0.f, 1.f);
	const FTransform Frame(AimFrame.GetRotation(), AimFrame.GetLocation());

	FBowHandIKTargets Targets;
	Targets.BowHand = Frame.TransformPosition(Settings.GripOffset);
	Targets.BowElbow = Frame.TransformPosition(Settings.BowElbowHint);
	Targets.StringHand = Frame.TransformPosition(FMath::Lerp(Settings.StringHandRestOffset, Settings.StringHandFullOffset, Draw));
	Targets.StringElbow = Frame.TransformPosition(FMath::Lerp(Settings.StringElbowHintRest, Settings.StringElbowHintFull, Draw));
	return Targets;
}

FBowHandIKTargets UHawkeyeBowIKAnimInstance::ComputeHandTargetsOnGrip(const FVector& Grip, const FRotator& Aim,
	float DrawFraction, const FBowHandIKSettings& Settings)
{
	const FQuat Rotation = Aim.Quaternion();
	return ComputeHandTargets(FTransform(Rotation, Grip - Rotation.RotateVector(Settings.GripOffset)), DrawFraction, Settings);
}

float UHawkeyeBowIKAnimInstance::ComputeStringCorrectionAlpha(float OffLineCm, const FBowHandIKSettings& Settings)
{
	const float Dead = FMath::Max(0.f, Settings.ClipCorrectionDeadZone);
	const float Full = FMath::Max(Dead + 0.01f, Settings.ClipCorrectionFullDistance);
	return FMath::Clamp(Settings.ClipCorrectionMaxAlpha, 0.f, 1.f) * FMath::SmoothStep(Dead, Full, OffLineCm);
}

FVector UHawkeyeBowIKAnimInstance::ComputeArrowLinePoint(const FVector& Rest, const FVector& LaunchDirection, const FVector& Point,
	float MinDraw)
{
	const FVector Back = -LaunchDirection.GetSafeNormal();
	if (Back.IsNearlyZero())
	{
		return Point;
	}
	const float Along = FMath::Max(FVector::DotProduct(Point - Rest, Back), FMath::Max(0.f, MinDraw));
	return Rest + Back * Along;
}

FVector2D UHawkeyeBowIKAnimInstance::ComputeAimOffsetInput(const FRotator& Aim, float BodyYaw, const FBowHandIKSettings& Settings,
	const UBlendSpace* AimOffsetAsset)
{
	float YawLow = -Settings.MaxAimOffsetYaw;
	float YawHigh = Settings.MaxAimOffsetYaw;
	float PitchLow = -Settings.MaxAimOffsetPitch;
	float PitchHigh = Settings.MaxAimOffsetPitch;
	if (AimOffsetAsset)
	{
		const FBlendParameter& YawAxis = AimOffsetAsset->GetBlendParameter(0);
		const FBlendParameter& PitchAxis = AimOffsetAsset->GetBlendParameter(1);
		YawLow = FMath::Max(YawLow, YawAxis.Min);
		YawHigh = FMath::Min(YawHigh, YawAxis.Max);
		PitchLow = FMath::Max(PitchLow, PitchAxis.Min);
		PitchHigh = FMath::Min(PitchHigh, PitchAxis.Max);
	}
	const float Pitch = FRotator::NormalizeAxis(Aim.Pitch);
	const float Scaled = Pitch * (Pitch >= 0.f ? Settings.AimOffsetPitchScale : Settings.AimOffsetPitchScaleDown)
		+ Settings.AimOffsetPitchBias;
	return FVector2D(FMath::Clamp(FRotator::NormalizeAxis(Aim.Yaw - BodyYaw), YawLow, FMath::Max(YawLow, YawHigh)),
		FMath::Clamp(Scaled, PitchLow, FMath::Max(PitchLow, PitchHigh)));
}

float UHawkeyeBowIKAnimInstance::StepAlpha(float Current, float Target, float DeltaSeconds, float BlendSeconds)
{
	if (BlendSeconds <= 0.f)
	{
		return Target;
	}
	return FMath::FInterpConstantTo(Current, Target, FMath::Max(0.f, DeltaSeconds), 1.f / BlendSeconds);
}

float UHawkeyeBowIKAnimInstance::ComputeSpineTwistDegrees(float AimYaw, float BodyYaw, const FBowHandIKSettings& Settings)
{
	const float TowardAim = FMath::Clamp(FRotator::NormalizeAxis(AimYaw - BodyYaw), -Settings.MaxAimTwistDegrees,
		Settings.MaxAimTwistDegrees);
	return TowardAim + Settings.SideOnDegrees;
}

void UHawkeyeBowIKAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AActor* Owner = GetOwningActor();
	// The traversal pose first: ComposeOutputs, at the end of the bow's update, takes its hands.
	UpdateTraversalPose(DeltaSeconds);
	UpdateFromBow(Owner ? Owner->FindComponentByClass<UBowComponent>() : nullptr, DeltaSeconds);
}

void UHawkeyeBowIKAnimInstance::UpdateTraversalPose(float DeltaSeconds)
{
	AHawkeyeCharacter* Owner = Cast<AHawkeyeCharacter>(GetOwningActor());
	if (!Owner)
	{
		return;
	}
	FTraversalPoseState& State = Owner->GetTraversalPoseState();
	if (!IsTraversalPoseEnabled())
	{
		State.Reset();
		ApplyTraversalPose(FTraversalPoseOutput());
		return;
	}
	FTraversalPoseInput Input = Owner->BuildTraversalPoseInput(DeltaSeconds);
	// A teleport (a fast travel, a respawn) moves her further in a frame than she can run; nothing of the pose carries
	// over.
	const FVector Here = Owner->GetActorLocation();
	const float Jump = static_cast<float>(FVector::Dist(Here, LastActorLocation));
	Input.bTeleported = bHaveLastActorLocation &&
		Jump > FMath::Max(300.f, static_cast<float>(Input.Velocity.Size()) * FMath::Max(DeltaSeconds, 0.f) * 3.f + 150.f);
	LastActorLocation = Here;
	bHaveLastActorLocation = true;
	ApplyTraversalPose(State.Step(Input, Owner->GetTraversalPoseSettings()));
}

void UHawkeyeBowIKAnimInstance::ApplyTraversalPose(const FTraversalPoseOutput& Pose)
{
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const AActor* Owner = GetOwningActor();
	TraversalPose = Pose;
	if (!Mesh || !Owner || !Pose.IsActive())
	{
		TraversalAlpha = 0.f;
		TraversalPelvisRotation = TraversalSpine01Rotation = TraversalSpine02Rotation = TraversalSpine03Rotation =
			FRotator::ZeroRotator;
		TraversalNeckRotation = TraversalThighLRotation = TraversalThighRRotation = FRotator::ZeroRotator;
		TraversalCalfLRotation = TraversalCalfRRotation = FRotator::ZeroRotator;
		TraversalPelvisOffset = FVector::ZeroVector;
		TraversalLeftAlpha = TraversalRightAlpha = 0.f;
		return;
	}
	const FQuat ActorRotation = Owner->GetActorQuat();
	const FQuat ComponentRotation = Mesh->GetComponentQuat();
	auto Turn = [&ActorRotation, &ComponentRotation](const FQuat& Local)
	{ return FRotator(FTraversalPoseRules::ActorToComponent(Local, ActorRotation, ComponentRotation)); };
	TraversalAlpha = 1.f;
	TraversalPelvisRotation = Turn(Pose.Pelvis);
	TraversalSpine01Rotation = Turn(Pose.Spine[0]);
	TraversalSpine02Rotation = Turn(Pose.Spine[1]);
	TraversalSpine03Rotation = Turn(Pose.Spine[2]);
	TraversalNeckRotation = Turn(Pose.Neck);
	TraversalThighLRotation = Turn(Pose.ThighL);
	TraversalThighRRotation = Turn(Pose.ThighR);
	TraversalCalfLRotation = Turn(Pose.CalfL);
	TraversalCalfRRotation = Turn(Pose.CalfR);

	// Offsets and targets are cm in the actor's frame (no scale); the component's own units divide out its scale.
	const FTransform& Component = Mesh->GetComponentTransform();
	const FTransform Frame(Owner->GetActorQuat(), Owner->GetActorLocation());
	TraversalPelvisOffset = Component.InverseTransformVector(Frame.TransformVector(Pose.PelvisOffset));
	auto ToComponent = [&Component, &Frame](const FVector& Local)
	{ return Component.InverseTransformPosition(Frame.TransformPosition(Local)); };
	TraversalLeftHand = ToComponent(Pose.LeftHand);
	TraversalLeftElbow = ToComponent(Pose.LeftElbow);
	TraversalRightHand = ToComponent(Pose.RightHand);
	TraversalRightElbow = ToComponent(Pose.RightElbow);
	TraversalLeftAlpha = Pose.LeftHandAlpha;
	TraversalRightAlpha = Pose.RightHandAlpha;
}

void UHawkeyeBowIKAnimInstance::UpdateFromBow(const UBowComponent* Bow, float DeltaSeconds)
{
	UpdateFromBowWithClipWeight(Bow, DeltaSeconds, Bow ? Bow->GetBowClipWeight() : 0.f);
}

void UHawkeyeBowIKAnimInstance::UpdateFromBowWithClip(const UBowComponent* Bow, float DeltaSeconds, bool bClipHoldsBow)
{
	const bool bHasBow = Bow && Bow->GetBow();
	const float BlendSeconds = Bow ? Bow->HandsIK.BlendSeconds : FBowHandIKSettings().BlendSeconds;
	ClipAlpha = StepAlpha(ClipAlpha, (bHasBow && bClipHoldsBow) ? 1.f : 0.f, DeltaSeconds, BlendSeconds);
	UpdateAfterClipAlpha(Bow, DeltaSeconds);
}

void UHawkeyeBowIKAnimInstance::UpdateFromBowWithClipWeight(const UBowComponent* Bow, float DeltaSeconds, float Weight)
{
	// The slot's own blend is the clip's blend: the IK lets go of the arms exactly as fast as the clip takes them.
	ClipAlpha = (Bow && Bow->GetBow()) ? FMath::Clamp(Weight, 0.f, 1.f) : 0.f;
	UpdateAfterClipAlpha(Bow, DeltaSeconds);
}

void UHawkeyeBowIKAnimInstance::UpdateAfterClipAlpha(const UBowComponent* Bow, float DeltaSeconds)
{
	const bool bHasBow = Bow && Bow->GetBow();
	const float BlendSeconds = Bow ? Bow->HandsIK.BlendSeconds : FBowHandIKSettings().BlendSeconds;
	BowAlpha = StepAlpha(BowAlpha, (bHasBow && Bow->IsBowRaised()) ? 1.f : 0.f, DeltaSeconds, BlendSeconds);
	DrawAlpha = StepAlpha(DrawAlpha, (bHasBow && Bow->IsDrawPose()) ? 1.f : 0.f, DeltaSeconds, BlendSeconds);
	StringCorrectionAlpha = 0.f;
	StringOffLine = 0.f;

	if (!bHasBow || (BowAlpha <= 0.f && DrawAlpha <= 0.f))
	{
		SpineTwist = FRotator::ZeroRotator;
		NeckTwist = FRotator::ZeroRotator;
		AimOffsetAlpha = 0.f;
	}
	else
	{
		UpdateTargets(*Bow);
	}
	ComposeOutputs();
	LastRightTarget = RightHandTarget;
	LastRightAlpha = RightArmAlpha;
}

void UHawkeyeBowIKAnimInstance::ComposeOutputs()
{
	// A bow clip holds the arms: the bow hand's IK is off, and the string hand's is only the correction.
	LeftArmAlpha = BowAlpha * (1.f - ClipAlpha);
	RightArmAlpha = DrawAlpha * FMath::Lerp(1.f, StringCorrectionAlpha, ClipAlpha);
	// A strike owns a hand outright while it is on: the fist goes where the strike says, not the string.
	if (StrikeRightHandAlpha > 0.f)
	{
		RightHandTarget = StrikeRightHandTarget;
		RightElbowTarget = StrikeRightElbowTarget;
		RightArmAlpha = FMath::Clamp(StrikeRightHandAlpha, 0.f, 1.f);
	}
	if (StrikeLeftHandAlpha > 0.f)
	{
		LeftHandTarget = StrikeLeftHandTarget;
		LeftElbowTarget = StrikeLeftElbowTarget;
		LeftArmAlpha = FMath::Clamp(StrikeLeftHandAlpha, 0.f, 1.f);
	}
	// The grapple trip's hands (the rope hand and the free one) when no strike or bow has the arm: the bow wins past
	// half.
	if (TraversalLeftAlpha > 0.f && StrikeLeftHandAlpha <= 0.f && BowAlpha < 0.5f)
	{
		LeftHandTarget = TraversalLeftHand;
		LeftElbowTarget = TraversalLeftElbow;
		LeftArmAlpha = TraversalLeftAlpha;
	}
	if (TraversalRightAlpha > 0.f && StrikeRightHandAlpha <= 0.f && DrawAlpha < 0.5f)
	{
		RightHandTarget = TraversalRightHand;
		RightElbowTarget = TraversalRightElbow;
		RightArmAlpha = TraversalRightAlpha;
	}
	HitLean = UHawkeyeMeleeRules::ComputeHitLeanRotation(HitLeanDirection, HitLeanAlpha, HitLeanDegrees);
	// The blow's recoil on top: the spine's tip folds into the lean, the head gets its own node (HeadRecoil).
	const FRotator SpineTip = FHawkeyeHitRecoil::MakeTipRotation(RecoilSpineOffset);
	if (!SpineTip.IsNearlyZero())
	{
		HitLean = (SpineTip.Quaternion() * HitLean.Quaternion()).Rotator();
	}
	HeadRecoil = FHawkeyeHitRecoil::MakeHeadRotation(RecoilHeadOffset, RecoilRight, RecoilHeadYawShare);
}

void UHawkeyeBowIKAnimInstance::SetStrikePose(const FHawkeyeStrikePoseSample& Sample, const FTransform& ActorFrame)
{
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const FTransform Component = Mesh ? Mesh->GetComponentTransform() : FTransform::Identity;
	const FTransform Frame(ActorFrame.GetRotation(), ActorFrame.GetLocation());
	auto ToComponent = [&Component, &Frame](const FVector& Local)
	{
		return Component.InverseTransformPosition(Frame.TransformPosition(Local));
	};
	StrikeRightHandAlpha = Sample.RightAlpha;
	StrikeLeftHandAlpha = Sample.LeftAlpha;
	if (Sample.RightAlpha > 0.f)
	{
		StrikeRightHandTarget = ToComponent(Sample.RightHand);
		StrikeRightElbowTarget = ToComponent(Sample.RightElbow);
	}
	if (Sample.LeftAlpha > 0.f)
	{
		StrikeLeftHandTarget = ToComponent(Sample.LeftHand);
		StrikeLeftElbowTarget = ToComponent(Sample.LeftElbow);
	}
}

void UHawkeyeBowIKAnimInstance::SetHitRecoil(const FVector& WorldSpineOffset, const FVector& WorldHeadOffset, const FVector& WorldRight,
	float HeadYawShare)
{
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	auto ToComponent = [Mesh](const FVector& World)
	{
		const FVector Flat(World.X, World.Y, 0.f);
		return Mesh ? Mesh->GetComponentTransform().InverseTransformVectorNoScale(Flat) : Flat;
	};
	RecoilSpineOffset = ToComponent(WorldSpineOffset);
	RecoilHeadOffset = ToComponent(WorldHeadOffset);
	RecoilRight = ToComponent(WorldRight.GetSafeNormal2D());
	RecoilHeadYawShare = HeadYawShare;
}

void UHawkeyeBowIKAnimInstance::SetHitLean(const FVector& WorldDirection, float Alpha)
{
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	HitLeanAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	HitLeanDirection = Mesh ? Mesh->GetComponentTransform().InverseTransformVectorNoScale(WorldDirection.GetSafeNormal2D())
		: WorldDirection.GetSafeNormal2D();
}

void UHawkeyeBowIKAnimInstance::UpdateTargets(const UBowComponent& Bow)
{
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const AActor* Owner = Bow.GetOwner();
	if (!Mesh || !Owner)
	{
		return;
	}
	const FBowHandIKSettings& Settings = Bow.HandsIK;

	// The head from the last pose: IK never moves it, so the targets do not chase themselves, and the
	// component's current transform keeps them on the body however fast it is moving.
	const FVector Anchor = Mesh->DoesSocketExist(Settings.AnchorBone)
		? Mesh->GetSocketLocation(Settings.AnchorBone) : Mesh->GetComponentLocation();
	const FRotator Aim = Bow.ComputeHandsAimRotation(Anchor);
	const FBowHandIKTargets World = ComputeHandTargets(FTransform(Aim, Anchor), Bow.GetDrawFraction(), Settings);

	const FTransform& Component = Mesh->GetComponentTransform();
	LeftHandTarget = Component.InverseTransformPosition(World.BowHand);
	LeftElbowTarget = Component.InverseTransformPosition(World.BowElbow);
	RightHandTarget = Component.InverseTransformPosition(World.StringHand);
	RightElbowTarget = Component.InverseTransformPosition(World.StringElbow);
	if (ClipAlpha > 0.f)
	{
		// The clip placed the bow (it rides in the clip's hand) and the string hand; the IK only nudges
		// the string hand onto the arrow line.
		const UStaticMeshComponent* BowMesh = Bow.GetBowMeshComponent();
		const FVector Grip = BowMesh ? BowMesh->GetComponentLocation()
			: (Mesh->DoesSocketExist(Settings.GripFallbackBone) ? Mesh->GetSocketLocation(Settings.GripFallbackBone) : World.BowHand);
		FVector ClipElbow;
		const FVector ClipTarget = ComputeClipStringTarget(Bow, Grip, ClipElbow);
		RightHandTarget = FMath::Lerp(RightHandTarget, ClipTarget, ClipAlpha);
		RightElbowTarget = FMath::Lerp(RightElbowTarget, ClipElbow, ClipAlpha);
	}

	// The aim offset pitches and turns the upper body under a clip: its inputs every update, its weight
	// the clip's (and the bow's, so it lets go as the bow comes down).
	const FVector2D AimInput = ComputeAimOffsetInput(Aim, Owner->GetActorRotation().Yaw, Settings, AimOffset);
	AimOffsetYaw = AimInput.X;
	AimOffsetPitch = AimInput.Y;
	AimOffsetAlpha = AimOffset ? ClipAlpha * BowAlpha : 0.f;

	// Component space is only yawed off the actor, so a yaw here is a yaw about world up. A clip has
	// its own side-on stance, so only the turn toward the aim is added on top of it, and not even that
	// where the aim offset turns the body.
	const float SideOn = Settings.SideOnDegrees * (1.f - ClipAlpha);
	const float TowardAim = ComputeSpineTwistDegrees(Aim.Yaw, Owner->GetActorRotation().Yaw, Settings) - Settings.SideOnDegrees;
	const float Twist = TowardAim * (1.f - (AimOffset ? ClipAlpha : 0.f)) + SideOn;
	SpineTwist = FRotator(0.f, Twist * BowAlpha, 0.f);
	NeckTwist = FRotator(0.f, -SideOn * BowAlpha, 0.f);
}

FVector UHawkeyeBowIKAnimInstance::ComputeClipStringTarget(const UBowComponent& Bow, const FVector& Grip, FVector& OutElbow)
{
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const FBowHandIKSettings& Settings = Bow.HandsIK;
	static const FName HandBone(TEXT("hand_r"));
	static const FName ElbowBone(TEXT("lowerarm_r"));
	if (!Mesh || !Mesh->DoesSocketExist(HandBone))
	{
		OutElbow = RightElbowTarget;
		return RightHandTarget;
	}
	const FTransform& Component = Mesh->GetComponentTransform();
	// The last pose's hand is the clip's hand moved LastRightAlpha of the way to last update's target (the
	// IK blends the hand's component-space position linearly by its alpha), so the clip's own hand is
	// recovered from it; the palm, where the nock sits, moves with it.
	const FVector Final = Component.InverseTransformPosition(Mesh->GetSocketLocation(HandBone));
	const float Last = FMath::Clamp(LastRightAlpha, 0.f, 0.9f);
	const FVector ClipHand = Last > 0.f ? (Final - LastRightTarget * Last) / (1.f - Last) : Final;
	const FName PalmSocket = Mesh->DoesSocketExist(Settings.StringHandSocket) ? Settings.StringHandSocket : HandBone;
	const FVector ClipPalm = Component.InverseTransformPosition(Mesh->GetSocketLocation(PalmSocket)) + (ClipHand - Final);

	FVector Launch = Bow.ComputeAimPoint() - Bow.GetArrowSpawnLocation();
	if (Launch.IsNearlyZero())
	{
		Launch = Bow.ComputeHandsAimRotation(Grip).Vector();
	}
	const FVector OnLine = ComputeArrowLinePoint(Component.InverseTransformPosition(Grip),
		Component.InverseTransformVectorNoScale(Launch), ClipPalm, Settings.ClipMinDrawLength);
	StringOffLine = FVector::Dist(OnLine, ClipPalm);
	StringCorrectionAlpha = ComputeStringCorrectionAlpha(StringOffLine, Settings);
	// The clip's own elbow is the pole: the IK keeps the arm in the plane the clip had it in.
	OutElbow = Mesh->DoesSocketExist(ElbowBone) ? Component.InverseTransformPosition(Mesh->GetSocketLocation(ElbowBone))
		: RightElbowTarget;
	return ClipHand + (OnLine - ClipPalm);
}
