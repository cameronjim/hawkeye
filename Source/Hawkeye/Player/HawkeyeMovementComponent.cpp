#include "Player/HawkeyeMovementComponent.h"

#include "GameFramework/Actor.h"
#include "Hawkeye.h"

namespace HawkeyeZip
{
	static constexpr uint8 ZipMode = static_cast<uint8>(EHawkeyeCustomMovement::Zip);

	/** A straighten and a retry on the same step is the most the grapple's rules ever ask for. */
	static constexpr int32 MaxHitResponses = 2;
}

namespace HawkeyeTraversal
{
	static constexpr uint8 TraversalMode = static_cast<uint8>(EHawkeyeCustomMovement::Traversal);
}

bool UHawkeyeMovementComponent::IsZipping() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == HawkeyeZip::ZipMode;
}

bool UHawkeyeMovementComponent::IsTraversing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == HawkeyeTraversal::TraversalMode;
}

float UHawkeyeMovementComponent::GetZipProgress() const
{
	if (!IsZipping())
	{
		return 0.f;
	}
	return ZipLength > 0.f ? ZipTravelled / ZipLength : 1.f;
}

float UHawkeyeMovementComponent::GetMaxSpeed() const
{
	return IsZipping() ? ZipSpeed : Super::GetMaxSpeed();
}

FString UHawkeyeMovementComponent::GetMovementName() const
{
	if (IsZipping())
	{
		return TEXT("Zip");
	}
	return IsTraversing() ? TEXT("Traversal") : Super::GetMovementName();
}

void UHawkeyeMovementComponent::StartZip(const FHawkeyeZipLine& Line)
{
	if (!UpdatedComponent)
	{
		return;
	}
	const bool bWasZipping = IsZipping();
	const FVector LineDirection = (Line.End - Line.Launch).GetSafeNormal();
	// A fresh zip carries the momentum she brings along the line; a chain carries the zip's own.
	if (!bWasZipping)
	{
		ZipLineSpeed = FMath::Clamp(static_cast<float>(FVector::DotProduct(Velocity, LineDirection)),
			FMath::Min(ZipMinStartSpeed, ZipSpeed), ZipSpeed);
	}
	const bool bRedirect = bWasZipping && !bZipHopping;
	bZipRedirecting = bRedirect && RedirectBlendSeconds > 0.f && !ZipDirection.IsNearlyZero();
	RedirectElapsed = 0.f;
	RedirectFromDirection = ZipDirection;
	if (!bZipRedirecting)
	{
		ZipDirection = LineDirection;
	}

	ZipHopStart = UpdatedComponent->GetComponentLocation();
	ZipLaunch = Line.Launch;
	ZipEnd = Line.End;
	ZipLength = FVector::Dist(ZipLaunch, ZipEnd);
	ZipTravelled = 0.f;
	bZipHopping = Line.bHop && ZipHopSeconds > 0.f;
	HopElapsed = 0.f;

	// A chain turns her with the direction of travel; anything else faces the landing point at once.
	const FVector Facing = FVector::VectorPlaneProject(ZipEnd - ZipHopStart, GetGravityDirection()).GetSafeNormal();
	if (!bZipRedirecting && !Facing.IsNearlyZero())
	{
		MoveUpdatedComponent(FVector::ZeroVector, Facing.ToOrientationQuat(), /*bSweep=*/false);
	}
	if (!bWasZipping)
	{
		PendingZipEnd = EHawkeyeZipEnd::Interrupted;
		SetMovementMode(MOVE_Custom, HawkeyeZip::ZipMode);
	}
}

void UHawkeyeMovementComponent::ReleaseZip(bool bJump)
{
	if (IsZipping())
	{
		const FVector Boost = bJump ? -GetGravityDirection() * ReleaseJumpBoost : FVector::ZeroVector;
		EndZip(EHawkeyeZipEnd::Released, Velocity * ReleaseVelocityScale + Boost);
	}
}

void UHawkeyeMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (CustomMovementMode == HawkeyeZip::ZipMode)
	{
		PhysZip(DeltaTime, Iterations);
		return;
	}
	if (CustomMovementMode == HawkeyeTraversal::TraversalMode)
	{
		PhysTraversal(DeltaTime, Iterations);
		return;
	}
	Super::PhysCustom(DeltaTime, Iterations);
}

void UHawkeyeMovementComponent::PhysZip(float DeltaTime, int32 Iterations)
{
	float RemainingTime = DeltaTime;
	while (IsZipping() && RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations)
	{
		++Iterations;
		const float TimeTick = GetSimulationTimeStep(RemainingTime, Iterations);
		RemainingTime -= TimeTick;
		bJustTeleported = false;

		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const float LineTime = bZipHopping ? StepZipHop(TimeTick) : TimeTick;
		// Arriving or blocked ends the zip with the velocity it leaves with; the rest of the frame is not spent.
		if (LineTime >= MIN_TICK_TIME && !StepZipLine(LineTime))
		{
			return;
		}
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / TimeTick;
		OnZipStep.ExecuteIfBound();
	}
}

float UHawkeyeMovementComponent::StepZipHop(float DeltaTime)
{
	const float HopTime = FMath::Min(DeltaTime, ZipHopSeconds - HopElapsed);
	HopElapsed += HopTime;
	const float Alpha = FMath::Clamp(HopElapsed / ZipHopSeconds, 0.f, 1.f);
	// Through whatever she stood on or beside: the launch point is above it, and the grapple has
	// already checked the line from there.
	const FVector Target = FMath::Lerp(ZipHopStart, ZipLaunch, FMath::Sin(Alpha * HALF_PI));
	MoveUpdatedComponent(Target - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(),
		/*bSweep=*/false);
	bZipHopping = Alpha < 1.f;
	return DeltaTime - HopTime;
}

float UHawkeyeMovementComponent::AdvanceZipSpeed(float DeltaTime)
{
	if (ZipLineSpeed >= ZipSpeed || ZipAcceleration <= 0.f)
	{
		return ZipLineSpeed * DeltaTime;
	}
	// Exact for a constant acceleration that stops at ZipSpeed, so the step size never changes the result.
	const float AccelTime = FMath::Min(DeltaTime, (ZipSpeed - ZipLineSpeed) / ZipAcceleration);
	const float Distance = ZipLineSpeed * AccelTime + 0.5f * ZipAcceleration * AccelTime * AccelTime;
	ZipLineSpeed = FMath::Min(ZipSpeed, ZipLineSpeed + ZipAcceleration * AccelTime);
	return Distance + ZipLineSpeed * (DeltaTime - AccelTime);
}

bool UHawkeyeMovementComponent::StepZipLine(float DeltaTime)
{
	// Head for the landing point from wherever she is: on a straight line that is the line itself,
	// and after a chain the direction swings from the old line onto the new.
	const FVector Current = UpdatedComponent->GetComponentLocation();
	const FVector ToEnd = ZipEnd - Current;
	const float Remaining = ToEnd.Size();
	const FVector Straight = Remaining > KINDA_SMALL_NUMBER ? ToEnd / Remaining : ZipDirection;
	FVector Direction = Straight;
	bool bSwinging = bZipRedirecting;
	if (bSwinging)
	{
		RedirectElapsed += DeltaTime;
		const float Alpha = FMath::Clamp(RedirectElapsed / RedirectBlendSeconds, 0.f, 1.f);
		const FVector Blended = RedirectFromDirection * (1.f - Alpha) + Straight * Alpha;
		Direction = Blended.IsNearlyZero() ? Straight : Blended.GetSafeNormal();
		bZipRedirecting = Alpha < 1.f;
	}
	const float Step = AdvanceZipSpeed(DeltaTime);
	const bool bArrives = Remaining <= Step;
	FVector Target = bArrives ? ZipEnd : Current + Direction * Step;
	const FVector Flat = FVector::VectorPlaneProject(Direction, GetGravityDirection()).GetSafeNormal();
	const FQuat Rotation = bSwinging && !Flat.IsNearlyZero() ? Flat.ToOrientationQuat() : UpdatedComponent->GetComponentQuat();

	FHitResult Hit;
	SafeMoveUpdatedComponent(Target - Current, Rotation, /*bSweep=*/true, Hit);
	for (int32 Response = 0; Hit.IsValidBlockingHit() && Response < HawkeyeZip::MaxHitResponses; ++Response)
	{
		const EHawkeyeZipHitResponse Answer = OnZipHit.IsBound() ? OnZipHit.Execute(Hit, bSwinging, bArrives)
			: EHawkeyeZipHitResponse::Stop;
		if (Answer == EHawkeyeZipHitResponse::Stop)
		{
			break;
		}
		if (Answer == EHawkeyeZipHitResponse::Straighten)
		{
			// Back to where the step began and along the line itself, which the grapple swept clear.
			MoveUpdatedComponent(Current - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(),
				/*bSweep=*/false);
			bZipRedirecting = false;
			bSwinging = false;
			Direction = Straight;
			Target = Current + Straight * Step;
		}
		Hit = FHitResult();
		SafeMoveUpdatedComponent(Target - UpdatedComponent->GetComponentLocation(), Rotation, /*bSweep=*/true, Hit);
	}
	ZipDirection = Direction;
	if (Hit.IsValidBlockingHit())
	{
		EndZip(EHawkeyeZipEnd::Blocked, FVector::ZeroVector);
		return false;
	}
	// Progress is how much nearer the landing point she is, never going back, so a chain's swing cannot undo it.
	ZipTravelled = FMath::Clamp(ZipLength - static_cast<float>(FVector::Dist(UpdatedComponent->GetComponentLocation(), ZipEnd)),
		ZipTravelled, ZipLength);
	if (bArrives)
	{
		ZipTravelled = ZipLength;
		const FVector Along = FVector::VectorPlaneProject(ZipDirection, GetGravityDirection()).GetSafeNormal();
		EndZip(EHawkeyeZipEnd::Arrived, Along * FMath::Min(ZipLineSpeed, ArrivalCarrySpeed));
		return false;
	}
	return true;
}

void UHawkeyeMovementComponent::EndZip(EHawkeyeZipEnd Reason, const FVector& NewVelocity)
{
	PendingZipEnd = Reason;
	Velocity = NewVelocity;
	SetMovementMode(Reason == EHawkeyeZipEnd::Arrived ? MOVE_Walking : MOVE_Falling);
	if (Reason != EHawkeyeZipEnd::Arrived || !IsMovingOnGround())
	{
		return;
	}
	// The anchor's roof is only there to stand on once OnZipEnded has let the grapple stop ignoring it,
	// so the floor is looked for again after the mode change.
	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, /*bCanUseCachedLocation=*/false);
	if (!CurrentFloor.IsWalkableFloor())
	{
		SetMovementMode(MOVE_Falling);
		return;
	}
	AdjustFloorHeight();
	SetBaseFromFloor(CurrentFloor);
}

void UHawkeyeMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	if (PreviousMovementMode != MOVE_Custom)
	{
		return;
	}
	if (PreviousCustomMode == HawkeyeZip::ZipMode && !IsZipping())
	{
		const EHawkeyeZipEnd Reason = PendingZipEnd;
		PendingZipEnd = EHawkeyeZipEnd::Interrupted;
		bZipHopping = false;
		bZipRedirecting = false;
		ZipDirection = FVector::ZeroVector;
		ZipLineSpeed = 0.f;
		OnZipEnded.ExecuteIfBound(Reason);
	}
	else if (PreviousCustomMode == HawkeyeTraversal::TraversalMode && !IsTraversing())
	{
		// Our own ways out cleared it before the mode changed; anything else must not leave an obstacle ignored.
		ClearTraversalPassThrough();
		const EHawkeyeTraversalEnd Reason = PendingTraversalEnd;
		PendingTraversalEnd = EHawkeyeTraversalEnd::Interrupted;
		bTraversalHolding = false;
		TraversalMove = FHawkeyeTraversalMove();
		OnTraversalEnded.ExecuteIfBound(Reason);
	}
}

void UHawkeyeMovementComponent::StartTraversal(const FHawkeyeTraversalMove& Move)
{
	if (!UpdatedComponent)
	{
		return;
	}
	ClearTraversalPassThrough();
	TraversalMove = Move;
	TraversalElapsed = 0.f;
	bTraversalHolding = false;
	bTraversalHitLogged = false;
	if (UpdatedPrimitive)
	{
		for (const TWeakObjectPtr<AActor>& Actor : TraversalMove.PassThrough)
		{
			if (Actor.IsValid() && !UpdatedPrimitive->GetMoveIgnoreActors().Contains(Actor.Get()))
			{
				UpdatedPrimitive->IgnoreActorWhenMoving(Actor.Get(), true);
				TraversalIgnored.Add(Actor);
			}
		}
	}
	if (!IsTraversing())
	{
		PendingTraversalEnd = EHawkeyeTraversalEnd::Interrupted;
		SetMovementMode(MOVE_Custom, HawkeyeTraversal::TraversalMode);
	}
}

void UHawkeyeMovementComponent::MoveTraversalHold(const FVector& Location)
{
	if (IsTraversalHolding())
	{
		TraversalHoldPoint = Location;
	}
}

void UHawkeyeMovementComponent::ReleaseTraversal(const FVector& NewVelocity, const FVector& StepOff)
{
	if (!IsTraversing())
	{
		return;
	}
	ClearTraversalPassThrough();
	if (!StepOff.IsNearlyZero())
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(StepOff, UpdatedComponent->GetComponentQuat(), /*bSweep=*/true, Hit);
	}
	LeaveTraversal(MOVE_Falling, NewVelocity, EHawkeyeTraversalEnd::Released);
}

void UHawkeyeMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!IsTraversing())
	{
		Super::PhysicsRotation(DeltaTime);
	}
}

void UHawkeyeMovementComponent::PhysTraversal(float DeltaTime, int32 Iterations)
{
	if (bTraversalHolding)
	{
		// The held point moves only when a shimmy moves it, once a frame: one step covers it.
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		StepTraversalTo(TraversalHoldPoint, {});
		Velocity = DeltaTime > 0.f ? (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime : FVector::ZeroVector;
		return;
	}
	// Set from the montage's root motion before the physics ran; constant over the frame's sub-steps.
	const bool bRootMotion = TraversalMove.Keys.IsEmpty();
	const FVector RootMotionVelocity = bRootMotion && HasAnimRootMotion() ? Velocity : FVector::ZeroVector;
	float RemainingTime = DeltaTime;
	while (IsTraversing() && !bTraversalHolding && RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations)
	{
		++Iterations;
		const float TimeTick = GetSimulationTimeStep(RemainingTime, Iterations);
		RemainingTime -= TimeTick;
		bJustTeleported = false;
		// The path is a function of time, not of the steps taken, so the frame rate cannot change where she goes.
		TraversalElapsed = FMath::Min(TraversalElapsed + TimeTick, TraversalMove.Seconds);
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		if (bRootMotion)
		{
			StepTraversalTo(OldLocation + RootMotionVelocity * TimeTick, {});
		}
		else
		{
			const FHawkeyeTraversalKey Key = TraversalMove.Sample(
				TraversalMove.Seconds > 0.f ? TraversalElapsed / TraversalMove.Seconds : 1.f);
			StepTraversalTo(Key.Location, Key.Yaw);
		}
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / TimeTick;
		if (TraversalElapsed >= TraversalMove.Seconds)
		{
			FinishTraversal();
		}
	}
}

void UHawkeyeMovementComponent::StepTraversalTo(const FVector& Target, TOptional<float> Yaw)
{
	const FVector Delta = Target - UpdatedComponent->GetComponentLocation();
	const FQuat Rotation = Yaw.IsSet() ? FRotator(0.f, Yaw.GetValue(), 0.f).Quaternion() : UpdatedComponent->GetComponentQuat();
	FHitResult Hit;
	SafeMoveUpdatedComponent(Delta, Rotation, /*bSweep=*/true, Hit);
	if (!Hit.IsValidBlockingHit())
	{
		return;
	}
	if (!bTraversalHitLogged)
	{
		bTraversalHitLogged = true;
		UE_LOG(LogHawkeye, Log, TEXT("%s: traversal: %s is in the way at %s; sliding along it"), *GetNameSafe(GetOwner()),
			*GetNameSafe(Hit.GetActor()), *FVector(Hit.ImpactPoint).ToCompactString());
	}
	SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, /*bHandleImpact=*/true);
}

void UHawkeyeMovementComponent::FinishTraversal()
{
	if (TraversalMove.Exit == EHawkeyeTraversalExit::Hold)
	{
		// Held where she is (the path's end unless something stopped her); the hang's wall stays passed through.
		bTraversalHolding = true;
		TraversalHoldPoint = UpdatedComponent->GetComponentLocation();
		Velocity = FVector::ZeroVector;
		OnTraversalEnded.ExecuteIfBound(EHawkeyeTraversalEnd::Held);
		return;
	}
	// The top she climbed onto is a floor again before Walking looks for one.
	ClearTraversalPassThrough();
	const bool bWalk = TraversalMove.Exit == EHawkeyeTraversalExit::Walk;
	LeaveTraversal(bWalk ? MOVE_Walking : MOVE_Falling, TraversalMove.ExitVelocity, EHawkeyeTraversalEnd::Exited);
}

void UHawkeyeMovementComponent::ClearTraversalPassThrough()
{
	if (UpdatedPrimitive)
	{
		for (const TWeakObjectPtr<AActor>& Actor : TraversalIgnored)
		{
			if (Actor.IsValid())
			{
				UpdatedPrimitive->IgnoreActorWhenMoving(Actor.Get(), false);
			}
		}
	}
	TraversalIgnored.Reset();
}

void UHawkeyeMovementComponent::LeaveTraversal(EMovementMode Mode, const FVector& NewVelocity, EHawkeyeTraversalEnd End)
{
	PendingTraversalEnd = End;
	Velocity = NewVelocity;
	SetMovementMode(Mode);
	// Entering Walking found the floor; a landing over a drop has none.
	if (MovementMode == MOVE_Walking && !CurrentFloor.IsWalkableFloor())
	{
		SetMovementMode(MOVE_Falling);
	}
}
