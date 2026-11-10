#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "SafehouseSubsystem.generated.h"

class AHawkeyePlayerController;
class APawn;
class ASafehouse;

/** One row of the fast-travel list: a safehouse the district has, and whether it has been found. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeSafehouseEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse")
	FName SafehouseId;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse")
	FText DisplayName;

	/** The door, at pavement level. */
	UPROPERTY(BlueprintReadOnly, Category = "Safehouse")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Safehouse")
	bool bDiscovered = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFastTravelFinishedSignature, FName, SafehouseId, float, Seconds);

/**
 * The district's safehouses and fast travel between them.
 *
 * Every ASafehouse registers here at BeginPlay. The rules (tested in FastTravelTest.cpp):
 * - A safehouse is discovered by walking into its entry zone (or entering it with E); only discovered
 *   ones can be travelled to, and they show on the compass as a small house.
 * - The fast-travel list is every safehouse in the district but the one you are at, in id order;
 *   undiscovered ones are listed as "[Undiscovered]", greyed, and cannot be picked.
 * - Travel is refused, with a "[Can't fast travel now]" toast, while a street crime is on or a side
 *   challenge is running, and while a travel is already under way.
 * - Travel: fade to black (FadeOutSeconds), move the player to the destination's door and the partner
 *   beside her, record it as the last-used safehouse, autosave ("fast travel"), hold (HoldSeconds),
 *   fade back in (FadeInSeconds). No map load: the district is one map. Under 3 s from fade to fade.
 * - The pause menu's "Mark nearest safehouse" puts a secondary objective marker on the nearest
 *   safehouse, found or not (an unfound one is "[Unknown safehouse]" with a "?"); walking into any
 *   safehouse clears it. Every safehouse is on the compass: found ones as a house, unfound hollow.
 * - The first time the player comes within NoticeRadius of an unfound safehouse, a "[Safehouse
 *   nearby]" toast and the marker on it (AHawkeyePlayerController asks FindUnnoticed; the save keeps
 *   the noticed ids).
 */
UCLASS()
class HAWKEYE_API USafehouseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Safehouse", meta = (WorldContext = "WorldContextObject"))
	static USafehouseSubsystem* Get(const UObject* WorldContextObject);

	/** The secondary marker source "Mark nearest safehouse" sets. */
	static const FName MarkerSource;

	/** How far above the door the marker floats, cm. */
	static constexpr float MarkerUp = 250.f;

	// --- Tuning ------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safehouse|Travel", meta = (ClampMin = "0.05"))
	float FadeOutSeconds = 0.5f;

	/** Black between the move and the fade in: the camera and the partner settle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safehouse|Travel", meta = (ClampMin = "0.0"))
	float HoldSeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safehouse|Travel", meta = (ClampMin = "0.05"))
	float FadeInSeconds = 0.6f;

	// --- The district's safehouses -------------------------------------------------------------------

	void RegisterSafehouse(ASafehouse* Safehouse);
	void UnregisterSafehouse(ASafehouse* Safehouse);

	/** Every registered safehouse, in id order. */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	TArray<ASafehouse*> GetSafehouses() const;

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	ASafehouse* FindSafehouse(FName SafehouseId) const;

	/** Every safehouse as a list row, marked discovered from Discovered. */
	TArray<FHawkeyeSafehouseEntry> MakeEntries(const TArray<FName>& Discovered) const;

	/** MakeEntries with the save's discovered set. */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	TArray<FHawkeyeSafehouseEntry> GetEntries() const;

	/** The fast-travel list: All less CurrentId, in id order, each marked from Discovered. */
	static TArray<FHawkeyeSafehouseEntry> BuildTravelList(const TArray<FHawkeyeSafehouseEntry>& All, FName CurrentId,
		const TArray<FName>& Discovered);

	/** Of the safehouses in Discovered, the one nearest From, or null. */
	ASafehouse* FindNearest(const FVector& From, const TArray<FName>& Discovered) const;

	/** The discovered safehouse nearest From, or null when none is found yet. */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	ASafehouse* FindNearestDiscovered(const FVector& From) const;

	/** The safehouse nearest From, discovered or not; null only when the district has none. */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	ASafehouse* FindNearestAny(const FVector& From) const;

	/** How close an unfound safehouse has to be for the "[Safehouse nearby]" toast, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safehouse|Marker", meta = (ClampMin = "0.0"))
	float NoticeRadius = 6000.f;

	/** The nearest safehouse within Radius of From that is in neither Discovered nor Noticed, or null. */
	ASafehouse* FindUnnoticed(const FVector& From, float Radius, const TArray<FName>& Discovered,
		const TArray<FName>& Noticed) const;

	/** What the marker and the toast call a safehouse: its name once found, "[Unknown safehouse]" before. */
	static FText GetMarkerName(const ASafehouse* Safehouse, bool bDiscovered);

	/** GetMarkerName with the save's discovered set. */
	FText GetMarkerNameNow(const ASafehouse* Safehouse) const;

	/** True when the save has Safehouse as found. */
	bool IsDiscoveredNow(const ASafehouse* Safehouse) const;

	// --- Fast travel ---------------------------------------------------------------------------------

	/**
	 * "[Can't fast travel now]" when any of these holds, else empty. bInFight: a thug alerted or Clint's fight
	 * clock running, or the player down; the fade would otherwise leave her standing in their fire.
	 */
	static FText GetTravelRefusal(bool bCrimeActive, bool bChallengeRunning, bool bTravelling, bool bInFight = false);

	/** GetTravelRefusal from this world's crime and challenge subsystems, its fights and the player. */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	FText GetTravelRefusalNow() const;

	/**
	 * Puts Player at Destination's door facing the street and Partner (may be null) beside her, both
	 * standing, still and with no path left to follow. False when the destination or player is missing.
	 */
	static bool PlaceAtArrival(ASafehouse* Destination, APawn* Player, APawn* Partner);

	/**
	 * Starts the fade, move, save, fade in. False (and nothing happens) when the destination is not a
	 * discovered safehouse, a refusal applies, or there is no player to move.
	 */
	bool BeginFastTravel(AHawkeyePlayerController* PC, ASafehouse* Destination);

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	bool IsTravelling() const { return bTravelling; }

	/** Real seconds from the last travel's fade out starting to its fade in ending; 0 before one. */
	UFUNCTION(BlueprintPure, Category = "Safehouse")
	float GetLastTravelSeconds() const { return LastTravelSeconds; }

	UPROPERTY(BlueprintAssignable, Category = "Safehouse")
	FOnFastTravelFinishedSignature OnFastTravelFinished;

	// --- The safehouse marker ------------------------------------------------------------------------

	/** Marks the safehouse nearest From, found or not. Returns it, or null when the district has none. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	ASafehouse* MarkNearestSafehouse(const FVector& From);

	/** Marks Safehouse (the nearby toast uses it). False for null. */
	bool MarkSafehouse(ASafehouse* Safehouse);

	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void ClearSafehouseMarker();

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	ASafehouse* GetMarkedSafehouse() const { return Marked.Get(); }

	//~ Begin USubsystem interface
	virtual void Deinitialize() override;
	//~ End USubsystem interface

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleFadedOut();
	void HandleHoldDone();
	void HandleFadedIn();

	/** Gives the player back control and the HUD. */
	void ReleasePlayer(AHawkeyePlayerController* PC);

	TArray<TWeakObjectPtr<ASafehouse>> Safehouses;
	TWeakObjectPtr<ASafehouse> Marked;

	TWeakObjectPtr<AHawkeyePlayerController> TravelPC;
	TWeakObjectPtr<ASafehouse> TravelDestination;
	FTimerHandle TravelTimer;
	double TravelStartedAt = 0.0;
	float LastTravelSeconds = 0.f;
	bool bTravelling = false;
};
