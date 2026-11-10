#include "Player/HawkeyeMovementComponent.h"

namespace HawkeyeZip
{
	static constexpr uint8 ZipMode = static_cast<uint8>(EHawkeyeCustomMovement::Zip);

	/** A straighten and a retry on the same step is the most the grapple's rules ever ask for. */
	static constexpr int32 MaxHitResponses = 2;
}

bool UHawkeyeMovementComponent::IsZipping() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == HawkeyeZip::ZipMode;
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
	return IsZipping() ? TEXT("Zip") : Super::GetMovementName();
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
	if (PreviousMovementMode != MOVE_Custom || PreviousCustomMode != HawkeyeZip::ZipMode || IsZipping())
	{
		return;
	}
	const EHawkeyeZipEnd Reason = PendingZipEnd;
	PendingZipEnd = EHawkeyeZipEnd::Interrupted;
	bZipHopping = false;
	bZipRedirecting = false;
	ZipDirection = FVector::ZeroVector;
	ZipLineSpeed = 0.f;
	OnZipEnded.ExecuteIfBound(Reason);
}
