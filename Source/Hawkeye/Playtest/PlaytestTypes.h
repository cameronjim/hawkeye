#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "PlaytestTypes.generated.h"

/**
 * What the game was doing when a playtest note was taken: the fields the NOTE log line, notes.json and
 * the report all show. Plain data; UPlaytestSubsystem::CaptureState fills it from a live world.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FPlaytestState
{
	GENERATED_BODY()

	/** The movement component's mode: "Walking", "Falling", "Flying", "Custom", "None" (no pawn). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Movement = TEXT("None");

	/** Horizontal speed, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	bool bSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	bool bCrouching = false;

	/** The parkour move in progress ("Vault", "Mantle", ...), "Hanging" on a ledge, or "None". */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Parkour = TEXT("None");

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	bool bZipping = false;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	bool bAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	bool bDowned = false;

	/** Current health of the player pawn; 0 with none. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float Health = 0.f;

	/** Living alerted thugs in the world; above 0 is "in a fight". */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 AlertedThugs = 0;

	/** The running street crime's name, empty with none. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Crime;

	/** The running side challenge's name, empty with none. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Challenge;

	/** An interior map (the game mode's indoor camera). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	bool bInterior = false;

	/** The map's short name ("L_District_EastVillage"). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Map;

	/** "Night" or "Day": the time of day the district is lit for. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString TimeOfDay;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Difficulty;

	/** The world is paused (a menu, the map, the phone, a flashback, photo mode). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	bool bPaused = false;
};

/** One F12 / Menu-hold note: where, when, what, and the log just before it. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FPlaytestNote
{
	GENERATED_BODY()

	/** 1-based, in the order taken this session. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 Index = 0;

	/** Seconds since the session began (the launch). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float SessionSeconds = 0.f;

	/** Local wall-clock time, "2026-09-29 14:03:11". */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString WallClock;

	/** The player pawn's location, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FVector Position = FVector::ZeroVector;

	/** The view's yaw, degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float Yaw = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FPlaytestState State;

	/** HawkeyePlaytest::DescribeState(State), as the log line has it. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString StateText;

	/** The screenshot's file name inside the session folder ("note_3.png"). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Screenshot;

	/** The screenshot's full path. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString ScreenshotPath;

	/** The last LogHawkeye lines before the note, oldest first (at most HawkeyePlaytest::RecentLogLines). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	TArray<FString> RecentLog;
};

/** notes.json: the session and every note so far. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FPlaytestNotesFile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Session;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	TArray<FPlaytestNote> Notes;
};

/** summary.json: the session's counters, written on exit and on every map change. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FPlaytestSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Session;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString StartedAt;

	/** When this summary was written. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString WrittenAt;

	/** "exit" or "map change". */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	FString Reason;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 Notes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 Photos = 0;

	/** Real seconds since the session began, paused time included. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float SessionSeconds = 0.f;

	/** Real seconds with the world running (no menu, map, phone, flashback or photo mode). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float PlaySeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float PausedSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 Deaths = 0;

	/** Fights (a stretch with at least one living alerted thug) that ended. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 Fights = 0;

	/** Of those, the ones every thug in went down and she was standing at the end. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 FightsWon = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 CrimesStarted = 0;

	/** Crimes that ended Completed. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 CrimesStopped = 0;

	/** Challenge runs that ended, however they ended. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 ChallengesRun = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 GoldMedals = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 SilverMedals = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 BronzeMedals = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 FastTravels = 0;

	/** Metres the player pawn moved while the world ran, jumps over MaxStepCm (teleports, loads) left out. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float DistanceMetres = 0.f;

	/** Grapple zips started. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	int32 Grapples = 0;

	/** Real seconds aiming, fighting (a thug alerted), while the world ran. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float AimSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	float FightSeconds = 0.f;

	/** Real seconds in each HawkeyePlaytest::MovementKey ("Idle", "Run", "Sprint", "Air", "Zip", "Paused", ...). */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	TMap<FString, float> MovementSeconds;

	/** Maps played, in the order first entered. */
	UPROPERTY(BlueprintReadOnly, Category = "Playtest")
	TArray<FString> Maps;

	/** One frame's step of the player pawn; a step longer than this is a teleport and is not distance, cm. */
	static constexpr float MaxStepCm = 2500.f;

	void AddMovement(const FString& Key, float Seconds);

	/** Adds the step From..To to DistanceMetres unless it is a teleport. Returns whether it counted. */
	bool AddStep(const FVector& From, const FVector& To);

	/** A challenge run ended with Medal (0 none, 1 bronze, 2 silver, 3 gold: EChallengeMedal's order). */
	void AddChallengeEnd(uint8 Medal);

	void AddCrimeEnd(bool bStopped);

	void AddFightEnd(bool bWon);

	void AddMap(const FString& MapName);
};

/** A rising edge: true once each time the watched flag goes from false to true. */
struct FPlaytestEdge
{
	bool bLast = false;

	bool Rise(bool bNow)
	{
		const bool bRose = bNow && !bLast;
		bLast = bNow;
		return bRose;
	}
};

/**
 * One fight from the first alerted thug to a quiet QuietSeconds with none. The subsystem feeds it the
 * alerted count and, for the thugs seen alerted this fight, how many are still standing.
 */
struct HAWKEYE_API FPlaytestFightWatch
{
	enum class EEnd : uint8
	{
		None,
		Won,
		Other,
	};

	/** Alerted-free time before a fight counts as over, s (thugs lose her and re-alert). */
	double QuietSeconds = 1.0;

	bool bInFight = false;
	double LastAlertedSeconds = 0.0;

	/**
	 * AlertedNow: living alerted thugs. SeenTotal / SeenStanding: thugs seen alerted since the fight began,
	 * and how many of them are alive now. Won when every one of them is down and the player is standing.
	 */
	EEnd Update(int32 AlertedNow, int32 SeenTotal, int32 SeenStanding, bool bPlayerStanding, double NowSeconds);
};

/** The last few LogHawkeye lines, from any thread. */
class HAWKEYE_API FPlaytestLogRing
{
public:
	explicit FPlaytestLogRing(int32 InCapacity = 20) : Capacity(FMath::Max(1, InCapacity)) {}

	void Add(const FString& Line);

	/** Oldest first. */
	TArray<FString> Snapshot() const;

private:
	int32 Capacity;
	TArray<FString> Lines;
	mutable FCriticalSection Lock;
};

namespace HawkeyePlaytest
{
	/** How many LogHawkeye lines a note carries. */
	constexpr int32 RecentLogLines = 20;

	/** The launch timestamp as a folder name: "2026-09-29_14-03-11". */
	HAWKEYE_API FString MakeSessionName(const FDateTime& LocalTime);

	/** A -PlaytestSession= value made safe for a folder name (letters, digits, '-', '_'); empty when nothing is left. */
	HAWKEYE_API FString SanitizeSessionName(const FString& Raw);

	/**
	 * "move=Walking speed=512 sprint=no crouch=no parkour=None zip=no aim=no downed=no health=100 fight=0
	 * crime=- challenge=- interior=no map=L_District_EastVillage tod=Night difficulty=Normal paused=no".
	 * Every field, always in this order, so a log grep for "fight=2" or "zip=yes" works.
	 */
	HAWKEYE_API FString DescribeState(const FPlaytestState& State);

	/** "NOTE #3 at 734.2s: pos=(1234,-567,89) yaw=45 state=<DescribeState>": what the log line says after "LogHawkeye: ". */
	HAWKEYE_API FString FormatNoteLine(const FPlaytestNote& Note);

	/**
	 * The one movement state a frame counts toward, first match: Paused, Downed, Zip, the parkour move
	 * (Vault, Mantle, LedgeGrab, Climb, DropToHang, Hanging), Air (falling), Crouch, Sprint, Run (over 300
	 * cm/s), Walk (over 20 cm/s), Idle.
	 */
	HAWKEYE_API FString MovementKey(const FPlaytestState& State);

	/** Pure fight rule: at least one thug fought, every one of them down, and the player standing. */
	HAWKEYE_API bool IsFightWon(int32 SeenTotal, int32 SeenStanding, bool bPlayerStanding);

	HAWKEYE_API FString NotesToJson(const FPlaytestNotesFile& File);
	HAWKEYE_API bool NotesFromJson(const FString& Json, FPlaytestNotesFile& OutFile);
	HAWKEYE_API FString SummaryToJson(const FPlaytestSummary& Summary);
	HAWKEYE_API bool SummaryFromJson(const FString& Json, FPlaytestSummary& OutSummary);
}
