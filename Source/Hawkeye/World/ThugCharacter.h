#pragma once

#include "CoreMinimal.h"
#include "Combat/MeleeComponent.h"
#include "Combat/StrikePose.h"
#include "Combat/Takedownable.h"
#include "GameFramework/Character.h"
#include "ISpudObject.h"
#include "World/ThugTypes.h"
#include "ThugCharacter.generated.h"

class APickupActor;
class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class UBowComponent;
class UEnvQuery;
class UHealthComponent;
class UHitReactionComponent;
class UPointLightComponent;
class UNiagaraSystem;
class USoundBase;
class UStateTree;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class UWeaponComponent;

/** What a thug currently believes about the player. See claude-docs/gameplay-semantics.md. */
UENUM(BlueprintType)
enum class EThugAlertState : uint8
{
	/** Patrolling. Takedown valid, does not shoot. */
	Calm,
	/** Heard or half-saw something. Investigates. Takedown still valid. */
	Suspicious,
	/** Has confirmed the player. Attacks. Takedown refused. */
	Alerted
};

/** What a thug fights with. Fists and Bat rush in and swing; Pistol and Bow keep their distance. */
UENUM(BlueprintType)
enum class EThugWeapon : uint8
{
	Fists,
	Bat,
	/** The gunner: hitscan pistol, 3-shot bursts of 12 after a 0.8 s telegraph, takes cover. */
	Pistol,
	/** Barney's archer (BP_Archer): his own UBowComponent, keeps 1500 to 2500 cm, relocates by zip. */
	Bow,
	/**
	 * The heavy (BP_Thug_Heavy): 200 HP, a riot shield on the left forearm that blocks arrows and light
	 * strikes from the front 120 degrees, a shield bash and a slow bat swing, walks at 300.
	 */
	Shield
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAlertStateChangedSignature, EThugAlertState, OldState, EThugAlertState, NewState);

/**
 * A Tracksuit thug: 100 HP, a weapon (fists, a bat, the gunner's pistol or an archer's bow), an alert
 * state, and a body that drops what it was carrying. Punches and arrows stagger him, Kate's heavy
 * knocks him down for KnockdownSeconds, and he ragdolls when he dies. TODO(stage3): the heavy.
 *
 * AThugAIController drives the state; this class owns the state itself so a Blueprint, a
 * takedown or a bullet can all read and change it without knowing about the controller.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AThugCharacter : public ACharacter, public ITakedownable, public ISpudObject, public ISpudObjectCallback
{
	GENERATED_BODY()

public:
	AThugCharacter();

	UFUNCTION(BlueprintPure, Category = "Thug")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Thug")
	UWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	/** The swing the Fists and Bat thugs use. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	UMeleeComponent* GetMeleeComponent() const { return MeleeComponent; }

	/** The hit-stop, the physical reaction to a blow and the knockout's throw and settle. */
	UHitReactionComponent* GetHitReactionComponent() const { return HitReactionComponent; }

	UFUNCTION(BlueprintPure, Category = "Thug")
	EThugAlertState GetAlertState() const { return AlertState; }

	/** Sets the alert state and broadcasts OnAlertStateChanged when it actually changed. */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void SetAlertState(EThugAlertState NewState);

	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsAlerted() const { return AlertState == EThugAlertState::Alerted; }

	// --- Combat ---------------------------------------------------------------------------------

	/** Fists, Bat or Pistol. Set per placed thug; the generator sets the district's four. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	EThugWeapon Weapon = EThugWeapon::Pistol;

	/** True for the Pistol thug: he keeps his distance and shoots instead of rushing. */
	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	bool IsGunner() const { return Weapon == EThugWeapon::Pistol; }

	/** True for the Shield thug (BP_Thug_Heavy): he blocks from the front and only a heavy hit staggers him. */
	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	bool IsHeavy() const { return Weapon == EThugWeapon::Shield; }

	/** True for the Bow thug (BP_Archer): he keeps range, draws with BowComponent, relocates by zip. */
	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	bool IsArcher() const { return Weapon == EThugWeapon::Bow; }

	/** The archer's bow. Present on every thug and inert until it has OwnBow (BP_Archer sets it). */
	UFUNCTION(BlueprintPure, Category = "Thug")
	UBowComponent* GetBowComponent() const { return BowComponent; }

	/**
	 * The StateTree that decides what he does: ST_Thug, built headless by UHawkeyeThugTreeBuilder. Unset (a thug
	 * spawned from C++), AThugAIController runs its DefaultStateTree, the same ST_Thug.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|AI")
	TObjectPtr<UStateTree> ThugStateTree;

	/** EQS_CoverPoints: the gunner's cover search. Without one the controller rings candidates in C++. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|AI")
	TObjectPtr<UEnvQuery> CoverQuery;

	// --- Weapon look and telegraph ------------------------------------------------------------------

	/**
	 * The gunner raises his pistol toward AimPoint (the telegraph and the burst) or lowers it to his
	 * side. No animation exists, so the pistol floats out in front of the right shoulder, as the bow does.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug|Combat")
	void SetWeaponRaised(bool bRaised, FVector AimPoint);

	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	bool IsWeaponRaised() const { return bWeaponRaised; }

	/** The glint on the muzzle (gunner) or on the nocked arrow's tip (archer) that says a shot is coming. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Combat")
	void SetTelegraphGlint(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	bool IsTelegraphGlintOn() const { return bGlintOn; }

	/** Where the glint sits now: the pistol's muzzle or the arrow tip. */
	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	FVector GetGlintLocation() const;

	UFUNCTION(BlueprintPure, Category = "Thug|Components")
	UPointLightComponent* GetTelegraphLight() const { return TelegraphLight; }

	UFUNCTION(BlueprintPure, Category = "Thug|Components")
	UStaticMeshComponent* GetPistolComponent() const { return PistolComponent; }

	/** Glint colour for the gunner's muzzle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FLinearColor PistolGlintColor = FLinearColor(1.f, 0.85f, 0.55f);

	/** Glint colour for the archer's arrow tip: Barney's purple. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FLinearColor BowGlintColor = FLinearColor(0.6f, 0.15f, 1.f);

	/** The glint light's brightness, lumens. Small: a glint, not a lamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat", meta = (ClampMin = "0.0"))
	float GlintLumens = 120.f;

	/** The glint bead's emissive intensity (M_Emissive's Intensity), so it reads from the next roof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat", meta = (ClampMin = "0.0"))
	float GlintEmissive = 40.f;

	/** Raised pistol position from the right upper arm, in the aim frame (X along the aim), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FVector PistolRaisedOffset = FVector(55.f, 4.f, -6.f);

	/**
	 * The swing for his weapon: BatAttack for a Bat thug, FistsAttack for fists, and for the heavy the
	 * shield bash on even swings (SwingIndex 0, 2, ...) and the slow bat swing on odd ones. With ComboAttacks set,
	 * ComboAttacks[SwingIndex % its length] whatever the weapon.
	 */
	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	FHawkeyeMeleeAttack GetMeleeAttack(int32 SwingIndex = 0) const;

	/**
	 * A combo in place of the weapon's swing (a boss's move set, set by UBossFightSubsystem from his definition).
	 * Empty: the weapon's.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	TArray<FHawkeyeMeleeAttack> ComboAttacks;

	/**
	 * A finisher on him takes this fraction of his max health (killing him only when that is all he has) instead of
	 * everything: a boss. 0, a thug: lethal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FinisherDamageFraction = 0.f;

	/**
	 * Kate's heavy strike does not put him on the floor (Knockdown leans him back and nothing more): a boss stands his
	 * ground, so his finisher window is earned (winded, a parry), not a heavy away. A blast or a bola (KnockdownFor)
	 * still does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	bool bResistsKnockdown = false;

	/**
	 * A finisher takes him only inside a window GrantFinisherWindow opened (a boss's winded window): a parry, a
	 * knockdown or a combo ender opens nothing (UBossRules::IsFinisherOpen). Off, a thug: any of them does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	bool bFinisherNeedsGrantedWindow = false;

	/** A parry staggers him at most this long, s (a boss: brief). 0: as long as the parry says (her 1.5 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat", meta = (ClampMin = "0.0"))
	float MaxParryStaggerSeconds = 0.f;

	/** The heavy's shield bash: 30 damage after a 0.8 s telegraph, knocks her back 250 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FHawkeyeMeleeAttack ShieldBashAttack;

	/** The heavy's bat: 25 damage after a slow 1.0 s wind-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FHawkeyeMeleeAttack HeavySwingAttack;

	// --- Shield (the heavy) -----------------------------------------------------------------------------

	/** Width of the front arc the shield covers, degrees, centred on where he faces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Shield", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float ShieldArcDegrees = 120.f;

	/** How long a heavy strike (or anything else that gets through his guard) staggers him, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Shield", meta = (ClampMin = "0.0"))
	float GuardBreakSeconds = 1.f;

	/** The slab, in the actor's frame: X thickness, Y width, Z height, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Shield")
	FVector ShieldSize = FVector(5.f, 60.f, 110.f);

	/** The shield's centre in guard, held up on the left forearm, in the actor's frame, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Shield")
	FVector ShieldGuardOffset = FVector(40.f, -16.f, 6.f);

	/** How far forward the shield drives at the end of the bash, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Shield", meta = (ClampMin = "0.0"))
	float ShieldBashReach = 35.f;

	/** Smoked grey polycarbonate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Shield")
	FLinearColor ShieldColor = FLinearColor(0.06f, 0.07f, 0.08f);

	/** True when ToAttacker (from him toward the hit's source) lies inside the front ArcDegrees of Facing. Pure. */
	static bool IsInShieldArc(const FVector& Facing, const FVector& ToAttacker, float ArcDegrees);

	/** The heavy's shield is up: alive, on his feet, not staggered or getting up. */
	UFUNCTION(BlueprintPure, Category = "Thug|Shield")
	bool IsShieldRaised() const;

	/** Whether a hit coming from FromDirection (him toward its source) meets the raised shield. */
	UFUNCTION(BlueprintPure, Category = "Thug|Shield")
	bool BlocksHitFrom(const FVector& FromDirection) const;

	/**
	 * An arrow or a light strike (What, for the log) from Attacker, coming from FromDirection: true when the
	 * shield takes it. A block costs nothing, rings the shield, and turns him on the attacker.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug|Shield")
	bool TryBlock(AActor* Attacker, FVector FromDirection, const FString& What);

	/** Hits the shield has taken. */
	UFUNCTION(BlueprintPure, Category = "Thug|Shield")
	int32 GetBlockCount() const { return BlockCount; }

	/**
	 * Where a line from Start along Direction meets the shield's face, kept inside the slab: where a blocked
	 * arrow sticks. False when he has no shield.
	 */
	bool ComputeShieldImpact(const FVector& Start, const FVector& Direction, FVector& OutPoint) const;

	UFUNCTION(BlueprintPure, Category = "Thug|Components")
	UStaticMeshComponent* GetShieldComponent() const { return ShieldComponent; }

	/** Places the shield in guard, or driving forward in a bash. Called from Tick. */
	void UpdateShieldPose();

	// --- Readability ----------------------------------------------------------------------------------

	/** The "!" or "?" over his head right now, or 0. */
	TCHAR GetAlertGlyph() const { return AlertGlyph.GetGlyph(); }

	/** Seconds the glyph has left. */
	UFUNCTION(BlueprintPure, Category = "Thug|Readability")
	float GetAlertGlyphRemaining() const { return AlertGlyph.GetRemaining(); }

	/** The thin health bar's opacity for a viewer Distance away (0 unhurt, beyond 1500 cm, or 3.5 s after the last hit). */
	UFUNCTION(BlueprintPure, Category = "Thug|Readability")
	float GetHealthBarAlpha(float Distance) const;

	/** His capsule's top plus 28 cm: where the overhead stack sits for a thug with no head bone. */
	UFUNCTION(BlueprintPure, Category = "Thug|Readability")
	FVector GetOverheadLocation() const;

	/**
	 * Where the overhead stack's bottom sits: GlyphHeadClearance over his head bone (GetOverheadLocation when he
	 * has none). The health bar sits on it; the parry line and the glyph stack above the bar when it shows.
	 */
	UFUNCTION(BlueprintPure, Category = "Thug|Readability")
	FVector GetGlyphLocation() const;

	/** How far over his head bone the overhead stack sits, cm: about the top of his head, whatever his capsule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Readability", meta = (ClampMin = "0.0"))
	float GlyphHeadClearance = 20.f;

	/** Moves the glyph clock on. Called from Tick; public for tests. */
	void AdvanceReadability(float DeltaSeconds);

	/** 15 damage after a 0.6 s telegraphed wind-up, then 0.6 s to recover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FHawkeyeMeleeAttack FistsAttack;

	/** 25 damage after the same wind-up, a little more reach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FHawkeyeMeleeAttack BatAttack;

	/** The bat he carries; shown in his right hand only when Weapon is Bat. Assigned in BP_Thug. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	TObjectPtr<UStaticMesh> BatMesh;

	/** Shows the bat for a Bat thug and hides it for anyone else. Runs at BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Combat")
	void RefreshHeldWeapon();

	UFUNCTION(BlueprintPure, Category = "Thug|Components")
	UStaticMeshComponent* GetHeldWeaponComponent() const { return HeldWeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "Thug|Components")
	UStaticMeshComponent* GetHeadwearComponent() const { return Headwear; }

	/**
	 * Puts the headwear on his head bone, upright and facing his way with HeadwearOffset (his frame, cm) from the bone,
	 * whatever the skeleton's bone axes are. BeginPlay does it; nothing happens with no headwear mesh.
	 */
	void FitHeadwear();

	/**
	 * Which way the bat points from the grip, in world space, for the swing he is (or is not) in:
	 * hanging down along the leg (BatHangDirection) while he walks, up to horizontal behind him
	 * (BatCockedDirection) over the wind-up, across to horizontal in front (BatSwungDirection) in
	 * the first BatSwingSeconds of the recovery, and back down over the rest of it.
	 */
	UFUNCTION(BlueprintPure, Category = "Thug|Combat")
	FVector ComputeBatDirection() const;

	/** Places the bat in his right hand along ComputeBatDirection, gripped near its end. Called from Tick. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Combat")
	void UpdateHeldWeaponPose();

	/** The bat hanging from the hand while walking, in the actor's frame (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FVector BatHangDirection = FVector(0.2f, 0.18f, -1.f);

	/** The bat at the end of the wind-up: out behind him, level, in the actor's frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FVector BatCockedDirection = FVector(-0.45f, 1.f, 0.1f);

	/** The bat at the end of the swing: across in front of him, level, in the actor's frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat")
	FVector BatSwungDirection = FVector(0.7f, -1.f, -0.05f);

	/** How long the swing from behind to in front takes, at the start of the recovery, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat", meta = (ClampMin = "0.01"))
	float BatSwingSeconds = 0.15f;

	/** How far from the bat's end the hand grips it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Combat", meta = (ClampMin = "0.0"))
	float BatGripFromEnd = 8.f;

	// --- Hit reactions --------------------------------------------------------------------------

	/**
	 * A hit he survives (an arrow, a punch): a brief stagger. He stops, is shoved HitShoveSpeed
	 * away from HitBy, drops any swing he was winding up, his AI holds off for StaggerSeconds, and
	 * he is Alerted if he was not. Bound to the health component's OnStaggered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void HitReaction(AActor* HitBy);

	/** True for StaggerSeconds after a HitReaction. The AI does nothing while it is. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsStaggered() const { return StaggerRemaining > 0.f; }

	/** Seconds of stagger left. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	float GetStaggerRemaining() const { return StaggerRemaining; }

	/**
	 * Kate's heavy: he goes over (a ragdoll when the mesh can, otherwise only the state), stays down
	 * KnockdownSeconds, then stands back up where his body landed: the capsule moves under the
	 * body and the ragdoll pose blends into the animated one over GetUpSeconds (the physics blend
	 * weight going 1 to 0). His swing is dropped, his AI holds off until he is up, and he is Alerted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void Knockdown(AActor* By);

	/**
	 * Knockdown for Seconds instead of KnockdownSeconds, thrown at LaunchSpeed (below 0 uses
	 * KnockdownLaunchSpeed). The bola's trip (2.5 s) and the explosive's blast (2 s) come through here.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void KnockdownFor(AActor* By, float Seconds, float LaunchSpeed = -1.f);

	/** Seconds this knockdown has left, 0 when he is not down. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	float GetKnockdownRemaining() const { return bKnockedDown ? KnockdownRemaining : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsKnockedDown() const { return bKnockedDown; }

	/** The ragdoll pose blending back into the animated one after a knockdown. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsGettingUp() const { return bGettingUp; }

	/** Staggered, on the floor or getting up: the AI does nothing. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsIncapacitated() const { return IsStaggered() || bKnockedDown || bGettingUp; }

	/** Counts a knockdown down and stands him up at the end. Called from Tick; public for tests. */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void UpdateKnockdown(float DeltaSeconds);

	/**
	 * A HitReaction held for Seconds instead of StaggerSeconds. Unlike a plain stagger it moves the heavy
	 * too (his guard is open for it). Nothing while he is down or dead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void StaggerFor(AActor* By, float Seconds);

	/**
	 * Kate parried his swing or bash: the wind-up is dropped without landing and he is staggered for
	 * Seconds (her ParryStaggerSeconds, 1.5).
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void Parried(AActor* By, float Seconds);

	/**
	 * A combo ender (Kate's third light or a heavy) just landed on him: a finisher can take him for
	 * Seconds (her ComboEnderFinisherSeconds, 1.0), whether or not he is still staggered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void OpenFinisherWindow(float Seconds);

	/**
	 * True when a finisher can take him: knocked down, staggered by a parry, or inside a combo ender's
	 * window, or a granted window; with bFinisherNeedsGrantedWindow only the granted window counts
	 * (UBossRules::IsFinisherOpen). An ordinary hit's 0.5 s stagger does not count.
	 */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsFinisherOpen() const;

	/** Seconds left of a combo ender's finisher window, 0 when none is open. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	float GetFinisherWindowRemaining() const { return FinisherWindowRemaining; }

	/** Shuts the windows OpenFinisherWindow and GrantFinisherWindow opened (a boss's winded window after the finisher that used it). */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void CloseFinisherWindow() { FinisherWindowRemaining = 0.f; GrantedFinisherRemaining = 0.f; }

	/**
	 * A window only the fight grants (a boss's winded window): the one a bFinisherNeedsGrantedWindow thug can be finished
	 * in. Any thug is open in it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void GrantFinisherWindow(float Seconds);

	UFUNCTION(BlueprintPure, Category = "Thug")
	float GetGrantedFinisherRemaining() const { return GrantedFinisherRemaining; }

	/** Staggered by a parry and not yet recovered from it. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsParryStaggered() const { return ParryStaggerRemaining > 0.f && IsStaggered(); }

	/** Counts the finisher window and the parry stagger down. Called from Tick; public for tests. */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void UpdateFinisherOpening(float DeltaSeconds);

	/** Stretches a stagger he is already in to at least Seconds, quietly (a finisher closing in). */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void HoldStagger(float Seconds);

	/**
	 * A finisher's blow: lethal damage from By whatever his shield or health (a boss: FinisherDamageFraction of his
	 * max health), then his ragdoll thrown at Velocity (cm/s) if it is simulating. True when this killed him.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	bool ReceiveFinisher(AActor* By, FVector Velocity);

	/** The short lean away from the last hit (the bow-IK graph's spine_01). */
	const FHawkeyeHitLeanClock& GetHitLean() const { return HitLean; }

	/** Where the last hit that cost him health came from. */
	UFUNCTION(BlueprintPure, Category = "Thug|Hit")
	EHawkeyeHitDirection GetLastHitDirection() const { return LastHitDirection; }

	/** Starts the lean from a hit by HitBy (from in front, behind, left or right of him). */
	void StartHitLean(const AActor* HitBy);

	/** Advances the hit lean and hands it to the bow-IK anim instance, if the mesh runs one. */
	void UpdateHitLean(float DeltaSeconds);

	/** How long a hit reaction holds him, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Hit", meta = (ClampMin = "0.0"))
	float StaggerSeconds = 0.5f;

	/** Speed of the shove away from whoever hit him, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Hit", meta = (ClampMin = "0.0"))
	float HitShoveSpeed = 250.f;

	/** Seconds a heavy keeps him on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Hit", meta = (ClampMin = "0.0"))
	float KnockdownSeconds = 3.f;

	/** How long the ragdoll pose takes to blend back into the animated one when he gets up, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Hit", meta = (ClampMin = "0.01"))
	float GetUpSeconds = 0.4f;

	/** Speed the body is thrown away from the hit at, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Hit", meta = (ClampMin = "0.0"))
	float KnockdownLaunchSpeed = 450.f;

	/** Pulses the body's emissive for HitFlashSeconds. Every hit that costs him health calls it. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Hit")
	void FlashHit();

	/** 1 the instant he is hit, fading to 0 over HitFlashSeconds. */
	UFUNCTION(BlueprintPure, Category = "Thug|Hit")
	float GetHitFlashAlpha() const { return HitFlashSeconds > 0.f ? HitFlashRemaining / HitFlashSeconds : 0.f; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Hit", meta = (ClampMin = "0.0"))
	float HitFlashSeconds = 0.1f;

	// --- Patrol and loot ------------------------------------------------------------------------

	/** Points this thug walks between while Calm. Place ATargetPoints and fill this in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Patrol")
	TArray<TObjectPtr<AActor>> PatrolPoints;

	/** Seconds spent standing at each patrol point before moving on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Patrol", meta = (ClampMin = "0.0"))
	float PatrolWaitSeconds = 2.f;

	/**
	 * Pickups dropped when this thug goes down, by takedown or by bullet. Set per instance in
	 * the level (a thug can carry a keycard), not on the class.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Loot")
	TArray<TSubclassOf<APickupActor>> DropOnDeath;

	/**
	 * Pickups placed in the level that he carries (an interior's keycard, generate_interior.py): out of
	 * sight and reach from BeginPlay, dropped at his feet with DropOnDeath when he goes down.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Loot")
	TArray<TObjectPtr<APickupActor>> CarriedPickups;

	/** His crew. His squad alert reaches only thugs of the same group; None reaches only None. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Squad")
	FName AlertGroup;

	/**
	 * The archer's own eyes see all round (a lookout on a roof). Off, as indoors: while Calm he sees only
	 * inside his sight cone, like any thug, and all round once Suspicious or Alerted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Archer")
	bool bArcherSeesAllRound = true;

	UPROPERTY(BlueprintAssignable, Category = "Thug")
	FOnAlertStateChangedSignature OnAlertStateChanged;

	/** Spawns everything in DropOnDeath around the thug's feet. Only ever runs once. */
	UFUNCTION(BlueprintCallable, Category = "Thug|Loot")
	void DropLoot();

	/**
	 * Ragdolls the mesh, disables the capsule and stops the AI. Safe to call twice.
	 *
	 * When the mesh cannot ragdoll - no physics asset, or one whose bodies do not match the
	 * skeleton, which makes InitArticulated find no root body and SetSimulatePhysics a no-op -
	 * the thug falls over procedurally instead. Killer only steers which way he goes down.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void GoLimp(AActor* Killer);

	/** True once GoLimp has run, whichever way the body went down. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsLimp() const { return bLimp; }

	/** True when the mesh is actually simulating physics, i.e. the real ragdoll path took. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsRagdolling() const;

	/** True while the procedural fallback is tipping the body over. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	bool IsCollapsing() const { return bCollapsing; }

	/** 0 upright, 1 flat on the floor. Stays 1 once the collapse has finished. */
	UFUNCTION(BlueprintPure, Category = "Thug")
	float GetCollapseAlpha() const;

	// --- Animation ------------------------------------------------------------------------------

	/**
	 * His combat clips (DA_AnimSet_Thug, DA_AnimSet_Archer): swings, hit reactions, knockdown and
	 * get-up, the finisher's victim, the archer's bow. Empty roles, or none at all, keep the procedural
	 * reactions (claude-docs/gameplay-semantics.md, "Combat animation clips").
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Thug|Animation")
	TObjectPtr<UCombatAnimSet> CombatAnimSet;

	/** Hands CombatAnimSet to the melee and bow components and loads it. BeginPlay calls it. */
	void ApplyCombatAnimSet();

	void SetCombatAnimSet(UCombatAnimSet* InSet) { CombatAnimSet = InSet; }

	/** Plays ClipRole's clip on him, at rate 1 or fitted to FitToSeconds; false when there is none that plays. */
	bool PlayCombatClip(ECombatAnimRole ClipRole, float FitToSeconds = 0.f);

	/** True while any montage plays on him (a swing, a reaction, the finisher's victim, a knockdown). */
	bool IsAnyCombatClipPlaying() const;

	/** How hard a takedown from behind pushes his body over (the knockout's weight, 0 to 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Hit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TakedownImpactWeight = 0.15f;

	/** True while he is down in (or getting up through) his knockdown clip rather than the ragdoll. */
	UFUNCTION(BlueprintPure, Category = "Thug|Animation")
	bool IsKnockdownFromClip() const { return bKnockdownClip || bGetUpFromClip; }

	/**
	 * Plays Idle, Walk or Run depending on ground speed, and only when the choice changes.
	 *
	 * The mannequin pack's AnimBP does not compile headless, so thugs drove nothing and stood
	 * in a T-pose. Sequences on the mesh's single-node animation slot are all a patrolling
	 * thug needs, and it is something a test can assert on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug|Animation")
	void UpdateLocomotionAnimation();

	/** RunAnim above RunAnimSpeedThreshold, WalkAnim above WalkAnimSpeedThreshold, IdleAnim below. */
	UFUNCTION(BlueprintPure, Category = "Thug|Animation")
	UAnimSequence* SelectLocomotionAnim() const;

	/** The sequence the mesh is currently playing, or null. */
	UFUNCTION(BlueprintPure, Category = "Thug|Animation")
	UAnimSequence* GetCurrentLocomotionAnim() const { return CurrentLocomotionAnim; }

	/** Idle pose. Assigned in BP_Thug from the mannequin pack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Animation")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Animation")
	TObjectPtr<UAnimSequence> WalkAnim;

	/** Played above RunAnimSpeedThreshold (the melee rush). Falls back to WalkAnim when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Animation")
	TObjectPtr<UAnimSequence> RunAnim;

	/** Optional. Only used when the mesh has no physics asset to ragdoll with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Animation")
	TObjectPtr<UAnimSequence> DeathAnim;

	/** Ground speed in uu/s above which the thug is walking rather than standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Animation", meta = (ClampMin = "0.0"))
	float WalkAnimSpeedThreshold = 20.f;

	/** Ground speed in uu/s above which the rush plays RunAnim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Animation", meta = (ClampMin = "0.0"))
	float RunAnimSpeedThreshold = 350.f;

	/**
	 * Loading a save made after this thug died: he goes down where he lies, with no loot drop.
	 * Routed through health so a thug group objective counts him like any other death.
	 */
	UFUNCTION(BlueprintCallable, Category = "Thug")
	void RestoreAsDead();

	//~ Begin ISpudObjectCallback interface (SPUD saves position and bSavedDead)
	virtual void SpudPreStore_Implementation(const USpudState* State) override;
	virtual void SpudPostRestore_Implementation(const USpudState* State) override;
	//~ End ISpudObjectCallback interface

	/**
	 * Spawned by a street crime (UCrimeSubsystem): left out of the save, since a crime is never saved
	 * part way and a respawned stranger would have no crime to belong to.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Thug")
	bool bSkipSave = false;

	/**
	 * A runner who cannot be stopped yet (a chase's thief before its beat, UChaseSubsystem): takedowns are refused.
	 * Off for every other thug.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Thug")
	bool bRefusesTakedown = false;

	//~ Begin ISpudObject interface
	virtual bool ShouldSkip_Implementation() const override { return bSkipSave; }
	//~ End ISpudObject interface

	//~ Begin ITakedownable interface
	virtual bool CanBeTakenDown_Implementation(AActor* Attacker) override;
	virtual void OnTakedown_Implementation(AActor* Attacker) override;
	//~ End ITakedownable interface

	// --- Audio (no speech: grunts and tells only) -------------------------------------------------

	/** The gunner's and archer's tell as the glint comes on (MS_Thug_Telegraph). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Audio")
	TSoftObjectPtr<USoundBase> TelegraphSound;

	/** Hit and still standing (MS_Thug_Hurt). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Audio")
	TSoftObjectPtr<USoundBase> HurtSound;

	/** Going down for good (MS_Thug_Death). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Audio")
	TSoftObjectPtr<USoundBase> DeathSound;

	/** The heavy's shield taking an arrow or a strike (MS_Melee_Block). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Audio")
	TSoftObjectPtr<USoundBase> BlockSound;

	/** A hit that staggers him (MS_Melee_Stagger). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Audio")
	TSoftObjectPtr<USoundBase> StaggerSound;

	/** His body meeting the ground, knocked down or dead (MS_Roll_Thump), GroundThudDelay after he goes over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Audio")
	TSoftObjectPtr<USoundBase> GroundThudSound;

	/** Grey slush thrown off the ground there (NS_KnockdownDust at GroundDustScale; the white landing puff did not show on snow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Readability")
	TSoftObjectPtr<UNiagaraSystem> GroundDustVfx;

	/** Seconds from going over to the body meeting the ground: a ragdoll (a blast, a death standing) is down by then. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Readability", meta = (ClampMin = "0.0"))
	float GroundThudDelay = 0.45f;

	/**
	 * The same for a knockdown played by his clip (a heavy, a trip), which takes longer to fall: AM_Knockdown_KnockedOut's
	 * pelvis comes within 35 cm of the ground 1.3 s after he goes over (knockdown_dust.png, 2026-09-29).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Readability", meta = (ClampMin = "0.0"))
	float KnockdownClipThudDelay = 1.3f;

	/** The delay the last thud was scheduled with (GroundThudDelay or KnockdownClipThudDelay): tests. */
	float GetScheduledThudDelay() const { return ScheduledThudDelay; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Readability", meta = (ClampMin = "0.0"))
	float GroundDustScale = 1.f;

	/** Thuds (and puffs) played so far: tests. */
	UFUNCTION(BlueprintPure, Category = "Thug|Readability")
	int32 GetGroundThudCount() const { return GroundThudCount; }

	/** True while a thud is waiting for the body to land. */
	UFUNCTION(BlueprintPure, Category = "Thug|Readability")
	bool IsGroundThudPending() const { return GroundThudRemaining > 0.f; }

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION()
	void HandleDeath(UHealthComponent* Health, AActor* Killer);

	UFUNCTION()
	void HandleStaggered(UHealthComponent* Health, AActor* DamageInstigator);

	UFUNCTION()
	void HandleHealthChanged(UHealthComponent* Health, float NewHealth, float Delta, AActor* DamageInstigator);

	/** Places the pistol (raised or lowered) and hides it for anyone but the gunner. Called from Tick. */
	void UpdatePistolPose();

	/** Moves the glint to the muzzle or the arrow tip. Called from Tick. */
	void UpdateTelegraphGlint();

	/** One dynamic material instance per slot, so his flashes are his alone. Runs at BeginPlay. */
	void CreateBodyMaterials();

	/** Writes the hit flash and the swing telegraph into the body's materials. Called from Tick. */
	void UpdateMaterialPulse(float DeltaSeconds);

	/** Throws the mesh into a ragdoll for a knockdown. False when it cannot simulate. */
	bool BeginKnockdownRagdoll(AActor* By, float LaunchSpeed);

	/**
	 * Ends a knockdown: the capsule moves to where the body lies and, when he was a ragdoll, the
	 * bodies keep simulating while their blend weight falls to 0 over GetUpSeconds.
	 */
	void StandUp();

	/** Moves the get-up blend on; at the end the bodies stop simulating. Called from Tick. */
	void UpdateGetUp(float DeltaSeconds);

	/** Puts the mesh back to an ordinary animated character mesh. */
	void FinishGetUp();

	/** The knockdown clip instead of the ragdoll. False when he has none that plays. */
	bool PlayKnockdownClip();

	/** Ends a clip knockdown: the get-up clip (or the knockdown clip blending out) and back on his feet. */
	void StandUpFromClip();

	/** Tells the brain (or, without one, the state) that By just hurt him. */
	void AlertTo(AActor* By);

	/** Starts the no-physics fallback: tip the mesh over and drop it, away from the killer. */
	void BeginProceduralCollapse(AActor* Killer);

	/** Writes one frame of the collapse onto the mesh. Called from Tick while bCollapsing. */
	void UpdateProceduralCollapse(float DeltaSeconds);

	/** Seconds the procedural collapse takes from upright to flat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Death", meta = (ClampMin = "0.01"))
	float CollapseSeconds = 0.6f;

	/** How far the body tips. 90 would be exactly flat; 85 leaves it looking dropped, not laid out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Death", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float CollapsePitchDegrees = 85.f;

	/** How far the mesh sinks over the collapse, so the body ends up on the floor not above it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thug|Death", meta = (ClampMin = "0.0"))
	float CollapseDropDistance = 20.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UMeleeComponent> MeleeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UHitReactionComponent> HitReactionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UBowComponent> BowComponent;

	/** The gunner's pistol: a dark slide with a grip under it (engine cubes). Hidden for anyone else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UStaticMeshComponent> PistolComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UStaticMeshComponent> PistolGrip;

	/** The telegraph glint: a small glowing bead plus a point light, off until a shot is coming. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UStaticMeshComponent> GlintMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UPointLightComponent> TelegraphLight;

	/** The heavy's riot shield: an engine cube slab on the left forearm. Hidden unless Weapon is Shield. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UStaticMeshComponent> ShieldComponent;

	/** The bat, in the right hand. Empty and hidden unless Weapon is Bat. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UStaticMeshComponent> HeldWeaponComponent;

	/** Socket the bat hangs off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	FName HeldWeaponSocketName = FName(TEXT("hand_r"));

	/**
	 * A greybox mask, hat or antlers on his head (a crew's look: chapter 2's reindeer). Empty and drawn as nothing on
	 * the stock thug; a Blueprint child sets its mesh. Its origin is the middle of his head, +X his facing, +Z up.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	TObjectPtr<UStaticMeshComponent> Headwear;

	/** The bone the headwear rides on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	FName HeadwearSocketName = FName(TEXT("head"));

	/** Where the headwear sits from that bone, in his own frame (forward, right, up), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Components")
	FVector HeadwearOffset = FVector(2.f, 0.f, 10.f);

	/** Scalar parameter on the body material pulsed by FlashHit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Hit")
	FName HitFlashParameter = FName(TEXT("HitFlash"));

	/** Scalar parameter held at 1 through a swing's wind-up: the mask glows red. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Hit")
	FName TelegraphParameter = FName(TEXT("Telegraph"));

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;

	/** Whichever of IdleAnim / WalkAnim / RunAnim is playing, so Tick only re-plays on a real change. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentLocomotionAnim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Thug")
	EThugAlertState AlertState = EThugAlertState::Calm;

	/** Offset applied to each dropped pickup so two drops do not land inside each other. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thug|Loot")
	float DropSpacing = 40.f;

private:
	/** Seconds of stagger left. */
	float StaggerRemaining = 0.f;

	/** Seconds left of a combo ender's finisher window, and of the last parry's stagger. */
	float FinisherWindowRemaining = 0.f;
	float GrantedFinisherRemaining = 0.f;
	float ParryStaggerRemaining = 0.f;

	/** The lean from the last hit, and where that hit came from. */
	FHawkeyeHitLeanClock HitLean;
	EHawkeyeHitDirection LastHitDirection = EHawkeyeHitDirection::Front;

	/** Hits the shield took. */
	int32 BlockCount = 0;
	bool bShieldTinted = false;

	/** The glyph over his head, and world seconds of the last hit that cost him health. */
	FHawkeyeAlertGlyph AlertGlyph;
	double LastDamagedSeconds = -1000.0;

	bool bWeaponRaised = false;
	FVector WeaponAimPoint = FVector::ZeroVector;
	bool bGlintOn = false;
	bool bGlintTinted = false;

	bool bKnockedDown = false;
	bool bKnockdownRagdoll = false;
	float KnockdownRemaining = 0.f;
	bool bGettingUp = false;
	float GetUpElapsed = 0.f;

	/** Down in the knockdown clip, not the ragdoll; getting up through a clip, and how long it takes. */
	bool bKnockdownClip = false;
	bool bGetUpFromClip = false;
	float GetUpClipSeconds = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> KnockdownMontage;

	TWeakObjectPtr<UAnimInstance> KnockdownInstance;
	float HitFlashRemaining = 0.f;

	/** Seconds until the body meets the ground after a knockdown or a death; 0 when none is waiting. */
	float GroundThudRemaining = 0.f;
	float ScheduledThudDelay = 0.f;
	int32 GroundThudCount = 0;

	/** Starts the countdown to the thud; a second fall restarts it. */
	void ScheduleGroundThud(float Delay);

	/** Counts it down; at 0 plays GroundThudSound and GroundDustVfx under his pelvis. */
	void UpdateGroundThud(float DeltaSeconds);

	/** What the materials were last given, so the parameters are only written on a change. */
	float LastFlashWritten = -1.f;
	float LastTelegraphWritten = -1.f;

	/** The mesh's mount on the capsule, restored when he stands back up. */
	FTransform MeshRelativeTransform = FTransform::Identity;

	/** Dead at the last save. Written by SpudPreStore, read by SpudPostRestore. */
	UPROPERTY(SaveGame)
	bool bSavedDead = false;

	bool bLootDropped = false;
	bool bLimp = false;

	/** Procedural collapse state. Only used when the mesh refused to simulate. */
	bool bCollapsing = false;
	float CollapseElapsed = 0.f;

	/** Where the mesh was standing when the collapse started; every frame is built off this. */
	FVector CollapseStartLocation = FVector::ZeroVector;
	FQuat CollapseStartRotation = FQuat::Identity;

	/** World axis the body rotates about, so his head goes down away from whoever killed him. */
	FVector CollapseAxis = FVector::RightVector;
};
