#include "Player/HawkeyeSpringArmComponent.h"

#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Math/RotationMatrix.h"
#include "Player/CameraRules.h"

namespace
{
	/** hawkeye.Camera.Debug 1 draws the arm's sweeps (shoulder, lens probe, side feelers); see gameplay-semantics.md, "Camera rules". */
	TAutoConsoleVariable<int32> CVarCameraDebug(TEXT("hawkeye.Camera.Debug"), 0,
		TEXT("1 draws the camera arm: the shoulder, the lens probe sphere and the two side feelers."), ECVF_Cheat);

	/** A teleport (the parent moved with a teleport type) further than this is a cut; nearer, the lag carries it, cm. */
	constexpr float TeleportCutDistance = 150.f;

	/** How fast the dead zone's reference height settles onto her while she is on the ground, per second. */
	constexpr float HoldSettleSpeed = 14.f;

	/**
	 * A target rotation that turns further than this in one update is a snap of the control rotation (a scripted turn, a
	 * respawn), not a hand on the mouse (a flick is 9 degrees a frame at 60 fps): the arm cuts to it instead of swinging
	 * round at the rotation lag's opening rate (12 times the angle a second), which is a thousand degrees a second.
	 */
	constexpr float RotationCutYaw = 90.f;
	constexpr float RotationCutPitch = 60.f;
}

UHawkeyeSpringArmComponent::UHawkeyeSpringArmComponent()
{
	bWantsOnUpdateTransform = true;
	ProbeSize = 20.f;
}

void UHawkeyeSpringArmComponent::ResetLag()
{
	bPendingReset = true;
	// Now as well as at the next update: anything that reads the history in between (a sweep from the old pose) sees the new one.
	PreviousDesiredLoc = GetComponentLocation();
	PreviousArmOrigin = GetComponentLocation() + TargetOffset;
	PreviousDesiredRot = GetTargetRotation();
}

void UHawkeyeSpringArmComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
	if (Teleport != ETeleportType::None)
	{
		bTeleportSeen = true;
	}
}

FRotator UHawkeyeSpringArmComponent::LagRotation(const FRotator& DesiredRot, bool bDoRotationLag, bool bCut, float Delta)
{
	FRotator Result = DesiredRot;
	if (bDoRotationLag && !bCut)
	{
		if (bUseCameraLagSubstepping && Delta > CameraLagMaxTimeStep && CameraRotationLagSpeed > 0.f)
		{
			const FRotator ArmRotStep = (DesiredRot - PreviousDesiredRot).GetNormalized() * (1.f / Delta);
			FRotator LerpTarget = PreviousDesiredRot;
			float RemainingTime = Delta;
			while (RemainingTime > UE_KINDA_SMALL_NUMBER)
			{
				const float LerpAmount = FMath::Min(CameraLagMaxTimeStep, RemainingTime);
				LerpTarget += ArmRotStep * LerpAmount;
				RemainingTime -= LerpAmount;
				Result = FRotator(FMath::QInterpTo(FQuat(PreviousDesiredRot), FQuat(LerpTarget), LerpAmount, CameraRotationLagSpeed));
				PreviousDesiredRot = Result;
			}
		}
		else
		{
			Result = FRotator(FMath::QInterpTo(FQuat(PreviousDesiredRot), FQuat(DesiredRot), Delta, CameraRotationLagSpeed));
		}
	}
	PreviousDesiredRot = Result;
	return Result;
}

FVector UHawkeyeSpringArmComponent::LagPivot(const FVector& Target, bool bDoLocationLag, bool bCut, float Delta)
{
	FVector Result = Target;
	if (bDoLocationLag && !bCut)
	{
		if (bUseCameraLagSubstepping && Delta > CameraLagMaxTimeStep && CameraLagSpeed > 0.f)
		{
			const FVector ArmMovementStep = (Target - PreviousDesiredLoc) * (1.f / Delta);
			FVector LerpTarget = PreviousDesiredLoc;
			float RemainingTime = Delta;
			while (RemainingTime > UE_KINDA_SMALL_NUMBER)
			{
				const float LerpAmount = FMath::Min(CameraLagMaxTimeStep, RemainingTime);
				LerpTarget += ArmMovementStep * LerpAmount;
				RemainingTime -= LerpAmount;
				Result = FMath::VInterpTo(PreviousDesiredLoc, LerpTarget, LerpAmount, CameraLagSpeed);
				PreviousDesiredLoc = Result;
			}
		}
		else
		{
			Result = FMath::VInterpTo(PreviousDesiredLoc, Target, Delta, CameraLagSpeed);
		}
		// At zip speed 1800 / lag 10 the pivot trails by 180 cm and arrives under a roof deck; the engine's own clamp is
		// off (CameraLagMaxDistance 0), so this one is what keeps it near her.
		Result = Target + HawkeyeCameraRules::ClampLagOffset(Result - Target, LagMaxHorizontal, LagMaxVertical);
	}
	PreviousArmOrigin = Target + TargetOffset;
	PreviousDesiredLoc = Result;
	return Result;
}

float UHawkeyeSpringArmComponent::UpdateHoldReference(float ActualZ, bool bCut, float Delta)
{
	if (bCut)
	{
		HoldReferenceZ = ActualZ;
	}
	else if (!bVerticalHold)
	{
		HoldReferenceZ = FMath::FInterpTo(HoldReferenceZ, ActualZ, Delta, HoldSettleSpeed);
	}
	return HoldReferenceZ;
}

void UHawkeyeSpringArmComponent::ResolveShoulder(const FVector& Base, const FVector& Right, const FCollisionQueryParams& Query,
	float Delta, bool bCut, FVector& OutPivotOffset, float& OutShoulderScale)
{
	UWorld* World = GetWorld();
	const FCollisionShape Probe = FCollisionShape::MakeSphere(ProbeSize);

	// Stage 0: the lift. A pivot lifted into a ceiling would start every later sweep inside it.
	OutPivotOffset = TargetOffset;
	float LiftHit = 1.f;
	if (!TargetOffset.IsNearlyZero(1.0))
	{
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Base, Base + TargetOffset, FQuat::Identity, ProbeChannel, Probe, Query))
		{
			LiftHit = Hit.bStartPenetrating ? 0.f : HawkeyeCameraRules::BackOffFraction(Hit.Time, TargetOffset.Size());
		}
	}
	LiftScale = bCut ? LiftHit : HawkeyeCameraRules::EaseFraction(LiftScale, 1.f, LiftHit, Delta, ReleaseSpeed);
	OutPivotOffset = TargetOffset * LiftScale;

	// Stage 1: the shoulder, out from the lifted pivot.
	const FVector Pivot = Base + OutPivotOffset;
	float ShoulderHit = 1.f;
	if (FMath::Abs(SocketOffset.Y) > 1.0)
	{
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Pivot, Pivot + Right * SocketOffset.Y, FQuat::Identity, ProbeChannel, Probe, Query))
		{
			ShoulderHit = Hit.bStartPenetrating ? 0.f : HawkeyeCameraRules::BackOffFraction(Hit.Time, FMath::Abs(SocketOffset.Y));
		}
	}
	ShoulderScale = bCut ? ShoulderHit : HawkeyeCameraRules::EaseFraction(ShoulderScale, 1.f, ShoulderHit, Delta, ReleaseSpeed);
	OutShoulderScale = ShoulderScale;
}

float UHawkeyeSpringArmComponent::SweepArm(const FVector& Shoulder, const FVector& Lens, const FCollisionQueryParams& Query,
	float Delta, bool bCut)
{
	UWorld* World = GetWorld();

	float MainHit = 1.f;
	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, Shoulder, Lens, FQuat::Identity, ProbeChannel, FCollisionShape::MakeSphere(ProbeSize), Query))
	{
		MainHit = Hit.bStartPenetrating ? 0.f : HawkeyeCameraRules::BackOffFraction(Hit.Time, (Lens - Shoulder).Size());
	}

	// The feelers: the same reach, turned a little to each side. A wall they find first eases the arm in before the main
	// sweep touches it, so turning toward a corner does not snap the lens in at the last frame.
	float FeelerHit = 1.f;
	const FVector Reach = Lens - Shoulder;
	const FCollisionShape Feeler = FCollisionShape::MakeSphere(FeelerRadius);
	for (const float Side : { -1.f, 1.f })
	{
		const FVector End = Shoulder + Reach.RotateAngleAxis(Side * FeelerAngle, FVector::UpVector);
		if (World->SweepSingleByChannel(Hit, Shoulder, End, FQuat::Identity, ProbeChannel, Feeler, Query) && !Hit.bStartPenetrating)
		{
			FeelerHit = FMath::Min(FeelerHit, Hit.Time);
		}
	}
	FeelerFraction = bCut ? FMath::Max(FeelerHit, 0.55f)
		: HawkeyeCameraRules::EaseFeelerFraction(FeelerFraction, FeelerHit, Delta);
	ArmFraction = bCut ? FMath::Min(FeelerFraction, MainHit)
		: HawkeyeCameraRules::EaseFraction(ArmFraction, FeelerFraction, MainHit, Delta, ReleaseSpeed);
	return ArmFraction;
}

float UHawkeyeSpringArmComponent::FindFloor(const FVector& Origin, const FCollisionQueryParams& Query) const
{
	UWorld* World = GetWorld();
	const FVector End = Origin - FVector(0.f, 0.f, FloorSearchDistance);
	float Floor = HawkeyeCameraRules::NoFloor;
	FHitResult Hit;
	// What blocks the lens, and the world's static geometry whether or not it blocks the lens (a bin the lens passes
	// through is still a floor to stay above).
	if (World->LineTraceSingleByChannel(Hit, Origin, End, ProbeChannel, Query) && !Hit.bStartPenetrating)
	{
		Floor = FMath::Max(Floor, static_cast<float>(Hit.ImpactPoint.Z));
	}
	if (World->LineTraceSingleByObjectType(Hit, Origin, End, FCollisionObjectQueryParams(ECC_WorldStatic), Query) && !Hit.bStartPenetrating)
	{
		Floor = FMath::Max(Floor, static_cast<float>(Hit.ImpactPoint.Z));
	}
	return Floor;
}

FVector UHawkeyeSpringArmComponent::ApplyFloorRule(const FVector& Lens, const FVector& Base, const FCollisionQueryParams& Query,
	float Delta, bool bCut)
{
	UWorld* World = GetWorld();
	const float FloorUnderCharacter = FindFloor(Base, Query);

	// Under the lens: from just above it, but never from above a ceiling the lens is under (that would find the roof).
	constexpr float LookAbove = 50.f;
	FVector Start = Lens + FVector(0.f, 0.f, LookAbove);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Lens, Start, ProbeChannel, Query))
	{
		Start.Z = Hit.ImpactPoint.Z - 2.0;
	}
	const float FloorUnderLens = FindFloor(Start, Query);

	const float Need = HawkeyeCameraRules::RiseNeededForFloor(Lens.Z, FloorUnderLens, FloorUnderCharacter, FloorClearance);
	FloorCorrection = bCut ? Need : HawkeyeCameraRules::EaseFloorCorrection(FloorCorrection, Need, Delta);
	if (FloorCorrection < 0.5f)
	{
		return Lens;
	}
	// Up, but not through a ceiling.
	const FVector Raised = Lens + FVector(0.f, 0.f, FloorCorrection);
	if (World->SweepSingleByChannel(Hit, Lens, Raised, FQuat::Identity, ProbeChannel, FCollisionShape::MakeSphere(ProbeSize * 0.5f), Query)
		&& !Hit.bStartPenetrating)
	{
		return Hit.Location;
	}
	return Raised;
}

void UHawkeyeSpringArmComponent::DrawDebug(const FVector& Pivot, const FVector& Shoulder, const FVector& Lens, const FVector& Direction) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World || CVarCameraDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
	const FVector Reach = Lens - Shoulder;
	DrawDebugLine(World, Pivot, Shoulder, FColor::Cyan, false, -1.f, SDPG_Foreground, 1.f);
	DrawDebugLine(World, Shoulder, Lens, FColor::Green, false, -1.f, SDPG_Foreground, 1.f);
	DrawDebugSphere(World, Lens, ProbeSize, 12, FColor::Yellow, false, -1.f, SDPG_Foreground, 0.5f);
	DrawDebugSphere(World, Shoulder, 6.f, 8, FColor::Cyan, false, -1.f, SDPG_Foreground, 0.5f);
	for (const float Side : { -1.f, 1.f })
	{
		DrawDebugLine(World, Shoulder, Shoulder + Reach.RotateAngleAxis(Side * FeelerAngle, FVector::UpVector), FColor::Orange, false, -1.f,
			SDPG_Foreground, 0.5f);
	}
	DrawDebugLine(World, Lens, Lens + Direction * 40.f, FColor::White, false, -1.f, SDPG_Foreground, 0.5f);
#endif
}

void UHawkeyeSpringArmComponent::UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	UWorld* World = GetWorld();
	const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	const float Delta = HawkeyeCameraRules::UnscaledDelta(DeltaTime, Settings ? Settings->GetEffectiveTimeDilation() : 1.f);

	const FVector Base = GetComponentLocation();
	const float Jump = bHasHistory ? static_cast<float>(FVector::Dist(Base, LastBase)) : 0.f;
	const FRotator TargetRotation = GetTargetRotation();
	const bool bRotationSnap = bHasHistory
		&& (FMath::Abs(FRotator::NormalizeAxis(TargetRotation.Yaw - LastTargetRotation.Yaw)) > RotationCutYaw
			|| FMath::Abs(FRotator::NormalizeAxis(TargetRotation.Pitch - LastTargetRotation.Pitch)) > RotationCutPitch);
	const bool bCut = bPendingReset || !bHasHistory || Jump > SnapDistance || (bTeleportSeen && Jump > TeleportCutDistance) || bRotationSnap;
	if (bCut && bHasHistory)
	{
		++CutCount;
	}
	LastTargetRotation = TargetRotation;
	bPendingReset = false;
	bTeleportSeen = false;
	bHasHistory = true;
	LastBase = Base;

	const FRotator DesiredRot = LagRotation(TargetRotation, bDoRotationLag, bCut, Delta);

	// The pivot's target: her centre, its height held inside the dead zone while she is in the air.
	FVector PivotTarget = Base;
	const float Reference = UpdateHoldReference(Base.Z, bCut, Delta);
	if (bVerticalHold && !bCut)
	{
		PivotTarget.Z = Reference + HawkeyeCameraRules::DeadZoneOffset(Base.Z - Reference, VerticalDeadZone);
	}
	const FVector LaggedBase = LagPivot(PivotTarget, bDoLocationLag, bCut, Delta);

	const FRotationMatrix Axes(DesiredRot);
	const FVector Forward = DesiredRot.Vector();
	const FVector Right = Axes.GetScaledAxis(EAxis::Y);

	const bool bSweep = bDoTrace && TargetArmLength != 0.f && World;
	FVector PivotOffset = TargetOffset;
	float Shoulder01 = 1.f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(HawkeyeSpringArm), false, GetOwner());
	if (bSweep)
	{
		ResolveShoulder(Base, Right, Query, Delta, bCut, PivotOffset, Shoulder01);
	}
	else
	{
		LiftScale = ShoulderScale = ArmFraction = FeelerFraction = 1.f;
		FloorCorrection = 0.f;
	}

	// The lift is added after the lag, exact; the shoulder is the offset the sweeps allowed.
	const FVector Socket(SocketOffset.X, SocketOffset.Y * Shoulder01, SocketOffset.Z);
	const FVector LensTarget = LaggedBase + PivotOffset - Forward * TargetArmLength + Axes.TransformVector(Socket);
	const FVector Pivot = Base + PivotOffset;
	const FVector Shoulder = Pivot + Right * (SocketOffset.Y * Shoulder01);
	ResolvedShoulder = Shoulder;

	FVector Lens = LensTarget;
	if (bSweep)
	{
		Lens = Shoulder + (LensTarget - Shoulder) * SweepArm(Shoulder, LensTarget, Query, Delta, bCut);
		Lens = ApplyFloorRule(Lens, Base, Query, Delta, bCut);
	}
	UnfixedCameraPosition = LensTarget;

	// The shake goes on last and is itself swept, so a hit's shake cannot put the lens in a wall.
	if (!PostSweepShake.IsNearlyZero(0.05f))
	{
		const FVector Shaken = Lens + Axes.TransformVector(FVector(0.f, PostSweepShake.X, PostSweepShake.Y));
		FHitResult Hit;
		if (bSweep && World->SweepSingleByChannel(Hit, Lens, Shaken, FQuat::Identity, ProbeChannel,
				FCollisionShape::MakeSphere(ProbeSize * 0.5f), Query))
		{
			Lens = Hit.bStartPenetrating ? Lens : FVector(Hit.Location);
		}
		else
		{
			Lens = Shaken;
		}
	}
	bIsCameraFixed = !Lens.Equals(LensTarget, 0.5);
	DrawDebug(Pivot, Shoulder, Lens, Forward);

	const FTransform WorldCamTM(DesiredRot, Lens);
	const FTransform RelCamTM = WorldCamTM.GetRelativeTransform(GetComponentTransform());
	RelativeSocketLocation = RelCamTM.GetLocation();
	RelativeSocketRotation = RelCamTM.GetRotation();

	UpdateChildTransforms();
}
