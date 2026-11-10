#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/HawkeyeMusicRules.h"
#include "HawkeyeMusicSubsystem.generated.h"

class AThugCharacter;
class APawn;
class UAudioComponent;
class USoundBase;

/**
 * The combat music (claude-docs/gameplay-semantics.md, "Audio", music). Plays MS_Music_Score, one looping
 * MetaSound with every layer on one clock, from the first tick of a game world, and every frame reads the
 * thugs, the local pawn, the crime and the challenge subsystems (GatherInputs), runs FHawkeyeMusicDirector
 * and fades the score's five gain inputs toward the state's layers. MS_Music_Win plays on the win.
 *
 * Each transition logs "Music: <from> -> <to>" on LogHawkeye at Log. Both sounds are soft paths set here,
 * so no map or Blueprint needs to carry them. With no audio device (tests) the rules still run and nothing
 * plays.
 */
UCLASS()
class HAWKEYE_API UHawkeyeMusicSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UHawkeyeMusicSubsystem();

	static UHawkeyeMusicSubsystem* Get(const UObject* WorldContext);

	//~ Begin UWorldSubsystem / FTickableGameObject
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override;
	//~ End UWorldSubsystem / FTickableGameObject

	/**
	 * What the music reads for Player now. Adds every thug alerted and standing to this fight's engaged
	 * list, and counts that list's standing and down (dead, limp or destroyed). Null Player: no distance,
	 * no crime nearby, not downed.
	 */
	FHawkeyeMusicInputs GatherInputs(const APawn* Player);

	/** Steps the director with Inputs, logs and stings a change, fades the layers and sets the score's gains. */
	void Advance(const FHawkeyeMusicInputs& Inputs, float DeltaSeconds);

	EHawkeyeMusicState GetState() const { return Director.GetState(); }

	const FHawkeyeMusicLayers& GetLayers() const { return Layers; }

	const FHawkeyeMusicDirector& GetDirector() const { return Director; }

	/**
	 * Holds (or lets go of) the score and the win sting. Tick calls it with the world's pause every frame, so a
	 * pause screen stops the sound as well as the state machine; the state and the layers are kept for the resume.
	 */
	void SetScorePaused(bool bPaused);

	/** A pause screen is holding the music. */
	bool IsScorePaused() const { return bScorePaused; }

	/** How many times the state has changed in this world. */
	int32 GetTransitionCount() const { return TransitionCount; }

	/** Thugs this fight has seen alerted (weak; forgotten when the music reaches Roam). */
	int32 GetEngagedCount() const { return Engaged.Num(); }

	/** The score: one loop, gains PadGain, PulseGain, PercGain, MotifGain, DroneGain. */
	UPROPERTY(EditAnywhere, Category = "Music")
	TSoftObjectPtr<USoundBase> ScoreSound;

	/** The 2 s resolve when the last thug of a fight goes down. */
	UPROPERTY(EditAnywhere, Category = "Music")
	TSoftObjectPtr<USoundBase> WinSound;

private:
	void StartScore();
	void PushLayers();

	FHawkeyeMusicDirector Director;
	FHawkeyeMusicLayers Layers;
	FHawkeyeMusicLayers PushedLayers;

	TArray<TWeakObjectPtr<AThugCharacter>> Engaged;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Score = nullptr;

	/** The last win sting, held with the score under a pause. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WinSting = nullptr;

	bool bScorePaused = false;

	bool bScoreTried = false;
	bool bPushedOnce = false;
	int32 TransitionCount = 0;
};
