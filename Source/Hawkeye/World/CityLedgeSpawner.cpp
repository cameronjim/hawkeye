#include "World/CityLedgeSpawner.h"

#include "Hawkeye.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Player/GrappleComponent.h"
#include "Vfx/HawkeyeVfxSubsystem.h"
#include "World/CityLedgeData.h"
#include "World/FireEscapeLanding.h"
#include "World/GrappleAnchor.h"

const FName ACityLedgeSpawner::LedgeTag(TEXT("CityLedge"));
const FName ACityLedgeSpawner::AnchorTag(TEXT("CityAnchor"));
const FName ACityLedgeSpawner::FireEscapeTag(TEXT("CityFireEscape"));

namespace HawkeyeCitySpawn
{
	static const FName CityTag(TEXT("City"));
	static const TCHAR* LedgeLabelPrefix = TEXT("City_Ledge_");
	static const TCHAR* AnchorLabelPrefix = TEXT("City_Anchor_");
	static const TCHAR* FireEscapeLabelPrefix = TEXT("City_FireEscape_");
	static const TCHAR* FireEscapeLedgeLabelPrefix = TEXT("City_FireEscapeLedge_");
	static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* CylinderPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

	static FActorSpawnParameters MakeParams(AActor* Owner, bool bEditorWorld)
	{
		FActorSpawnParameters Params;
		Params.Owner = Owner;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bAllowDuringConstructionScript = true;
		Params.ObjectFlags |= RF_Transient;
#if WITH_EDITOR
		Params.bHideFromSceneOutliner = !bEditorWorld;
		Params.bCreateActorPackage = false;
#endif
		return Params;
	}
}

ACityLedgeSpawner::ACityLedgeSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
	FireEscapeClass = AFireEscapeLanding::StaticClass();
	ChimneyWispVfx = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/VFX/NS_ChimneyWisp.NS_ChimneyWisp")));
}

bool ACityLedgeSpawner::IsSpawnComplete() const
{
	return Data && SpawnedLedges.Num() >= Data->Ledges.Num() && SpawnedAnchors.Num() >= Data->Anchors.Num()
		&& SpawnedFireEscapes.Num() >= Data->FireEscapes.Num();
}

int32 ACityLedgeSpawner::GetFireEscapeInstanceCount() const
{
	return (FireEscapeCubes ? FireEscapeCubes->GetInstanceCount() : 0)
		+ (FireEscapeCylinders ? FireEscapeCylinders->GetInstanceCount() : 0);
}

int32 ACityLedgeSpawner::GetClutterInstanceCount() const
{
	int32 Count = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* Instances : ClutterComponents)
	{
		Count += Instances ? Instances->GetInstanceCount() : 0;
	}
	return Count;
}

void ACityLedgeSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (!Data || !GetWorld())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no ledge data; nothing spawned."), *GetName());
		return;
	}

	SpawnAnchors();

	const double Start = FPlatformTime::Seconds();
	SpawnFireEscapeVisuals();
	const double Visuals = FPlatformTime::Seconds();
	SpawnClutter();
	const double Clutter = FPlatformTime::Seconds();
	QueueLedges(FindFocus());
	SpawnQueuedLedges(ImmediateRadius, TNumericLimits<double>::Max());
	LoadLedgeSeconds = static_cast<float>(FPlatformTime::Seconds() - Start);
	TotalLedgeSeconds = LoadLedgeSeconds;
	// The smoke waits for the first frame: nothing on screen needs it before the player can move.
	bChimneyWispsPending = GetWorld()->IsGameWorld();

	UE_LOG(LogHawkeye, Log, TEXT("%s: at load %d anchors in %.0f ms, fire-escape bars in %.0f ms, %d clutter instances in %d groups in %.0f ms, %d of %d ledges and %d of %d fire-escape landings (%d parts) within %.0f m in %.0f ms (%.0f ms in all); the rest at %.1f ms a frame"),
		*GetName(), SpawnedAnchors.Num(), AnchorSeconds * 1000.f, (Visuals - Start) * 1000.0, GetClutterInstanceCount(),
		ClutterComponents.Num(), (Clutter - Visuals) * 1000.0, SpawnedLedges.Num(), Data->Ledges.Num(), SpawnedFireEscapes.Num(),
		Data->FireEscapes.Num(), GetFireEscapeInstanceCount(), ImmediateRadius / 100.f,
		(FPlatformTime::Seconds() - Clutter) * 1000.0, LoadLedgeSeconds * 1000.f, FrameBudgetMs);
	SetActorTickEnabled(!IsSpawnComplete() || bChimneyWispsPending);
}

void ACityLedgeSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bChimneyWispsPending)
	{
		// The game mode streams the effects in after the load; spawning before the system has landed
		// (and compiled, uncooked) would load it on this frame. Past the wait it loads anyway.
		ChimneyWaitSeconds += DeltaSeconds;
		const UNiagaraSystem* System = ChimneyWispVfx.Get();
		bool bReady = ChimneyWispVfx.IsNull() || System;
#if WITH_EDITORONLY_DATA
		bReady = bReady && !(System && System->HasOutstandingCompilationRequests());
#endif
		if (bReady || ChimneyWaitSeconds > ChimneyWaitLimitSeconds)
		{
			bChimneyWispsPending = false;
			const double WispStart = FPlatformTime::Seconds();
			SpawnChimneyWisps();
			UE_LOG(LogHawkeye, Log, TEXT("%s: chimney wisps %.1f s after the first frame in %.1f ms."), *GetName(),
				ChimneyWaitSeconds, (FPlatformTime::Seconds() - WispStart) * 1000.0);
		}
	}
	if (!Data || !bLedgesQueued || LedgeQueueNext >= LedgeQueue.Num())
	{
		SetActorTickEnabled(bChimneyWispsPending);
		return;
	}
	const double Start = FPlatformTime::Seconds();
	SpawnQueuedLedges(TNumericLimits<float>::Max(), Start + FrameBudgetMs / 1000.0);
	TotalLedgeSeconds += static_cast<float>(FPlatformTime::Seconds() - Start);
	++BackgroundFrames;
	if (LedgeQueueNext >= LedgeQueue.Num())
	{
		SetActorTickEnabled(bChimneyWispsPending);
		UE_LOG(LogHawkeye, Log, TEXT("%s: all %d ledges and %d fire-escape landings out; %.0f ms at load, %.0f ms in total over %d more frames"),
			*GetName(), SpawnedLedges.Num(), SpawnedFireEscapes.Num(), LoadLedgeSeconds * 1000.f, TotalLedgeSeconds * 1000.f,
			BackgroundFrames);
	}
}

void ACityLedgeSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// On a level change or quit the world takes everything down anyway.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		DestroySpawned();
	}
	Super::EndPlay(EndPlayReason);
}

void ACityLedgeSpawner::Destroyed()
{
	DestroySpawned();
	Super::Destroyed();
}

void ACityLedgeSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld() || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}
	if (bSpawnInEditor)
	{
		if (!IsSpawnComplete())
		{
			SpawnAll();
		}
	}
	else
	{
		DestroySpawned();
	}
#endif
}

FVector ACityLedgeSpawner::FindFocus() const
{
	UWorld* World = GetWorld();
	if (const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
	}
	if (World)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			return It->GetActorLocation();
		}
	}
	return GetActorLocation();
}

void ACityLedgeSpawner::MakeTraceOnly(AActor* Ledge)
{
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Ledge);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (UTextRenderComponent* Text = Cast<UTextRenderComponent>(Primitive))
		{
			Text->SetVisibility(false);
			Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			continue;
		}
		// The ledge lines the traversal reads: geometry only, never drawn in a game, but a visible spline is still a
		// scene primitive (thousands of them once the block has streamed in: every frame's visibility pass walks
		// them, and a level change spent 0.6 s of render thread removing them). Hidden, the queries work the same.
		// In the editor they stay drawn: that is where someone looks at them.
		if (USplineComponent* Spline = Cast<USplineComponent>(Primitive))
		{
			if (Ledge->GetWorld() && Ledge->GetWorld()->IsGameWorld())
			{
				Spline->SetVisibility(false);
			}
			continue;
		}
		if (!Primitive->IsA<UStaticMeshComponent>())
		{
			continue;
		}
		Primitive->SetVisibility(false);
		Primitive->SetCastShadow(false);
		Primitive->SetCollisionObjectType(ECC_WorldStatic);
		Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
		Primitive->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
		Primitive->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

AActor* ACityLedgeSpawner::SpawnLedgeActor(const FTransform& Transform, const TArray<FName>& LedgeTags, const FString& Label)
{
	UWorld* World = GetWorld();
	if (!World || !LedgeClass)
	{
		return nullptr;
	}
	const bool bEditorWorld = !World->IsGameWorld();
	FActorSpawnParameters Params = HawkeyeCitySpawn::MakeParams(this, bEditorWorld);
	Params.CustomPreSpawnInitialization = [LedgeTags](AActor* Actor)
	{
		Actor->Tags = LedgeTags;
	};
#if WITH_EDITOR
	if (bEditorWorld)
	{
		Params.InitialActorLabel = Label;
	}
#endif
	// Deferred, then finished with a non-default transform: a plain SpawnActor of a Blueprint whose
	// root comes from its construction script takes the template's scale, not ours.
	Params.bDeferConstruction = true;
	AActor* Ledge = World->SpawnActor(LedgeClass, &Transform, Params);
	if (Ledge)
	{
		Ledge->FinishSpawning(Transform, /*bIsDefaultTransform=*/false);
		MakeTraceOnly(Ledge);
	}
	return Ledge;
}

AActor* ACityLedgeSpawner::SpawnLedge(const FCityLedgeRecord& Record)
{
	const FName OsmTag(*(TEXT("osm:") + Record.OsmId));
	AActor* Ledge = SpawnLedgeActor(Record.Transform, { HawkeyeCitySpawn::CityTag, LedgeTag, OsmTag },
		FString::Printf(TEXT("%s%s_%d"), HawkeyeCitySpawn::LedgeLabelPrefix, *Record.OsmId, Record.EdgeIndex));
	if (Ledge)
	{
		SpawnedLedges.Add(Ledge);
	}
	return Ledge;
}

AFireEscapeLanding* ACityLedgeSpawner::SpawnFireEscape(const FCityFireEscapeRecord& Record)
{
	UWorld* World = GetWorld();
	if (!World || !FireEscapeClass)
	{
		return nullptr;
	}
	const bool bEditorWorld = !World->IsGameWorld();
	const FName OsmTag(*(TEXT("osm:") + Record.OsmId));
	const FName FloorTag(*FString::Printf(TEXT("floor:%d"), Record.Floor));
	const FString Suffix = FString::Printf(TEXT("%s_%d"), *Record.OsmId, Record.Floor);
	// Params only views the label, so it has to outlive the spawn.
	const FString Label = HawkeyeCitySpawn::FireEscapeLabelPrefix + Suffix;
	FActorSpawnParameters Params = HawkeyeCitySpawn::MakeParams(this, bEditorWorld);
	Params.CustomPreSpawnInitialization = [OsmTag, FloorTag, Record](AActor* Actor)
	{
		Actor->Tags = { HawkeyeCitySpawn::CityTag, FireEscapeTag, OsmTag, FloorTag };
		if (AFireEscapeLanding* Landing = Cast<AFireEscapeLanding>(Actor))
		{
			Landing->ApplyRecord(Record);
		}
	};
#if WITH_EDITOR
	if (bEditorWorld)
	{
		Params.InitialActorLabel = Label;
	}
#endif
	const FTransform Placement(Record.Transform.GetRotation(), Record.Transform.GetLocation());
	AFireEscapeLanding* Landing = World->SpawnActor<AFireEscapeLanding>(FireEscapeClass, Placement, Params);
	if (!Landing)
	{
		return nullptr;
	}
	SpawnedFireEscapes.Add(Landing);
	SpawnedFireEscapeLedges.Add(SpawnLedgeActor(AFireEscapeLanding::ComputeLedgeTransform(Record),
		{ HawkeyeCitySpawn::CityTag, LedgeTag, FireEscapeTag, OsmTag, FloorTag }, HawkeyeCitySpawn::FireEscapeLedgeLabelPrefix + Suffix));
	return Landing;
}

void ACityLedgeSpawner::SpawnFireEscapeVisuals()
{
	if (!Data || Data->FireEscapes.Num() == 0 || FireEscapeCubes)
	{
		return;
	}
	UStaticMesh* Cube = FireEscapeCube ? FireEscapeCube.Get() : LoadObject<UStaticMesh>(nullptr, HawkeyeCitySpawn::CubePath);
	UStaticMesh* Cylinder = FireEscapeCylinder ? FireEscapeCylinder.Get() : LoadObject<UStaticMesh>(nullptr, HawkeyeCitySpawn::CylinderPath);
	if (!Cube || !Cylinder)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no cube or cylinder mesh; fire escapes have collision but no bars."), *GetName());
		return;
	}
	auto Make = [this](const TCHAR* Name, UStaticMesh* Mesh, bool bShadows)
	{
		UHierarchicalInstancedStaticMeshComponent* Instances = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			this, Name, RF_Transient);
		Instances->SetMobility(EComponentMobility::Static);
		Instances->SetStaticMesh(Mesh);
		if (FireEscapeMaterial)
		{
			Instances->SetMaterial(0, FireEscapeMaterial);
		}
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->SetCanEverAffectNavigation(false);
		Instances->SetCastShadow(bShadows);
		Instances->SetupAttachment(RootComponent);
		Instances->RegisterComponent();
		return Instances;
	};
	FireEscapeCubes = Make(TEXT("FireEscapeCubes"), Cube, true);
	FireEscapeCylinders = Make(TEXT("FireEscapeCylinders"), Cylinder, false);

	TArray<FTransform> Cubes;
	TArray<FTransform> Cylinders;
	for (const FCityFireEscapeRecord& Record : Data->FireEscapes)
	{
		AFireEscapeLanding::BuildParts(Record, Cubes, Cylinders);
	}
	FireEscapeCubes->AddInstances(Cubes, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
	FireEscapeCylinders->AddInstances(Cylinders, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
}

const FName ACityLedgeSpawner::ChimneyKind(TEXT("Chimney"));

TArray<FVector> ACityLedgeSpawner::PickChimneyTops(const TArray<FTransform>& Chimneys, const FVector& Focus, int32 Count,
	float TopCm)
{
	TArray<FTransform> Sorted = Chimneys;
	Sorted.Sort([&Focus](const FTransform& A, const FTransform& B)
	{
		return FVector::DistSquared2D(A.GetLocation(), Focus) < FVector::DistSquared2D(B.GetLocation(), Focus);
	});
	TArray<FVector> Tops;
	for (int32 Index = 0; Index < Sorted.Num() && Tops.Num() < Count; ++Index)
	{
		Tops.Add(Sorted[Index].TransformPosition(FVector(0.f, 0.f, TopCm)));
	}
	return Tops;
}

void ACityLedgeSpawner::SpawnChimneyWisps()
{
	UWorld* World = GetWorld();
	if (!Data || !World || !World->IsGameWorld() || ChimneyWisps.Num() > 0 || ChimneyWispVfx.IsNull()
		|| ChimneyWispCount <= 0)
	{
		return;
	}
	TArray<FTransform> Chimneys;
	for (const FCityClutterGroup& Group : Data->Clutter)
	{
		if (Group.Kind == ChimneyKind)
		{
			Chimneys.Append(Group.Instances);
		}
	}
	for (const FVector& Top : PickChimneyTops(Chimneys, FindFocus(), ChimneyWispCount, ChimneyTopCm))
	{
		if (UNiagaraComponent* Wisp = UHawkeyeVfxSubsystem::SpawnKept(this, ChimneyWispVfx, Top,
				UHawkeyeVfxSubsystem::ChimneyEvent))
		{
			ChimneyWisps.Add(Wisp);
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %d chimney wisp(s) of %d chimneys."), *GetName(), ChimneyWisps.Num(), Chimneys.Num());
}

void ACityLedgeSpawner::SpawnClutter()
{
	if (!Data || Data->Clutter.Num() == 0 || ClutterComponents.Num() > 0)
	{
		return;
	}
	for (const FCityClutterGroup& Group : Data->Clutter)
	{
		if (!Group.Mesh || Group.Instances.Num() == 0)
		{
			continue;
		}
		const FName Name = MakeUniqueObjectName(this, UHierarchicalInstancedStaticMeshComponent::StaticClass(),
			FName(*FString::Printf(TEXT("Clutter_%s"), *Group.Kind.ToString())));
		UHierarchicalInstancedStaticMeshComponent* Instances = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			this, Name, RF_Transient);
		Instances->SetMobility(EComponentMobility::Static);
		Instances->SetStaticMesh(Group.Mesh);
		if (Group.Material)
		{
			Instances->SetMaterial(0, Group.Material);
		}
		if (Group.bCollision)
		{
			Instances->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Instances->SetCollisionObjectType(ECC_WorldStatic);
			Instances->SetCollisionResponseToAllChannels(ECR_Block);
			// Traces that aim, frame or find ledges look straight through it.
			Instances->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			Instances->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			Instances->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
		}
		else
		{
			Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Instances->SetCanEverAffectNavigation(false);
		Instances->SetCastShadow(Group.bCastShadow);
		Instances->SetupAttachment(RootComponent);
		Instances->RegisterComponent();
		Instances->AddInstances(Group.Instances, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
		ClutterComponents.Add(Instances);
	}
}

void ACityLedgeSpawner::SpawnQueued(int32 Entry)
{
	if (!Data)
	{
		return;
	}
	if (Entry < Data->Ledges.Num())
	{
		SpawnLedge(Data->Ledges[Entry]);
	}
	else if (Data->FireEscapes.IsValidIndex(Entry - Data->Ledges.Num()))
	{
		SpawnFireEscape(Data->FireEscapes[Entry - Data->Ledges.Num()]);
	}
}

void ACityLedgeSpawner::QueueLedges(const FVector& Focus)
{
	LedgeQueue.Reset();
	LedgeQueueDistance.Reset();
	LedgeQueueNext = 0;
	bLedgesQueued = true;
	if (!Data)
	{
		return;
	}
	TArray<TPair<float, int32>> Order;
	Order.Reserve(Data->Ledges.Num() + Data->FireEscapes.Num());
	for (int32 Index = 0; Index < Data->Ledges.Num(); ++Index)
	{
		Order.Emplace(FVector::Dist2D(Data->Ledges[Index].Transform.GetLocation(), Focus), Index);
	}
	for (int32 Index = 0; Index < Data->FireEscapes.Num(); ++Index)
	{
		Order.Emplace(FVector::Dist2D(Data->FireEscapes[Index].Transform.GetLocation(), Focus), Data->Ledges.Num() + Index);
	}
	Order.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	for (const TPair<float, int32>& Entry : Order)
	{
		LedgeQueueDistance.Add(Entry.Key);
		LedgeQueue.Add(Entry.Value);
	}
}

void ACityLedgeSpawner::SpawnQueuedLedges(float MaxDistance, double Deadline)
{
	if (!Data)
	{
		return;
	}
	while (LedgeQueueNext < LedgeQueue.Num())
	{
		if (LedgeQueueDistance[LedgeQueueNext] > MaxDistance || FPlatformTime::Seconds() >= Deadline)
		{
			return;
		}
		SpawnQueued(LedgeQueue[LedgeQueueNext]);
		++LedgeQueueNext;
	}
}

void ACityLedgeSpawner::SpawnAnchors()
{
	UWorld* World = GetWorld();
	if (!World || !Data || !AnchorClass || SpawnedAnchors.Num() > 0)
	{
		return;
	}
	const bool bEditorWorld = !World->IsGameWorld();
	const double Start = FPlatformTime::Seconds();
	SpawnedAnchors.Reserve(Data->Anchors.Num());
	for (const FCityAnchorRecord& Record : Data->Anchors)
	{
		FActorSpawnParameters Params = HawkeyeCitySpawn::MakeParams(this, bEditorWorld);
		const FName OsmTag(*(TEXT("osm:") + Record.OsmId));
		const FVector LandingOffset = Record.LandingOffset;
		// Before registration: the anchor's components are Static, and a registered Static
		// component refuses to move in a game world.
		Params.CustomPreSpawnInitialization = [OsmTag, LandingOffset](AActor* Actor)
		{
			Actor->Tags = { HawkeyeCitySpawn::CityTag, AnchorTag, OsmTag };
			if (const AGrappleAnchor* Anchor = Cast<AGrappleAnchor>(Actor))
			{
				if (USceneComponent* Landing = Anchor->GetLandingPoint())
				{
					Landing->SetRelativeLocation(LandingOffset);
				}
			}
		};
#if WITH_EDITOR
		const FString Label = bEditorWorld ? FString::Printf(TEXT("%s%d"), HawkeyeCitySpawn::AnchorLabelPrefix, Record.Index) : FString();
		if (bEditorWorld)
		{
			Params.InitialActorLabel = Label;
		}
#endif
		if (AGrappleAnchor* Anchor = World->SpawnActor<AGrappleAnchor>(AnchorClass, Record.Transform, Params))
		{
			SpawnedAnchors.Add(Anchor);
		}
	}
	AnchorSeconds = static_cast<float>(FPlatformTime::Seconds() - Start);

	// Grapple components bucket anchors lazily; one that already looked needs to look again.
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		if (UGrappleComponent* Grapple = It->FindComponentByClass<UGrappleComponent>())
		{
			Grapple->RebuildAnchorGrid();
		}
	}
}

int32 ACityLedgeSpawner::SpawnAll()
{
	if (!Data || !GetWorld())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no world or no ledge data; nothing spawned."), *GetName());
		return 0;
	}
	const int32 Before = SpawnedLedges.Num() + SpawnedAnchors.Num() + SpawnedFireEscapes.Num();
	SpawnAnchors();

	const double Start = FPlatformTime::Seconds();
	SpawnFireEscapeVisuals();
	SpawnClutter();
	if (!bLedgesQueued)
	{
		QueueLedges(FindFocus());
	}
	SpawnQueuedLedges(TNumericLimits<float>::Max(), TNumericLimits<double>::Max());
	LoadLedgeSeconds = static_cast<float>(FPlatformTime::Seconds() - Start);
	TotalLedgeSeconds = LoadLedgeSeconds;
	SetActorTickEnabled(false);

	UE_LOG(LogHawkeye, Log, TEXT("%s: SpawnAll: %d ledges and %d fire-escape landings in %.0f ms and %d anchors in %.0f ms from %s"),
		*GetName(), SpawnedLedges.Num(), SpawnedFireEscapes.Num(), LoadLedgeSeconds * 1000.f, SpawnedAnchors.Num(),
		AnchorSeconds * 1000.f, *GetNameSafe(Data));
	return SpawnedLedges.Num() + SpawnedAnchors.Num() + SpawnedFireEscapes.Num() - Before;
}

void ACityLedgeSpawner::DestroySpawned()
{
	for (AActor* Ledge : SpawnedLedges)
	{
		if (IsValid(Ledge))
		{
			Ledge->Destroy();
		}
	}
	for (AGrappleAnchor* Anchor : SpawnedAnchors)
	{
		if (IsValid(Anchor))
		{
			Anchor->Destroy();
		}
	}
	for (AFireEscapeLanding* Landing : SpawnedFireEscapes)
	{
		if (IsValid(Landing))
		{
			Landing->Destroy();
		}
	}
	for (AActor* Ledge : SpawnedFireEscapeLedges)
	{
		if (IsValid(Ledge))
		{
			Ledge->Destroy();
		}
	}
	for (UHierarchicalInstancedStaticMeshComponent* Instances : { FireEscapeCubes.Get(), FireEscapeCylinders.Get() })
	{
		if (IsValid(Instances))
		{
			Instances->DestroyComponent();
		}
	}
	for (UHierarchicalInstancedStaticMeshComponent* Instances : ClutterComponents)
	{
		if (IsValid(Instances))
		{
			Instances->DestroyComponent();
		}
	}
	ClutterComponents.Reset();
	for (TObjectPtr<UNiagaraComponent>& Wisp : ChimneyWisps)
	{
		UHawkeyeVfxSubsystem::Kill(Wisp);
	}
	ChimneyWisps.Reset();
	FireEscapeCubes = nullptr;
	FireEscapeCylinders = nullptr;
	SpawnedFireEscapes.Reset();
	SpawnedFireEscapeLedges.Reset();
	SpawnedLedges.Reset();
	SpawnedAnchors.Reset();
	LedgeQueue.Reset();
	LedgeQueueDistance.Reset();
	LedgeQueueNext = 0;
	bLedgesQueued = false;
}
