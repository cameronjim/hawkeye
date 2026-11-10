#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dialogue/DialogueLine.h"
#include "Math/RandomStream.h"
#include "BanterComponent.generated.h"

class UDataTable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBanterLineSignature, FName, Speaker, EHawkeyeBanterSituation,
	Situation, FText, Text);

/**
 * Kate and Clint talking (claude-docs/gameplay-semantics.md, "partner and switching"). Lives on the
 * player controller. Picks a line from DT_Dialogue for the situation and whoever's turn it is, shows
 * it as a subtitle for SubtitleSeconds, and hands the turn to the other one. Roaming (no fight going
 * on) it speaks every MinRoamSeconds to MaxRoamSeconds; a fight ending, the current objective
 * coming within ObjectiveNearDistance, or the lead dropping under LowHealthFraction speak on the
 * spot, at most once per MinEventGapSeconds. Silent while there is no partner in the world.
 */
UCLASS(ClassGroup = (Hawkeye), meta = (BlueprintSpawnableComponent))
class HAWKEYE_API UBanterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBanterComponent();

	/** DT_Dialogue (rows of FHawkeyeDialogueLine). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter")
	TObjectPtr<UDataTable> DialogueTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter", meta = (ClampMin = "1.0"))
	float MinRoamSeconds = 45.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter", meta = (ClampMin = "1.0"))
	float MaxRoamSeconds = 90.f;

	/** How long a subtitle stays up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter", meta = (ClampMin = "0.5"))
	float SubtitleSeconds = 4.f;

	/** Event lines never come closer together than this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter", meta = (ClampMin = "0.0"))
	float MinEventGapSeconds = 8.f;

	/** The current objective this close is ObjectiveNear, cm. Once per objective. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter", meta = (ClampMin = "0.0"))
	float ObjectiveNearDistance = 2500.f;

	/** Under this fraction of max health is LowHealth; it re-arms above LowHealthRearmFraction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthFraction = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthRearmFraction = 0.6f;

	/** Who speaks first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter")
	FName FirstSpeaker = FName(TEXT("Kate"));

	/** The other voice. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Banter")
	FName SecondSpeaker = FName(TEXT("Clint"));

	/**
	 * The index into Lines of a line for Situation spoken by Speaker, chosen with Stream, avoiding
	 * AvoidIndex when there is another choice. INDEX_NONE when Speaker has no line for it. Pure.
	 */
	static int32 PickLineIndex(const TArray<FHawkeyeDialogueLine>& Lines, EHawkeyeBanterSituation Situation, FName Speaker,
		int32 AvoidIndex, FRandomStream& Stream);

	/**
	 * Speaks a line for Situation now: the speaker whose turn it is (the other one if they have no
	 * line for it), shown as a subtitle, and the turn passes. False when nobody has a line.
	 */
	UFUNCTION(BlueprintCallable, Category = "Banter")
	bool PlaySituation(EHawkeyeBanterSituation Situation);

	/** Replaces the lines (DialogueTable's rows at BeginPlay). Tests hand theirs in here. */
	void SetLines(const TArray<FHawkeyeDialogueLine>& InLines) { Lines = InLines; }

	const TArray<FHawkeyeDialogueLine>& GetLines() const { return Lines; }

	/** Whose turn it is. */
	UFUNCTION(BlueprintPure, Category = "Banter")
	FName GetNextSpeaker() const { return NextSpeaker; }

	/** The last line spoken, and by whom. */
	UFUNCTION(BlueprintPure, Category = "Banter")
	FText GetLastText() const { return LastText; }

	UFUNCTION(BlueprintPure, Category = "Banter")
	FName GetLastSpeaker() const { return LastSpeaker; }

	/** Seconds of roaming left before the next idle line. */
	UFUNCTION(BlueprintPure, Category = "Banter")
	float GetRoamSecondsLeft() const { return RoamSecondsLeft; }

	/** Fixes the random stream, so a test picks the same lines every run. */
	void SetRandomSeed(int32 Seed) { Stream.Initialize(Seed); }

	UPROPERTY(BlueprintAssignable, Category = "Banter")
	FOnBanterLineSignature OnBanterLine;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** An event line, unless one played within MinEventGapSeconds. */
	void TriggerEvent(EHawkeyeBanterSituation Situation);

	/** Fight edges, the objective distance and the lead's health. */
	void WatchEvents();

	/** A new wait of MinRoamSeconds to MaxRoamSeconds. */
	void ResetRoamClock();

	UPROPERTY(Transient)
	TArray<FHawkeyeDialogueLine> Lines;

	UPROPERTY(Transient)
	FName NextSpeaker;

	UPROPERTY(Transient)
	FName LastSpeaker;

	UPROPERTY(Transient)
	FText LastText;

private:
	FRandomStream Stream;
	int32 LastIndex = INDEX_NONE;
	float RoamSecondsLeft = 60.f;
	double LastLineSeconds = -1000.0;
	bool bWasInFight = false;
	bool bLowHealthArmed = true;
	FVector AnnouncedObjective = FVector(TNumericLimits<float>::Max());
};
