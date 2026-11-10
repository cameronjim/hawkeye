#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UI/HawkeyeHints.h"
#include "HawkeyeHintSubsystem.generated.h"

class AHawkeyeCharacter;
class AHawkeyePlayerController;
class APawn;

/**
 * The first-time hints (claude-docs/gameplay-semantics.md, HUD, "First-time hints"). Every frame of a game
 * world it reads the local player's controller and pawn and the world (Sense), runs FHawkeyeHintQueue over the
 * rules in UHawkeyeHintRules, and saves each show and each learned hint in the campaign (UHawkeyeSaveSubsystem,
 * like NoticedPlaces). UHawkeyeHintWidget, in the HUD, draws the line this holds.
 *
 * It only reads: nothing here takes input, pauses or slows the game. Off under automation unless
 * hawkeye.Hints 2 (as the "nearby" toasts are), so a scripted shot or lap sees no hint it did not ask for.
 */
UCLASS()
class HAWKEYE_API UHawkeyeHintSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UHawkeyeHintSubsystem* Get(const UObject* WorldContext);

	//~ Begin UWorldSubsystem / FTickableGameObject
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	//~ End UWorldSubsystem / FTickableGameObject

	/**
	 * Whether hints run at all here: hawkeye.Hints 0 never, 1 (default) outside automation, 2 always. The
	 * Settings toggle is on top of this.
	 */
	static bool IsRunAllowed();

	/** What keeps the line down now for Controller (null: nothing to read, not suppressed). */
	static FHawkeyeHintSuppression GatherSuppression(const AHawkeyePlayerController* Controller);

	/** The triggers that hold and the actions done this frame, for the rules in Needed (a mask of triggers). */
	void Sense(AHawkeyePlayerController* Controller, float DeltaSeconds, uint64 Needed, uint64& OutTriggers, uint64& OutActions);

	/** The hint on the screen now, or null. */
	const FHawkeyeHintRule* GetShown() const { return Queue.IsVisible() ? Queue.GetCurrent() : nullptr; }

	/** Seconds the shown hint has been up (the widget fades it in over the first 0.2). */
	float GetShownAge() const { return Queue.GetCurrentAge(); }

	/** The shown hint's line resolved for the local player's device and sprint mode; empty with none. */
	FString GetShownLineForLog() const;

	/** The context lines resolve against now: the controller's last device, the sprint setting, the partner's name. */
	FHawkeyeHintContext GetContext() const;

	/** Times Id has shown (its MaxShows once learned): the campaign's count, or this world's with no save. */
	int32 GetShowCount(FName Id) const;

	const FHawkeyeHintQueue& GetQueue() const { return Queue; }

	/**
	 * For a screenshot or a test: starts the queue over, sets every rule's count to its MaxShows except Only
	 * (and the other names in it), which go to 0. Empty Only zeroes them all.
	 */
	void ResetForTest(const TArray<FName>& Only);

	/** Hints shown in this world, in order (the log and the tests read it). */
	const TArray<FName>& GetShownHistory() const { return ShownHistory; }

	/** Sets Id's count in the campaign (or this world's with no save). */
	void SetShowCount(FName Id, int32 Count);

private:

	/** A mask of the triggers of rules that still have shows left, so a learned hint costs no traces. */
	uint64 ComputeNeededTriggers() const;

	FHawkeyeHintQueue Queue;
	TMap<FName, int32> LocalCounts;
	TArray<FName> ShownHistory;

	// Sensing state, between frames.
	TWeakObjectPtr<APawn> LastPawn;
	bool bHaveLastPawn = false;
	float LastHealth = -1.f;
	int32 LastTrickArrows = 0;
	int32 LastParryCount = -1;
	int32 LastNoticedPlaces = -1;
	int32 LastDiscovered = -1;
	bool bWasDrawing = false;
	TWeakObjectPtr<AActor> LastZipAnchor;
	bool bHaveStart = false;
	FVector StartLocation = FVector::ZeroVector;
	/** The traced triggers are refreshed 5 times a second; between refreshes the last answer holds. */
	float ProbeCooldown = 0.f;
	uint64 LastProbed = 0;
	/** A "nearby" toast holds the map trigger this long, s. */
	float NoticePulseRemaining = 0.f;
};
