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
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "Traversal/GrappleReachQuery.h"
#include "Traversal/GrappleTraces.h"
#include "Vfx/GrappleRopeComponent.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/GrappleAnchor.h"
#include "World/GrappleArrowProjectile.h"

static TAutoConsoleVariable<int32> CVarHawkeyeDebugGrapple(
	TEXT("hawkeye.DebugGrapple"),
	0,
	TEXT("1 writes on the HUD why a grey grapple diamond cannot be zipped to, and the marked anchor's state."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarHawkeyeLegacyBeam(
	TEXT("hawkeye.Grapple.LegacyBeam"),
	0,
	TEXT("1 draws the old Niagara beam (NS_ZipLine) along the rope while she zips; the rope draws without it."),
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
	CancelShot();
	for (FRopeSlot& Slot : Ropes)
	{
		if (UGrappleRopeComponent* View = Slot.View.Get())
		{
			View->DestroyComponent();
		}
	}
	Ropes.Reset();
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
	// After the body has animated (the bow does the same), so the rope and the arrow's release sit on this frame's hand.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	ArrowClass = AGrappleArrowProjectile::StaticClass();
}

void UGrappleComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AdvanceShot(DeltaTime);
	if (IsZipping())
	{
		UpdateZipLine();
		if (ZipLoop)
		{
			ZipLoop->SetFloatParameter(SpeedParameter, ComputeZipSoundSpeed());
		}
	}
	UpdateRopes(DeltaTime);
	UpdateBowHold(DeltaTime);
	UpdateLingeringArrow(DeltaTime);

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

void UGrappleComponent::SetTargetForTest(AGrappleAnchor* Anchor)
{
	TargetAnchor = Anchor;
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

	if (CanPlayShot())
	{
		// Raise, draw, release: the arrow leaves at the end of the draw and flies the rest of the same press-to-pull time.
		BeginShot(Anchor, Definition, IsZipping());
		return true;
	}

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
		// The bow is its source, so it whistles and trails like any arrow; it has no bow definition and no damage.
		Arrow->InitArrow(Definition, nullptr, 0.f, Owner, Owner->FindComponentByClass<UBowComponent>());
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
	// The hand people see: the look's, when she has one. The driver's hand is the one the traversal pose puts on the
	// rope and the look's is retargeted onto it, but a longer arm can fall a few centimetres short, and the line must
	// leave what is drawn.
	if (const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Character))
	{
		const USkeletalMeshComponent* Body = Hawkeye->GetBodyMesh();
		if (Body && Body != Mesh && !ZipLineSocket.IsNone() && Body->DoesSocketExist(ZipLineSocket))
		{
			return Body->GetSocketLocation(ZipLineSocket);
		}
	}
	if (Mesh && !ZipLineSocket.IsNone() && Mesh->DoesSocketExist(ZipLineSocket))
	{
		return Mesh->GetSocketLocation(ZipLineSocket);
	}
	return Character->GetActorLocation() + FVector(0.f, 0.f, 40.f);
}

bool UGrappleComponent::IsLegacyBeamForced()
{
	return CVarHawkeyeLegacyBeam.GetValueOnGameThread() != 0;
}

void UGrappleComponent::UpdateZipLine()
{
	const AGrappleAnchor* Anchor = ZipAnchor.Get();
	if (!Anchor)
	{
		return;
	}
	if (Shot.bEnabled && !IsLegacyBeamForced())
	{
		// The rope draws the line; the emissive beam it replaced was the laser the owner saw.
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
		UHawkeyeAudioSubsystem::PlayAt(this, HitSound, Anchor->GetMarkerLocation(), TEXT("grapple bite"));
	}
	if (InFlightArrow.Get() == Arrow)
	{
		InFlightArrow.Reset();
	}
	// The rope snaps straight the moment the arrow bites, before the pull (and a chain's old rope) lets go.
	TightenRope(Arrow, Anchor);
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
		// The old line comes away from her hand and whips back to its anchor; the new rope stays.
		ReleaseRopes(Anchor);
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
	// Landed or let go: the rope goes slack and is drawn back; the bow comes down a moment after.
	ReleaseRopes(nullptr);
	if (bBowHeld)
	{
		BowHoldRemaining = Shot.BowHoldAfterZipSeconds;
	}
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
	else if (Shot.bEnabled && Anchor)
	{
		// Let go (or interrupted): the arrow stays in the anchor a moment, then is taken out.
		LingerAnchor = Anchor;
		LingerRemaining = Shot.ArrowLingerSeconds;
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

// --- The shot -------------------------------------------------------------------------------------

bool UGrappleComponent::CanPlayShot() const
{
	const AActor* Owner = GetOwner();
	const UBowComponent* Bow = Owner ? Owner->FindComponentByClass<UBowComponent>() : nullptr;
	return Shot.bEnabled && Bow && Bow->GetBow() && !bShotPending;
}

float UGrappleComponent::GetArrowSpeed(const UArrowDefinition* Definition, bool bChain) const
{
	if (bChain)
	{
		return ChainArrowSpeed;
	}
	// The projectile class's own speed (BP_Arrow_Grapple), else the component's arrow class.
	const UClass* Class = (Definition && Definition->ProjectileClass) ? Definition->ProjectileClass.Get() : ArrowClass.Get();
	const AGrappleArrowProjectile* Default = Class ? Cast<AGrappleArrowProjectile>(Class->GetDefaultObject()) : nullptr;
	return Default ? Default->Speed : GetDefault<AGrappleArrowProjectile>()->Speed;
}

void UGrappleComponent::BeginShot(AGrappleAnchor* Anchor, UArrowDefinition* Definition, bool bChain)
{
	AActor* Owner = GetOwner();
	UBowComponent* Bow = Owner ? Owner->FindComponentByClass<UBowComponent>() : nullptr;
	if (!Anchor || !Bow)
	{
		return;
	}
	bShotPending = true;
	bShotChain = bChain;
	ShotElapsed = 0.f;
	ShotAnchor = Anchor;
	ShotDefinition = Definition;

	const FVector Marker = Anchor->GetMarkerLocation();
	const float Distance = static_cast<float>(FVector::Dist(Bow->GetArrowSpawnLocation(), Marker));
	ShotSchedule = FGrappleShotRules::MakeSchedule(Distance, GetArrowSpeed(Definition, bChain), bChain, Shot);

	// The bow comes up on the line now and stays there (legs keep going: the clip is upper body, the IK and the aim offset
	// turn the chest).
	Bow->BeginScriptedShot(Marker, ShotSchedule.RaiseSeconds + ShotSchedule.DrawSeconds);
	bBowHeld = true;
	BowHoldRemaining = Shot.BowHoldAfterZipSeconds;

	UE_LOG(LogHawkeye, Log,
		TEXT("%s: grapple shot at %s, %.0f cm: release at %.2f s (raise %.2f, draw %.2f, squeezed to %.0f%%), arrow %.0f cm/s for %.2f s, pull at %.2f s"),
		*GetNameSafe(Owner), *GetNameSafe(Anchor), Distance, ShotSchedule.ReleaseSeconds, ShotSchedule.RaiseSeconds,
		ShotSchedule.DrawSeconds, ShotSchedule.Compression * 100.f, ShotSchedule.ArrowSpeed, ShotSchedule.FlightSeconds,
		ShotSchedule.TotalSeconds);

	if (ShotSchedule.HasReleased(0.f))
	{
		// No draw to play (a chain that still arrives at once): the string lets go in the same frame.
		ReleaseShot();
	}
}

void UGrappleComponent::AdvanceShot(float DeltaSeconds)
{
	if (!bShotPending)
	{
		return;
	}
	if (!ShotAnchor.IsValid())
	{
		CancelShot();
		return;
	}
	ShotElapsed += FMath::Max(DeltaSeconds, 0.f);
	if (UBowComponent* Bow = GetOwner() ? GetOwner()->FindComponentByClass<UBowComponent>() : nullptr)
	{
		Bow->SetScriptedDrawFraction(FGrappleShotRules::DrawFractionAt(ShotElapsed, ShotSchedule));
	}
	if (ShotSchedule.HasReleased(ShotElapsed))
	{
		ReleaseShot();
	}
}

void UGrappleComponent::ReleaseShot()
{
	AGrappleAnchor* Anchor = ShotAnchor.Get();
	UArrowDefinition* Definition = ShotDefinition.Get();
	const bool bChain = bShotChain;
	AActor* Owner = GetOwner();
	UBowComponent* Bow = Owner ? Owner->FindComponentByClass<UBowComponent>() : nullptr;
	bShotPending = false;
	ShotDefinition = nullptr;
	if (!Anchor || !Bow)
	{
		CancelShot();
		return;
	}
	Bow->ReleaseScriptedShot();
	UHawkeyeAudioSubsystem::PlayAt(this, FireSound, Bow->GetArrowSpawnLocation(), TEXT("grapple fire"));

	AGrappleArrowProjectile* Arrow = SpawnGrappleArrow(Definition, Anchor);
	if (!Arrow)
	{
		// No world to fly through: the arrow is there at once.
		Anchor->AddStuckArrow(nullptr, Owner);
		HandleArrowArrived(nullptr, Anchor);
		return;
	}
	InFlightArrow = Arrow;
	StartRope(Arrow, Anchor);

	// The rest of the press-to-pull time, from where the hand is now (it has come up since the press).
	const float Distance = static_cast<float>(FVector::Dist(Arrow->GetActorLocation(), Anchor->GetMarkerLocation()));
	const float Speed = ShotSchedule.FlightSeconds > KINDA_SMALL_NUMBER ? Distance / ShotSchedule.FlightSeconds
		: (bChain ? ChainArrowSpeed : 0.f);
	Arrow->Launch(Anchor, this, Speed);
}

void UGrappleComponent::ClearShot()
{
	CancelShot();
	for (FRopeSlot& Slot : Ropes)
	{
		Slot.State.Reset();
		Slot.Arrow.Reset();
		Slot.Anchor.Reset();
		if (UGrappleRopeComponent* View = Slot.View.Get())
		{
			View->Hide();
		}
	}
	LingerAnchor.Reset();
	LingerRemaining = 0.f;
	BowHoldRemaining = 0.f;
}

void UGrappleComponent::CancelShot()
{
	const bool bWas = bShotPending;
	bShotPending = false;
	ShotDefinition = nullptr;
	ShotAnchor.Reset();
	if (bWas || bBowHeld)
	{
		if (UBowComponent* Bow = GetOwner() ? GetOwner()->FindComponentByClass<UBowComponent>() : nullptr)
		{
			Bow->EndScriptedAim();
		}
		bBowHeld = false;
	}
}

EGrappleShotPhase UGrappleComponent::GetShotPhase() const
{
	if (bShotPending)
	{
		return FGrappleShotRules::PhaseAt(ShotElapsed, ShotSchedule, false);
	}
	if (InFlightArrow.IsValid())
	{
		return EGrappleShotPhase::Flight;
	}
	if (IsZipping() || (Ropes.Num() > 0 && GetNewestRopeState() == EGrappleRopeState::Taut))
	{
		return EGrappleShotPhase::Stuck;
	}
	return EGrappleShotPhase::None;
}

void UGrappleComponent::UpdateBowHold(float DeltaSeconds)
{
	if (!bBowHeld)
	{
		return;
	}
	UBowComponent* Bow = GetOwner() ? GetOwner()->FindComponentByClass<UBowComponent>() : nullptr;
	if (!Bow)
	{
		bBowHeld = false;
		return;
	}
	// Up and on the line while the shot, the arrow or the pull needs it: the bow hand keeps the line through the jerk.
	const bool bNeeded = bShotPending || InFlightArrow.IsValid() || IsZipping();
	if (bNeeded)
	{
		BowHoldRemaining = Shot.BowHoldAfterZipSeconds;
		Bow->ExtendFollowThrough(FMath::Max(0.12f, DeltaSeconds * 2.f));
		return;
	}
	BowHoldRemaining -= DeltaSeconds;
	if (BowHoldRemaining <= 0.f)
	{
		bBowHeld = false;
		Bow->EndScriptedAim();
	}
}

void UGrappleComponent::UpdateLingeringArrow(float DeltaSeconds)
{
	if (LingerRemaining <= 0.f)
	{
		return;
	}
	LingerRemaining -= DeltaSeconds;
	if (LingerRemaining > 0.f)
	{
		return;
	}
	LingerRemaining = 0.f;
	if (AGrappleAnchor* Anchor = LingerAnchor.Get())
	{
		// Only if she has not shot at it again since.
		if (!IsZipping() || ZipAnchor.Get() != Anchor)
		{
			ReelBackArrow(Anchor, TEXT("the zip ended"));
		}
	}
	LingerAnchor.Reset();
}

// --- The rope -------------------------------------------------------------------------------------

UGrappleComponent::FRopeSlot& UGrappleComponent::AcquireRope()
{
	for (FRopeSlot& Slot : Ropes)
	{
		if (!Slot.State.IsVisible())
		{
			Slot.State.Reset();
			Slot.Arrow.Reset();
			Slot.Anchor.Reset();
			return Slot;
		}
	}
	if (Ropes.Num() < MaxRopes)
	{
		return Ropes.AddDefaulted_GetRef();
	}
	// All busy: the oldest (the first) is taken over.
	Ropes[0].State.Reset();
	Ropes[0].Arrow.Reset();
	Ropes[0].Anchor.Reset();
	return Ropes[0];
}

void UGrappleComponent::StartRope(AGrappleArrowProjectile* Arrow, AGrappleAnchor* Anchor)
{
	AActor* Owner = GetOwner();
	if (!Shot.bEnabled || !Owner || !Arrow)
	{
		return;
	}
	FRopeSlot& Slot = AcquireRope();
	if (!Slot.View.IsValid())
	{
		UGrappleRopeComponent* View = NewObject<UGrappleRopeComponent>(Owner, NAME_None, RF_Transient);
		View->SetupAttachment(Owner->GetRootComponent());
		View->RegisterComponent();
		Slot.View = View;
	}
	Slot.Arrow = Arrow;
	Slot.Anchor = Anchor;
	Slot.LastEnd = Arrow->GetTailLocation();
	Slot.State.Begin();
	// The newest rope goes last, so GetNewestRopeState and the oldest-first takeover agree.
	const int32 Index = static_cast<int32>(&Slot - Ropes.GetData());
	if (Index != Ropes.Num() - 1)
	{
		Ropes.Swap(Index, Ropes.Num() - 1);
	}
}

void UGrappleComponent::TightenRope(AGrappleArrowProjectile* Arrow, AGrappleAnchor* Anchor)
{
	for (int32 Index = Ropes.Num() - 1; Index >= 0; --Index)
	{
		FRopeSlot& Slot = Ropes[Index];
		if (Slot.State.GetState() == EGrappleRopeState::Flying && (!Arrow || Slot.Arrow.Get() == Arrow))
		{
			Slot.State.Arrive();
			if (Arrow)
			{
				Slot.LastEnd = Arrow->GetTailLocation();
			}
			return;
		}
	}
	// An arrow that arrived in the same call it was launched in (a chain at speed 0, no world) before a rope was tied: make one, taut.
	if (Shot.bEnabled && Arrow && GetOwner())
	{
		StartRope(Arrow, Anchor);
		if (Ropes.Num() > 0)
		{
			Ropes.Last().State.Arrive();
		}
	}
}

void UGrappleComponent::ReleaseRopes(const AGrappleAnchor* KeepAnchor)
{
	const FVector Hand = GetZipLineStart();
	for (FRopeSlot& Slot : Ropes)
	{
		if (Slot.State.GetState() != EGrappleRopeState::Taut)
		{
			continue;
		}
		if (KeepAnchor && Slot.Anchor.Get() == KeepAnchor)
		{
			continue;
		}
		Slot.State.Release(Hand);
	}
}

void UGrappleComponent::UpdateRopes(float DeltaSeconds)
{
	if (Ropes.Num() == 0)
	{
		return;
	}
	const FVector Hand = GetZipLineStart();
	TArray<FVector> Points;
	for (FRopeSlot& Slot : Ropes)
	{
		UGrappleRopeComponent* View = Slot.View.Get();
		if (!View)
		{
			Slot.State.Reset();
			continue;
		}
		if (!Slot.State.IsVisible())
		{
			View->Hide();
			continue;
		}
		Slot.State.Step(DeltaSeconds, Rope);
		if (!Slot.State.IsVisible())
		{
			View->Hide();
			continue;
		}
		// Tied to the arrow's tail while it flies and where it sticks; where it was when the arrow is taken out.
		if (const AGrappleArrowProjectile* Arrow = Slot.Arrow.Get())
		{
			Slot.LastEnd = Arrow->GetTailLocation();
		}
		Slot.State.BuildPoints(Hand, Slot.LastEnd, Rope, Points);
		View->SetPoints(Points, Rope.ThicknessCm);
	}
}

int32 UGrappleComponent::GetVisibleRopeCount() const
{
	int32 Count = 0;
	for (const FRopeSlot& Slot : Ropes)
	{
		Count += Slot.State.IsVisible() ? 1 : 0;
	}
	return Count;
}

EGrappleRopeState UGrappleComponent::GetNewestRopeState() const
{
	return Ropes.Num() > 0 ? Ropes.Last().State.GetState() : EGrappleRopeState::Hidden;
}
