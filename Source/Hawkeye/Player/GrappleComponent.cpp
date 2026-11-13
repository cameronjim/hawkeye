#include "Player/GrappleComponent.h"

#include "Camera/CameraComponent.h"
#include "Hawkeye.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/InventoryComponent.h"
#include "NiagaraComponent.h"
#include "Traversal/GrappleReachQuery.h"
#include "Traversal/GrappleTraces.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/GrappleAnchor.h"
#include "World/GrappleArrowProjectile.h"

static TAutoConsoleVariable<int32> CVarHawkeyeDebugGrapple(
	TEXT("hawkeye.DebugGrapple"),
	0,
	TEXT("1 writes on the HUD why a grey grapple diamond cannot be zipped to, and the marked anchor's state."),
	ECVF_Default);

namespace HawkeyeGrapple
{
	/**
	 * Line-of-sight traces per refresh at most; the candidates are tried nearest the middle first. At 8 a
	 * visible anchor behind eight hidden ones nearer the middle was never marked (Hawkeye.Grapple.Audit).
	 */
	static constexpr int32 MaxSightTraces = 32;

	/** How far below an anchor's landing point its building is looked for. */
	static constexpr float SupportProbeDepth = 150.f;
}

const FName UGrappleComponent::SpeedParameter(TEXT("Speed"));
const FName UGrappleComponent::BeamStartParameter(TEXT("User.BeamStart"));
const FName UGrappleComponent::BeamEndParameter(TEXT("User.BeamEnd"));

void UGrappleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UHawkeyeAudioSubsystem::StopLoop(ZipLoop, TEXT("grapple zip"));
	UHawkeyeVfxSubsystem::Kill(ZipLine);
	Super::EndPlay(EndPlayReason);
}

float UGrappleComponent::ComputeZipSoundSpeed() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	const float Full = FMath::Clamp((Movement ? Movement->ZipSpeed : 0.f) / 2400.f, 0.2f, 1.f);
	return IsHopping() ? Full * 0.4f : Full;
}

UGrappleComponent::UGrappleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	ArrowClass = AGrappleArrowProjectile::StaticClass();
}

void UGrappleComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsZipping())
	{
		UpdateZipLine();
		if (ZipLoop)
		{
			ZipLoop->SetFloatParameter(SpeedParameter, ComputeZipSoundSpeed());
		}
	}

	RefreshAccumulator += DeltaTime;
	if (RefreshAccumulator >= RefreshSeconds)
	{
		RefreshAccumulator = 0.f;
		if (ShouldRefreshTarget())
		{
			RefreshTarget();
		}
		else
		{
			TargetAnchor.Reset();
			BlockedAnchor.Reset();
		}
		RecoverNearbyArrows();
	}
}

ACharacter* UGrappleComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

UInventoryComponent* UGrappleComponent::GetInventory() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UInventoryComponent>() : nullptr;
}

int32 UGrappleComponent::FindGrappleSlot() const
{
	const UInventoryComponent* Inventory = GetInventory();
	return Inventory ? Inventory->FindArrowSlotByEffect(EArrowHitEffect::Grapple) : INDEX_NONE;
}

// --- Targeting ----------------------------------------------------------------------------------

FIntPoint UGrappleComponent::CellOf(const FVector& Location) const
{
	return FIntPoint(FMath::FloorToInt(Location.X / GridCellSize), FMath::FloorToInt(Location.Y / GridCellSize));
}

void UGrappleComponent::BuildGrid() const
{
	AnchorGrid.Reset();
	bGridBuilt = true;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 Count = 0;
	for (TActorIterator<AGrappleAnchor> It(World); It; ++It)
	{
		AnchorGrid.FindOrAdd(CellOf(It->GetActorLocation())).Add(*It);
		++Count;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %d grapple anchors in %d grid cells of %.0f cm"),
		*GetNameSafe(GetOwner()), Count, AnchorGrid.Num(), GridCellSize);
}

void UGrappleComponent::RebuildAnchorGrid()
{
	BuildGrid();
}

void UGrappleComponent::GatherNearbyAnchors(const FVector& Location, TArray<AGrappleAnchor*>& OutAnchors) const
{
	if (!bGridBuilt)
	{
		BuildGrid();
	}

	const FIntPoint Centre = CellOf(Location);
	const int32 Reach = FMath::CeilToInt(Range / GridCellSize);
	for (int32 DX = -Reach; DX <= Reach; ++DX)
	{
		for (int32 DY = -Reach; DY <= Reach; ++DY)
		{
			const TArray<TWeakObjectPtr<AGrappleAnchor>>* Cell = AnchorGrid.Find(Centre + FIntPoint(DX, DY));
			if (!Cell)
			{
				continue;
			}
			for (const TWeakObjectPtr<AGrappleAnchor>& Anchor : *Cell)
			{
				if (AGrappleAnchor* Live = Anchor.Get())
				{
					OutAnchors.Add(Live);
				}
			}
		}
	}
}

bool UGrappleComponent::IsAnchorValid(const AGrappleAnchor* Anchor, const FVector& From, const FVector& ViewLocation,
	const FVector& ViewForward, float& OutAngleDegrees) const
{
	if (!IsValid(Anchor) || !Anchor->bEnabled || (IsZipping() && Anchor == ZipAnchor.Get()))
	{
		return false;
	}

	const FVector Marker = Anchor->GetMarkerLocation();
	const float Distance = FVector::Dist(From, Marker);
	if (Distance > Range || Distance < MinRange)
	{
		return false;
	}

	OutAngleDegrees = FGrappleReachQuery::AngleOffViewDegrees(ViewLocation, ViewForward, Marker);
	return OutAngleDegrees <= ConeDegrees && IsOnScreen(ViewLocation, ViewForward, Marker);
}

bool UGrappleComponent::IsOnScreen(const FVector& ViewLocation, const FVector& ViewForward, const FVector& Point) const
{
	FGrappleReachRules Rules;
	Rules.ViewFieldOfView = ViewFieldOfView;
	Rules.ViewAspectRatio = ViewAspectRatio;
	Rules.ScreenEdgeMarginDegrees = ScreenEdgeMarginDegrees;
	return FGrappleReachQuery::IsOnScreen(Rules, ViewLocation, ViewForward, Point);
}

int32 UGrappleComponent::ChoosePick(const TArray<FVector2f>& AnglesAndDistances, float TieDegrees)
{
	return FGrappleReachQuery::ChoosePick(AnglesAndDistances, TieDegrees);
}

FGrappleReachRules UGrappleComponent::MakeReachRules() const
{
	FGrappleReachRules Rules;
	Rules.Range = Range;
	Rules.MinRange = MinRange;
	Rules.ConeDegrees = ConeDegrees;
	Rules.PickTieDegrees = PickTieDegrees;
	Rules.ScreenEdgeMarginDegrees = ScreenEdgeMarginDegrees;
	Rules.ViewFieldOfView = ViewFieldOfView;
	Rules.ViewAspectRatio = ViewAspectRatio;
	Rules.SightTolerance = SightTolerance;
	Rules.SupportRadius = SupportRadius;
	Rules.ZipLaunchHeight = ZipLaunchHeight;
	Rules.ZipStartIgnoreRadius = ZipStartIgnoreRadius;
	const ACharacter* Character = GetCharacter();
	if (const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr)
	{
		Rules.CapsuleRadius = Capsule->GetScaledCapsuleRadius();
		Rules.CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	}
	return Rules;
}

bool UGrappleComponent::HasLineOfSight(const AGrappleAnchor* Anchor, const FVector& ViewLocation) const
{
	const UWorld* World = GetWorld();
	if (!World || !Anchor)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleSight), false, GetOwner());
	Params.AddIgnoredActor(Anchor);
	return HawkeyeGrappleTraces::HasLineOfSight(*World, ViewLocation, Anchor->GetMarkerLocation(), SightTolerance, Params);
}

bool UGrappleComponent::IsDebugEnabled()
{
	return CVarHawkeyeDebugGrapple.GetValueOnGameThread() != 0;
}

bool UGrappleComponent::ShouldRefreshTarget() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return !Pawn || !Pawn->GetController() || Pawn->IsPlayerControlled();
}

EGrappleTargetState UGrappleComponent::GetTargetState() const
{
	const AGrappleAnchor* Anchor = TargetAnchor.Get();
	if (!IsValid(Anchor) || !Anchor->bEnabled)
	{
		return EGrappleTargetState::None;
	}
	if (!CanChain())
	{
		return EGrappleTargetState::TooEarlyToChain;
	}
	if (IsArrowInFlight())
	{
		return EGrappleTargetState::ArrowInFlight;
	}
	// Grapple arrows have no count (gameplay-semantics.md, "Grapple arrow"): nothing else can hold a press back.
	return EGrappleTargetState::Ready;
}

AGrappleAnchor* UGrappleComponent::SelectBestAnchor(const FVector& ViewLocation, const FVector& ViewForward) const
{
	AGrappleAnchor* Blocked = nullptr;
	FString Reason;
	return SelectBestAnchor(ViewLocation, ViewForward, Blocked, Reason);
}

AGrappleAnchor* UGrappleComponent::SelectBestAnchor(const FVector& ViewLocation, const FVector& ViewForward,
	AGrappleAnchor*& OutBlocked, FString& OutBlockedReason) const
{
	OutBlocked = nullptr;
	OutBlockedReason.Reset();
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	// The zip line is checked from where she is now, the way StartZip would fly it: from the
	// ground with the hop, mid-zip (a chain) from here with the line she is on counting as her start.
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const bool bFromGround = !IsZipping() && Movement && Movement->IsMovingOnGround();
	return SelectBestAnchorFrom(Owner->GetActorLocation(), bFromGround, ViewLocation, ViewForward, OutBlocked, OutBlockedReason);
}

AGrappleAnchor* UGrappleComponent::SelectBestAnchorFrom(const FVector& From, bool bFromGround, const FVector& ViewLocation,
	const FVector& ViewForward, AGrappleAnchor*& OutBlocked, FString& OutBlockedReason) const
{
	OutBlocked = nullptr;
	OutBlockedReason.Reset();
	TArray<AGrappleAnchor*> Nearby;
	GatherNearbyAnchors(From, Nearby);

	struct FCandidate
	{
		float Angle;
		float Distance;
		AGrappleAnchor* Anchor;
	};
	TArray<FCandidate> Candidates;
	for (AGrappleAnchor* Anchor : Nearby)
	{
		float Angle = 0.f;
		if (IsAnchorValid(Anchor, From, ViewLocation, ViewForward, Angle))
		{
			Candidates.Add({ Angle, static_cast<float>(FVector::Dist(From, Anchor->GetMarkerLocation())), Anchor });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.Angle < B.Angle || (A.Angle == B.Angle && A.Distance < B.Distance);
	});

	const ACharacter* Character = GetCharacter();
	TArray<AActor*> CurrentSupports;
	if (IsZipping())
	{
		for (const TWeakObjectPtr<AActor>& Support : ZipIgnoredSupports)
		{
			if (AActor* Actor = Support.Get())
			{
				CurrentSupports.Add(Actor);
			}
		}
	}

	// Nearest the middle first; once one passes, any as near the middle (within PickTieDegrees) that is nearer to
	// her and passes too takes it (ChoosePick's rule).
	const int32 Traces = FMath::Min(Candidates.Num(), HawkeyeGrapple::MaxSightTraces);
	int32 ClearChecks = 0;
	int32 Best = INDEX_NONE;
	float WindowEnd = 0.f;   // the first to pass sets it: its angle plus PickTieDegrees
	for (int32 Index = 0; Index < Traces; ++Index)
	{
		const FCandidate& Candidate = Candidates[Index];
		if (Best != INDEX_NONE)
		{
			if (Candidate.Angle > WindowEnd)
			{
				break;
			}
			if (Candidate.Distance >= Candidates[Best].Distance)
			{
				continue;
			}
		}
		AGrappleAnchor* Anchor = Candidate.Anchor;
		if (!HasLineOfSight(Anchor, ViewLocation))
		{
			continue;
		}
		if (!bRequireClearZip || !Character)
		{
			WindowEnd = Best == INDEX_NONE ? Candidate.Angle + PickTieDegrees : WindowEnd;
			Best = Index;
			continue;
		}
		if (ClearChecks >= MaxClearChecks)
		{
			break;
		}
		++ClearChecks;
		AActor* Blocker = nullptr;
		if (IsZipClear(From, Anchor, bFromGround, &Blocker, IsZipping() ? &CurrentSupports : nullptr))
		{
			WindowEnd = Best == INDEX_NONE ? Candidate.Angle + PickTieDegrees : WindowEnd;
			Best = Index;
			continue;
		}
		if (!OutBlocked && Best == INDEX_NONE)
		{
			OutBlocked = Anchor;
			OutBlockedReason = FString::Printf(TEXT("the line hits %s"), Blocker ? *Blocker->GetName() : TEXT("something"));
		}
	}
	return Best != INDEX_NONE ? Candidates[Best].Anchor : nullptr;
}

void UGrappleComponent::UpdateTarget(const FVector& ViewLocation, const FVector& ViewForward)
{
	AGrappleAnchor* Blocked = nullptr;
	FString Reason;
	TargetAnchor = SelectBestAnchor(ViewLocation, ViewForward, Blocked, Reason);
	if (Blocked != BlockedAnchor.Get() && Blocked)
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: grapple anchor %s is in view but greyed: %s"), *GetNameSafe(GetOwner()),
			*GetNameSafe(Blocked), *Reason);
	}
	BlockedAnchor = Blocked;
	BlockedReason = Reason;
}

void UGrappleComponent::GetViewPoint(FVector& OutLocation, FVector& OutForward) const
{
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(GetOwner());
	if (const UCameraComponent* Camera = Hawkeye ? Hawkeye->GetFollowCamera() : nullptr)
	{
		OutLocation = Camera->GetComponentLocation();
		OutForward = Camera->GetForwardVector();
		return;
	}

	FRotator EyesRotation = FRotator::ZeroRotator;
	OutLocation = FVector::ZeroVector;
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetActorEyesViewPoint(OutLocation, EyesRotation);
	}
	OutForward = EyesRotation.Vector();
}

void UGrappleComponent::RefreshTarget()
{
	FVector ViewLocation, ViewForward;
	GetViewPoint(ViewLocation, ViewForward);
	// The screen the marker is drawn on: the camera's field of view and the viewport's shape.
	if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(GetOwner()))
	{
		if (const UCameraComponent* Camera = Hawkeye->GetFollowCamera())
		{
			ViewFieldOfView = Camera->FieldOfView;
		}
		if (const APlayerController* PC = Cast<APlayerController>(Hawkeye->GetController()))
		{
			int32 SizeX = 0, SizeY = 0;
			PC->GetViewportSize(SizeX, SizeY);
			if (SizeX > 0 && SizeY > 0)
			{
				ViewAspectRatio = static_cast<float>(SizeX) / static_cast<float>(SizeY);
			}
		}
	}
	UpdateTarget(ViewLocation, ViewForward);
}

// --- Firing -------------------------------------------------------------------------------------

bool UGrappleComponent::TryFire()
{
	const EGrappleTargetState State = GetTargetState();
	if (State != EGrappleTargetState::Ready)
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: grapple press refused: %s%s"), *GetNameSafe(GetOwner()),
			*UEnum::GetValueAsString(State),
			BlockedAnchor.IsValid() ? *FString::Printf(TEXT(" (%s is greyed: %s)"), *BlockedAnchor->GetName(), *BlockedReason)
				: TEXT(""));
		return false;
	}
	AGrappleAnchor* Anchor = TargetAnchor.Get();

	// Whatever slot is nocked, Q (and a release with the grapple slot active) shoots the quiver's grapple
	// arrow type; it has no count, so nothing is spent (without a grapple slot the component's own ArrowClass flies).
	const UInventoryComponent* Inventory = GetInventory();
	const int32 Slot = FindGrappleSlot();
	UArrowDefinition* Definition = (Inventory && Slot != INDEX_NONE) ? Inventory->GetArrowSlot(Slot).Arrow.Get() : nullptr;
	++UseCount;

	const AActor* Owner = GetOwner();
	AGrappleArrowProjectile* Arrow = SpawnGrappleArrow(Definition, Anchor);
	if (Owner)
	{
		UHawkeyeAudioSubsystem::PlayAt(this, FireSound, Owner->GetActorLocation(), TEXT("grapple fire"));
	}

	UE_LOG(LogHawkeye, Log, TEXT("%s: grapple arrow at %s, %.0f cm away"), *GetNameSafe(Owner),
		*GetNameSafe(Anchor), Owner ? FVector::Dist(Owner->GetActorLocation(), Anchor->GetMarkerLocation()) : 0.f);

	if (!Arrow)
	{
		// No world to fly through: the arrow is there at once.
		Anchor->AddStuckArrow(nullptr, GetOwner());
		HandleArrowArrived(nullptr, Anchor);
		return true;
	}
	InFlightArrow = Arrow;
	if (IsZipping())
	{
		// A chain: the arrow has to beat the rest of the line, or she lands first and it is a
		// fresh zip off the roof rather than a redirect in the air.
		Arrow->Launch(Anchor, this, ChainArrowSpeed);
	}
	else
	{
		Arrow->Launch(Anchor, this);
	}
	return true;
}

AGrappleArrowProjectile* UGrappleComponent::SpawnGrappleArrow(UArrowDefinition* Definition, const AGrappleAnchor* Anchor) const
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!World || !Owner || !Anchor)
	{
		return nullptr;
	}

	// Through the bow when there is one, so it leaves the hand; the definition's class wins when
	// it is a grapple projectile (BP_Arrow_Grapple), ArrowClass otherwise.
	AGrappleArrowProjectile* Arrow = nullptr;
	if (const UBowComponent* Bow = Owner->FindComponentByClass<UBowComponent>())
	{
		const FVector Start = Bow->GetArrowSpawnLocation();
		const FVector Direction = (Anchor->GetMarkerLocation() - Start).GetSafeNormal();
		AArrowProjectile* Spawned = Bow->SpawnArrowProjectile(Definition, ArrowClass, Direction);
		Arrow = Cast<AGrappleArrowProjectile>(Spawned);
		if (Spawned && !Arrow)
		{
			UE_LOG(LogHawkeye, Warning, TEXT("%s: %s's projectile %s is not a grapple arrow; using %s."),
				*GetNameSafe(Owner), *GetNameSafe(Definition), *GetNameSafe(Spawned->GetClass()), *GetNameSafe(ArrowClass));
			Spawned->Destroy();
		}
	}
	if (!Arrow && ArrowClass)
	{
		const FVector Start = Owner->GetActorTransform().TransformPosition(ArrowLaunchOffset);
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Owner = Owner;
		Arrow = World->SpawnActor<AGrappleArrowProjectile>(ArrowClass, Start, FRotator::ZeroRotator, SpawnParams);
	}
	if (Arrow)
	{
		Arrow->InitArrow(Definition, nullptr, 0.f, Owner, nullptr);
	}
	return Arrow;
}

FVector UGrappleComponent::GetZipLineStart() const
{
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	}
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (Mesh && !ZipLineSocket.IsNone() && Mesh->DoesSocketExist(ZipLineSocket))
	{
		return Mesh->GetSocketLocation(ZipLineSocket);
	}
	return Character->GetActorLocation() + FVector(0.f, 0.f, 40.f);
}

void UGrappleComponent::UpdateZipLine()
{
	const AGrappleAnchor* Anchor = ZipAnchor.Get();
	if (!Anchor)
	{
		return;
	}
	const FVector Start = GetZipLineStart();
	if (!ZipLine)
	{
		ZipLine = UHawkeyeVfxSubsystem::SpawnKept(this, ZipLineVfx, Start, UHawkeyeVfxSubsystem::ZipLineEvent);
	}
	if (IsValid(ZipLine))
	{
		// The component sits at her hand so its bounds hold the near end; both ends are world positions.
		ZipLine->SetWorldLocation(Start);
		ZipLine->SetVariableVec3(BeamStartParameter, Start);
		ZipLine->SetVariableVec3(BeamEndParameter, Anchor->GetMarkerLocation());
	}
}

void UGrappleComponent::HandleArrowArrived(AGrappleArrowProjectile* Arrow, AGrappleAnchor* Anchor)
{
	if (Anchor)
	{
		const AActor* Owner = GetOwner();
		const FVector Back = Owner ? (Owner->GetActorLocation() - Anchor->GetMarkerLocation()).GetSafeNormal() : FVector::UpVector;
		UHawkeyeVfxSubsystem::SpawnAt(this, AnchorSparksVfx, Anchor->GetMarkerLocation(), Back.Rotation(),
			UHawkeyeVfxSubsystem::AnchorSparksEvent);
	}
	if (InFlightArrow.Get() == Arrow)
	{
		InFlightArrow.Reset();
	}
	StartZip(Anchor);
}

int32 UGrappleComponent::RecoverNearbyArrows()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 0;
	}

	TArray<AGrappleAnchor*> Nearby;
	GatherNearbyAnchors(Owner->GetActorLocation(), Nearby);
	int32 Recovered = 0;
	for (AGrappleAnchor* Anchor : Nearby)
	{
		if (Anchor->GetStuckArrowCountFor(Owner) > 0
			&& FVector::Dist(Owner->GetActorLocation(), Anchor->GetActorLocation()) <= RecoverRadius)
		{
			// Only her own: the partner zipping in beside Kate's anchor used to pocket her arrow.
			Recovered += Anchor->RecoverStuckArrows(const_cast<AActor*>(Owner));
		}
	}
	if (Recovered > 0)
	{
		// No count to put them back into: they are only tidied out of the anchor.
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: tidied %d stuck grapple arrow(s) out of nearby anchors"), *GetNameSafe(Owner),
			Recovered);
	}
	return Recovered;
}

void UGrappleComponent::ReelBackArrow(AGrappleAnchor* Anchor, const TCHAR* Why)
{
	AActor* Owner = GetOwner();
	if (!Anchor || !Owner)
	{
		return;
	}
	const int32 Reeled = Anchor->RecoverStuckArrows(Owner);
	if (Reeled > 0)
	{
		UE_LOG(LogHawkeye, Verbose, TEXT("%s: reeled %d grapple arrow(s) back out of %s (%s)"), *GetNameSafe(Owner), Reeled,
			*GetNameSafe(Anchor), Why);
	}
}

// --- Zip ----------------------------------------------------------------------------------------

FVector UGrappleComponent::ComputeZipEnd(const AGrappleAnchor* Anchor) const
{
	if (!Anchor)
	{
		return FVector::ZeroVector;
	}
	const ACharacter* Character = GetCharacter();
	const float HalfHeight = Character && Character->GetCapsuleComponent()
		? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	// Feet on the landing point, with the same 2 cm of air a teleport leaves so the floor is found.
	return Anchor->GetLandingLocation() + FVector(0.f, 0.f, HalfHeight + 2.f);
}

void UGrappleComponent::FindAnchorSupports(const AGrappleAnchor* Anchor, TArray<AActor*>& OutSupports) const
{
	const UWorld* World = GetWorld();
	if (!World || !Anchor)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleSupport), true, GetOwner());
	Params.AddIgnoredActor(Anchor);
	HawkeyeGrappleTraces::FindAnchorSupports(*World, Anchor->GetLandingLocation(), Anchor->GetActorLocation(),
		HawkeyeGrapple::SupportProbeDepth, SupportRadius, Params, OutSupports);
}

void UGrappleComponent::FindStartSupports(const FVector& Start, TArray<AActor*>& OutSupports) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleStartSupport), true, GetOwner());
	const ACharacter* Character = GetCharacter();
	const float HalfHeight = Character && Character->GetCapsuleComponent()
		? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	HawkeyeGrappleTraces::FindStartSupports(*World, Start, HalfHeight, ZipStartIgnoreRadius, Params, OutSupports);
}

float UGrappleComponent::GetStartReleaseDistance() const
{
	const ACharacter* Character = GetCharacter();
	const float Radius = Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 34.f;
	return ZipStartIgnoreRadius + Radius;
}

FVector UGrappleComponent::ComputeZipLaunch(const FVector& Start, bool bFromGround) const
{
	return bFromGround ? Start + FVector(0.f, 0.f, ZipLaunchHeight) : Start;
}

bool UGrappleComponent::IsZipClear(const FVector& From, const AGrappleAnchor* Anchor, bool bFromGround, AActor** OutBlocker,
	const TArray<AActor*>* ExtraStartSupports) const
{
	const UWorld* World = GetWorld();
	const ACharacter* Character = GetCharacter();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (OutBlocker)
	{
		*OutBlocker = nullptr;
	}
	if (!World || !Capsule || !Anchor)
	{
		return false;
	}
	const FVector Launch = ComputeZipLaunch(From, bFromGround);
	const FVector End = ComputeZipEnd(Anchor);
	TArray<AActor*> AnchorSupports;
	FindAnchorSupports(Anchor, AnchorSupports);
	TArray<AActor*> StartSupports;
	FindStartSupports(From, StartSupports);
	if (ExtraStartSupports)
	{
		for (AActor* Support : *ExtraStartSupports)
		{
			StartSupports.AddUnique(Support);
		}
	}

	// Where the start supports count again: the first point on the line that far from From.
	const FVector ReleasePoint = FGrappleReachQuery::ComputeStartReleasePoint(From, Launch, End, GetStartReleaseDistance());

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleZipClear), false, Character);
	Params.AddIgnoredActor(Anchor);
	FHitResult Hit;
	if (HawkeyeGrappleTraces::IsZipClear(*World, Launch, ReleasePoint, End, Shape, Capsule->GetCollisionObjectType(), Responses,
			Params, AnchorSupports, StartSupports, &Hit))
	{
		return true;
	}
	if (OutBlocker)
	{
		*OutBlocker = Hit.GetActor();
	}
	return false;
}

void UGrappleComponent::SetSupportsIgnored(bool bIgnore)
{
	const ACharacter* Character = GetCharacter();
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return;
	}
	for (const TWeakObjectPtr<AActor>& Support : ZipIgnoredSupports)
	{
		if (AActor* Actor = Support.Get())
		{
			Capsule->IgnoreActorWhenMoving(Actor, bIgnore);
		}
	}
	for (const TWeakObjectPtr<AActor>& Support : ZipStartSupports)
	{
		if (AActor* Actor = Support.Get())
		{
			// Released start supports are already back; letting go of everything includes them.
			if (!bIgnore || bStartSupportsIgnored)
			{
				Capsule->IgnoreActorWhenMoving(Actor, bIgnore);
			}
		}
	}
}

void UGrappleComponent::ReleaseStartSupports()
{
	const ACharacter* Character = GetCharacter();
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule || !bStartSupportsIgnored)
	{
		return;
	}
	bStartSupportsIgnored = false;
	for (const TWeakObjectPtr<AActor>& Support : ZipStartSupports)
	{
		AActor* Actor = Support.Get();
		if (Actor && !ZipIgnoredSupports.Contains(Support))
		{
			Capsule->IgnoreActorWhenMoving(Actor, false);
		}
	}
}

UHawkeyeMovementComponent* UGrappleComponent::GetMovement() const
{
	const ACharacter* Character = GetCharacter();
	return Character ? Cast<UHawkeyeMovementComponent>(Character->GetCharacterMovement()) : nullptr;
}

bool UGrappleComponent::IsZipping() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	return Movement && Movement->IsZipping();
}

float UGrappleComponent::GetZipProgress() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	return Movement ? Movement->GetZipProgress() : 0.f;
}

float UGrappleComponent::GetZipLength() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	return Movement ? Movement->GetZipLength() : 0.f;
}

bool UGrappleComponent::IsHopping() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	return Movement && Movement->IsZipHopping();
}

FVector UGrappleComponent::GetZipLaunch() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	return Movement ? Movement->GetZipLaunch() : FVector::ZeroVector;
}

bool UGrappleComponent::IsRedirecting() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	return Movement && Movement->IsZipRedirecting();
}

FVector UGrappleComponent::GetZipDirection() const
{
	const UHawkeyeMovementComponent* Movement = GetMovement();
	return Movement ? Movement->GetZipDirection() : FVector::ZeroVector;
}

bool UGrappleComponent::StartZip(AGrappleAnchor* Anchor)
{
	ACharacter* Character = GetCharacter();
	UHawkeyeMovementComponent* Movement = GetMovement();
	if (!IsValid(Anchor) || !Movement)
	{
		return false;
	}

	// From the ground there is a hop to the launch point; a chain or a mid-air zip goes straight.
	const bool bWasZipping = Movement->IsZipping();
	const bool bFromGround = !bWasZipping && Movement->IsMovingOnGround();
	const bool bRedirect = bWasZipping && !Movement->IsZipHopping();
	// A chain in the air is often right at the old anchor's parapet: that building counts as
	// where she starts from, ignored until she is clear of it like her own roof on a fresh zip.
	TArray<TWeakObjectPtr<AActor>> OldAnchorSupports;
	if (bRedirect)
	{
		OldAnchorSupports = ZipIgnoredSupports;
	}
	if (bWasZipping)
	{
		// A chain: the old building stops being ignored, the new one starts, and the old line's
		// arrow comes back (she never gets within reach of it to pull it out).
		SetSupportsIgnored(false);
		if (ZipAnchor.Get() != Anchor)
		{
			ReelBackArrow(ZipAnchor.Get(), TEXT("chained off it"));
		}
	}
	if (bRedirect)
	{
		++RedirectCount;
	}
	if (AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Character))
	{
		Hawkeye->StopAim();
	}

	ZipAnchor = Anchor;
	ZipStart = Character->GetActorLocation();
	FHawkeyeZipLine Line;
	Line.Launch = ComputeZipLaunch(ZipStart, bFromGround);
	Line.End = ComputeZipEnd(Anchor);
	Line.bHop = bFromGround;

	// The line ends on the anchor's roof and clips its parapet on the way in; that building is
	// expected, and so is the roof and parapet she leaves from until she is clear of them.
	// Anything else in the way cancels the zip.
	TArray<AActor*> Supports;
	FindAnchorSupports(Anchor, Supports);
	ZipIgnoredSupports.Reset();
	for (AActor* Support : Supports)
	{
		ZipIgnoredSupports.Add(Support);
	}
	TArray<AActor*> StartSupports;
	FindStartSupports(ZipStart, StartSupports);
	ZipStartSupports.Reset();
	for (AActor* Support : StartSupports)
	{
		ZipStartSupports.Add(Support);
	}
	for (const TWeakObjectPtr<AActor>& Support : OldAnchorSupports)
	{
		if (Support.IsValid())
		{
			ZipStartSupports.AddUnique(Support);
		}
	}
	bStartSupportsIgnored = true;
	SetSupportsIgnored(true);

	Movement->OnZipHit.BindUObject(this, &UGrappleComponent::HandleZipHit);
	Movement->OnZipStep.BindUObject(this, &UGrappleComponent::HandleZipStep);
	Movement->OnZipEnded.BindUObject(this, &UGrappleComponent::HandleZipEnded);
	Movement->StartZip(Line);

	if (!ZipLoop)
	{
		ZipLoop = UHawkeyeAudioSubsystem::PlayAttached(ZipSound, Character->GetRootComponent(), TEXT("grapple zip"));
	}
	if (ZipLoop)
	{
		ZipLoop->SetFloatParameter(SpeedParameter, ComputeZipSoundSpeed());
	}
	UpdateZipLine();

	UE_LOG(LogHawkeye, Log, TEXT("%s: zip to %s, %.0f cm from %.0f cm/s up to %.0f, %s, line %+.0f cm, ignoring %s%s and %d start support(s)"),
		*GetNameSafe(Character), *GetNameSafe(Anchor), Movement->GetZipLength(), Movement->GetZipLineSpeed(), Movement->ZipSpeed,
		Movement->IsZipRedirecting() ? *FString::Printf(TEXT("mid-air redirect over %.2f s"), Movement->RedirectBlendSeconds)
			: (Movement->IsZipHopping() ? *FString::Printf(TEXT("hop %.0f cm in %.2f s"), ZipLaunchHeight, Movement->ZipHopSeconds)
				: TEXT("no hop")),
		Line.End.Z - Line.Launch.Z, *GetNameSafe(Supports.Num() > 0 ? Supports[0] : nullptr),
		Supports.Num() > 1 ? *FString::Printf(TEXT(" and %d more"), Supports.Num() - 1) : TEXT(""), StartSupports.Num());
	return true;
}

EHawkeyeZipHitResponse UGrappleComponent::HandleZipHit(const FHitResult& Hit, bool bSwinging, bool bArriving)
{
	const ACharacter* Character = GetCharacter();
	AActor* HitActor = Hit.GetActor();
	if (bSwinging && !bArriving)
	{
		// The swing from the old line onto the new one bulges off the line TryFire swept clear, and on
		// a chain past a roof edge that bulge can clip the parapet. Only a hit on the line itself drops her.
		UE_LOG(LogHawkeye, Log, TEXT("%s: chain swing to %s clipped %s; straightening onto the line"),
			*GetNameSafe(Character), *GetNameSafe(ZipAnchor.Get()), *GetNameSafe(HitActor));
		return EHawkeyeZipHitResponse::Straighten;
	}
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!bArriving && Capsule && HitActor && !bStartSupportsIgnored && ZipStartSupports.Contains(HitActor))
	{
		// The roof she left counts again once she is GetStartReleaseDistance() from where the zip began,
		// but after a chain swing she can still be over its parapet there. TryFire cleared the line
		// with that roof ignored up to the same distance along the line itself, so keep ignoring it.
		UE_LOG(LogHawkeye, Log, TEXT("%s: zip to %s grazed %s, the roof it started from; ignoring it for the rest of the zip"),
			*GetNameSafe(Character), *GetNameSafe(ZipAnchor.Get()), *GetNameSafe(HitActor));
		Capsule->IgnoreActorWhenMoving(HitActor, true);
		ZipIgnoredSupports.AddUnique(HitActor);
		return EHawkeyeZipHitResponse::Retry;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: zip to %s blocked by %s at %s (%.0f%% along); dropping"),
		*GetNameSafe(Character), *GetNameSafe(ZipAnchor.Get()), *GetNameSafe(HitActor),
		*Hit.Location.ToCompactString(), GetZipProgress() * 100.f);
	return EHawkeyeZipHitResponse::Stop;
}

void UGrappleComponent::HandleZipStep()
{
	const ACharacter* Character = GetCharacter();
	if (bStartSupportsIgnored && Character && FVector::Dist(Character->GetActorLocation(), ZipStart) > GetStartReleaseDistance())
	{
		ReleaseStartSupports();
	}
}

void UGrappleComponent::HandleZipEnded(EHawkeyeZipEnd Reason)
{
	ACharacter* Character = GetCharacter();
	AGrappleAnchor* Anchor = ZipAnchor.Get();
	SetSupportsIgnored(false);
	ZipIgnoredSupports.Reset();
	ZipStartSupports.Reset();
	bStartSupportsIgnored = false;
	ZipAnchor.Reset();
	UHawkeyeAudioSubsystem::StopLoop(ZipLoop, TEXT("grapple zip"));
	UHawkeyeVfxSubsystem::Kill(ZipLine);
	const FString Where = Character ? Character->GetActorLocation().ToCompactString() : TEXT("?");
	const float Speed = Character ? Character->GetVelocity().Size() : 0.f;

	if (Reason == EHawkeyeZipEnd::Arrived)
	{
		if (Character)
		{
			UHawkeyeAudioSubsystem::PlayAt(this, LandSound, Character->GetActorLocation(), TEXT("grapple land"));
		}
		if (AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Character))
		{
			Hawkeye->NotifyGrappleLanded();
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: grapple landed at %s on %s, moving on at %.0f cm/s"), *GetNameSafe(Character), *Where,
			*GetNameSafe(Anchor), Speed);
		OnGrappleLanded.Broadcast(Anchor);
		return;
	}
	if (Reason == EHawkeyeZipEnd::Blocked)
	{
		ReelBackArrow(Anchor, TEXT("the zip was blocked"));
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: zip to %s %s at %s; falling at %.0f cm/s"), *GetNameSafe(Character), *GetNameSafe(Anchor),
		Reason == EHawkeyeZipEnd::Blocked ? TEXT("blocked") : Reason == EHawkeyeZipEnd::Released ? TEXT("let go")
		: TEXT("interrupted by a movement mode change"), *Where, Speed);
	OnGrappleCancelled.Broadcast(Anchor);
}

void UGrappleComponent::ReleaseZip(bool bJump)
{
	UHawkeyeMovementComponent* Movement = GetMovement();
	if (!Movement || !Movement->IsZipping())
	{
		return;
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: let go of the zip to %s at %.0f%% with %s"), *GetNameSafe(GetOwner()),
		*GetNameSafe(ZipAnchor.Get()), Movement->GetZipProgress() * 100.f, bJump ? TEXT("a jump") : TEXT("a crouch"));
	Movement->ReleaseZip(bJump);
}
