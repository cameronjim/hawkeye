#include "Combat/AimAssist.h"

#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Settings/DifficultySubsystem.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "World/ThugCharacter.h"

namespace HawkeyeAimAssist
{
	/** Story, Normal, Hard: every cone, pull and bend is multiplied by the table's AimAssistScale. */
	static float DifficultyScale(EHawkeyeDifficulty Difficulty)
	{
		return UDifficultySubsystem::GetTableValue(Difficulty, EDifficultyStat::AimAssistScale);
	}
}

FHawkeyeAimAssistTuning UHawkeyeAimAssist::GetTuning(EHawkeyeAimAssist Level, EHawkeyeDifficulty Difficulty)
{
	FHawkeyeAimAssistTuning Tuning;
	if (Level == EHawkeyeAimAssist::Off)
	{
		return Tuning;
	}
	const bool bStrong = Level == EHawkeyeAimAssist::Strong;
	const float Scale = HawkeyeAimAssist::DifficultyScale(Difficulty);
	Tuning.MagnetismConeDegrees = (bStrong ? 7.f : 4.f) * Scale;
	Tuning.SnapConeDegrees = 12.f * Scale;
	Tuning.BendMaxDegrees = (bStrong ? 4.f : 2.f) * Scale;
	Tuning.PadPullDegreesPerSecond = (bStrong ? 10.f : 6.f) * Scale;
	Tuning.MousePullDegreesPerSecond = Tuning.PadPullDegreesPerSecond / 3.f;
	// The slowdown is how much of the rate is taken away at his chest; the scale shrinks or grows that part.
	Tuning.PadSlowdown = 1.f - FMath::Clamp(0.4f * Scale, 0.f, 0.8f);
	Tuning.MouseSlowdown = 1.f - FMath::Clamp(0.15f * Scale, 0.f, 0.8f);
	return Tuning;
}

FText UHawkeyeAimAssist::GetLevelName(EHawkeyeAimAssist Level)
{
	switch (Level)
	{
	case EHawkeyeAimAssist::Off:
		return NSLOCTEXT("Hawkeye", "AimAssistOff", "Off");
	case EHawkeyeAimAssist::Strong:
		return NSLOCTEXT("Hawkeye", "AimAssistStrong", "Strong");
	default:
		return NSLOCTEXT("Hawkeye", "AimAssistNormal", "Normal");
	}
}

FHawkeyeAimAssistTuning UHawkeyeAimAssist::GetTuningFor(const UObject* WorldContextObject)
{
	const FHawkeyeSettings Settings = UHawkeyeSettingsSubsystem::GetCurrentSettings(WorldContextObject);
	return GetTuning(Settings.AimAssist, Settings.Difficulty);
}

float UHawkeyeAimAssist::AngleToPoint(const FVector& From, const FVector& Direction, const FVector& Point)
{
	const FVector To = (Point - From).GetSafeNormal();
	const FVector Dir = Direction.GetSafeNormal();
	if (To.IsNearlyZero() || Dir.IsNearlyZero())
	{
		return 0.f;
	}
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(To, Dir), -1.f, 1.f)));
}

int32 UHawkeyeAimAssist::FindBestIndex(const FVector& From, const FVector& Direction,
	const TArray<FHawkeyeAimAssistCandidate>& Candidates, float ConeDegrees, float InMaxRange)
{
	int32 Best = INDEX_NONE;
	float BestAngle = ConeDegrees;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FVector& Chest = Candidates[Index].Chest;
		if (FVector::Dist(From, Chest) > InMaxRange)
		{
			continue;
		}
		const float Angle = AngleToPoint(From, Direction, Chest);
		if (Angle <= BestAngle + KINDA_SMALL_NUMBER)
		{
			Best = Index;
			BestAngle = Angle;
		}
	}
	return Best;
}

bool UHawkeyeAimAssist::IsLineOnBody(const FVector& From, const FVector& Direction, const FHawkeyeAimAssistCandidate& Candidate)
{
	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}
	// The closest points between the aim ray and the body's feet-to-head segment.
	const FVector RayEnd = From + Dir * (FVector::Dist(From, Candidate.Chest) + 500.f);
	FVector OnRay;
	FVector OnBody;
	FMath::SegmentDistToSegmentSafe(From, RayEnd, Candidate.Feet, Candidate.Head, OnRay, OnBody);
	return FVector::DotProduct(OnRay - From, Dir) > 0.f && FVector::Dist(OnRay, OnBody) <= Candidate.Radius;
}

float UHawkeyeAimAssist::ComputeLookScale(float AngleDegrees, float ConeDegrees, float Slowdown)
{
	if (ConeDegrees <= 0.f || AngleDegrees >= ConeDegrees)
	{
		return 1.f;
	}
	const float Alpha = FMath::Clamp(AngleDegrees / ConeDegrees, 0.f, 1.f);
	return FMath::Lerp(FMath::Clamp(Slowdown, 0.f, 1.f), 1.f, Alpha);
}

FRotator UHawkeyeAimAssist::ComputePull(const FRotator& ViewRotation, const FVector& ViewLocation, const FVector& Point,
	float ConeDegrees, float PullDegreesPerSecond, float DeltaSeconds)
{
	const float Angle = AngleToPoint(ViewLocation, ViewRotation.Vector(), Point);
	if (ConeDegrees <= 0.f || Angle >= ConeDegrees || Angle <= KINDA_SMALL_NUMBER || PullDegreesPerSecond <= 0.f
		|| DeltaSeconds <= 0.f)
	{
		return ViewRotation;
	}
	const float Strength = 1.f - Angle / ConeDegrees;
	const float Step = FMath::Min(Angle, PullDegreesPerSecond * Strength * DeltaSeconds);
	const FQuat From = ViewRotation.Quaternion();
	const FQuat To = ComputeSnapRotation(ViewLocation, Point).Quaternion();
	FRotator Pulled = FQuat::Slerp(From, To, Step / Angle).Rotator();
	Pulled.Roll = 0.f;
	return Pulled;
}

FRotator UHawkeyeAimAssist::ComputeSnapRotation(const FVector& ViewLocation, const FVector& Point)
{
	FRotator Rotation = (Point - ViewLocation).Rotation();
	Rotation.Roll = 0.f;
	return Rotation;
}

FVector UHawkeyeAimAssist::BendToward(const FVector& Aim, const FVector& Wanted, float MaxDegrees)
{
	const FVector From = Aim.GetSafeNormal();
	const FVector To = Wanted.GetSafeNormal();
	if (From.IsNearlyZero() || To.IsNearlyZero() || MaxDegrees <= 0.f)
	{
		return From;
	}
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(From, To), -1.f, 1.f)));
	if (Angle <= MaxDegrees)
	{
		return To;
	}
	return FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(From, To), MaxDegrees / Angle).RotateVector(From).GetSafeNormal();
}

void UHawkeyeAimAssist::GatherCandidates(const UWorld* World, const FVector& From, float InMaxRange, const AActor* IgnoreActor,
	TArray<FHawkeyeAimAssistCandidate>& OutCandidates)
{
	OutCandidates.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AThugCharacter> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const AThugCharacter* Thug = *It;
		const UHealthComponent* Health = Thug->GetHealthComponent();
		if (!Health || !Health->IsAlive() || Thug->IsLimp() || Thug->IsKnockedDown())
		{
			continue;
		}
		const FVector Centre = Thug->GetActorLocation();
		if (FVector::Dist(From, Centre) > InMaxRange)
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Thug->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
		FHawkeyeAimAssistCandidate Candidate;
		Candidate.Actor = const_cast<AThugCharacter*>(Thug);
		Candidate.Chest = Centre + FVector(0.f, 0.f, ChestHeight);
		Candidate.Feet = Centre - FVector(0.f, 0.f, HalfHeight);
		Candidate.Head = Centre + FVector(0.f, 0.f, HalfHeight);
		Candidate.Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f;
		Candidate.Velocity = Thug->GetVelocity();

		// Only thugs she can see: nothing between her eye and his chest.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeAimAssist), /*bTraceComplex=*/false, IgnoreActor);
		Params.AddIgnoredActor(Thug);
		FHitResult Blocker;
		if (World->LineTraceSingleByChannel(Blocker, From, Candidate.Chest, ECC_Visibility, Params))
		{
			continue;
		}
		OutCandidates.Add(Candidate);
	}
}
