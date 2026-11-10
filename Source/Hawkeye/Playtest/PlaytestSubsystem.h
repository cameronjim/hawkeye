#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Playtest/PlaytestTypes.h"
#include "PlaytestSubsystem.generated.h"

class AThugCharacter;
class APawn;
class APlayerController;
class UCrimeDefinition;
class UChallengeDefinition;
struct FChallengeResult;
struct FCrimeResult;

/**
 * The playtest capture kit (claude-docs/testing.md, "Playtest capture"). One session per launch, named
 * by the launch time (or -PlaytestSession=<name>), with its files in Saved/Playtest/<session>/:
 *
 *   note_<n>.png, notes.json   F12 (Menu held 0.6 s on a pad): a NOTE log line, a screenshot with the HUD,
 *                              and a record with the state and the last 20 LogHawkeye lines.
 *   photo_<n>.png              photo mode's clean shots (no HUD, no overlay).
 *   summary.json               the session's counters, written on every map change and at exit.
 *   Hawkeye.log                the game's own log, copied at exit.
 *
 * The counters (play time, deaths, fights won, crimes stopped, challenges and medals, fast travels,
 * distance, grapples, time in each movement state) are kept by watching the player and the world's
 * crime, challenge and safehouse subsystems; nothing else in the game knows the kit exists.
 * Automation runs (-game with RunTests, the headless suite) only write a folder once a note or photo is
 * taken, so test runs do not fill Saved/Playtest.
 */
UCLASS()
class HAWKEYE_API UPlaytestSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	using FOnSaved = TFunction<void(int32 Index, bool bShotOk)>;

	static UPlaytestSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bInitialized; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

	const FString& GetSessionName() const { return SessionName; }

	/** Saved/Playtest/<session>/, absolute, with a trailing slash. Not created until something is written. */
	FString GetSessionFolder() const;

	/**
	 * Takes note n+1 now: logs the NOTE line, queues note_<n>.png with the HUD on, and rewrites notes.json.
	 * OnSaved runs on the game thread once the screenshot is written (or failed). Returns the note's index.
	 */
	int32 TakeNote(const APlayerController* PC, FOnSaved OnSaved = FOnSaved());

	/** Queues photo_<n>.png with no UI at all. Returns the photo's index. */
	int32 TakePhoto(FOnSaved OnSaved = FOnSaved());

	/** LogHawkeye at Verbose for Seconds of real time, logged when it starts and ends. A second call restarts the clock. */
	void StartVerboseBump(float Seconds = 10.f);

	bool IsVerboseBumpActive() const { return VerboseBumpEndSeconds > 0.0; }

	/** Writes summary.json (Reason: "exit", "map change", "test"). False when nothing was written. */
	bool WriteSummary(const FString& Reason, bool bForce = false);

	const FPlaytestSummary& GetSummary() const { return Summary; }
	const TArray<FPlaytestNote>& GetNotes() const { return NotesFile.Notes; }

	/** The state a note records, read from PC's world and pawn. bCountThugs walks the thugs for the alerted count. */
	static FPlaytestState CaptureState(const APlayerController* PC, bool bCountThugs = true);

	/** Automation drives the game: no session folder until a note or photo asks for one. */
	static bool IsAutomationRun();

protected:
	UFUNCTION()
	void HandleCrimeStarted(UCrimeDefinition* Crime);

	UFUNCTION()
	void HandleCrimeEnded(const FCrimeResult& Result);

	UFUNCTION()
	void HandleChallengeEnded(const FChallengeResult& Result);

	UFUNCTION()
	void HandleFastTravelFinished(FName SafehouseId, float Seconds);

private:
	void HandlePreLoadMap(const FString& MapName);
	void BindWorld(UWorld* World);
	void WatchFight(UWorld* World, const APawn* Pawn, double Now);
	bool SaveNotes() const;
	void EndVerboseBump();
	void CopyGameLog() const;

	bool bInitialized = false;
	bool bSummaryWrittenAtExit = false;
	FString SessionName;
	FDateTime StartedAt;
	double StartSeconds = 0.0;
	double LastTickSeconds = 0.0;

	FPlaytestNotesFile NotesFile;
	FPlaytestSummary Summary;

	/** Captures the last LogHawkeye lines from any thread. */
	TSharedPtr<class FPlaytestLogDevice> LogDevice;

	TWeakObjectPtr<UWorld> BoundWorld;
	TWeakObjectPtr<const APawn> LastPawn;
	FVector LastPawnLocation = FVector::ZeroVector;
	FPlaytestEdge ZipEdge;
	FPlaytestEdge DeadEdge;
	FPlaytestFightWatch FightWatch;
	TSet<TWeakObjectPtr<AThugCharacter>> FightThugs;
	int32 CachedAlerted = 0;
	double NextFightPollSeconds = 0.0;

	double VerboseBumpEndSeconds = 0.0;
	ELogVerbosity::Type VerbosityBeforeBump = ELogVerbosity::Log;

	FDelegateHandle PreLoadMapHandle;
};
