#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Traversal/GrappleAudit.h"
#include "TraversalDebuggerSubsystem.generated.h"

class ULineBatchComponent;
class UWorld;

/** What the debugger can draw in the level viewport. */
enum class ETraversalDebugLayer : uint8
{
	Anchors,
	Ledges,
	FireEscapes,
	UnreachedRoofs,
	Reach,
	Count
};

/**
 * The traversal debugger's state and drawing (claude-docs/tools-traversal-debugger.md). Reads the district from the
 * generated data (FGrappleReachWorld::BuildScene), so it works in the editor world with nothing spawned; draws into
 * the editor world's persistent line batcher under its own batch id, so clearing it never touches anyone else's lines;
 * answers "reach from here" with FGrappleReachQuery and runs the audit with FTraversalAuditRunner. STraversalDebuggerPanel
 * is its only UI and listens to OnChanged.
 */
UCLASS()
class UTraversalDebuggerSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Re-reads the scene and the player's profile from the editor world. False with the reason in GetStatus. */
	bool RefreshScene();

	bool IsLayerVisible(ETraversalDebugLayer Layer) const;
	void SetLayerVisible(ETraversalDebugLayer Layer, bool bVisible);

	/** Checks every anchor from the floor under the level viewport's camera. */
	bool ReachFromViewport();

	/** Checks every anchor from the floor under the first selected actor. */
	bool ReachFromSelection();

	/** Checks every anchor from a character standing on the floor under Point. */
	bool ReachFrom(const FVector& Point, const FString& Where);

	/** Runs the grapple audit on the editor world (spawning the city props for it if they are not out). */
	bool RunAudit(bool bWriteJson);

	/** Moves the level viewports to look at a roof or an anchor of the scene. */
	void FocusRoof(int32 RoofIndex) const;
	void FocusAnchor(int32 AnchorIndex) const;

	/** Clears and redraws every visible layer. */
	void Redraw();

	/** Removes everything this tool drew. */
	void ClearDrawing() const;

	bool HasScene() const { return bHasScene; }
	const FGrappleReachScene& GetScene() const { return Scene; }
	const FGrappleReachProfile& GetProfile() const { return Profile; }

	bool HasReach() const { return bHasReach; }
	const FGrappleReachSpot& GetReachSpot() const { return ReachSpot; }
	const FString& GetReachWhere() const { return ReachWhere; }
	/** The in-range anchors of the last reach check, Reachable first. */
	const TArray<FGrappleReachEvaluation>& GetReachResults() const { return ReachResults; }

	/** The last audit, or null. */
	const FGrappleAuditReport* GetAuditReport() const { return AuditReport.GetPtrOrNull(); }

	/** One line about what happened last. */
	const FString& GetStatus() const { return Status; }

	/** Comma-separated objective ids the audit chains (passed to FGrappleAuditInputs::ObjectiveChain). */
	FString ObjectiveChain;

	/** Fires after anything the panel shows has changed. */
	FSimpleMulticastDelegate OnChanged;

private:
	UWorld* GetEditorWorld() const;
	ULineBatchComponent* GetLineBatcher() const;
	void HandleMapChanged(uint32 MapChangeFlags);
	void SetStatus(const FString& InStatus);
	void Reset();
	bool EnsureScene();

	void DrawAnchors(ULineBatchComponent& Lines) const;
	void DrawLedges(ULineBatchComponent& Lines) const;
	void DrawFireEscapes(ULineBatchComponent& Lines) const;
	void DrawUnreachedRoofs(ULineBatchComponent& Lines) const;
	void DrawReach(ULineBatchComponent& Lines) const;
	void DrawRoofOutline(ULineBatchComponent& Lines, int32 RoofIndex, const FLinearColor& Color) const;

	FGrappleReachScene Scene;
	FGrappleReachProfile Profile;
	TWeakObjectPtr<UWorld> SceneWorld;
	bool bHasScene = false;

	FGrappleReachSpot ReachSpot;
	FString ReachWhere;
	TArray<FGrappleReachEvaluation> ReachResults;
	bool bHasReach = false;

	TOptional<FGrappleAuditReport> AuditReport;

	/** One bit per ETraversalDebugLayer; anchors, unreached roofs and reach on to start. */
	uint8 LayerMask = 0x19;
	FString Status;
	FDelegateHandle MapChangeHandle;
};
