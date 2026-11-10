#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Crime/CrimeTypes.h"
#include "CrimeTracker.generated.h"

class UCrimeDefinition;

/** Fired once when a crime ends, however it ends, after the completion count is updated. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCrimeEnded, const FCrimeResult&);

/**
 * All of the street crimes' rules for the crime that is on, with no dependency on a world: its clock,
 * the victim's hits, how it ends, and the completions per type. UCrimeSubsystem owns one and feeds it
 * a FCrimeTickState each frame; tests create one with NewObject. Rules are listed on UCrimeRules.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UCrimeTracker : public UObject
{
	GENERATED_BODY()

public:
	/** Starts Definition at SpotLocation with ThugsTotal thugs. Refused (false, logged) while one is on, or without a definition. */
	bool StartCrime(UCrimeDefinition* Definition, const FVector& SpotLocation, int32 ThugsTotal);

	/**
	 * One step of the crime that is on: the clock moves, then, in order, completion (every thug down, or
	 * the loot recovered), the victim's clock (a mugging with thugs on him), the loot escaping, and the
	 * player abandoning it. True when the crime ended during this step.
	 */
	bool Advance(float DeltaSeconds, const FCrimeTickState& State);

	/** Ends the crime that is on as Abandoned. */
	void AbortCrime();

	bool IsActive() const { return Active != nullptr; }
	UCrimeDefinition* GetActiveDefinition() const { return Active; }
	const FVector& GetSpotLocation() const { return SpotLocation; }
	float GetElapsedSeconds() const { return ElapsedSeconds; }

	/** Seconds the mugging's thugs have stood over the victim. */
	float GetSecondsOnVictim() const { return SecondsOnVictim; }

	int32 GetVictimHits() const { return VictimHits; }
	int32 GetThugsDown() const { return ThugsDown; }
	int32 GetThugsTotal() const { return ThugsTotal; }

	/** How the last crime that ended went. Valid once HasLastResult. */
	const FCrimeResult& GetLastResult() const { return LastResult; }
	bool HasLastResult() const { return bHasLastResult; }

	/** The reward's arrows go on the last result once the subsystem has paid them. */
	void NoteArrowsGiven(int32 Arrows) { LastResult.ArrowsGiven = Arrows; }

	/** Crimes of Type stopped so far. */
	int32 GetCompletions(ECrimeType Type) const;

	/** The completions as parallel arrays for the save (SPUD stores plain arrays): type keys and counts, types with none left out. */
	void ExportCompletions(TArray<FName>& OutTypes, TArray<int32>& OutCounts) const;

	/** Replaces every count with the saved arrays. Unknown keys and rows past the shorter array are dropped with a warning. */
	void ImportCompletions(const TArray<FName>& Types, const TArray<int32>& Counts);

	/** Drops the crime (without ending it) and every count. */
	void Reset();

	FOnCrimeEnded OnCrimeEnded;

private:
	/** Count, result and the broadcast; clears the crime. */
	void EndCrime(ECrimeEndReason Reason);

	UPROPERTY(Transient)
	TObjectPtr<UCrimeDefinition> Active = nullptr;

	FVector SpotLocation = FVector::ZeroVector;
	float ElapsedSeconds = 0.f;
	float SecondsOnVictim = 0.f;
	int32 VictimHits = 0;
	int32 ThugsDown = 0;
	int32 ThugsTotal = 0;

	TMap<ECrimeType, int32> Completions;

	UPROPERTY(Transient)
	FCrimeResult LastResult;

	bool bHasLastResult = false;
};
