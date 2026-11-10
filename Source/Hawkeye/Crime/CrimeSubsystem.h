#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Crime/CrimeTypes.h"
#include "CrimeSubsystem.generated.h"

class ACivilian;
class ACrimeLoot;
class ACrimeSpot;
class AThugCharacter;
class APawn;
class UCrimeDefinition;
class UCrimeTracker;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCrimeStartedSignature, UCrimeDefinition*, Crime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCrimeEndedSignature, const FCrimeResult&, Result);

/**
 * The street crimes in a running world, Insomniac style: while the player roams, every 90 to 150 s a
 * crime starts at one of the district's City_CrimeSpot_ actors 25 to 40 m from her (out of her sight
 * when one is), with its roster spawned there, a secondary objective marker on it and a HUD line
 * ("[Crime: mugging] 38 m"). One at a time; a running crime pauses during a challenge or a chapter
 * beat. When it ends the marker goes, a toast says how it went, a stopped crime pays its arrows and
 * is counted in the save, and its thugs, victim and loot are cleaned up later (UCrimeRules).
 *
 * The rules are in UCrimeRules and UCrimeTracker; the counts are mirrored into UHawkeyeCampaignState.
 * hawkeye.CrimeInterval sets the seconds between crimes (and lets them start in automation runs, which
 * otherwise never get one); hawkeye.CrimeType forces the type.
 */
UCLASS()
class HAWKEYE_API UCrimeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Crime", meta = (WorldContext = "WorldContextObject"))
	static UCrimeSubsystem* Get(const UObject* WorldContextObject);

	/** The secondary marker source the crimes set. */
	static const FName MarkerSource;

	/** The tag every crime-spawned thug carries. */
	static const FName CrimeThugTag;

	/** hawkeye.CrimeInterval: seconds between crimes, 0 for the normal roll. */
	static float GetIntervalOverride();

	/** hawkeye.CrimeType, parsed. False when it is empty or names no type. */
	static bool GetForcedType(ECrimeType& OutType);

	// --- Tuning ------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime|Schedule", meta = (ClampMin = "1.0"))
	float MinIntervalSeconds = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime|Schedule", meta = (ClampMin = "1.0"))
	float MaxIntervalSeconds = 150.f;

	/** A crime starts this far from the player at the least, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime|Schedule", meta = (ClampMin = "0.0"))
	float SpawnMinDistance = 2500.f;

	/** And this far at the most, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime|Schedule", meta = (ClampMin = "0.0"))
	float SpawnMaxDistance = 4000.f;

	/** A crime's spot is at least this far from the last one's, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime|Schedule", meta = (ClampMin = "0.0"))
	float LastCrimeApart = 6000.f;

	/** A due crime with no spot to go to tries again this often, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crime|Schedule", meta = (ClampMin = "0.1"))
	float RetrySeconds = 2.f;

	/** False keeps the end toast off (tests read the result instead). */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Crime")
	bool bShowToast = true;

	// --- Starting and ending ----------------------------------------------------------------------

	/**
	 * Starts Definition at Spot for Player now, ignoring the clock and the distance rules: spawns the
	 * roster (and the victim or the loot), marks it, and starts the tracker. Refused while a crime is on,
	 * or without a spot, a definition or a player. True when it began.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crime")
	bool StartCrimeAt(ACrimeSpot* Spot, UCrimeDefinition* Definition, APawn* Player);

	/**
	 * A due crime: picks a spot and one of its crimes by the rules (hawkeye.CrimeType filters the type)
	 * and starts it. False when no spot qualifies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crime")
	bool TryStartDueCrime();

	/** Ends the crime that is on as abandoned. */
	UFUNCTION(BlueprintCallable, Category = "Crime")
	void AbortCrime();

	/** Removes every leftover of past crimes now, whatever the clocks say (tests, screenshots). */
	UFUNCTION(BlueprintCallable, Category = "Crime")
	void DespawnLeftoversNow();

	// --- State -------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Crime")
	bool IsCrimeActive() const;

	/** A crime is on but held: a challenge is running or a chapter beat is playing. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	bool IsCrimePaused() const;

	UFUNCTION(BlueprintPure, Category = "Crime")
	UCrimeTracker* GetTracker() const { return Tracker; }

	UFUNCTION(BlueprintPure, Category = "Crime")
	UCrimeDefinition* GetActiveDefinition() const;

	UFUNCTION(BlueprintPure, Category = "Crime")
	ACrimeSpot* GetActiveSpot() const { return ActiveSpot.Get(); }

	/** The crime's thugs, alive or not. */
	const TArray<TWeakObjectPtr<AThugCharacter>>& GetThugs() const { return Thugs; }

	UFUNCTION(BlueprintPure, Category = "Crime")
	ACivilian* GetVictim() const { return Victim.Get(); }

	UFUNCTION(BlueprintPure, Category = "Crime")
	ACrimeLoot* GetLoot() const { return Loot.Get(); }

	/** The robbery thug carrying (or who carried) the loot. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	AThugCharacter* GetRunner() const { return Runner.Get(); }

	/** Robbery: where the runner is heading. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	FVector GetEscapeLocation() const { return EscapeLocation; }

	/** Robbery: the runner has set off with the loot. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	bool HasRunnerSetOff() const { return bRunnerSetOff; }

	/**
	 * Where the marker and the HUD distance point: the loot (robbery), the victim (mugging), else the
	 * middle of the thugs still standing, else the spot.
	 */
	UFUNCTION(BlueprintPure, Category = "Crime")
	FVector GetCrimePoint() const;

	/** Thugs standing over the mugging's victim and not busy with the player, right now. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	int32 CountThugsOnVictim() const;

	/** Roster thugs dead or limp. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	int32 CountThugsDown() const;

	const FCrimeScheduleClock& GetScheduleClock() const { return Schedule; }

	/** Thugs, victims and loot of ended crimes still waiting to be cleaned up. */
	UFUNCTION(BlueprintPure, Category = "Crime")
	int32 GetLeftoverCount() const { return Leftovers.Num(); }

	UFUNCTION(BlueprintPure, Category = "Crime")
	int32 GetCompletions(ECrimeType Type) const;

	/** Every City_CrimeSpot_ in the world. */
	TArray<ACrimeSpot*> GetSpots() const;

	/** The player the schedule is for: the override when set (tests), else the first player's pawn. */
	APawn* FindPlayer() const;

	/** Tests: the pawn to schedule for in a world without a player controller. */
	TWeakObjectPtr<APawn> PlayerOverride;

	/**
	 * Tests: forgets where the last crime was, so the 60 m rule does not keep the next one off its spot. The
	 * laps run one after another on the same loaded district, which the map open does not reload.
	 */
	void ForgetLastCrime() { bHasLastSpot = false; }

	/** Tests: when set, used as "a chapter beat is playing" instead of the player controller. */
	TOptional<bool> ChapterBeatOverride;

	/** Writes the completion counts into the campaign state. The save calls this before writing. */
	void MirrorToCampaign() const;

	/** Reads the counts back from the campaign state. */
	void RestoreFromCampaign();

	UPROPERTY(BlueprintAssignable, Category = "Crime")
	FOnCrimeStartedSignature OnCrimeStarted;

	UPROPERTY(BlueprintAssignable, Category = "Crime")
	FOnCrimeEndedSignature OnCrimeEnded;

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem interface

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** The roaming clock and, once due, a crime. */
	void UpdateSchedule(float DeltaSeconds);

	/** One frame of the crime that is on. */
	void UpdateCrime(float DeltaSeconds);

	/** Robbery: when the runner sets off, keeping him going, dropping the loot, picking it up. */
	void UpdateRobbery(FCrimeTickState& State);

	/** Mugging: the victim's new hits, dealt by the nearest thug over him. */
	void DealVictimHits(int32 Before, int32 After);

	/** Spawns Definition's roster round Centre, the ring turned to Yaw (in an alley, along the passage at Yaw). */
	void SpawnRoster(const UCrimeDefinition* Definition, const FVector& Centre, float Yaw, APawn* Player, bool bAlley = false);

	/** One thug of Entry at Feet facing Yaw, with his own brain. */
	AThugCharacter* SpawnThug(const FCrimeRosterEntry& Entry, const FVector& Feet, float Yaw);

	/** The victim or the loot, by type. */
	void SpawnProps(const UCrimeDefinition* Definition, const ACrimeSpot* Spot);

	/** The marker on the crime point, or none while paused. */
	void RefreshMarker();

	/** The tracker's end of a crime: marker, reward, save, toast, cleanup list, next interval. */
	void HandleCrimeEnded(const FCrimeResult& Result);

	/** Removes the leftovers whose time has come. */
	void UpdateLeftovers();

	/** Whether the player's camera sees Point (a clear line, and in front of the lens). */
	bool IsInPlayerSight(const FVector& Point, const APawn* Player) const;

	/** A challenge is running (or its results card is up). */
	bool IsInChallenge() const;

	/** A title card, close-up, end card, flashback, main menu or difficulty prompt is up. */
	bool IsInChapterBeat() const;

	/** Dead (or, for the schedule, down). */
	static bool IsPlayerOut(const APawn* Player, bool bDownCounts);

	/** Crimes never start in an automation run unless hawkeye.CrimeInterval is set. */
	static bool IsSchedulingAllowed();

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	UPROPERTY(Transient)
	TObjectPtr<UCrimeTracker> Tracker = nullptr;

	FCrimeScheduleClock Schedule;
	bool bIntervalFromOverride = false;
	double NowSeconds = 0.0;
	double NextTryAt = 0.0;
	FRandomStream Random;

	bool bHasLastSpot = false;
	FVector LastSpotLocation = FVector::ZeroVector;

	TWeakObjectPtr<ACrimeSpot> ActiveSpot;
	TWeakObjectPtr<APawn> CrimePlayer;
	TArray<TWeakObjectPtr<AThugCharacter>> Thugs;
	TWeakObjectPtr<ACivilian> Victim;
	TWeakObjectPtr<ACrimeLoot> Loot;
	TWeakObjectPtr<AThugCharacter> Runner;
	FVector EscapeLocation = FVector::ZeroVector;
	bool bRunnerSetOff = false;
	double RunnerMoveAt = 0.0;
	bool bMarkerShown = false;

	/** Something an ended crime left in the world, and when and how that crime ended. */
	struct FLeftover
	{
		TWeakObjectPtr<AActor> Actor;
		double EndedAt = 0.0;
		bool bCompleted = false;
		float CompleteDelay = 60.f;
		float FailDelay = 20.f;
		float FarDistance = 4000.f;
	};
	TArray<FLeftover> Leftovers;
};
