#include "Player/ParkourComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Misc/Paths.h"
#include "MotionWarpingComponent.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/GaspTraversal.h"

namespace HawkeyeParkour
{
	static const TCHAR* TraversalAnims = TEXT("/Game/Characters/UEFN_Mannequin/Animations/Traversal/");

	static FHawkeyeParkourClip MakeClip(const TCHAR* RelativePath, float StartTime, float EndTime, float MaxHeight = 0.f)
	{
		FHawkeyeParkourClip Clip;
		const FString Package = FString(TraversalAnims) + RelativePath;
		Clip.Montage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(Package + TEXT(".") + FPaths::GetBaseFilename(Package)));
		Clip.StartTime = StartTime;
		Clip.EndTime = EndTime;
		Clip.MaxHeight = MaxHeight;
		return Clip;
	}

	/** The sample's warp target names, as its traversal montages' motion-warping windows ask for them. */
	static const FName FrontLedge(TEXT("FrontLedge"));
	static const FName BackLedge(TEXT("BackLedge"));
	static const FName BackFloor(TEXT("BackFloor"));

	/** A path move is handed over as this many even steps; the corner's quarter turn needs no more. */
	static constexpr int32 PathSteps = 24;
	/** A clip blends out over this once a move ends. */
	static constexpr float ClipBlendOut = 0.2f;

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

	// The Game Animation Sample's montages (copied by import_gasp.py), each from the start of its first
	// motion-warping window to before its blend-out window. The hurdle puts the hands on FrontLedge by 0.4 s,
	// clears BackLedge by 0.6 and lands on BackFloor by 0.83; the 1 m mantle has its hands on FrontLedge at
	// 0.7 s and stands by 1.4; the 2.5 m climb has its hands on the edge at 0.73 s and stands by 1.85.
	using HawkeyeParkour::MakeClip;
	VaultClip = MakeClip(TEXT("Hurdle/AM_M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Lfoot"), 0.08f, 1.0f);
	MantleClips = {
		MakeClip(TEXT("Mantle/AM_M_Neutral_Traversal_Mantle_1_0_stand_F_Lfoot"), 0.14f, 1.4f, 150.f),
		MakeClip(TEXT("Climb/AM_M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot"), 0.4f, 1.85f),
	};
	GrabClip = MakeClip(TEXT("Climb/AM_M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot"), 0.3f, 0.75f);
	CatchClip = MakeClip(TEXT("Climb/AM_M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot"), 0.65f, 0.75f);
	ClimbClip = MakeClip(TEXT("Climb/AM_M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot"), 0.75f, 1.85f);
}

void UParkourComponent::BeginPlay()
{
	Super::BeginPlay();
	SampleTraversal = HawkeyeGaspTraversal::FindTraversalLogic(GetOwner());
	// The rules for a frame (a shimmy's next point, a move's start) are in before the movement runs it.
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		Movement->AddTickPrerequisiteComponent(this);
	}
	// Only a character with an AnimBP plays them; one with none (a test's) never waits on a load.
	const ACharacter* Character = GetCharacter();
	if (!Character || !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance())
	{
		return;
	}
	TArray<const FHawkeyeParkourClip*> Clips = { &VaultClip, &GrabClip, &CatchClip, &ClimbClip };
	for (const FHawkeyeParkourClip& Clip : MantleClips)
	{
		Clips.Add(&Clip);
	}
	for (const FHawkeyeParkourClip* Clip : Clips)
	{
		if (UAnimMontage* Montage = Clip->Montage.IsNull() ? nullptr : Clip->Montage.LoadSynchronous())
		{
			LoadedClips.AddUnique(Montage);
		}
		else if (!Clip->Montage.IsNull())
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: parkour clip %s does not load; its moves follow the plain path"),
				*GetNameSafe(GetOwner()), *Clip->Montage.ToString());
		}
	}
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

UHawkeyeMovementComponent* UParkourComponent::GetHawkeyeMovement() const
{
	return Cast<UHawkeyeMovementComponent>(GetMovement());
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

int32 UParkourComponent::ChooseClipForHeight(const TArray<FHawkeyeParkourClip>& Clips, float Height)
{
	return Clips.IndexOfByPredicate([Height](const FHawkeyeParkourClip& Clip)
	{
		return Clip.MaxHeight <= 0.f || Height <= Clip.MaxHeight;
	});
}

float UParkourComponent::GetExitSpeed(EHawkeyeParkourMove Move, float EntrySpeed) const
{
	switch (Move)
	{
	case EHawkeyeParkourMove::Vault:
		return FMath::Clamp(EntrySpeed, VaultExitMinSpeed, FMath::Max(VaultExitMinSpeed, VaultExitMaxSpeed));
	case EHawkeyeParkourMove::Mantle:
	case EHawkeyeParkourMove::Climb:
		return StepOnSpeed;
	default:
		return 0.f;
	}
}

float UParkourComponent::GetVaultSeconds(float Distance, float EntrySpeed) const
{
	if (EntrySpeed <= KINDA_SMALL_NUMBER)
	{
		return VaultSeconds;
	}
	return FMath::Clamp(Distance / EntrySpeed, FMath::Min(VaultMinSeconds, VaultSeconds), VaultSeconds);
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
		const float EntrySpeed = static_cast<float>(FVector::DotProduct(Movement->Velocity, ToWall));
		if (TryStartSampleTraversal())
		{
			// The sample's montage decides the speed she comes out at; the exit rule is ours.
			bSampleExitPending = true;
			SampleExitDirection = ToWall;
			SampleExitSpeed = GetExitSpeed(Move, EntrySpeed);
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
		bGuardMantle = true;
		if (StartMove(EHawkeyeParkourMove::Mantle, Onto))
		{
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
		// Out of the guard's mantle where she is: the leap's hop starts from there and replaces it in the movement.
		const EHawkeyeParkourMove Move = ActiveMove;
		ActiveMove = EHawkeyeParkourMove::None;
		StopClip(0.1f);
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
	// A leap, not a vault: it ends in the air past the top, so it follows its path and the clip only poses her.
	bGuardLeap = true;
	bGuardLeap = BeginMove(EHawkeyeParkourMove::Vault, End, Rise, VaultSeconds * 0.7f, VaultClip, { GuardParapet.Actor.Get() });
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
		// A move, a hang or the sample's traversal is busy, a custom mode is a zip or one of ours: the held jump is over.
		if (!Movement || IsBusy() || Movement->MovementMode == MOVE_Custom)
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
	if (!Movement || Movement->MovementMode == MOVE_Custom)
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
	const UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement || !Obstacle.bFound || IsBusy())
	{
		return false;
	}
	const FVector Lift(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + HawkeyeParkour::FloorGap);
	const TArray<TWeakObjectPtr<AActor>> Climbed = FindClimbedActors(Obstacle);
	LastObstacle = Obstacle;
	switch (Move)
	{
	case EHawkeyeParkourMove::Vault:
	{
		if (!Obstacle.bClearBeyond)
		{
			return false;
		}
		const FVector End = Obstacle.LandingPoint + Lift;
		const float EntrySpeed = static_cast<float>(FVector::DotProduct(Movement->Velocity, -Obstacle.WallNormal.GetSafeNormal2D()));
		const float Seconds = GetVaultSeconds(static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), End)), EntrySpeed);
		return BeginMove(Move, End, Obstacle.Height + 10.f, Seconds, VaultClip, Climbed);
	}
	case EHawkeyeParkourMove::Mantle:
	{
		static const FHawkeyeParkourClip NoClip;
		const int32 Clip = ChooseClipForHeight(MantleClips, Obstacle.Height);
		return Obstacle.bStandingSurface && BeginMove(Move, Obstacle.StandPoint + Lift, -1.f, MantleSeconds,
			MantleClips.IsValidIndex(Clip) ? MantleClips[Clip] : NoClip, Climbed);
	}
	case EHawkeyeParkourMove::LedgeGrab:
	{
		HangObstacle = Obstacle;
		// Falling onto it is the quick catch; a jump still rising (the late catch) reaches up the grab's way.
		const bool bFalling = Movement->IsFalling() && Movement->Velocity.Z <= 0.f;
		return BeginMove(Move, HangLocationFor(Obstacle), -1.f, bFalling ? CatchSeconds : GrabSeconds,
			bFalling ? CatchClip : GrabClip, Climbed);
	}
	case EHawkeyeParkourMove::DropToHang:
	{
		// Over the lip (a parapet or a rail) with a hop that clears it, then down past the floor she leaves to the hang.
		HangObstacle = Obstacle;
		DropHop = FMath::Max(0.f, Obstacle.Height) + HawkeyeParkour::DropHopClearance;
		TArray<TWeakObjectPtr<AActor>> Through = Climbed;
		Through.Add(Movement->CurrentFloor.HitResult.GetActor());
		return BeginMove(Move, HangLocationFor(Obstacle), DropHop, DropToHangSeconds, CatchClip, Through);
	}
	default:
		return false;
	}
}

TArray<TWeakObjectPtr<AActor>> UParkourComponent::FindClimbedActors(const FHawkeyeParkourObstacle& Obstacle) const
{
	TArray<TWeakObjectPtr<AActor>> Actors = { Obstacle.Actor.Get() };
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Actors;
	}
	// The probe names what its trace met first (a city ledge block stands 2 cm proud of the facade); the capsule
	// collides with whatever is solid just under the top behind the front face, so that is what the move passes.
	const FVector Under = Obstacle.LedgePoint - Obstacle.WallNormal.GetSafeNormal2D() * 15.f - FVector(0.f, 0.f, 15.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HawkeyeParkourClimbed), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Under, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(10.f), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Overlap.bBlockingHit && Overlap.GetActor())
		{
			Actors.AddUnique(Overlap.GetActor());
		}
	}
	return Actors;
}

bool UParkourComponent::BeginMove(EHawkeyeParkourMove Move, const FVector& End, float Rise, float Seconds,
	const FHawkeyeParkourClip& Clip, const TArray<TWeakObjectPtr<AActor>>& PassThrough)
{
	ACharacter* Character = GetCharacter();
	UHawkeyeMovementComponent* Movement = GetHawkeyeMovement();
	if (!Character || !Movement)
	{
		return false;
	}
	MoveStart = Character->GetActorLocation();
	MoveEnd = End;
	MoveRise = Rise < 0.f ? End.Z - MoveStart.Z : Rise;
	MoveSeconds = Seconds;
	MoveYaw = Move == EHawkeyeParkourMove::Climb ? Character->GetActorRotation().Yaw : (-LastObstacle.WallNormal).Rotation().Yaw;
	ActiveMove = Move;
	LastMove = Move;
	LastRoute = EHawkeyeParkourRoute::Procedural;
	bHanging = false;

	PlayMoveClip(Move, Clip, Seconds);
	FHawkeyeTraversalMove Traversal;
	Traversal.Seconds = Seconds;
	Traversal.PassThrough = PassThrough;
	SetMoveExit(Traversal);
	if (bMoveByRootMotion)
	{
		// Facing the wall from the start, as a path move does; the warp windows do the rest.
		Character->SetActorRotation(FRotator(0.f, MoveYaw, 0.f));
	}
	else
	{
		BuildMovePath(Traversal);
	}
	Movement->OnTraversalEnded.BindUObject(this, &UParkourComponent::HandleTraversalEnded);
	Movement->StartTraversal(Traversal);
	UE_LOG(LogHawkeye, Log, TEXT("%s: parkour %s from %s to %s over %.2f s (%s), leaving at %.0f cm/s"), *GetNameSafe(Character),
		*UEnum::GetValueAsString(Move), *MoveStart.ToCompactString(), *MoveEnd.ToCompactString(), Seconds, *DescribeMoveDrive(),
		Traversal.ExitVelocity.Size());
	PlayEffortSound(Move);
	OnParkourStarted.Broadcast(Move, LastRoute);
	return true;
}

void UParkourComponent::PlayMoveClip(EHawkeyeParkourMove Move, const FHawkeyeParkourClip& Clip, float Seconds)
{
	bMoveByRootMotion = IsClipDriven(Move) && SetWarpTargets(Move, LastObstacle) && PlayClip(Clip, Seconds, /*bDriveCapsule=*/true);
	if (!bMoveByRootMotion)
	{
		PlayClip(Clip, Seconds, /*bDriveCapsule=*/false);
	}
}

FString UParkourComponent::DescribeMoveDrive() const
{
	if (bMoveByRootMotion)
	{
		return FString::Printf(TEXT("%s root motion, warped"), *GetNameSafe(ActiveMontage));
	}
	return ActiveMontage ? FString::Printf(TEXT("path, posed by %s"), *GetNameSafe(ActiveMontage)) : FString(TEXT("plain path"));
}

bool UParkourComponent::IsClipDriven(EHawkeyeParkourMove Move) const
{
	// The clip's root motion ends where the clip does; a move that must end on the probe's point follows its path. So
	// the guard's leap (out over the drop), the guard's mantle (the middle of a thin parapet with a drop beyond), a
	// mantle over a thin top onto the floor behind it, and the climb from a hang, whose height never varies, do.
	switch (Move)
	{
	case EHawkeyeParkourMove::Vault:
		return !bGuardLeap;
	case EHawkeyeParkourMove::Mantle:
		return !bGuardMantle && FMath::Abs(LastObstacle.StandPoint.Z - LastObstacle.LedgePoint.Z) < HawkeyeParkour::FloorTolerance;
	default:
		return false;
	}
}

void UParkourComponent::BuildMovePath(FHawkeyeTraversalMove& OutMove) const
{
	OutMove.Keys.Reset(HawkeyeParkour::PathSteps + 1);
	for (int32 Step = 0; Step <= HawkeyeParkour::PathSteps; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / HawkeyeParkour::PathSteps;
		FHawkeyeTraversalKey& Key = OutMove.Keys.AddDefaulted_GetRef();
		Key.Location = ComputeMoveLocation(Alpha);
		Key.Yaw = ComputeMoveYaw(Alpha);
	}
}

void UParkourComponent::SetMoveExit(FHawkeyeTraversalMove& OutMove) const
{
	if (ActiveMove == EHawkeyeParkourMove::LedgeGrab || ActiveMove == EHawkeyeParkourMove::DropToHang || IsHangTransfer(ActiveMove))
	{
		OutMove.Exit = EHawkeyeTraversalExit::Hold;
		return;
	}
	if (bGuardLeap)
	{
		// Over the top and let go toward the drop.
		OutMove.Exit = EHawkeyeTraversalExit::Fall;
		OutMove.ExitVelocity = GuardDirection * GuardSpeed;
		return;
	}
	OutMove.Exit = EHawkeyeTraversalExit::Walk;
	if (bGuardMantle)
	{
		// Onto the parapet top and stopped there: no step on toward the drop.
		OutMove.ExitVelocity = FVector::ZeroVector;
		return;
	}
	const UCharacterMovementComponent* Movement = GetMovement();
	const FVector Along = FRotator(0.f, MoveYaw, 0.f).Vector();
	const float EntrySpeed = Movement ? static_cast<float>(FVector::DotProduct(Movement->Velocity, Along)) : 0.f;
	OutMove.ExitVelocity = Along * GetExitSpeed(ActiveMove, EntrySpeed);
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

void UParkourComponent::ComputeMoveShape(float Alpha, float& OutForward, float& OutUp) const
{
	if (ActiveMove == EHawkeyeParkourMove::DropToHang)
	{
		// Out over the lip in the first 70% with a hop that peaks halfway there; the clip only
		// poses the hands. The drop to the hang comes in with the left-over Z.
		OutForward = FMath::SmoothStep(0.f, 0.7f, Alpha);
		OutUp = FMath::Sin(PI * FMath::Min(Alpha / 0.7f, 1.f));
		return;
	}
	// Over in an arc (a vault, the guard's leap), or up first and then in.
	const bool bArc = ActiveMove == EHawkeyeParkourMove::Vault;
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

void UParkourComponent::HandleTraversalEnded(EHawkeyeTraversalEnd End)
{
	switch (End)
	{
	case EHawkeyeTraversalEnd::Held:
	case EHawkeyeTraversalEnd::Exited:
		if (IsPerformingMove())
		{
			FinishMove();
		}
		break;
	case EHawkeyeTraversalEnd::Interrupted:
		AbandonTraversal();
		break;
	default:
		// Released: the call that let go has already left the hang.
		break;
	}
}

void UParkourComponent::FinishMove()
{
	const EHawkeyeParkourMove Move = ActiveMove;
	// The last of a turn happened in the movement's tick that ended it.
	TickMoveTurnCamera();
	ActiveMove = EHawkeyeParkourMove::None;
	const bool bOntoParapet = bGuardMantle && Move == EHawkeyeParkourMove::Mantle;
	bGuardMantle = false;
	bMoveTurns = false;
	if (Move == EHawkeyeParkourMove::LedgeGrab || Move == EHawkeyeParkourMove::DropToHang || IsHangTransfer(Move))
	{
		EnterHang();
		return;
	}
	StopClip(HawkeyeParkour::ClipBlendOut);
	const FVector Velocity = GetMovement() ? GetMovement()->Velocity : FVector::ZeroVector;
	if (bGuardLeap)
	{
		bGuardLeap = false;
		UE_LOG(LogHawkeye, Log, TEXT("%s: roof-edge guard: over the parapet at %s, off the roof at %.0f cm/s"), *GetNameSafe(GetOwner()),
			GetOwner() ? *GetOwner()->GetActorLocation().ToCompactString() : TEXT("?"), Velocity.Size2D());
		OnParkourFinished.Broadcast(Move);
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: parkour %s done at %s, %s at %.0f cm/s"), *GetNameSafe(GetOwner()), *UEnum::GetValueAsString(Move),
		GetOwner() ? *GetOwner()->GetActorLocation().ToCompactString() : TEXT("?"),
		GetMovement() ? *GetMovement()->GetMovementName() : TEXT("?"), Velocity.Size2D());
	if (bOntoParapet)
	{
		BeginPerch();
	}
	OnParkourFinished.Broadcast(Move);
}

void UParkourComponent::AbandonTraversal()
{
	if (!IsPerformingMove() && !bHanging)
	{
		return;
	}
	const EHawkeyeParkourMove Move = IsPerformingMove() ? ActiveMove : EHawkeyeParkourMove::LedgeGrab;
	UE_LOG(LogHawkeye, Log, TEXT("%s: parkour %s ends: the movement mode changed to %s under it"), *GetNameSafe(GetOwner()),
		bHanging ? TEXT("hang") : *UEnum::GetValueAsString(Move), GetMovement() ? *GetMovement()->GetMovementName() : TEXT("?"));
	ActiveMove = EHawkeyeParkourMove::None;
	bHanging = false;
	bGuardMantle = false;
	bGuardLeap = false;
	bMoveTurns = false;
	EndHangState();
	StopClip(HawkeyeParkour::ClipBlendOut);
	OnParkourFinished.Broadcast(Move);
}

void UParkourComponent::EnterHang()
{
	bHanging = true;
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
	return BeginMove(EHawkeyeParkourMove::Climb, HangObstacle.StandPoint + Lift, -1.f, ClimbSeconds, ClimbClip,
		FindClimbedActors(HangObstacle));
}

bool UParkourComponent::DropFromHang()
{
	ACharacter* Character = GetCharacter();
	UHawkeyeMovementComponent* Movement = GetHawkeyeMovement();
	if (!bHanging || !Character || !Movement)
	{
		return false;
	}
	bHanging = false;
	EndHangState();
	StopClip(HawkeyeParkour::ClipBlendOut);
	// Falling from still, a step off the wall first so the capsule does not scrape down it.
	Movement->ReleaseTraversal(FVector::ZeroVector, HangObstacle.WallNormal.GetSafeNormal2D() * 15.f);
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
	TickSampleExit();
	if (RootMotionRestoreIn > 0.f)
	{
		RootMotionRestoreIn -= DeltaTime;
		if (RootMotionRestoreIn <= 0.f && !IsPerformingMove() && !bHanging)
		{
			RestoreRootMotionMode();
		}
	}
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
		// The movement runs the move; the camera follows a turning one.
		TickMoveTurnCamera();
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

void UParkourComponent::TickSampleExit()
{
	UCharacterMovementComponent* Movement = GetMovement();
	// The sample sets the mode it leaves in as its montage blends out; until then it is still Flying.
	if (!bSampleExitPending || !Movement || IsSampleTraversalActive() || Movement->IsFlying())
	{
		return;
	}
	bSampleExitPending = false;
	const float Along = static_cast<float>(FVector::DotProduct(Movement->Velocity, SampleExitDirection));
	if ((!Movement->IsMovingOnGround() && !Movement->IsFalling()) || Along >= SampleExitSpeed)
	{
		return;
	}
	Movement->Velocity += SampleExitDirection * (SampleExitSpeed - Along);
	UE_LOG(LogHawkeye, Log, TEXT("%s: the sample's traversal is over: out at %.0f cm/s along it, raised to %.0f"),
		*GetNameSafe(GetOwner()), Along, SampleExitSpeed);
}

// --- Clips --------------------------------------------------------------------------------------

bool UParkourComponent::PlayClip(const FHawkeyeParkourClip& Clip, float Seconds, bool bDriveCapsule)
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	// Resident since BeginPlay (LoadedClips), or not at all: a move never waits on a load.
	UAnimMontage* Montage = Clip.Montage.Get();
	if (!Anim || !Montage || Clip.EndTime <= Clip.StartTime || (bDriveCapsule && !Montage->HasRootMotion()))
	{
		return false;
	}
	SetClipRootMotion(bDriveCapsule);
	const float End = FMath::Min(Clip.EndTime, Montage->GetPlayLength());
	const float Rate = (End - Clip.StartTime) / FMath::Max(Seconds, 0.05f);
	if (Anim->Montage_Play(Montage, Rate, EMontagePlayReturnType::MontageLength, Clip.StartTime) <= 0.f)
	{
		return false;
	}
	ActiveMontage = Montage;
	RootMotionRestoreIn = 0.f;
	return true;
}

bool UParkourComponent::SetWarpTargets(EHawkeyeParkourMove Move, const FHawkeyeParkourObstacle& Obstacle) const
{
	using namespace HawkeyeParkour;
	const AActor* Owner = GetOwner();
	UMotionWarpingComponent* Warp = Owner ? Owner->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
	if (!Warp)
	{
		return false;
	}
	// Where the sample's traversal puts them: on the top's edges, facing into the wall.
	const FVector Normal = Obstacle.WallNormal.GetSafeNormal2D();
	const FRotator Facing(0.f, (-Normal).Rotation().Yaw, 0.f);
	Warp->AddOrUpdateWarpTargetFromLocationAndRotation(FrontLedge, Obstacle.LedgePoint, Facing);
	if (Move == EHawkeyeParkourMove::Vault && Obstacle.bClearBeyond)
	{
		Warp->AddOrUpdateWarpTargetFromLocationAndRotation(BackLedge, Obstacle.LedgePoint - Normal * Obstacle.Depth, Facing);
		Warp->AddOrUpdateWarpTargetFromLocationAndRotation(BackFloor, Obstacle.LandingPoint, Facing);
	}
	else
	{
		Warp->RemoveWarpTarget(BackLedge);
		Warp->RemoveWarpTarget(BackFloor);
	}
	return true;
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
	bMoveByRootMotion = false;
	if (bRootMotionOverridden)
	{
		// The blend-out must not drive the capsule she has just been given back; the AnimBP's mode returns after it.
		SetClipRootMotion(false);
		RootMotionRestoreIn = BlendOutSeconds + 0.05f;
	}
}

void UParkourComponent::SetClipRootMotion(bool bDriveCapsule)
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return;
	}
	if (!bRootMotionOverridden)
	{
		SavedRootMotionMode = Anim->RootMotionMode;
		bRootMotionOverridden = true;
	}
	Anim->SetRootMotionMode(bDriveCapsule ? ERootMotionMode::RootMotionFromMontagesOnly : ERootMotionMode::IgnoreRootMotion);
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
