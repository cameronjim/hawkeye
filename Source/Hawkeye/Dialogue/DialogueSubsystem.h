#pragma once

#include "CoreMinimal.h"
#include "Dialogue/DialogueQueue.h"
#include "Subsystems/WorldSubsystem.h"
#include "DialogueSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDialogueLineStartedSignature, FName, Row, FName, Speaker, FText, Text);

/**
 * Every spoken line in a running world (claude-docs/gameplay-semantics.md, "Dialogue"): the
 * subtitle with the speaker's name, the line's audio when it has any, a queue so two lines never
 * talk over each other, and walk-and-talk sequences from DT_DialogueSequences that carry on through
 * traversal and wait out fights (AHawkeyeGameMode::IsWorldInCombat). Banter speaks through
 * PresentLine and keeps quiet while this is busy.
 *
 * The rules are in UDialogueQueue. Played sequences are mirrored into UHawkeyeCampaignState.
 * Ticks only while the game is not paused, so a menu or the phone holds a sequence where it is.
 */
UCLASS()
class HAWKEYE_API UDialogueSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dialogue", meta = (WorldContext = "WorldContextObject"))
	static UDialogueSubsystem* Get(const UObject* WorldContextObject);

	/** Speaks DT_Dialogue's row now, or after whatever is already queued. False for an unknown row. */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	bool PlayLine(FName RowName);

	/**
	 * Queues a walk-and-talk sequence. False for an unknown sequence, one already queued, or one
	 * this campaign has already played (bEvenIfPlayed overrides that).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	bool PlaySequence(FName SequenceName, bool bEvenIfPlayed = false);

	/**
	 * Shows Speaker's Text as the subtitle for Seconds and plays Audio if given, without the queue.
	 * The banter component calls this.
	 */
	void PresentLine(FName Speaker, const FText& Text, float Seconds, USoundBase* Audio);

	/** A scripted line is speaking or queued. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool IsBusy() const { return Queue && Queue->IsBusy(); }

	UFUNCTION(BlueprintPure, Category = "Dialogue")
	UDialogueQueue* GetQueue() const { return Queue; }

	/** Writes the played sequences into the campaign state. */
	void MirrorToCampaign() const;

	void RestoreFromCampaign();

	/** When set (tests, screenshots), the fight check uses this instead of the world. */
	TOptional<bool> CombatOverride;

	UPROPERTY(BlueprintAssignable, Category = "Dialogue")
	FOnDialogueLineStartedSignature OnLineStarted;

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

	/** Runs the queue and presents whatever starts. */
	void Step(float DeltaSeconds);

	/** The HUD subtitle and the audio for a row that just started. */
	void PresentRow(FName Row);

	bool IsInCombat() const;

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	UPROPERTY(Transient)
	TObjectPtr<UDialogueQueue> Queue = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> VoiceAudio = nullptr;
};
