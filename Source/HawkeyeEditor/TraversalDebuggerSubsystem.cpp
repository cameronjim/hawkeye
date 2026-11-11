#include "TraversalDebuggerSubsystem.h"

#include "CollisionQueryParams.h"
#include "Components/LineBatchComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HawkeyeEditorModule.h"
#include "LevelEditorViewport.h"
#include "Selection.h"
#include "TraversalAuditRunner.h"
#include "TraversalDebuggerViewModel.h"
#include "World/FireEscapeLanding.h"

namespace HawkeyeTraversalDebugger
{
	/** Every line this tool draws carries this id, so ClearBatch removes ours and nobody else's. */
	static constexpr uint32 BatchId = 0x48415754;   // "HAWT"

	/** How far below a viewport camera or a selected actor the floor is looked for, cm. */
	static constexpr float FloorSearchDepth = 20000.f;

	static const FLinearColor AnchorColor(0.1f, 0.6f, 1.f);
	static const FLinearColor DisabledColor(0.4f, 0.4f, 0.4f);
	static const FLinearColor LedgeColor(1.f, 0.8f, 0.1f);
	static const FLinearColor FireEscapeColor(0.9f, 0.4f, 0.1f);
	static const FLinearColor UnreachedColor(1.f, 0.05f, 0.05f);
	static const FLinearColor NoStreetColor(0.7f, 0.2f, 1.f);
	static const FLinearColor StandColor(1.f, 1.f, 1.f);

	static FLinearColor ColorOf(EGrappleReachVerdict Verdict)
	{
		switch (Verdict)
		{
		case EGrappleReachVerdict::Reachable:
			return FLinearColor(0.1f, 1.f, 0.2f);
		case EGrappleReachVerdict::TooSteep:
			return FLinearColor(1.f, 1.f, 0.1f);
		case EGrappleReachVerdict::SightBlocked:
			return FLinearColor(1.f, 0.5f, 0.f);
		case EGrappleReachVerdict::ZipBlocked:
			return FLinearColor(1.f, 0.05f, 0.05f);
		default:
			return DisabledColor;
		}
	}

	static uint8 BitOf(ETraversalDebugLayer Layer)
	{
		return static_cast<uint8>(1u << static_cast<uint32>(Layer));
	}
}

void UTraversalDebuggerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	MapChangeHandle = FEditorDelegates::MapChange.AddUObject(this, &UTraversalDebuggerSubsystem::HandleMapChanged);
}

void UTraversalDebuggerSubsystem::Deinitialize()
{
	FEditorDelegates::MapChange.Remove(MapChangeHandle);
	ClearDrawing();
	Super::Deinitialize();
}

UWorld* UTraversalDebuggerSubsystem::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

ULineBatchComponent* UTraversalDebuggerSubsystem::GetLineBatcher() const
{
	const UWorld* World = GetEditorWorld();
	return World ? World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent) : nullptr;
}

void UTraversalDebuggerSubsystem::HandleMapChanged(uint32 MapChangeFlags)
{
	// The old world's lines went with it; the scene's indices mean nothing in the new one.
	Reset();
	SetStatus(TEXT("Map changed: refresh the scene."));
}

void UTraversalDebuggerSubsystem::Reset()
{
	Scene = FGrappleReachScene();
	SceneWorld.Reset();
	bHasScene = false;
	ReachResults.Reset();
	bHasReach = false;
	AuditReport.Reset();
}

void UTraversalDebuggerSubsystem::SetStatus(const FString& InStatus)
{
	Status = InStatus;
	UE_LOG(LogHawkeyeEditor, Display, TEXT("Traversal debugger: %s"), *Status);
	OnChanged.Broadcast();
}

bool UTraversalDebuggerSubsystem::RefreshScene()
{
	UWorld* World = GetEditorWorld();
	Reset();
	FString Error;
	if (!World || !FTraversalAuditRunner::BuildScene(World, Scene, Error))
	{
		SetStatus(FString::Printf(TEXT("No scene: %s."), World ? *Error : TEXT("no editor world")));
		Redraw();
		return false;
	}
	Profile = FTraversalAuditRunner::MakeProfile(World, FString());
	SceneWorld = World;
	bHasScene = true;
	Redraw();
	SetStatus(FString::Printf(TEXT("%d roofs, %d anchors, %d ledges, %d fire-escape landings; rules from %s (range %.0f, cone %.0f)."),
		Scene.Roofs.Num(), Scene.Anchors.Num(), Scene.Ledges.Num(), Scene.FireEscapes.Num(), *Profile.Source, Profile.Rules.Range,
		Profile.Rules.ConeDegrees));
	return true;
}

bool UTraversalDebuggerSubsystem::EnsureScene()
{
	return (bHasScene && SceneWorld.Get() == GetEditorWorld()) || RefreshScene();
}

bool UTraversalDebuggerSubsystem::IsLayerVisible(ETraversalDebugLayer Layer) const
{
	return (LayerMask & HawkeyeTraversalDebugger::BitOf(Layer)) != 0;
}

void UTraversalDebuggerSubsystem::SetLayerVisible(ETraversalDebugLayer Layer, bool bVisible)
{
	const uint8 Bit = HawkeyeTraversalDebugger::BitOf(Layer);
	LayerMask = bVisible ? (LayerMask | Bit) : (LayerMask & ~Bit);
	if (bVisible && !bHasScene)
	{
		RefreshScene();
		return;
	}
	Redraw();
	OnChanged.Broadcast();
}

// --- Reach from here -----------------------------------------------------------------------------

bool UTraversalDebuggerSubsystem::ReachFromViewport()
{
	if (!GCurrentLevelEditingViewportClient)
	{
		SetStatus(TEXT("No level viewport to look from."));
		return false;
	}
	return ReachFrom(GCurrentLevelEditingViewportClient->GetViewLocation(), TEXT("the viewport camera"));
}

bool UTraversalDebuggerSubsystem::ReachFromSelection()
{
	const USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
	const AActor* Selected = Selection && Selection->Num() > 0 ? Cast<AActor>(Selection->GetSelectedObject(0)) : nullptr;
	if (!Selected)
	{
		SetStatus(TEXT("Select an actor to stand on first."));
		return false;
	}
	return ReachFrom(Selected->GetActorLocation() + FVector(0.f, 0.f, 50.f), Selected->GetActorLabel());
}

bool UTraversalDebuggerSubsystem::ReachFrom(const FVector& Point, const FString& Where)
{
	if (!EnsureScene())
	{
		return false;
	}
	UWorld* World = GetEditorWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalDebuggerFloor), false);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Point, Point - FVector(0.f, 0.f, HawkeyeTraversalDebugger::FloorSearchDepth), ECC_Visibility, Params))
	{
		SetStatus(FString::Printf(TEXT("Nothing to stand on under %s."), *Where));
		return false;
	}
	ReachSpot.Centre = FGrappleReachQuery::ComputeStandingCentre(Profile.Rules, Hit.ImpactPoint);
	ReachSpot.Roof = INDEX_NONE;
	const AActor* Floor = Hit.GetActor();
	for (int32 R = 0; Floor && R < Scene.RoofActors.Num(); ++R)
	{
		if (Scene.RoofActors[R].Get() == Floor)
		{
			ReachSpot.Roof = R;
			break;
		}
	}
	ReachWhere = Where;

	const FGrappleReachOracle Oracle = FGrappleReachWorld::MakeOracle(World, Profile, &Scene);
	TArray<FGrappleReachEvaluation> All;
	FGrappleReachQuery::EvaluateAll(Profile.Rules, Profile.Lens, Oracle, ReachSpot, Scene.Anchors, All);
	ReachResults = FTraversalDebuggerViewModel::SelectReachResults(All);
	bHasReach = true;
	const int32 Reachable = ReachResults.FilterByPredicate([](const FGrappleReachEvaluation& Result)
	{
		return Result.Verdict == EGrappleReachVerdict::Reachable;
	}).Num();
	Redraw();
	SetStatus(FString::Printf(TEXT("From %s (%s): %d of %d anchors in range are reachable."), *Where,
		ReachSpot.Roof == INDEX_NONE ? TEXT("street") : *Scene.Roofs[ReachSpot.Roof].OsmId, Reachable, ReachResults.Num()));
	return true;
}

// --- The audit -----------------------------------------------------------------------------------

bool UTraversalDebuggerSubsystem::RunAudit(bool bWriteJson)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		SetStatus(TEXT("No editor world."));
		return false;
	}
	FTraversalAuditRunner::FOptions Options;
	Options.ObjectiveChain = FTraversalAuditRunner::ParseList(ObjectiveChain);
	FGrappleReachScene AuditScene;
	FGrappleAuditReport Report;
	FString Error;
	if (!FTraversalAuditRunner::Run(World, Options, AuditScene, Report, Error))
	{
		SetStatus(FString::Printf(TEXT("Audit failed: %s."), *Error));
		return false;
	}
	// The report's roof indices refer to the audit's scene: keep that one.
	Scene = MoveTemp(AuditScene);
	Profile = FTraversalAuditRunner::MakeProfile(World, FString());
	SceneWorld = World;
	bHasScene = true;
	bHasReach = false;
	ReachResults.Reset();
	AuditReport = MoveTemp(Report);

	FString Written;
	if (bWriteJson)
	{
		const FString Path = FGrappleAuditReport::GetDefaultPath();
		Written = AuditReport->WriteJson(Path) ? FString::Printf(TEXT(" Wrote %s."), *Path) : FString::Printf(TEXT(" Could not write %s."), *Path);
	}
	Redraw();
	SetStatus(FString::Printf(TEXT("Audit: %d street-facing roofs unreached, %d with no anchor from the street.%s"),
		AuditReport->StreetFacingUnreached.Num(), AuditReport->RoofsNoStreet.Num(), *Written));
	return true;
}

// --- Focus ---------------------------------------------------------------------------------------

void UTraversalDebuggerSubsystem::FocusRoof(int32 RoofIndex) const
{
	if (!GEditor || !Scene.Roofs.IsValidIndex(RoofIndex))
	{
		return;
	}
	const FGrappleReachRoof& Roof = Scene.Roofs[RoofIndex];
	const FGrappleAuditSettings Settings;
	const FBox Box(FVector(Roof.Box.Min.X, Roof.Box.Min.Y, 0.0), FVector(Roof.Box.Max.X, Roof.Box.Max.Y, Settings.RoofTopZ(Roof.HeightM)));
	GEditor->MoveViewportCamerasToBox(Box, /*bActiveViewportOnly=*/true);
}

void UTraversalDebuggerSubsystem::FocusAnchor(int32 AnchorIndex) const
{
	if (!GEditor || !Scene.Anchors.IsValidIndex(AnchorIndex))
	{
		return;
	}
	const FVector Marker = Scene.Anchors[AnchorIndex].Marker;
	GEditor->MoveViewportCamerasToBox(FBox(Marker - FVector(400.f), Marker + FVector(400.f)), /*bActiveViewportOnly=*/true);
}

// --- Drawing -------------------------------------------------------------------------------------

void UTraversalDebuggerSubsystem::ClearDrawing() const
{
	if (ULineBatchComponent* Lines = GetLineBatcher())
	{
		Lines->ClearBatch(HawkeyeTraversalDebugger::BatchId);
	}
}

void UTraversalDebuggerSubsystem::Redraw()
{
	ClearDrawing();
	ULineBatchComponent* Lines = GetLineBatcher();
	if (!Lines || !bHasScene || SceneWorld.Get() != GetEditorWorld())
	{
		return;
	}
	if (IsLayerVisible(ETraversalDebugLayer::Anchors))
	{
		DrawAnchors(*Lines);
	}
	if (IsLayerVisible(ETraversalDebugLayer::Ledges))
	{
		DrawLedges(*Lines);
	}
	if (IsLayerVisible(ETraversalDebugLayer::FireEscapes))
	{
		DrawFireEscapes(*Lines);
	}
	if (IsLayerVisible(ETraversalDebugLayer::UnreachedRoofs))
	{
		DrawUnreachedRoofs(*Lines);
	}
	if (IsLayerVisible(ETraversalDebugLayer::Reach))
	{
		DrawReach(*Lines);
	}
}

void UTraversalDebuggerSubsystem::DrawAnchors(ULineBatchComponent& Lines) const
{
	using namespace HawkeyeTraversalDebugger;
	for (const FGrappleReachAnchor& Anchor : Scene.Anchors)
	{
		const FLinearColor Color = Anchor.bEnabled ? AnchorColor : DisabledColor;
		Lines.DrawBox(Anchor.Marker, FVector(20.f), Color, 0.f, SDPG_World, 2.f, BatchId);
		Lines.DrawLine(Anchor.Marker, Anchor.Landing, Color, SDPG_World, 1.f, 0.f, BatchId);
	}
}

void UTraversalDebuggerSubsystem::DrawLedges(ULineBatchComponent& Lines) const
{
	using namespace HawkeyeTraversalDebugger;
	// A ledge record is a 1 m cube with its pivot at a bottom corner, turned and scaled into a slab.
	for (const FCityLedgeRecord& Ledge : Scene.Ledges)
	{
		const FVector Centre = Ledge.Transform.TransformPosition(FVector(50.f));
		const FVector Extent = Ledge.Transform.GetScale3D().GetAbs() * 50.f;
		Lines.DrawBox(Centre, Extent, Ledge.Transform.GetRotation(), LedgeColor, 0.f, SDPG_World, 1.f, BatchId);
	}
}

void UTraversalDebuggerSubsystem::DrawFireEscapes(ULineBatchComponent& Lines) const
{
	using namespace HawkeyeTraversalDebugger;
	TArray<FBox> Boxes;
	for (const FCityFireEscapeRecord& Escape : Scene.FireEscapes)
	{
		Boxes.Reset();
		AFireEscapeLanding::BuildCollisionBoxes(Escape, Boxes);
		for (const FBox& Local : Boxes)
		{
			const FVector Centre = Escape.Transform.TransformPosition(Local.GetCenter());
			const FVector Extent = Local.GetExtent() * Escape.Transform.GetScale3D().GetAbs();
			Lines.DrawBox(Centre, Extent, Escape.Transform.GetRotation(), FireEscapeColor, 0.f, SDPG_World, 1.f, BatchId);
		}
	}
}

void UTraversalDebuggerSubsystem::DrawUnreachedRoofs(ULineBatchComponent& Lines) const
{
	using namespace HawkeyeTraversalDebugger;
	const FGrappleAuditReport* Report = GetAuditReport();
	if (!Report)
	{
		return;
	}
	for (const FGrappleAuditRoofRow& Row : Report->Roofs)
	{
		if (Row.Reach == EGrappleRoofReach::Unreached)
		{
			DrawRoofOutline(Lines, Row.Roof, UnreachedColor);
		}
		else if (Row.Reach == EGrappleRoofReach::NoStreetInRange)
		{
			DrawRoofOutline(Lines, Row.Roof, NoStreetColor);
		}
	}
}

void UTraversalDebuggerSubsystem::DrawRoofOutline(ULineBatchComponent& Lines, int32 RoofIndex, const FLinearColor& Color) const
{
	if (!Scene.Roofs.IsValidIndex(RoofIndex))
	{
		return;
	}
	const FGrappleReachRoof& Roof = Scene.Roofs[RoofIndex];
	const FGrappleAuditSettings Settings;
	const float Z = Settings.RoofTopZ(Roof.HeightM) + 30.f;
	for (int32 I = 0; I < Roof.Ring.Num(); ++I)
	{
		const FVector2D& A = Roof.Ring[I];
		const FVector2D& B = Roof.Ring[(I + 1) % Roof.Ring.Num()];
		Lines.DrawLine(FVector(A.X, A.Y, Z), FVector(B.X, B.Y, Z), Color, SDPG_World, 6.f, 0.f, HawkeyeTraversalDebugger::BatchId);
	}
	// A pillar over the roof so it can be found from across the district.
	FVector2D Centroid;
	float Winding = 1.f;
	if (FGrappleReachQuery::RingCentroid(Roof.Ring, Centroid, Winding))
	{
		Lines.DrawLine(FVector(Centroid.X, Centroid.Y, Z), FVector(Centroid.X, Centroid.Y, Z + 2000.f), Color, SDPG_World, 10.f, 0.f,
			HawkeyeTraversalDebugger::BatchId);
	}
}

void UTraversalDebuggerSubsystem::DrawReach(ULineBatchComponent& Lines) const
{
	using namespace HawkeyeTraversalDebugger;
	if (!bHasReach)
	{
		return;
	}
	Lines.DrawCapsule(ReachSpot.Centre, Profile.Rules.CapsuleHalfHeight, Profile.Rules.CapsuleRadius, FQuat::Identity, StandColor, 0.f,
		SDPG_World, 2.f, BatchId);
	for (const FGrappleReachEvaluation& Result : ReachResults)
	{
		if (!Scene.Anchors.IsValidIndex(Result.Anchor))
		{
			continue;
		}
		const FLinearColor Color = ColorOf(Result.Verdict);
		const FVector& Marker = Scene.Anchors[Result.Anchor].Marker;
		const float Thickness = Result.Verdict == EGrappleReachVerdict::Reachable ? 4.f : 1.5f;
		Lines.DrawLine(ReachSpot.Centre, Marker, Color, SDPG_World, Thickness, 0.f, BatchId);
		Lines.DrawBox(Marker, FVector(30.f), Color, 0.f, SDPG_World, Thickness, BatchId);
	}
}
