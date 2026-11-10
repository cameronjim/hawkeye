#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Settings/HawkeyeSettings.h"
#include "HawkeyeHints.generated.h"

/**
 * What the world has to show for a first-time hint to come up (claude-docs/gameplay-semantics.md, HUD,
 * "First-time hints"). UHawkeyeHintSubsystem senses each one every frame; the rules only name them.
 * New values go on the end: a rule table refers to them by value.
 */
UENUM(BlueprintType)
enum class EHawkeyeHintTrigger : uint8
{
	None,
	/** Moving on the ground, not sprinting, nothing to climb within 3 m ahead, no fight. */
	WalkingOpenGround,
	/** Moving, with an obstacle a vault takes within 3 m ahead. */
	NearVaultable,
	/** Facing a wall whose top is 150 to 260 cm up, within 2 m (a mantle or a ledge grab). */
	FacingClimbableWall,
	/** On the ground at an edge she could drop to a hang from (a roof edge, a landing's rail). */
	AtRoofEdge,
	/** The grapple has a target a press fires at (the green diamond), not mid-zip. */
	GrappleReady,
	/** Mid-zip, past the chain point, with another anchor ready. */
	ChainAvailable,
	/** A standing thug 8 to 40 m away in the middle of the view, in line of sight, with a bow on her. */
	ThugAtRange,
	/** A standing thug within 3 m. */
	ThugClose,
	/** A thug within 3 m winding up a strike a parry answers (the telegraph "!"). */
	ParryTelegraph,
	/** She lost health this frame with a fight on. */
	TookHit,
	/** A knocked-down (or parry-staggered) thug within 8 m that a finisher would take. */
	FinisherOpen,
	/** A takedown is on: an unalerted thug in reach from behind. */
	TakedownReady,
	/** A trick arrow (slots 3 to 7) came into the quiver. */
	TrickArrowGained,
	/** The partner within 5 m of her with a fight on and switching allowed. */
	PartnerNearInFight,
	/** An unread text on the phone. */
	UnreadText,
	/** 40 m from where she started with no objective marker on screen, or a "nearby" toast just came up. */
	FarWithoutMarker,
	/** Within 15 m of a safehouse door. */
	NearSafehouse,
	/** Within 10 m of a challenge pedestal, no challenge running. */
	NearPedestal,
	/** Both safehouses are found (fires once, when the second one is). */
	BothSafehousesFound,
	/** A street crime is on and she is more than 15 m from it. */
	CrimeActive,
};

/**
 * What the player does that a hint teaches: doing it takes the hint down, or, before it has shown, means she
 * knows and it never shows. New values go on the end.
 */
UENUM(BlueprintType)
enum class EHawkeyeHintAction : uint8
{
	None,
	Sprinted,
	Vaulted,
	/** A mantle, a ledge grab or a climb. */
	Climbed,
	DroppedToHang,
	Zipped,
	Chained,
	/** A bow draw ended (loosed or let down). */
	FiredArrow,
	Struck,
	Parried,
	Dodged,
	Finished,
	TookDown,
	OpenedWheel,
	Switched,
	OpenedPhone,
	OpenedMap,
	/** A safehouse menu opened. */
	EnteredSafehouse,
	StartedChallenge,
	FastTravelled,
};

/** One first-time hint: when it shows, what it says, what takes it down. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeHintRule
{
	GENERATED_BODY()

	/** Saved with the campaign by this id ("sprint"). Never rename one: the count would start again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	FName Id;

	/** The line, with key tokens: {Sprint} {Jump} {Grapple} {Draw} {Aim} {Strike} {Dodge} {Crouch} {Interact}
	 *  {Takedown} {Phone} {Map} {Switch} {Wheel}, and {Partner} for the partner's name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	FText Text;

	/** The line when the sprint button toggles on the device in use (only the sprint hint has one). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	FText ToggleText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	EHawkeyeHintTrigger Trigger = EHawkeyeHintTrigger::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	EHawkeyeHintAction Dismiss = EHawkeyeHintAction::None;

	/** Higher goes first when two are waiting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	int32 Priority = 0;

	/** Takes the line from a lower hint at once, not after it has had MinShowSeconds (a parry cannot wait). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	bool bUrgent = false;

	/** The trigger has to hold this long before the hint shows, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (ClampMin = "0.0"))
	float HoldSeconds = 0.f;

	/** The hint stays this long after the trigger clears (and waits in the queue this long), s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (ClampMin = "0.0"))
	float LingerSeconds = 6.f;

	/** Times it shows in a campaign. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (ClampMin = "1"))
	int32 MaxShows = 1;
};

/** A run of a resolved hint line: plain text, or a key drawn as a keycap. */
struct HAWKEYE_API FHawkeyeHintSegment
{
	FString Text;
	bool bKey = false;
};

/** What a line resolves against. */
struct HAWKEYE_API FHawkeyeHintContext
{
	/** The last input came from a pad: pad names ("RB") rather than keys ("Q"). */
	bool bGamepad = false;
	EHawkeyeSprintMode SprintMode = EHawkeyeSprintMode::Default;
	/** The partner's name for {Partner}. */
	FString PartnerName = TEXT("Clint");
};

/**
 * The hint table. The C++ defaults are the shipped set (the CDO carries them, so nothing has to be built by a
 * script); a data asset of this class could replace them without code. GetRules() is what the game reads.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UHawkeyeHintRules : public UDataAsset
{
	GENERATED_BODY()

public:
	UHawkeyeHintRules();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hints")
	TArray<FHawkeyeHintRule> Rules;

	/** The shipped table (the class default object's). */
	static const TArray<FHawkeyeHintRule>& GetDefaultRules();

	/** The rule with Id in Rules, or null. */
	static const FHawkeyeHintRule* FindRule(const TArray<FHawkeyeHintRule>& InRules, FName Id);

	/** The key name a token stands for on a device ("Sprint", pad: "L3"); empty for an unknown token. */
	static FString GetKeyName(const FString& Token, bool bGamepad);

	/**
	 * Rule's line for Context: the toggle line when the sprint button toggles on that device, the tokens replaced
	 * by key names. Keys come back as their own segments; an unknown token is left as written ("{Foo}").
	 */
	static TArray<FHawkeyeHintSegment> Resolve(const FHawkeyeHintRule& Rule, const FHawkeyeHintContext& Context);

	/** Resolve as one string, keys in brackets: "Hold [Shift] to sprint". */
	static FString ResolveToString(const FHawkeyeHintRule& Rule, const FHawkeyeHintContext& Context);

	/** Tokenises Line (no rule): what Resolve does after it picks the line. */
	static TArray<FHawkeyeHintSegment> ResolveLine(const FString& Line, const FHawkeyeHintContext& Context);
};

/** Everything that keeps hints off the screen. Any one of them hides the line (and freezes its clocks). */
struct HAWKEYE_API FHawkeyeHintSuppression
{
	/** A chapter beat: the title card, a close-up, the end card, a flashback. */
	bool bChapterBeat = false;
	/** Any menu or full screen: pause, settings, inventory, phone, map, safehouse menu, the quiver wheel. */
	bool bMenu = false;
	bool bDowned = false;
	bool bChallengeResults = false;

	bool Any() const { return bChapterBeat || bMenu || bDowned || bChallengeResults; }
};

/** One frame of the queue: which hint (if any) came up, went down, or was learned without showing. */
struct HAWKEYE_API FHawkeyeHintStep
{
	FName Started;
	FName Ended;
	/** Rules whose action the player did (a shown one included): she knows it; never show it again. */
	TArray<FName> Learned;
};

/**
 * The hint queue (claude-docs/gameplay-semantics.md, HUD, "First-time hints"). Pure: fed each frame the
 * triggers that hold, the actions the player did, whether anything suppresses hints and whether they are on,
 * it keeps one line on the screen at most.
 *
 * A rule arms when its trigger has held HoldSeconds and it has shows left, and stays armed until LingerSeconds
 * after the trigger clears. The shown hint is the armed one of highest priority; a higher one takes the line
 * once the shown one has had MinShowSeconds, or at once when it is urgent. A shown hint goes down when its
 * action is done or it disarms. Suppression hides the line and freezes every clock, so a trigger that fired
 * under a menu waits for it to close. Doing a rule's action at any time counts it learned.
 */
class HAWKEYE_API FHawkeyeHintQueue
{
public:
	/** A shown hint keeps the line this long before a higher non-urgent one takes it, s. */
	static constexpr float MinShowSeconds = 1.5f;

	/** Bit for a trigger or an action in Tick's masks. */
	static uint64 Bit(EHawkeyeHintTrigger Trigger) { return 1ull << static_cast<uint8>(Trigger); }
	static uint64 Bit(EHawkeyeHintAction Action) { return 1ull << static_cast<uint8>(Action); }

	/** Starts over on Rules (kept by reference: it must outlive the queue). */
	void Reset(const TArray<FHawkeyeHintRule>& InRules);

	/**
	 * One frame. GetShows answers a rule's saved show count (learned rules answer MaxShows). The caller saves
	 * the step: +1 show for Started, MaxShows for each Learned.
	 */
	FHawkeyeHintStep Tick(float DeltaSeconds, uint64 Triggers, uint64 Actions, bool bSuppressed, bool bEnabled,
		TFunctionRef<int32(FName)> GetShows);

	/** The rule on the line (shown or held back by suppression), or null. */
	const FHawkeyeHintRule* GetCurrent() const;

	/** The line is on the screen now: a current rule, not suppressed, hints on. */
	bool IsVisible() const { return CurrentIndex != INDEX_NONE && bVisible; }

	/** Seconds the current hint has been on the screen (frozen while suppressed). */
	float GetCurrentAge() const { return CurrentAge; }

	/** Armed rules waiting for the line, most relevant first. */
	TArray<FName> GetWaiting() const;

private:
	struct FRuleState
	{
		float HeldSeconds = 0.f;
		float SinceClearSeconds = 0.f;
		bool bArmed = false;
		/** Shown once already with shows left: the trigger has to clear before it arms again. */
		bool bNeedsRearm = false;
		bool bDone = false;
	};

	void EndCurrent(FHawkeyeHintStep& Step, TFunctionRef<int32(FName)> GetShows);

	const TArray<FHawkeyeHintRule>* Rules = nullptr;
	TArray<FRuleState> States;
	int32 CurrentIndex = INDEX_NONE;
	float CurrentAge = 0.f;
	bool bVisible = false;
};
