// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ParkourComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/Paths.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/GaspTraversal.h"

namespace HawkeyeParkour
{
	static const TCHAR* TraversalAnims = TEXT("/Game/Characters/UEFN_Mannequin/Animations/Traversal/");

	static FHawkeyeParkourClip MakeClip(const TCHAR* RelativePath, float StartTime, float EndTime)
	{
		FHawkeyeParkourClip Clip;
		const FString Package = FString(TraversalAnims) + RelativePath;
		Clip.Sequence = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(Package + TEXT(".") + FPaths::GetBaseFilename(Package)));
		Clip.StartTime = StartTime;
		Clip.EndTime = EndTime;
		return Clip;
	}

	/** Heights the front face is probed at, above the feet. The lowest skips kerbs and steps. */
	static constexpr float FaceProbeLowest = 35.f;
	static constexpr float FaceProbeStep = 35.f;
	/** How far behind the front face the top is looked for, cm. Less than a fire-escape rail is thick. */
	static constexpr float TopInset = 4.f;
	/** A ledge higher than this above the feet needs open air from here up on the character's side (a ceiling fails). */
	static constexpr float HeadroomProbeFrom = 100.f;

	/** A top this steep is not a top. */
	static constexpr float MinFloorNormalZ = 0.7f;
	/** Step between back-edge probes, cm. */
	static constexpr float BeyondStep = 15.f;
	/** Past the back edge, the landing capsule is this much further out, cm. */
	static constexpr float LandingClearance = 25.f;
	/** If the capsule does not fit there, the landing is looked for up to this much further out, cm. */
	static constexpr float LandingSearch = 75.f;
	/** On top, the standing capsule is this much further in than its radius, cm. */
	static constexpr float StandInset = 15.f;
	/** The top of the landing search below the feet, cm. */
	static constexpr float MaxProbeDrop = 800.f;
	/** Capsule centres are placed this far above the floor so the first floor check finds it. */
	static constexpr float FloorGap = 2.f;
	/** Step between the probes that walk out to an edge, cm. */
	static constexpr float EdgeStep = 5.f;
	/** How far below the feet the drop beyond an edge is measured, cm. */
	static constexpr float EdgeProbeDepth = 1000.f;
	/** A floor within this of the feet is still the floor being stood on, cm. */
	static constexpr float FloorTolerance = 20.f;
	/** The drop to hang hops this far over the lip on its way over, cm. */
	static constexpr float DropHopClearance = 30.f;
	/** In the air the front face is probed from this far above the feet, cm. */
	static constexpr float AirProbeLowest = 5.f;
	/** In the air a top this far above the feet is still worth a mantle (she would slide down its face). */
	static constexpr float AirMinHeight = 8.f;
	/**
	 * A walking capsule floats up to 2.4 cm over the floor (the movement's MAX_FLOOR_DIST), so a top
	 * measured from the capsule's bottom reads that much low: a 40 cm wall is 38 above the "feet".
	 * The lowest accepted height is compared this much short of its value.
	 */
	static constexpr float HeightSlack = 3.f;
	/** A ledge within this of the one just let go of is the same ledge, cm. */
	static constexpr float SameLedgeTolerance = 40.f;
}

UParkourComponent::UParkourComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	// The Game Animation Sample's clips (copied by import_gasp.py). Times from the root motion:
	// the hurdle clears 1 m between 0.1 and 1.0 s, the mantle rises 1 m and steps on between 0.3 and
	// 1.4 s, and the 2.5 m climb has both hands on the edge at 0.75 s and stands on top by 2.1 s.
	VaultClip = HawkeyeParkour::MakeClip(TEXT("Hurdle/M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Lfoot"), 0.1f, 1.0f);
	MantleClip = HawkeyeParkour::MakeClip(TEXT("Mantle/M_Neutral_Traversal_Mantle_1_0_stand_F_Lfoot"), 0.3f, 1.4f);
	GrabClip = HawkeyeParkour::MakeClip(TEXT("Climb/M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot"), 0.3f, 0.75f);
	CatchClip = HawkeyeParkour::MakeClip(TEXT("Climb/M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot"), 0.65f, 0.75f);
	ClimbClip = HawkeyeParkour::MakeClip(TEXT("Climb/M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot"), 0.75f, 2.1f);
}

void UParkourComponent::BeginPlay()
{
	Super::BeginPlay();
	SampleTraversal = HawkeyeGaspTraversal::FindTraversalLogic(GetOwner());
}

void UParkourComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreRootMotionMode();
	Super::EndPlay(EndPlayReason);
}

ACharacter* UParkourComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

UCharacterMovementComponent* UParkourComponent::GetMovement() const
{
	const ACharacter* Character = GetCharacter();
	return Character ? Character->GetCharacterMovement() : nullptr;
}

float UParkourComponent::GetFeetZ() const
{
	const ACharacter* Character = GetCharacter();
	return Character ? Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
}

bool UParkourComponent::IsSampleTraversalActive() const
{
	return HawkeyeGaspTraversal::IsDoingTraversal(SampleTraversal.Get());
}

float UParkourComponent::GetMoveSeconds(EHawkeyeParkourMove Move) const
{
	switch (Move)
	{
	case EHawkeyeParkourMove::Vault: return VaultSeconds;
	case EHawkeyeParkourMove::Mantle: return MantleSeconds;
	case EHawkeyeParkourMove::LedgeGrab: return GrabSeconds;
	case EHawkeyeParkourMove::Climb: return ClimbSeconds;
	case EHawkeyeParkourMove::DropToHang: return DropToHangSeconds;
	case EHawkeyeParkourMove::HangCorner: return CornerSeconds;
	case EHawkeyeParkourMove::HangLeap: return HangLeapSeconds;
	case EHawkeyeParkourMove::HangHop: return HangHopSeconds;
	default: return 0.f;
	}
}

// --- Choosing -----------------------------------------------------------------------------------

EHawkeyeParkourMove UParkourComponent::ChooseMove(float Height, bool bClearBeyond, bool bStandingSurface) const
{
	return ChooseMoveFrom(VaultMinHeight, Height, bClearBeyond, bStandingSurface);
}

EHawkeyeParkourMove UParkourComponent::ChooseMoveFrom(float MinHeight, float Height, bool bClearBeyond, bool bStandingSurface) const
{
	if (Height < MinHeight || Height > LedgeMaxHeight)
	{
		return EHawkeyeParkourMove::None;
	}
	if (Height <= VaultMaxHeight)
	{
		if (bClearBeyond)
		{
			return EHawkeyeParkourMove::Vault;
		}
		return bStandingSurface ? EHawkeyeParkourMove::Mantle : EHawkeyeParkourMove::None;
	}
	if (Height <= MantleMaxHeight)
	{
		return bStandingSurface ? EHawkeyeParkourMove::Mantle : EHawkeyeParkourMove::None;
	}
	return EHawkeyeParkourMove::LedgeGrab;
}

EHawkeyeParkourMove UParkourComponent::ChooseMoveFor(const FHawkeyeParkourObstacle& Obstacle, bool bAuto) const
{
	if (!Obstacle.bFound)
	{
		return EHawkeyeParkourMove::None;
	}
	const float MaxDrop = bAuto ? MaxAutoVaultDrop : MaxVaultDrop;
	const bool bClear = Obstacle.bClearBeyond && Obstacle.LandingDrop <= MaxDrop;
	// The jump key takes lower tops than the sprint trigger: a knee-high wall is worth a hop over.
	// A probed height reads up to HeightSlack low (the capsule floats over the floor).
	const float MinHeight = bAuto ? VaultMinHeight : FMath::Min(JumpMinHeight, VaultMinHeight);
	return ChooseMoveFrom(MinHeight - HawkeyeParkour::HeightSlack, Obstacle.Height, bClear,
		Obstacle.bStandingSurface);
}

FString UParkourComponent::DescribeRefusal(const FHawkeyeParkourObstacle& Obstacle, bool bLateCatch) const
{
	const float MinHeight = FMath::Min(JumpMinHeight, VaultMinHeight);
	const float MaxHeight = bLateCatch ? CatchMaxHeight : LedgeMaxHeight;
	// In the air a top may be close above the feet and still count when it is well above the take-off.
	const bool bHighOverTakeOff = bLateCatch && Obstacle.Height >= HawkeyeParkour::AirMinHeight
		&& Obstacle.LedgePoint.Z - LateCatchTakeOffZ >= MinHeight - HawkeyeParkour::HeightSlack;
	if (Obstacle.Height < MinHeight - HawkeyeParkour::HeightSlack && !bHighOverTakeOff)
	{
		return FString::Printf(TEXT("too low (%.0f cm, from %.0f)"), Obstacle.Height, MinHeight);
	}
	if (Obstacle.Height > MaxHeight)
	{
		return FString::Printf(TEXT("too tall (%.0f cm, up to %.0f)"), Obstacle.Height, MaxHeight);
	}
	if (Obstacle.bClearBeyond && !Obstacle.bStandingSurface
		&& Obstacle.LandingDrop > (bLateCatch ? MaxAutoVaultDrop : MaxVaultDrop))
	{
		return FString::Printf(TEXT("a %.0f cm drop beyond and nowhere to stand on top"), Obstacle.LandingDrop);
	}
	if (!Obstacle.bClearBeyond && !Obstacle.BeyondWhyNot.IsEmpty())
	{
		return FString::Printf(TEXT("no room to stand on the %.0f cm top%s and no vault over it (%s)"), Obstacle.Height,
			Obstacle.StandWhyNot.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Obstacle.StandWhyNot),
			*Obstacle.BeyondWhyNot);
	}
	return FString::Printf(TEXT("no room to stand on the %.0f cm top"), Obstacle.Height);
}

// --- Detection ----------------------------------------------------------------------------------

bool UParkourComponent::TraceLine(const FVector& From, const FVector& To, FHitResult& OutHit) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeParkour), false, GetOwner());
	return World->LineTraceSingleByChannel(OutHit, From, To, ECC_Visibility, Params);
}

bool UParkourComponent::CapsuleFits(const FVector& Centre) const
{
	const ACharacter* Character = GetCharacter();
	const UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius() - 2.f;
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight() - 2.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeParkourFit), false, Character);
	return !World->OverlapBlockingTestByChannel(Centre, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
}

FString UParkourComponent::DescribeBlocker(const FVector& Centre) const
{
	const ACharacter* Character = GetCharacter();
	const UWorld* World = GetWorld();
	if (!Character || !World)
	{
		return TEXT("?");
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeParkourBlocker), false, Character);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Centre, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() - 2.f, Capsule->GetScaledCapsuleHalfHeight() - 2.f), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Overlap.bBlockingHit)
		{
			return FString::Printf(TEXT("%s.%s"), *GetNameSafe(Overlap.GetActor()), *GetNameSafe(Overlap.GetComponent()));
		}
	}
	return TEXT("nothing found");
}

bool UParkourComponent::FindFrontFace(const FVector& Feet, const FVector& Forward, float Reach, float MaxHeight,
	FHitResult& OutHit, float LowestProbe) const
{
	bool bFound = false;
	const float Lowest = LowestProbe >= 0.f ? LowestProbe : HawkeyeParkour::FaceProbeLowest;
	for (float Height = Lowest; Height <= MaxHeight; Height += HawkeyeParkour::FaceProbeStep)
	{
		const FVector From = Feet + FVector(0.f, 0.f, Height);
		FHitResult Hit;
		if (!TraceLine(From, From + Forward * Reach, Hit) || FMath::Abs(Hit.ImpactNormal.Z) > 0.5f)
		{
			continue;
		}
		if (!bFound || Hit.Distance < OutHit.Distance - 1.f)
		{
			OutHit = Hit;
			bFound = true;
		}
	}
	return bFound;
}

bool UParkourComponent::FindTop(const FHitResult& Face, const FVector& Normal, const FVector& Feet, float MaxHeight,
	FVector& OutTop) const
{
	const FVector Inside = Face.ImpactPoint - Normal * HawkeyeParkour::TopInset;
	FHitResult Top;
	const FVector From(Inside.X, Inside.Y, Feet.Z + MaxHeight + 50.f);
	const FVector To(Inside.X, Inside.Y, Face.ImpactPoint.Z - 1.f);
	if (!TraceLine(From, To, Top) || Top.ImpactNormal.Z < HawkeyeParkour::MinFloorNormalZ)
	{
		return false;
	}

	// Open air above the edge: from the character's side over the top, 30 cm up. A wall that
	// carries on above the probe (the trace started inside it) fails here.
	FHitResult Above;
	const float AboveZ = Top.ImpactPoint.Z + 30.f;
	const FVector Outside = Face.ImpactPoint + Normal * 40.f;
	if (TraceLine(FVector(Outside.X, Outside.Y, AboveZ), FVector(Inside.X, Inside.Y, AboveZ), Above))
	{
		return false;
	}
	// And open air on the character's side from head height up to it: the top of a floor slab seen
	// past the edge of the ceiling over her head is not a ledge she can reach (indoors, a stumble near
	// a wall used to hang her from the ceiling).
	const float HeadZ = Feet.Z + HawkeyeParkour::HeadroomProbeFrom;
	if (AboveZ > HeadZ && TraceLine(FVector(Outside.X, Outside.Y, HeadZ), FVector(Outside.X, Outside.Y, AboveZ), Above))
	{
		return false;
	}
	OutTop = Top.ImpactPoint;
	return true;

}

void UParkourComponent::ProbeBeyond(const FVector& Feet, FHawkeyeParkourObstacle& Obstacle) const
{
	const FVector& N = Obstacle.WallNormal;
	const float TopZ = Obstacle.LedgePoint.Z;
	float BackEdge = 0.f;
	for (float Depth = HawkeyeParkour::BeyondStep; Depth <= MaxVaultDepth; Depth += HawkeyeParkour::BeyondStep)
	{
		const FVector Probe = Obstacle.LedgePoint - N * Depth;
		FHitResult Hit;
		const bool bHit = TraceLine(FVector(Probe.X, Probe.Y, TopZ + 30.f), FVector(Probe.X, Probe.Y, TopZ - 30.f), Hit);
		if (!bHit || Hit.ImpactPoint.Z < TopZ - 20.f)
		{
			BackEdge = Depth;
			break;
		}
	}
	if (BackEdge <= 0.f)
	{
		Obstacle.BeyondWhyNot = FString::Printf(TEXT("no back edge within %.0f cm"), MaxVaultDepth);
		return;
	}
	Obstacle.Depth = BackEdge;

	const ACharacter* Character = GetCharacter();
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// The landing: first LandingClearance past the back edge, then further out until the capsule fits.
	// Behind a parapet there is often the neighbour's own parapet, lower (so the back edge is found at
	// the first one's back) and right where the first landing spot would be (2026-09-29: the commonest refusal
	// pressed against the tenement row's party walls from a standstill).
	FHitResult Floor;
	bool bLanded = false;
	for (float Extra = HawkeyeParkour::LandingClearance; Extra <= HawkeyeParkour::LandingClearance + HawkeyeParkour::LandingSearch;
		 Extra += HawkeyeParkour::BeyondStep)
	{
		const float Out = BackEdge + Radius + Extra;
		const FVector Land = Obstacle.LedgePoint - N * Out;
		const bool bFloor = TraceLine(FVector(Land.X, Land.Y, TopZ + 30.f), FVector(Land.X, Land.Y, Feet.Z - HawkeyeParkour::MaxProbeDrop), Floor);
		if (Extra == HawkeyeParkour::LandingClearance)
		{
			// The ground just past it, landable or not: what a jump over it would fall to.
			Obstacle.FarSideDrop = bFloor ? Feet.Z - Floor.ImpactPoint.Z : HawkeyeParkour::MaxProbeDrop;
		}
		if (!bFloor)
		{
			Obstacle.BeyondWhyNot = FString::Printf(TEXT("no floor within %.0f cm below the feet beyond it: a roof edge"), HawkeyeParkour::MaxProbeDrop);
			continue;
		}
		if (Floor.bStartPenetrating || Floor.ImpactNormal.Z < HawkeyeParkour::MinFloorNormalZ)
		{
			Obstacle.BeyondWhyNot = FString::Printf(TEXT("the ground %.0f cm beyond is not a floor (normal z %.2f, on %s)"),
				Out, Floor.ImpactNormal.Z, *GetNameSafe(Floor.GetActor()));
			continue;
		}
		if (Floor.ImpactPoint.Z > TopZ - 20.f)
		{
			// Level with the top (the next parapet's top): that is not over it.
			Obstacle.BeyondWhyNot = FString::Printf(TEXT("the far side %.0f cm beyond is as high as the top"), Out);
			continue;
		}
		const FVector Centre = Floor.ImpactPoint + FVector(0.f, 0.f, HalfHeight + HawkeyeParkour::FloorGap);
		if (!CapsuleFits(Centre))
		{
			Obstacle.BeyondWhyNot = FString::Printf(TEXT("no room to land %.0f cm beyond the face (%s in the way)"), Out,
				*DescribeBlocker(Centre));
			continue;
		}
		bLanded = true;
		break;
	}
	if (!bLanded)
	{
		return;
	}
	Obstacle.BeyondWhyNot.Reset();
	Obstacle.bClearBeyond = true;
	Obstacle.LandingPoint = Floor.ImpactPoint;
	Obstacle.LandingDrop = Feet.Z - Floor.ImpactPoint.Z;
}

void UParkourComponent::ProbeStanding(FHawkeyeParkourObstacle& Obstacle) const
{
	const ACharacter* Character = GetCharacter();
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float TopZ = Obstacle.LedgePoint.Z;
	const FVector Stand = Obstacle.LedgePoint - Obstacle.WallNormal * (Radius + HawkeyeParkour::StandInset);
	FHitResult Floor;
	const bool bTop = TraceLine(FVector(Stand.X, Stand.Y, TopZ + 30.f), FVector(Stand.X, Stand.Y, TopZ - 30.f), Floor)
		&& Floor.ImpactNormal.Z >= HawkeyeParkour::MinFloorNormalZ
		&& FMath::Abs(Floor.ImpactPoint.Z - TopZ) <= 25.f;
	const FVector Centre = Floor.ImpactPoint + FVector(0.f, 0.f, HalfHeight + HawkeyeParkour::FloorGap);
	const bool bOnTop = bTop && CapsuleFits(Centre);
	if (bTop && !bOnTop)
	{
		Obstacle.StandWhyNot = FString::Printf(TEXT("%s in the way on top"), *DescribeBlocker(Centre));
	}
	if (bOnTop)
	{
		Obstacle.bStandingSurface = true;
		Obstacle.StandPoint = Floor.ImpactPoint;
		return;
	}
	// A parapet: too thin to stand on, but the roof behind it is close below the top.
	if (Obstacle.bClearBeyond && Obstacle.LandingPoint.Z >= TopZ - ClimbOverMaxDrop)
	{
		Obstacle.bStandingSurface = true;
		Obstacle.StandPoint = Obstacle.LandingPoint;
	}
}

bool UParkourComponent::DetectObstacle(float MaxDistance, FHawkeyeParkourObstacle& OutObstacle) const
{
	return DetectObstacleUpTo(MaxDistance, LedgeMaxHeight, OutObstacle);
}

bool UParkourComponent::DetectObstacleUpTo(float MaxDistance, float MaxHeight, FHawkeyeParkourObstacle& OutObstacle) const
{
	const ACharacter* Character = GetCharacter();
	return DetectObstacleAlong(Character ? Character->GetActorForwardVector() : FVector::ForwardVector, MaxDistance,
		MaxHeight, OutObstacle);
}

bool UParkourComponent::DetectObstacleAlong(const FVector& Direction, float MaxDistance, float MaxHeight,
	FHawkeyeParkourObstacle& OutObstacle, float LowestProbe) const
{
	OutObstacle = FHawkeyeParkourObstacle();
	const ACharacter* Character = GetCharacter();
	const FVector Forward = Direction.GetSafeNormal2D();
	if (!Character || !GetWorld() || Forward.IsNearlyZero())
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());

	FHitResult Face;
	if (!FindFrontFace(Feet, Forward, Radius + MaxDistance, MaxHeight, Face, LowestProbe))
	{
		return false;
	}
	const FVector Normal = Face.ImpactNormal.GetSafeNormal2D();
	FVector Top;
	if (FVector::DotProduct(Normal, -Forward) < 0.5f || !FindTop(Face, Normal, Feet, MaxHeight, Top))
	{
		return false;
	}

	OutObstacle.bFound = true;
	OutObstacle.WallPoint = Face.ImpactPoint;
	OutObstacle.WallNormal = Normal;
	OutObstacle.LedgePoint = FVector(Face.ImpactPoint.X, Face.ImpactPoint.Y, Top.Z);
	OutObstacle.Height = Top.Z - Feet.Z;
	OutObstacle.Distance = FMath::Max(0.f, Face.Distance - Radius);
	OutObstacle.Actor = Face.GetActor();
	ProbeBeyond(Feet, OutObstacle);
	ProbeStanding(OutObstacle);
	return true;
}

// --- Starting -----------------------------------------------------------------------------------

FVector UParkourComponent::GetJumpProbeDirection() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return FVector::ForwardVector;
	}
	// This frame's input when it has been added already, else what the movement consumed last frame.
	FVector Input = Pawn->GetPendingMovementInputVector().GetSafeNormal2D();
	if (Input.IsNearlyZero())
	{
		Input = Pawn->GetLastMovementInputVector().GetSafeNormal2D();
	}
	return Input.IsNearlyZero() ? Pawn->GetActorForwardVector().GetSafeNormal2D() : Input;
}

bool UParkourComponent::TryParkour(bool bAuto)
{
	if (!bAuto)
	{
		return TryJumpParkour(GetJumpProbeDirection());
	}
	const UCharacterMovementComponent* Movement = GetMovement();
	if (IsBusy() || !Movement || !(Movement->IsMovingOnGround() || Movement->IsFalling()))
	{
		return false;
	}
	FHawkeyeParkourObstacle Obstacle;
	if (!DetectObstacle(AutoTriggerDistance, Obstacle))
	{
		return false;
	}
	const EHawkeyeParkourMove Move = ChooseMoveFor(Obstacle, bAuto);
	if (Move == EHawkeyeParkourMove::None)
	{
		return false;
	}
	return StartChosenMove(Move, Obstacle, TEXT("auto"));
}

bool UParkourComponent::StartChosenMove(EHawkeyeParkourMove Move, const FHawkeyeParkourObstacle& Obstacle, const TCHAR* Trigger)
{
	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement)
	{
		return false;
	}
	LastObstacle = Obstacle;
	LastJumpRefusal.Reset();
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s %s: %.0f cm high, %.0f cm away, depth %.0f, clear beyond %d (drop %.0f), stand %d, on %s"),
		*GetNameSafe(GetOwner()), Trigger, *UEnum::GetValueAsString(Move), Obstacle.Height,
		Obstacle.Distance, Obstacle.Depth, Obstacle.bClearBeyond ? 1 : 0, Obstacle.LandingDrop,
		Obstacle.bStandingSurface ? 1 : 0, *GetNameSafe(Obstacle.Actor));

	const bool bSampleMove = Move == EHawkeyeParkourMove::Vault || Move == EHawkeyeParkourMove::Mantle;
	if (bSampleMove && Movement->IsMovingOnGround())
	{
		// The sample probes along the facing; an angled press turns her to the wall first so it
		// finds the same one the fan did.
		ACharacter* Character = GetCharacter();
		const FVector ToWall = -Obstacle.WallNormal.GetSafeNormal2D();
		if (Character && !ToWall.IsNearlyZero()
			&& FVector::DotProduct(Character->GetActorForwardVector().GetSafeNormal2D(), ToWall) < 0.985f)
		{
			Character->SetActorRotation(FRotator(0.f, ToWall.Rotation().Yaw, 0.f));
		}
		if (TryStartSampleTraversal())
		{
			LastMove = Move;
			LastRoute = EHawkeyeParkourRoute::SampleTraversal;
			PlayEffortSound(Move);
			OnParkourStarted.Broadcast(Move, LastRoute);
			return true;
		}
	}
	return StartMove(Move, Obstacle);
}

bool UParkourComponent::ProbeJumpFan(const FVector& Direction, float Reach, bool bLateCatch,
	FHawkeyeParkourObstacle& OutObstacle, EHawkeyeParkourMove& OutMove, FString& OutWhyNot, FHawkeyeParkourObstacle* OutRoofEdge) const
{
	OutObstacle = FHawkeyeParkourObstacle();
	if (OutRoofEdge)
	{
		*OutRoofEdge = FHawkeyeParkourObstacle();
	}
	OutMove = EHawkeyeParkourMove::None;
	OutWhyNot.Reset();
	const FVector Centre = Direction.GetSafeNormal2D();
	const UCharacterMovementComponent* Movement = GetMovement();
	if (Centre.IsNearlyZero() || !Movement)
	{
		OutWhyNot = TEXT("no direction to probe");
		return false;
	}
	const bool bRising = Movement->Velocity.Z > 0.f;
	const float MaxHeight = bLateCatch ? CatchMaxHeight : LedgeMaxHeight;
	const float MinHeight = FMath::Min(JumpMinHeight, VaultMinHeight);
	const int32 Rays = FMath::Max(1, JumpFanRays | 1);
	const int32 Side = Rays / 2;
	const float Step = Side > 0 ? JumpFanDegrees / Side : 0.f;
	FString FirstRefusal;
	for (int32 Index = 0; Index < Rays; ++Index)
	{
		// 0, -1, +1, -2, +2, ... steps: the middle of the fan first.
		const int32 Ring = (Index + 1) / 2;
		const float Angle = (Index % 2 == 1 ? -1.f : 1.f) * Ring * Step;
		const FVector Ray = Centre.RotateAngleAxis(Angle, FVector::UpVector);
		FHawkeyeParkourObstacle Obstacle;
		// Probed a little higher than any move allows, so "too tall" can be told from "nothing there".
		// In the air the probe starts just above the feet: a running jump meets a waist-high wall
		// with the feet a hand's width under its top, where the ground probe (35 cm up) passes over.
		if (!DetectObstacleAlong(Ray, Reach, MaxHeight + 60.f, Obstacle, bLateCatch ? HawkeyeParkour::AirProbeLowest : -1.f))
		{
			continue;
		}
		EHawkeyeParkourMove Move = EHawkeyeParkourMove::None;
		if (!bLateCatch)
		{
			Move = ChooseMoveFor(Obstacle, /*bAuto=*/false);
		}
		else if (Obstacle.Height >= HawkeyeParkour::AirMinHeight && Obstacle.Height <= MantleMaxHeight
			&& (Obstacle.Height >= MinHeight - HawkeyeParkour::HeightSlack
				|| Obstacle.LedgePoint.Z - LateCatchTakeOffZ >= MinHeight - HawkeyeParkour::HeightSlack))
		{
			// In the air with the top in reach: onto it, or over a thin one with the sprint
			// trigger's drop limit (a jump beside a parapet is not a request to go over it).
			const bool bClear = Obstacle.bClearBeyond && Obstacle.LandingDrop <= MaxAutoVaultDrop;
			Move = ChooseMoveFrom(HawkeyeParkour::AirMinHeight, Obstacle.Height, bClear, Obstacle.bStandingSurface);
		}
		if (bLateCatch && Move == EHawkeyeParkourMove::None && Obstacle.Height >= CatchMinHeight
			&& Obstacle.Height <= (bRising ? LedgeMaxHeight : CatchMaxHeight))
		{
			// Higher than that: hang from it (still rising, only as high as a standing grab reaches).
			Move = EHawkeyeParkourMove::LedgeGrab;
		}
		if (Move != EHawkeyeParkourMove::None)
		{
			OutObstacle = Obstacle;
			OutMove = Move;
			return true;
		}
		if (OutRoofEdge && !OutRoofEdge->bFound && !bLateCatch && IsRoofEdgeParapet(Obstacle))
		{
			*OutRoofEdge = Obstacle;
		}
		if (FirstRefusal.IsEmpty())
		{
			FirstRefusal = FString::Printf(TEXT("%s at %+.0f degrees, %.0f cm away, on %s"),
				*DescribeRefusal(Obstacle, bLateCatch), Angle, Obstacle.Distance, *GetNameSafe(Obstacle.Actor));
		}
	}
	OutWhyNot = FirstRefusal.IsEmpty()
		? FString::Printf(TEXT("no obstacle in the %.0f degree fan within %.0f cm"), JumpFanDegrees, Reach)
		: FirstRefusal;
	return false;
}

bool UParkourComponent::TryJumpParkour(const FVector& Direction)
{
	const UCharacterMovementComponent* Movement = GetMovement();
	if (IsBusy() || !Movement || !(Movement->IsMovingOnGround() || Movement->IsFalling()))
	{
		return false;
	}
	// A press in the air (mashing on the way up) probes the late catch's way: from just above the feet,
	// with tops measured against the take-off, so a wall she has risen level with is still found.
	const bool bInAir = Movement->IsFalling();
	// A second press soon after a guarded one that became a held jump: "I know", over it after all.
	if (bGuardFlight && GuardPressAge >= 0.f && GuardPressAge <= DoubleTapSeconds)
	{
		LeapFromGuard(TEXT("a double tap"));
		return true;
	}
	if (bInAir && LateCatchRemaining <= 0.f)
	{
		LateCatchTakeOffZ = GetFeetZ();
	}
	FHawkeyeParkourObstacle Obstacle;
	EHawkeyeParkourMove Move = EHawkeyeParkourMove::None;
	FString WhyNot;
	FHawkeyeParkourObstacle RoofEdge;
	if (!ProbeJumpFan(Direction, ManualTriggerDistance, /*bLateCatch=*/bInAir, Obstacle, Move, WhyNot, &RoofEdge))
	{
		LastJumpRefusal = WhyNot;
		UE_LOG(LogHawkeye, Log, TEXT("%s: jump%s: no parkour move: %s"), *GetNameSafe(GetOwner()),
			bInAir ? TEXT(" (in the air)") : TEXT(""), *WhyNot);
		// A parapet at a roof edge she is heading for: the plain jump that would follow must not carry her over it.
		if (!bInAir && RoofEdge.bFound && IsMovingToward(-RoofEdge.WallNormal))
		{
			return GuardRoofEdge(RoofEdge);
		}
		return false;
	}
	if (StartChosenMove(Move, Obstacle, TEXT("jump")))
	{
		return true;
	}
	LastJumpRefusal = FString::Printf(TEXT("%s would not start"), *UEnum::GetValueAsString(Move));
	UE_LOG(LogHawkeye, Log, TEXT("%s: jump: no parkour move: %s"), *GetNameSafe(GetOwner()), *LastJumpRefusal);
	return false;
}

void UParkourComponent::ArmLateCatch()
{
	if (LateCatchSeconds <= 0.f || IsBusy())
	{
		return;
	}
	if (LateCatchRemaining > 0.f)
	{
		// Pressed again on the way up or down: the window restarts, the take-off stays where she left
		// the ground (measured from her feet now, a wall's top would look 40 cm lower than it is).
		LateCatchRemaining = LateCatchSeconds;
		return;
	}
	LateCatchRemaining = LateCatchSeconds;
	LateCatchElapsed = 0.f;
	LateCatchTakeOffZ = GetFeetZ();
	bLateCatchAirborne = false;
}

void UParkourComponent::CancelLateCatch(const TCHAR* Why)
{
	if (LateCatchRemaining <= 0.f)
	{
		return;
	}
	LateCatchRemaining = 0.f;
	UE_LOG(LogHawkeye, Verbose, TEXT("%s: late catch off: %s"), *GetNameSafe(GetOwner()), Why);
}

bool UParkourComponent::TryLateCatch()
{
	const UCharacterMovementComponent* Movement = GetMovement();
	if (IsBusy() || RegrabCooldown > 0.f || !Movement || !Movement->IsFalling())
	{
		return false;
	}
	FHawkeyeParkourObstacle Obstacle;
	EHawkeyeParkourMove Move = EHawkeyeParkourMove::None;
	FString WhyNot;
	if (!ProbeJumpFan(GetJumpProbeDirection(), LateCatchReach, /*bLateCatch=*/true, Obstacle, Move, WhyNot))
	{
		LastJumpRefusal = WhyNot;
		return false;
	}
	if (bIgnoreDroppedLedge && FMath::Abs(Obstacle.LedgePoint.Z - DroppedLedgePoint.Z) < HawkeyeParkour::SameLedgeTolerance
		&& FVector::Dist2D(Obstacle.LedgePoint, DroppedLedgePoint) < 200.f)
	{
		return false;
	}
	LateCatchRemaining = 0.f;
	const FString Trigger = FString::Printf(TEXT("late catch %.2f s after the jump"), LateCatchElapsed);
	if (Move == EHawkeyeParkourMove::LedgeGrab)
	{
		bIgnoreDroppedLedge = false;
	}
	return StartChosenMove(Move, Obstacle, *Trigger);
}

void UParkourComponent::BufferJump()
{
	if (bGuardMantle && IsPerformingMove())
	{
		if (GuardPressAge >= 0.f && GuardPressAge <= DoubleTapSeconds)
		{
			LeapFromGuard(TEXT("a double tap during the mantle"));
			return;
		}
		// Mashing on the way up would fire a jump off the top the moment she stands on it: the guard eats it.
		UE_LOG(LogHawkeye, Log, TEXT("%s: jump dropped: the roof-edge guard's mantle is running (a double tap within %.1f s leaps)"),
			*GetNameSafe(GetOwner()), DoubleTapSeconds);
		return;
	}
	JumpBufferRemaining = JumpBufferSeconds;
}

// --- Roof-edge guard ------------------------------------------------------------------------------

bool UParkourComponent::IsRoofEdgeParapet(const FHawkeyeParkourObstacle& Obstacle) const
{
	const float MinHeight = FMath::Min(JumpMinHeight, VaultMinHeight) - HawkeyeParkour::HeightSlack;
	return Obstacle.bFound && Obstacle.Depth > 0.f && Obstacle.Height >= MinHeight && Obstacle.Height <= RoofEdgeGuardMaxHeight
		&& Obstacle.FarSideDrop > RoofEdgeGuardDrop;
}

bool UParkourComponent::FindParapetStand(const FHawkeyeParkourObstacle& Parapet, FVector& OutStandPoint) const
{
	const ACharacter* Character = GetCharacter();
	if (!Character || !Parapet.bFound || Parapet.Depth <= 0.f)
	{
		return false;
	}
	// The back edge was found somewhere in the last probe step: the middle of the top is about half of that in.
	const float Middle = FMath::Max(Parapet.Depth - HawkeyeParkour::BeyondStep * 0.5f, 1.f) * 0.5f;
	const FVector Point = Parapet.LedgePoint - Parapet.WallNormal * Middle;
	FHitResult Top;
	if (!TraceLine(Point + FVector(0.f, 0.f, 30.f), Point - FVector(0.f, 0.f, 30.f), Top)
		|| Top.ImpactNormal.Z < HawkeyeParkour::MinFloorNormalZ || FMath::Abs(Top.ImpactPoint.Z - Parapet.LedgePoint.Z) > 25.f)
	{
		return false;
	}
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (!CapsuleFits(Top.ImpactPoint + FVector(0.f, 0.f, HalfHeight + HawkeyeParkour::FloorGap)))
	{
		return false;
	}
	OutStandPoint = Top.ImpactPoint;
	return true;
}

bool UParkourComponent::IsMovingToward(const FVector& Direction) const
{
	const FVector Toward = Direction.GetSafeNormal2D();
	const UCharacterMovementComponent* Movement = GetMovement();
	if (Movement && FVector::DotProduct(FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.f), Toward) >= RoofEdgeGuardMinSpeed)
	{
		return true;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}
	FVector Input = Pawn->GetPendingMovementInputVector().GetSafeNormal2D();
	if (Input.IsNearlyZero())
	{
		Input = Pawn->GetLastMovementInputVector().GetSafeNormal2D();
	}
	return FVector::DotProduct(Input, Toward) >= 0.5f;
}

bool UParkourComponent::GuardRoofEdge(const FHawkeyeParkourObstacle& Parapet)
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement)
	{
		return false;
	}
	GuardParapet = Parapet;
	GuardDirection = -Parapet.WallNormal.GetSafeNormal2D();
	GuardSpeed = FMath::Max(GuardLeapSpeed, FVector::DotProduct(Movement->Velocity, GuardDirection));
	GuardPressAge = 0.f;
	EndPerch(TEXT("a new guarded press"));
	FVector Stand;
	if (FindParapetStand(Parapet, Stand))
	{
		FHawkeyeParkourObstacle Onto = Parapet;
		Onto.bStandingSurface = true;
		Onto.StandPoint = Stand;
		UE_LOG(LogHawkeye, Log,
			TEXT("%s: jump: roof-edge guard: a %.0f cm parapet with a %.0f cm drop beyond, on %s: mantle onto its top and stop there (a double tap within %.1f s leaps)"),
			*GetNameSafe(GetOwner()), Parapet.Height, Parapet.FarSideDrop, *GetNameSafe(Parapet.Actor), DoubleTapSeconds);
		// Ours, not the sample's: the sample's traversal picks its own move and could hurdle it.
		if (StartMove(EHawkeyeParkourMove::Mantle, Onto))
		{
			bGuardMantle = true;
			bGuardFlight = false;
			LastJumpRefusal.Reset();
			return true;
		}
	}
	UE_LOG(LogHawkeye, Log,
		TEXT("%s: jump: roof-edge guard: a %.0f cm parapet with a %.0f cm drop beyond, on %s, and no room on its top: a plain jump held short of it (a double tap within %.1f s leaps)"),
		*GetNameSafe(GetOwner()), Parapet.Height, Parapet.FarSideDrop, *GetNameSafe(Parapet.Actor), DoubleTapSeconds);
	bGuardMantle = false;
	bGuardFlight = true;
	bGuardFlightAirborne = false;
	GuardFlightElapsed = 0.f;
	const float Toward = FVector::DotProduct(Movement->Velocity, GuardDirection);
	if (Toward > 0.f)
	{
		Movement->Velocity -= GuardDirection * Toward;
	}
	return false;
}

void UParkourComponent::LeapFromGuard(const TCHAR* Why)
{
	const ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement)
	{
		return;
	}
	if (IsPerformingMove())
	{
		// Out of the guard's mantle where she is: the leap's hop starts from there.
		const EHawkeyeParkourMove Move = ActiveMove;
		ActiveMove = EHawkeyeParkourMove::None;
		StopClip(0.1f);
		RestoreRootMotionMode();
		OnParkourFinished.Broadcast(Move);
	}
	bGuardMantle = false;
	bGuardFlight = false;
	GuardPressAge = -1.f;
	EndPerch(TEXT("a leap"));
	// Over the top in a quick hop (a plain jump from a standstill 66 cm short of a 90 cm parapet does not clear it),
	// out to just past its back edge at the top's height, then let go with her speed toward the drop.
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Past = GuardParapet.LedgePoint - GuardParapet.WallNormal * (GuardParapet.Depth + Radius + HawkeyeParkour::LandingClearance);
	const FVector End(Past.X, Past.Y, GuardParapet.LedgePoint.Z + HalfHeight + 10.f);
	LastObstacle = GuardParapet;
	// Arched enough that the feet clear the top from wherever the mantle had got to.
	const float Rise = FMath::Max(30.f, (GuardParapet.LedgePoint.Z - GetFeetZ()) * 0.8f + 10.f);
	if (BeginMove(EHawkeyeParkourMove::Vault, End, Rise, VaultSeconds * 0.7f, VaultClip))
	{
		bGuardLeap = true;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: roof-edge guard: %s, so she leaps over (then %.0f cm/s toward the drop)"),
		*GetNameSafe(GetOwner()), Why, GuardSpeed);
}

void UParkourComponent::BeginPerch()
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement)
	{
		return;
	}
	if (!bPerched)
	{
		bPerchSavedWalkOff = Movement->bCanWalkOffLedges;
	}
	bPerched = true;
	PerchRoofDirection = -GuardDirection;
	PerchTopZ = GetFeetZ();
	Movement->bCanWalkOffLedges = false;
	UE_LOG(LogHawkeye, Log, TEXT("%s: roof-edge guard: on the parapet top at %.0f cm; she walks off it only back toward the roof"),
		*GetNameSafe(GetOwner()), PerchTopZ);
}

void UParkourComponent::EndPerch(const TCHAR* Why)
{
	if (!bPerched)
	{
		return;
	}
	bPerched = false;
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->bCanWalkOffLedges = bPerchSavedWalkOff;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: roof-edge guard: off the parapet top (%s)"), *GetNameSafe(GetOwner()), Why);
}

void UParkourComponent::TickRoofEdgeGuard(float DeltaSeconds)
{
	if (GuardPressAge >= 0.f)
	{
		GuardPressAge += DeltaSeconds;
		if (GuardPressAge > DoubleTapSeconds)
		{
			GuardPressAge = -1.f;
		}
	}
	UCharacterMovementComponent* Movement = GetMovement();
	if (bGuardFlight)
	{
		GuardFlightElapsed += DeltaSeconds;
		// Flying is a move or a hang, a custom mode is a zip: either way the held jump is over.
		if (!Movement || IsBusy() || Movement->MovementMode == MOVE_Flying || Movement->MovementMode == MOVE_Custom)
		{
			bGuardFlight = false;
		}
		else if (Movement->IsFalling())
		{
			// Held short: nothing toward the drop, the stick's air control included.
			bGuardFlightAirborne = true;
			const float Toward = FVector::DotProduct(Movement->Velocity, GuardDirection);
			if (Toward > 0.f)
			{
				Movement->Velocity -= GuardDirection * Toward;
			}
		}
		else if (bGuardFlightAirborne || GuardFlightElapsed > 0.25f)
		{
			bGuardFlight = false;
			UE_LOG(LogHawkeye, Log, TEXT("%s: roof-edge guard: the held jump landed short of the parapet"), *GetNameSafe(GetOwner()));
		}
	}
	if (bPerched)
	{
		if (!Movement || IsBusy() || !Movement->IsMovingOnGround())
		{
			EndPerch(IsBusy() ? TEXT("a move or a hang") : TEXT("off the ground"));
		}
		else if (GetFeetZ() < PerchTopZ - 30.f)
		{
			EndPerch(TEXT("back on the roof"));
		}
		else
		{
			// Only back the way she came: toward the drop the top's edge holds her.
			const APawn* Pawn = Cast<APawn>(GetOwner());
			FVector Input = Pawn ? Pawn->GetPendingMovementInputVector().GetSafeNormal2D() : FVector::ZeroVector;
			if (Input.IsNearlyZero() && Pawn)
			{
				Input = Pawn->GetLastMovementInputVector().GetSafeNormal2D();
			}
			Movement->bCanWalkOffLedges = FVector::DotProduct(Input, PerchRoofDirection) > 0.25f;
		}
	}
}

void UParkourComponent::TickJumpAssist(float DeltaSeconds)
{
	if (JumpBufferRemaining > 0.f)
	{
		if (!IsPerformingMove() && !IsSampleTraversalActive())
		{
			// The move is over (or it ended in a hang, where the press climbs): the press happens now.
			JumpBufferRemaining = 0.f;
			UE_LOG(LogHawkeye, Log, TEXT("%s: buffered jump fires at the end of the move"), *GetNameSafe(GetOwner()));
			if (ACharacter* Character = GetCharacter())
			{
				Character->Jump();
			}
		}
		else
		{
			JumpBufferRemaining = FMath::Max(0.f, JumpBufferRemaining - DeltaSeconds);
			if (JumpBufferRemaining <= 0.f)
			{
				UE_LOG(LogHawkeye, Log, TEXT("%s: buffered jump dropped: the move ran %.1f s past the press"),
					*GetNameSafe(GetOwner()), JumpBufferSeconds);
			}
		}
	}

	if (LateCatchRemaining <= 0.f)
	{
		return;
	}
	const UCharacterMovementComponent* Movement = GetMovement();
	if (IsBusy())
	{
		CancelLateCatch(TEXT("a move or a hang started"));
		return;
	}
	if (!Movement || Movement->MovementMode == MOVE_Flying || Movement->MovementMode == MOVE_Custom)
	{
		CancelLateCatch(TEXT("a zip, or no movement"));
		return;
	}
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(GetOwner());
	if (Hawkeye && Hawkeye->IsSliding())
	{
		CancelLateCatch(TEXT("a slide"));
		return;
	}
	LateCatchElapsed += DeltaSeconds;
	if (!Movement->IsFalling())
	{
		// The jump itself starts on the movement tick after the press; give it that long.
		if (bLateCatchAirborne || LateCatchElapsed > 0.25f)
		{
			CancelLateCatch(TEXT("on the ground"));
		}
		return;
	}
	bLateCatchAirborne = true;
	if (TryLateCatch())
	{
		return;
	}
	LateCatchRemaining -= DeltaSeconds;
	if (LateCatchRemaining <= 0.f)
	{
		LateCatchRemaining = 0.f;
		UE_LOG(LogHawkeye, Log, TEXT("%s: jump: the late catch found nothing in %.1f s (last: %s)"), *GetNameSafe(GetOwner()),
			LateCatchSeconds, *LastJumpRefusal);
	}
}

bool UParkourComponent::TryStartSampleTraversal()
{
	ACharacter* Character = GetCharacter();
	UActorComponent* Logic = SampleTraversal.Get();
	if (!bUseSampleTraversal || !Character || !Logic)
	{
		return false;
	}
	if (!bLoggedSampleApi)
	{
		bLoggedSampleApi = true;
		FString Report;
		HawkeyeGaspTraversal::HasTraversalApi(Character->GetClass(), Logic->GetClass(), Report);
		UE_LOG(LogHawkeye, Log, TEXT("%s: sample traversal API: %s"), *GetNameSafe(Character), *Report);
	}
	const HawkeyeGaspTraversal::FResult Result = HawkeyeGaspTraversal::TryTraversal(Character, Logic);
	UE_LOG(LogHawkeye, Log, TEXT("%s: sample TryTraversalAction called=%d check failed=%d montage failed=%d"),
		*GetNameSafe(Character), Result.bCalled ? 1 : 0, Result.bCheckFailed ? 1 : 0, Result.bMontageFailed ? 1 : 0);
	return Result.Started();
}

FVector UParkourComponent::HangLocationFor(const FHawkeyeParkourObstacle& Obstacle) const
{
	const ACharacter* Character = GetCharacter();
	const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
	return Obstacle.LedgePoint + Obstacle.WallNormal * HangBackFromEdge
		+ FVector(0.f, 0.f, HalfHeight - HangBelowLedge);
}

bool UParkourComponent::StartMove(EHawkeyeParkourMove Move, const FHawkeyeParkourObstacle& Obstacle)
{
	const ACharacter* Character = GetCharacter();
	if (!Character || !Obstacle.bFound || IsBusy())
	{
		return false;
	}
	const FVector Lift(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + HawkeyeParkour::FloorGap);
	LastObstacle = Obstacle;
	switch (Move)
	{
	case EHawkeyeParkourMove::Vault:
		return Obstacle.bClearBeyond
			&& BeginMove(Move, Obstacle.LandingPoint + Lift, Obstacle.Height + 10.f, VaultSeconds, VaultClip);
	case EHawkeyeParkourMove::Mantle:
		return Obstacle.bStandingSurface && BeginMove(Move, Obstacle.StandPoint + Lift, -1.f, MantleSeconds, MantleClip);
	case EHawkeyeParkourMove::LedgeGrab:
	{
		HangObstacle = Obstacle;
		// Falling onto it is the quick catch; a jump still rising (the late catch) reaches up the grab's way.
		const bool bFalling = GetMovement() && GetMovement()->IsFalling() && GetMovement()->Velocity.Z <= 0.f;
		return BeginMove(Move, HangLocationFor(Obstacle), -1.f, bFalling ? CatchSeconds : GrabSeconds,
			bFalling ? CatchClip : GrabClip);
	}
	case EHawkeyeParkourMove::DropToHang:
	{
		// Over the lip (a parapet or a rail) with a hop that clears it, then down to the hang.
		HangObstacle = Obstacle;
		DropHop = FMath::Max(0.f, Obstacle.Height) + HawkeyeParkour::DropHopClearance;
		return BeginMove(Move, HangLocationFor(Obstacle), DropHop, DropToHangSeconds, CatchClip);
	}
	default:
		return false;
	}
}

bool UParkourComponent::BeginMove(EHawkeyeParkourMove Move, const FVector& End, float Rise, float Seconds,
	const FHawkeyeParkourClip& Clip)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement)
	{
		return false;
	}
	if (Move != EHawkeyeParkourMove::Climb)
	{
		Character->SetActorRotation(FRotator(0.f, (-LastObstacle.WallNormal).Rotation().Yaw, 0.f));
	}
	MoveStart = Character->GetActorLocation();
	MoveEnd = End;
	MoveRise = Rise < 0.f ? End.Z - MoveStart.Z : Rise;
	MoveSeconds = Seconds;
	MoveElapsed = 0.f;
	ActiveMove = Move;
	LastMove = Move;
	LastRoute = EHawkeyeParkourRoute::Procedural;
	bHanging = false;

	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Flying);
	PlayClip(Clip, Seconds);
	UE_LOG(LogHawkeye, Log, TEXT("%s: parkour %s from %s to %s over %.2f s (%s)"), *GetNameSafe(Character),
		*UEnum::GetValueAsString(Move), *MoveStart.ToCompactString(), *MoveEnd.ToCompactString(), Seconds,
		MoveClip ? *MoveClip->GetName() : TEXT("plain arc"));
	PlayEffortSound(Move);
	OnParkourStarted.Broadcast(Move, LastRoute);
	return true;
}

void UParkourComponent::PlayEffortSound(EHawkeyeParkourMove Move) const
{
	const bool bEffort = Move == EHawkeyeParkourMove::Vault || Move == EHawkeyeParkourMove::Mantle
		|| Move == EHawkeyeParkourMove::LedgeGrab || Move == EHawkeyeParkourMove::Climb
		|| Move == EHawkeyeParkourMove::HangLeap || Move == EHawkeyeParkourMove::HangHop;
	if (const AActor* Owner = GetOwner(); bEffort && Owner)
	{
		UHawkeyeAudioSubsystem::PlayAt(this, EffortSound, Owner->GetActorLocation(), TEXT("parkour effort"));
	}
}

// --- Running ------------------------------------------------------------------------------------

FVector UParkourComponent::ClipRootOffset(float Time) const
{
	const ACharacter* Character = GetCharacter();
	const UAnimSequenceBase* Clip = MoveClip.Get();
	if (!Clip || !Character || !Character->GetMesh() || Time <= ClipStart)
	{
		return FVector::ZeroVector;
	}
	const FTransform Delta = Clip->ExtractRootMotionFromRange(ClipStart, Time, FAnimExtractContext());
	// Root motion is in the mesh's space; the mesh is turned relative to the actor (-90 yaw).
	return Character->GetMesh()->GetRelativeRotation().RotateVector(Delta.GetTranslation());
}

void UParkourComponent::ComputeMoveShape(float Alpha, float& OutForward, float& OutUp) const
{
	const bool bArc = ActiveMove == EHawkeyeParkourMove::Vault;
	if (ActiveMove == EHawkeyeParkourMove::DropToHang)
	{
		// Out over the lip in the first 70% with a hop that peaks halfway there; the clip only
		// poses the hands. The drop to the hang comes in with the left-over Z.
		OutForward = FMath::SmoothStep(0.f, 0.7f, Alpha);
		OutUp = FMath::Sin(PI * FMath::Min(Alpha / 0.7f, 1.f));
		return;
	}
	if (MoveClip && ClipForwardTotal > 1.f && ClipPeakUp > 1.f)
	{
		const FVector Offset = ClipRootOffset(FMath::Lerp(ClipStart, ClipEnd, Alpha));
		OutForward = FMath::Clamp(Offset.X / ClipForwardTotal, -0.5f, 1.5f);
		OutUp = Offset.Z / ClipPeakUp;
		return;
	}
	// No clip (a plain character, or a test): over in an arc, or up first and then in.
	OutForward = bArc ? Alpha : FMath::SmoothStep(0.35f, 1.f, Alpha);
	OutUp = bArc ? FMath::Sin(PI * Alpha) : FMath::SmoothStep(0.f, 0.6f, Alpha);
}

FVector UParkourComponent::ComputeMoveLocation(float Alpha) const
{
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	if (IsHangTransfer(ActiveMove))
	{
		return ComputeHangTransferLocation(A);
	}
	float Forward = 0.f;
	float Up = 0.f;
	ComputeMoveShape(A, Forward, Up);
	float EndUp = 0.f;
	float EndForward = 1.f;
	ComputeMoveShape(1.f, EndForward, EndUp);

	// Scaled so the peak of the shape is MoveRise; whatever the shape leaves over at the end (a
	// landing lower or higher than the start) is blended in linearly, so the end is exact.
	const FVector Horizontal(MoveEnd.X - MoveStart.X, MoveEnd.Y - MoveStart.Y, 0.f);
	const float LeftOverZ = (MoveEnd.Z - MoveStart.Z) - EndUp * MoveRise;
	const float ForwardScale = FMath::IsNearlyZero(EndForward) ? 1.f : 1.f / EndForward;
	return MoveStart + Horizontal * (Forward * ForwardScale)
		+ FVector(0.f, 0.f, Up * MoveRise + LeftOverZ * A);
}

void UParkourComponent::AdvanceMove(float DeltaSeconds)
{
	ACharacter* Character = GetCharacter();
	if (!IsPerformingMove() || !Character)
	{
		return;
	}
	MoveElapsed += DeltaSeconds;
	const float Alpha = MoveSeconds > 0.f ? MoveElapsed / MoveSeconds : 1.f;
	Character->SetActorLocation(ComputeMoveLocation(Alpha), false, nullptr, ETeleportType::None);
	if (bMoveTurns)
	{
		ApplyMoveTurn(FMath::Min(Alpha, 1.f));
	}
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->Velocity = FVector::ZeroVector;
	}
	if (Alpha >= 1.f)
	{
		FinishMove();
	}
}

void UParkourComponent::FinishMove()
{
	const EHawkeyeParkourMove Move = ActiveMove;
	ActiveMove = EHawkeyeParkourMove::None;
	const bool bOntoParapet = bGuardMantle && Move == EHawkeyeParkourMove::Mantle;
	bGuardMantle = false;
	bMoveTurns = false;
	if (bGuardLeap)
	{
		// The leap's hop is over the top: she carries on out over the drop.
		bGuardLeap = false;
		StopClip(0.2f);
		RestoreRootMotionMode();
		if (UCharacterMovementComponent* Movement = GetMovement())
		{
			Movement->SetMovementMode(MOVE_Falling);
			Movement->Velocity = GuardDirection * GuardSpeed;
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: roof-edge guard: over the parapet at %s, off the roof at %.0f cm/s"), *GetNameSafe(GetOwner()),
			GetOwner() ? *GetOwner()->GetActorLocation().ToCompactString() : TEXT("?"), GuardSpeed);
		OnParkourFinished.Broadcast(Move);
		return;
	}
	if (Move == EHawkeyeParkourMove::LedgeGrab || Move == EHawkeyeParkourMove::DropToHang || IsHangTransfer(Move))
	{
		EnterHang();
		return;
	}
	StopClip(0.2f);
	RestoreRootMotionMode();
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		// Walking finds the floor the move ended on, or falls if there is none.
		Movement->SetMovementMode(MOVE_Walking);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: parkour %s done at %s"), *GetNameSafe(GetOwner()), *UEnum::GetValueAsString(Move),
		GetOwner() ? *GetOwner()->GetActorLocation().ToCompactString() : TEXT("?"));
	if (bOntoParapet)
	{
		BeginPerch();
	}
	OnParkourFinished.Broadcast(Move);
}

void UParkourComponent::EnterHang()
{
	bHanging = true;
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->StopMovementImmediately();
	}
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && ActiveMontage)
	{
		Anim->Montage_Pause(ActiveMontage);
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: hanging at %s from a ledge %.0f cm up (climb %d)"), *GetNameSafe(GetOwner()),
		GetOwner() ? *GetOwner()->GetActorLocation().ToCompactString() : TEXT("?"), HangObstacle.LedgePoint.Z,
		HangObstacle.bStandingSurface ? 1 : 0);
	BeginHangState();
}

bool UParkourComponent::ClimbFromHang()
{
	const ACharacter* Character = GetCharacter();
	if (!bHanging || !Character)
	{
		return false;
	}
	// Measured where she is now: a shimmy may have taken her past the roof she caught the edge of.
	RefreshHangLedge();
	if (!HangObstacle.bStandingSurface)
	{
		LastHangRefusal = TEXT("nowhere to stand on the top");
		UE_LOG(LogHawkeye, Log, TEXT("%s: hang: no climb: nowhere to stand on the top here (%s)"), *GetNameSafe(Character),
			HangObstacle.StandWhyNot.IsEmpty() ? *HangObstacle.BeyondWhyNot : *HangObstacle.StandWhyNot);
		return false;
	}
	bHanging = false;
	EndHangState();
	const FVector Lift(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + HawkeyeParkour::FloorGap);
	return BeginMove(EHawkeyeParkourMove::Climb, HangObstacle.StandPoint + Lift, -1.f, ClimbSeconds, ClimbClip);
}

bool UParkourComponent::DropFromHang()
{
	ACharacter* Character = GetCharacter();
	if (!bHanging || !Character)
	{
		return false;
	}
	bHanging = false;
	EndHangState();
	StopClip(0.2f);
	RestoreRootMotionMode();
	Character->SetActorLocation(Character->GetActorLocation() + HangObstacle.WallNormal * 15.f);
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->SetMovementMode(MOVE_Falling);
	}
	RegrabCooldown = DropRegrabSeconds;
	DroppedLedgePoint = HangObstacle.LedgePoint;
	bIgnoreDroppedLedge = true;
	UE_LOG(LogHawkeye, Log, TEXT("%s: dropped from the hang on a ledge at %.0f cm, feet at %.0f"), *GetNameSafe(Character),
		HangObstacle.LedgePoint.Z, Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	OnParkourFinished.Broadcast(EHawkeyeParkourMove::LedgeGrab);
	return true;
}

bool UParkourComponent::TryCatchLedge()
{
	const UCharacterMovementComponent* Movement = GetMovement();
	if (IsBusy() || RegrabCooldown > 0.f || !Movement || !Movement->IsFalling() || Movement->Velocity.Z > 0.f)
	{
		return false;
	}
	FHawkeyeParkourObstacle Obstacle;
	if (!DetectObstacleUpTo(CatchDistance, CatchMaxHeight, Obstacle) || Obstacle.Height < CatchMinHeight
		|| Obstacle.Height > CatchMaxHeight)
	{
		return false;
	}
	if (bIgnoreDroppedLedge && FMath::Abs(Obstacle.LedgePoint.Z - DroppedLedgePoint.Z) < HawkeyeParkour::SameLedgeTolerance
		&& FVector::Dist2D(Obstacle.LedgePoint, DroppedLedgePoint) < 200.f)
	{
		return false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: falling, caught a ledge %.0f cm above the feet at %s on %s"), *GetNameSafe(GetOwner()),
		Obstacle.Height, *Obstacle.LedgePoint.ToCompactString(), *GetNameSafe(Obstacle.Actor));
	bIgnoreDroppedLedge = false;
	return StartMove(EHawkeyeParkourMove::LedgeGrab, Obstacle);
}

bool UParkourComponent::FindDropEdge(const FVector& Direction, FHawkeyeParkourObstacle& OutEdge) const
{
	OutEdge = FHawkeyeParkourObstacle();
	const ACharacter* Character = GetCharacter();
	const FVector D = Direction.GetSafeNormal2D();
	if (!Character || !GetWorld() || D.IsNearlyZero())
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());
	const float ProbeTop = MaxLipHeight + 20.f;
	const float MaxK = DropToHangReach + MaxLipDepth + HawkeyeParkour::EdgeStep;

	// Anything taller than a lip in the way is a wall, not an edge (a line trace that starts
	// inside a wall would miss it, so check across first).
	FHitResult Wall;
	const FVector High = Feet + FVector(0.f, 0.f, ProbeTop);
	if (TraceLine(High, High + D * (Radius + MaxK), Wall))
	{
		return false;
	}

	// Walk out from the capsule surface: floor, then maybe a lip, then the drop.
	float FloorZ = Feet.Z;
	float LipTopZ = 0.f;
	float LipStartK = -1.f;
	float EdgeK = -1.f;
	for (float K = 0.f; K <= MaxK; K += HawkeyeParkour::EdgeStep)
	{
		const FVector P = Feet + D * (Radius + K);
		FHitResult Down;
		const bool bHit = TraceLine(FVector(P.X, P.Y, Feet.Z + ProbeTop),
			FVector(P.X, P.Y, Feet.Z - HawkeyeParkour::EdgeProbeDepth), Down);
		const float H = bHit ? Down.ImpactPoint.Z - Feet.Z : -HawkeyeParkour::EdgeProbeDepth;
		if (LipStartK < 0.f)
		{
			if (FMath::Abs(H) <= HawkeyeParkour::FloorTolerance)
			{
				FloorZ = Down.ImpactPoint.Z;
				continue;
			}
			if (K > DropToHangReach)
			{
				return false;
			}
			if (H > HawkeyeParkour::FloorTolerance)
			{
				LipStartK = K;
				LipTopZ = Down.ImpactPoint.Z;
				continue;
			}
			if (H < -DropToHangMinDrop)
			{
				EdgeK = K;
				break;
			}
			return false;   // a step down, not an edge
		}
		if (H >= LipTopZ - Feet.Z - HawkeyeParkour::FloorTolerance)
		{
			if (K - LipStartK > MaxLipDepth)
			{
				return false;   // a raised floor, not a lip
			}
			LipTopZ = FMath::Max(LipTopZ, static_cast<float>(Down.ImpactPoint.Z));
			continue;
		}
		if (H < -DropToHangMinDrop)
		{
			EdgeK = K;
			break;
		}
		return false;   // a low wall with floor behind it
	}
	if (EdgeK < 0.f)
	{
		return false;
	}
	const float TopZ = LipStartK >= 0.f ? LipTopZ : FloorZ;

	// The outer face, from outside looking back in, just under the top.
	FHitResult Face;
	const FVector Outside = Feet + D * (Radius + EdgeK + 30.f);
	const FVector From(Outside.X, Outside.Y, TopZ - 3.f);
	if (!TraceLine(From, From - D * (Radius + EdgeK + 30.f), Face) || FMath::Abs(Face.ImpactNormal.Z) > 0.5f)
	{
		return false;
	}
	const FVector N = Face.ImpactNormal.GetSafeNormal2D();
	if (FVector::DotProduct(N, D) < 0.7f)
	{
		return false;
	}

	// The drop beyond, measured where the hanging capsule would fall.
	FHitResult Beyond;
	const FVector Below = Face.ImpactPoint + N * (Radius + 15.f);
	const bool bBeyond = TraceLine(FVector(Below.X, Below.Y, TopZ - 5.f),
		FVector(Below.X, Below.Y, Feet.Z - HawkeyeParkour::EdgeProbeDepth), Beyond);
	const float Drop = bBeyond ? Feet.Z - Beyond.ImpactPoint.Z : HawkeyeParkour::EdgeProbeDepth;
	if (Drop <= DropToHangMinDrop)
	{
		return false;
	}

	OutEdge.bFound = true;
	OutEdge.WallPoint = Face.ImpactPoint;
	OutEdge.WallNormal = N;
	OutEdge.LedgePoint = FVector(Face.ImpactPoint.X, Face.ImpactPoint.Y, TopZ);
	OutEdge.Height = TopZ - Feet.Z;
	OutEdge.Distance = LipStartK >= 0.f ? LipStartK : EdgeK;
	OutEdge.Depth = LipStartK >= 0.f ? EdgeK - LipStartK : 0.f;
	OutEdge.LandingDrop = Drop;
	OutEdge.LandingPoint = bBeyond ? FVector(Beyond.ImpactPoint) : Below - FVector(0.f, 0.f, HawkeyeParkour::EdgeProbeDepth);
	OutEdge.bStandingSurface = true;
	OutEdge.StandPoint = Feet;
	OutEdge.Actor = Face.GetActor();
	return CapsuleFits(HangLocationFor(OutEdge));
}

bool UParkourComponent::TryDropToHang(const FVector& Direction, const FString& Trigger)
{
	const UCharacterMovementComponent* Movement = GetMovement();
	if (IsBusy() || !Movement || !Movement->IsMovingOnGround())
	{
		return false;
	}
	FHawkeyeParkourObstacle Edge;
	if (!FindDropEdge(Direction, Edge))
	{
		return false;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %s: drop to hang over a %.0f cm lip %.0f cm away, %.0f cm drop beyond, on %s"),
		*GetNameSafe(GetOwner()), *Trigger, Edge.Height, Edge.Distance, Edge.LandingDrop, *GetNameSafe(Edge.Actor));
	return StartMove(EHawkeyeParkourMove::DropToHang, Edge);
}

void UParkourComponent::TryAutoParkour()
{
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Hawkeye || !Movement || !Hawkeye->IsSprinting() || Hawkeye->IsSliding() || Hawkeye->bIsCrouched
		|| !Movement->IsMovingOnGround())
	{
		return;
	}
	const FVector Velocity = Movement->Velocity.GetSafeNormal2D();
	if (Movement->Velocity.Size2D() < AutoMinSpeed
		|| FVector::DotProduct(Velocity, Hawkeye->GetActorForwardVector().GetSafeNormal2D()) < 0.7f)
	{
		return;
	}
	TryParkour(true);
}

void UParkourComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RegrabCooldown = FMath::Max(0.f, RegrabCooldown - DeltaTime);
	TickJumpAssist(DeltaTime);
	TickRoofEdgeGuard(DeltaTime);
	if (!bHanging && HandsAlpha > 0.f)
	{
		// Out of the hang the hands go back to the body's clip.
		HandsAlpha = FMath::Max(0.f, HandsAlpha - DeltaTime / 0.15f);
	}
	if (bIgnoreDroppedLedge && !bHanging && !IsPerformingMove())
	{
		const UCharacterMovementComponent* Movement = GetMovement();
		if (Movement && !Movement->IsFalling())
		{
			bIgnoreDroppedLedge = false;
		}
	}
	if (IsPerformingMove())
	{
		AdvanceMove(DeltaTime);
		return;
	}
	if (bHanging)
	{
		AdvanceHang(DeltaTime);
		return;
	}
	if (!TryCatchLedge())
	{
		TryAutoParkour();
	}
}

// --- Clips --------------------------------------------------------------------------------------

bool UParkourComponent::PlayClip(const FHawkeyeParkourClip& Clip, float Seconds)
{
	MoveClip = nullptr;
	ClipForwardTotal = ClipPeakUp = ClipEndUp = 0.f;
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim || Clip.Sequence.IsNull() || Clip.EndTime <= Clip.StartTime)
	{
		return false;
	}
	// Loaded synchronously: the move starts this frame and must not depend on the clip being resident.
	UAnimSequenceBase* Sequence = Clip.Sequence.LoadSynchronous();
	if (!Sequence)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: parkour clip %s does not load; using the plain arc"),
			*GetNameSafe(GetOwner()), *Clip.Sequence.ToString());
		return false;
	}
	MoveClip = Sequence;
	ClipStart = Clip.StartTime;
	ClipEnd = FMath::Min(Clip.EndTime, Sequence->GetPlayLength());
	for (int32 Step = 1; Step <= 16; ++Step)
	{
		const FVector Offset = ClipRootOffset(FMath::Lerp(ClipStart, ClipEnd, Step / 16.f));
		ClipPeakUp = FMath::Max(ClipPeakUp, Offset.Z);
		ClipForwardTotal = Offset.X;
		ClipEndUp = Offset.Z;
	}
	if (!bRootMotionOverridden)
	{
		SavedRootMotionMode = Anim->RootMotionMode;
		bRootMotionOverridden = true;
	}
	// The capsule follows our warped copy of the root motion; the clip only poses the body.
	Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	const float Rate = (ClipEnd - ClipStart) / FMath::Max(Seconds, 0.05f);
	ActiveMontage = Anim->PlaySlotAnimationAsDynamicMontage(Sequence, ClipSlot, 0.08f, 0.2f, Rate, 1, -1.f, ClipStart);
	return ActiveMontage != nullptr;
}

void UParkourComponent::StopClip(float BlendOutSeconds)
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && ActiveMontage)
	{
		Anim->Montage_Stop(BlendOutSeconds, ActiveMontage);
	}
	ActiveMontage = nullptr;
}

void UParkourComponent::RestoreRootMotionMode()
{
	if (!bRootMotionOverridden)
	{
		return;
	}
	bRootMotionOverridden = false;
	const ACharacter* Character = GetCharacter();
	if (UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr)
	{
		Anim->SetRootMotionMode(SavedRootMotionMode);
	}
}
