#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Audio/HawkeyeAudioTypes.h"
#include "Challenge/ChallengeTypes.h"
#include "Mission/MissionFlowController.h"
#include "Settings/HawkeyeSettings.h"
#include "Player/HawkeyeTapHold.h"
#include "HawkeyePlayerController.generated.h"

class SWidget;
class AHawkeyeCharacter;
class AHawkeyePartnerController;
class ASafehouse;
class UBanterComponent;
class UChallengeResultsWidget;
class UChapterTitleWidget;
class UPhoneWidget;
class UPlaytestPhotoMode;
class USnowfallComponent;
class UHawkeyeHudWidget;
class UHawkeyeInventoryWidget;
class UHawkeyeMapWidget;
class UHawkeyeMainMenuWidget;
class UHawkeyeDifficultyPromptWidget;
class UFlashbackReplayWidget;
class UHawkeyeSafehouseWidget;
class UHawkeyePauseWidget;
class UHawkeyeSettingsWidget;
class UFlashbackDefinition;
class UFlashbackWidget;
class UInputAction;
class UInputMappingContext;
class UMissionDefinition;
class UMissionEndCardWidget;
class UMissionFlowController;
class USoundBase;
class USoundClass;
class USoundMix;
struct FInputActionValue;
struct FInputKeyEventArgs;

/** Which list the safehouse menu is showing, so a picked row means the right thing. */
enum class ESafehouseList : uint8
{
	None,
	FastTravel,
	Chapters
};

/**
 * Owns the HUD and the flashback presentation: listens to the mission subsystem, plays the slideshow and
 * travels to the mission's NextLevel once it finishes.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API AHawkeyePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHawkeyePlayerController();

	/** UMG widget (reparented to UHawkeyeHudWidget) created on BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD")
	TSubclassOf<UHawkeyeHudWidget> HudWidgetClass;

	/** The live HUD, or nullptr when HudWidgetClass is unset. */
	UFUNCTION(BlueprintPure, Category = "HUD")
	UHawkeyeHudWidget* GetHawkeyeHud() const { return HudWidget; }

	/** Convenience for world actors: the local player's HUD, or nullptr. */
	UFUNCTION(BlueprintPure, Category = "HUD", meta = (WorldContext = "WorldContextObject"))
	static UHawkeyeHudWidget* GetHawkeyeHudFor(const UObject* WorldContextObject);

	/** UMG widget (reparented to UFlashbackWidget) used to present flashbacks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Flashback")
	TSubclassOf<UFlashbackWidget> FlashbackWidgetClass;

	/** Travel to the completed mission's NextLevel once the flashback ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Flashback")
	bool bOpenNextLevelAfterFlashback = true;

	/** Plays a flashback immediately (debug, or bespoke story beats). */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	UFlashbackWidget* PlayFlashback(UFlashbackDefinition* Flashback);

	// --- End card -------------------------------------------------------------------------------

	/** UMG widget (reparented to UMissionEndCardWidget) shown when a mission completes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "End card")
	TSubclassOf<UMissionEndCardWidget> EndCardWidgetClass;

	/**
	 * Level opened when a completed mission has no NextLevel, i.e. the campaign is over.
	 * Falls back to FallbackMenuLevel when this one does not exist.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "End card")
	FName MenuLevelName = FName(TEXT("/Game/Maps/L_MainMenu"));

	/** Used when MenuLevelName has not been built yet, so the campaign end never dead-ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "End card")
	FName FallbackMenuLevelName = FName(TEXT("/Game/Maps/L_District_EastVillage"));

	/** Shown on the final card, which waits for a key instead of counting down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "End card")
	FText FinalCardPrompt;

	/** The end-of-mission beat ordering. Created on BeginPlay and never null. */
	UFUNCTION(BlueprintPure, Category = "End card")
	UMissionFlowController* GetMissionFlow() const { return MissionFlow; }

	/**
	 * The end sequence's route for Mission: which beats it has (claude-docs/gameplay-semantics.md,
	 * "Chapter end"). bCloseUp says a close-up is running as it completes.
	 */
	UFUNCTION(BlueprintPure, Category = "End card")
	static FMissionFlowRoute MakeFlowRoute(const UMissionDefinition* Mission, bool bCloseUp);

	/**
	 * The camera blends to ViewTarget's camera for Seconds (blend in included), then back over
	 * BlendSeconds, with the player's move and look input held. An AChapterEndInteractable pushes
	 * its camera in meanwhile. A mission completing during it starts its end sequence with it.
	 */
	UFUNCTION(BlueprintCallable, Category = "End card")
	void PlayCloseUp(AActor* ViewTarget, float Seconds, float BlendSeconds);

	/** From PlayCloseUp until the view is back on the pawn. */
	UFUNCTION(BlueprintPure, Category = "End card")
	bool IsCloseUpActive() const { return bCloseUpActive; }

	/**
	 * The save subsystem calls this once the district is back from a flashback's playable scene and
	 * the player is at the return point: the end sequence carries on from the scene (roaming again
	 * with the chapter-complete toast, or the next level).
	 */
	void HandleReturnedFromScene();

	// --- Chapter title --------------------------------------------------------------------------

	/** The opening title card. UChapterTitleWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chapter title")
	TSubclassOf<UChapterTitleWidget> ChapterTitleWidgetClass;

	/** Shows Mission's title card now, whether or not it has been seen. */
	UFUNCTION(BlueprintCallable, Category = "Chapter title")
	UChapterTitleWidget* ShowChapterTitle(UMissionDefinition* Mission);

	UFUNCTION(BlueprintPure, Category = "Chapter title")
	bool IsChapterTitleShowing() const;

	UFUNCTION(BlueprintPure, Category = "Chapter title")
	UChapterTitleWidget* GetChapterTitleWidget() const { return ChapterTitleWidget; }

	// --- Phone ----------------------------------------------------------------------------------

	/** The phone panel. UPhoneWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Phone")
	TSubclassOf<UPhoneWidget> PhoneWidgetClass;

	/** P on the keyboard (IA_Phone). The pad holds D-pad down for PhoneHoldSeconds instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phone")
	TObjectPtr<UInputAction> PhoneAction;

	/**
	 * How long D-pad down is held to open the phone. A tap selects quiver slot 2 when the button comes
	 * back up, so a hold never also changes the arrow; the pad's slot 2 is handled here, not by IA_Slot2.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phone", meta = (ClampMin = "0.05"))
	float PhoneHoldSeconds = 0.4f;

	UFUNCTION(BlueprintCallable, Category = "Phone")
	void TogglePhone();

	/**
	 * Opens the phone over a paused game, or closes it. Refused while a menu, the inventory, a
	 * flashback, a close-up or an end sequence has the screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Phone")
	void SetPhoneOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Phone")
	bool IsPhoneOpen() const { return bPhoneOpen; }

	UFUNCTION(BlueprintPure, Category = "Phone")
	UPhoneWidget* GetPhoneWidget() const { return PhoneWidget; }

	/** The D-pad down press, release and hold as the controller sees them (real seconds). Tests drive these. */
	void HandleDPadDownPressed(double NowSeconds);
	void HandleDPadDownReleased(double NowSeconds);
	void TickDPadDown(double NowSeconds);

	// --- World map ------------------------------------------------------------------------------

	/** The world map. UHawkeyeMapWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Map")
	TSubclassOf<UHawkeyeMapWidget> MapWidgetClass;

	/** M on the keyboard (IA_Map). The pad holds D-pad up for MapHoldSeconds instead (View is the quiver). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Map")
	TObjectPtr<UInputAction> MapAction;

	/**
	 * How long D-pad up is held to open the map. A tap selects quiver slot 1 (standard arrows) when the
	 * button comes back up, so a hold never also changes the arrow; the pad's slot 1 is handled here.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Map", meta = (ClampMin = "0.05"))
	float MapHoldSeconds = 0.4f;

	UFUNCTION(BlueprintCallable, Category = "Map")
	void ToggleMap();

	/**
	 * Opens the world map over a paused game, or closes it. Refused while a menu, the inventory, the
	 * phone, a flashback, a close-up or an end sequence has the screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Map")
	void SetMapOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Map")
	bool IsMapOpen() const { return bMapOpen; }

	UFUNCTION(BlueprintPure, Category = "Map")
	UHawkeyeMapWidget* GetMapWidget() const { return MapWidget; }

	/** The D-pad up press, release and hold (real seconds). Tests drive these. */
	void HandleDPadUpPressed(double NowSeconds);
	void HandleDPadUpReleased(double NowSeconds);
	void TickDPadUp(double NowSeconds);

	/** True once a D-pad hold that started at PressedSeconds has lasted HoldSeconds by NowSeconds. */
	static bool IsHoldComplete(double PressedSeconds, double NowSeconds, float HoldSeconds)
	{
		return NowSeconds - PressedSeconds >= HoldSeconds;
	}

	// --- Pause ----------------------------------------------------------------------------------

	/** UMG widget (reparented to UHawkeyePauseWidget) shown while the game is paused. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Pause")
	TSubclassOf<UHawkeyePauseWidget> PauseWidgetClass;

	/**
	 * Enhanced Input action for the Escape key. Bound on the controller rather than the pawn so
	 * pausing still works while the pawn is locked out, dead or not yet possessed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UInputAction> PauseAction;

	/**
	 * Mapping context added by the controller itself. Normally IMC_Default, so the Escape key
	 * still reaches us when there is no pawn to add it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UInputMappingContext> PauseMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Pause", meta = (ClampMin = "0"))
	int32 PauseMappingPriority = 0;

	/** Opens the pause menu, or closes it when it is already open. Ignored when CanTogglePause is false. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void TogglePause();

	/** Opens or closes the pause menu explicitly. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void SetPauseMenuOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Pause")
	bool IsPauseMenuOpen() const { return bPauseMenuOpen; }

	/**
	 * False while a flashback is playing (the slideshow owns the pause) and while a mission
	 * restart is already under way, which is what dying starts.
	 */
	UFUNCTION(BlueprintPure, Category = "Pause")
	bool CanTogglePause() const;

	/** True between PlayFlashback and OnFlashbackFinished. */
	UFUNCTION(BlueprintPure, Category = "Flashback")
	bool IsFlashbackActive() const { return bFlashbackActive; }

	/** Pause menu "Restart mission": reloads the level now, without the death delay. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void RestartMissionFromPause();

	// --- Inventory ------------------------------------------------------------------------------

	/** UMG widget (reparented to UHawkeyeInventoryWidget) shown on Tab. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Inventory")
	TSubclassOf<UHawkeyeInventoryWidget> InventoryWidgetClass;

	/** Opens the inventory screen, or closes it when it is already open. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleInventory();

	/**
	 * Opens or closes the inventory screen. Opening pauses the game the way the pause menu
	 * does; Tab or Escape closes it. Refused while the pause menu or a flashback owns the pause.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetInventoryOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsInventoryOpen() const { return bInventoryOpen; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	UHawkeyeInventoryWidget* GetInventoryWidget() const { return InventoryWidget; }

	// --- Settings -------------------------------------------------------------------------------

	/** UMG widget (reparented to UHawkeyeSettingsWidget) shown when Settings is chosen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Settings")
	TSubclassOf<UHawkeyeSettingsWidget> SettingsWidgetClass;

	/** Swaps the pause menu for the settings screen. The game stays paused throughout. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void OpenSettings();

	/** Swaps the settings screen back for the pause menu. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void CloseSettings();

	UFUNCTION(BlueprintPure, Category = "Settings")
	bool IsSettingsOpen() const { return bSettingsOpen; }

	UFUNCTION(BlueprintPure, Category = "Settings")
	UHawkeyeSettingsWidget* GetSettingsWidget() const { return SettingsWidget; }

	// --- Replay flashbacks ------------------------------------------------------------------------

	/** The pause menu's list of seen flashbacks. UFlashbackReplayWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Flashback")
	TSubclassOf<UFlashbackReplayWidget> FlashbackReplayWidgetClass;

	/** Swaps the pause menu for the list of flashbacks the player has seen (the settings save's). */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	void OpenFlashbackReplay();

	/** Back to the pause menu. */
	UFUNCTION(BlueprintCallable, Category = "Flashback")
	void CloseFlashbackReplay();

	UFUNCTION(BlueprintPure, Category = "Flashback")
	bool IsFlashbackReplayOpen() const { return bFlashbackReplayOpen; }

	UFUNCTION(BlueprintPure, Category = "Flashback")
	UFlashbackReplayWidget* GetFlashbackReplayWidget() const { return FlashbackReplayWidget; }

	/** Pause menu "Quit to desktop". */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void QuitToDesktop();

	/** Pause menu "Quit to menu": the main menu over the paused game, as on first boot. */
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void QuitToMenu();

	// --- Main menu ------------------------------------------------------------------------------

	/** Drawn over the paused district on first boot and after Quit to menu. UHawkeyeMainMenuWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Main menu")
	TSubclassOf<UHawkeyeMainMenuWidget> MainMenuWidgetClass;

	/** Pauses the game under the main menu, Continue enabled when a save exists. */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void ShowMainMenu();

	/** Takes the main menu down and resumes play. */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void HideMainMenu();

	UFUNCTION(BlueprintPure, Category = "Main menu")
	bool IsMainMenuOpen() const { return bMainMenuOpen; }

	UFUNCTION(BlueprintPure, Category = "Main menu")
	UHawkeyeMainMenuWidget* GetMainMenuWidget() const { return MainMenuWidget; }

	/** Continue: loads the campaign save (or, if it cannot be used, starts a new game). */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void MainMenuContinue();

	/**
	 * New Game: over the freshly booted district this just starts playing (and writes the first
	 * save); anywhere else the district is reopened clean.
	 */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void MainMenuNewGame();

	/** Asked once at New Game, before it starts: Story, Normal or Hard. UHawkeyeDifficultyPromptWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Main menu")
	TSubclassOf<UHawkeyeDifficultyPromptWidget> DifficultyPromptWidgetClass;

	/** Puts the difficulty prompt over the main menu. New Game does, except in automation runs. */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void ShowDifficultyPrompt();

	/** Takes the prompt down without starting anything (Escape: back to the menu). */
	UFUNCTION(BlueprintCallable, Category = "Main menu")
	void HideDifficultyPrompt();

	UFUNCTION(BlueprintPure, Category = "Main menu")
	bool IsDifficultyPromptOpen() const { return bDifficultyPromptOpen; }

	UFUNCTION(BlueprintPure, Category = "Main menu")
	UHawkeyeDifficultyPromptWidget* GetDifficultyPromptWidget() const { return DifficultyPromptWidget; }

	/** The prompt's answer: stores the difficulty and starts the new game. */
	UFUNCTION()
	void HandleNewGameDifficultyChosen(EHawkeyeDifficulty Difficulty);

	// --- Safehouse ------------------------------------------------------------------------------

	/** The safehouse menu. UHawkeyeSafehouseWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Safehouse")
	TSubclassOf<UHawkeyeSafehouseWidget> SafehouseWidgetClass;

	/** ASafehouse calls this after healing and saving: pauses under the safehouse menu. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void OpenSafehouseMenu(ASafehouse* Safehouse);

	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void CloseSafehouseMenu();

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	bool IsSafehouseMenuOpen() const { return bSafehouseMenuOpen; }

	UFUNCTION(BlueprintPure, Category = "Safehouse")
	UHawkeyeSafehouseWidget* GetSafehouseWidget() const { return SafehouseWidget; }

	/** Refill arrows: every filled quiver slot of the pawn being played to its cap. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void SafehouseRefill();

	/** Save: writes the campaign now. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void SafehouseSave();

	/**
	 * Fast travel: the list of the district's other safehouses (undiscovered ones greyed). Refused with
	 * the "[Can't fast travel now]" toast during a crime or a challenge.
	 */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void SafehouseFastTravel();

	/**
	 * Chapter select: one row per chapter in campaign order (UHawkeyeCampaignOrder), named when open and
	 * "[Locked]" when not, padded with locked rows to the menu's slot count.
	 */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	void SafehouseChapterSelect();

	/**
	 * Closes the menu and travels to a discovered safehouse (USafehouseSubsystem::BeginFastTravel).
	 * False, with the refusal toast when that is why, when it does not start.
	 */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	bool FastTravelTo(FName SafehouseId);

	/** Pause menu "Mark nearest safehouse": closes the menu and marks the nearest one, found or not. */
	UFUNCTION(BlueprintCallable, Category = "Safehouse")
	bool MarkNearestSafehouse();

	/** Pause menu "Mark nearest challenge": closes the menu and marks the nearest pedestal. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	bool MarkNearestChallenge();

	/**
	 * The first-time "nearby" toasts: an unfound safehouse within its NoticeRadius (60 m) toasts and gets
	 * the marker, a pedestal within 40 m toasts; each id once a campaign. A marked pedestal's marker goes
	 * when she reaches it. PlayerTick runs it twice a second; public for tests. Returns the toasts pushed.
	 */
	int32 UpdatePlaceNotices();

	/** Chapter select's rows: the campaign's chapters (UHawkeyeCampaignOrder), then "[Locked]" up to this many. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Safehouse", meta = (ClampMin = "1"))
	int32 ChapterSelectSlots = 6;

	// --- Side challenges ----------------------------------------------------------------------

	/** The card at the end of a challenge. UChallengeResultsWidget by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Challenge")
	TSubclassOf<UChallengeResultsWidget> ChallengeResultsWidgetClass;

	/** UChallengeSubsystem calls this when a run ends: pauses under the results card. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	void OpenChallengeResults(const FChallengeResult& Result);

	/** The card's Leave (and Escape): back to roaming. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	void CloseChallengeResults();

	/** The card's Retry: shuts the card and runs the same challenge again from its pedestal. */
	UFUNCTION(BlueprintCallable, Category = "Challenge")
	void ChallengeRetry();

	UFUNCTION(BlueprintPure, Category = "Challenge")
	bool IsChallengeResultsOpen() const { return bChallengeResultsOpen; }

	UFUNCTION(BlueprintPure, Category = "Challenge")
	UChallengeResultsWidget* GetChallengeResultsWidget() const { return ChallengeResultsWidget; }

	// --- Input device -----------------------------------------------------------------------

	/**
	 * True from the moment any Gamepad_* key is pressed until the next mouse or keyboard key,
	 * so the HUD can swap "[F]" style hints for a gamepad glyph. Updated from every InputKey
	 * event, not just ones a Blueprint or Enhanced Input action happens to bind.
	 */
	UFUNCTION(BlueprintPure, Category = "Input")
	bool IsUsingGamepad() const { return bUsingGamepad; }

	// --- Playtest capture (claude-docs/testing.md, "Playtest capture") ---------------------------

	/**
	 * F12, or Menu held NoteHoldSeconds on a pad: a playtest note (UPlaytestSubsystem::TakeNote) and a
	 * "Note 3 saved" toast once its screenshot is written. Returns the note's index, 0 when none was taken.
	 */
	UFUNCTION(BlueprintCallable, Category = "Playtest")
	int32 TakePlaytestNote();

	/** A Menu hold this long is a note; a shorter press pauses, on release. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playtest", meta = (ClampMin = "0.1"))
	float NoteHoldSeconds = 0.6f;

	/** F12 or Menu still held this long after the press: LogHawkeye at Verbose for VerboseBumpSeconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playtest", meta = (ClampMin = "0.5"))
	float VerboseBumpHoldSeconds = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Playtest", meta = (ClampMin = "1.0"))
	float VerboseBumpSeconds = 10.f;

	/** F11, or Photo mode in the pause menu. False (with a toast) when GetPhotoModeRefusal has a reason. */
	UFUNCTION(BlueprintCallable, Category = "Playtest")
	bool EnterPhotoMode();

	UFUNCTION(BlueprintCallable, Category = "Playtest")
	void ExitPhotoMode();

	UFUNCTION(BlueprintPure, Category = "Playtest")
	bool IsPhotoModeActive() const;

	/**
	 * Empty when photo mode may start now; otherwise why not: "a menu is open", "a flashback", "a chapter
	 * beat", "downed", "dead", "no pawn", "the quiver wheel is open", "reloading". bFromPauseMenu lets the
	 * pause menu's own row through.
	 */
	FString GetPhotoModeRefusal(bool bFromPauseMenu = false) const;

	UPlaytestPhotoMode* GetPhotoMode() const { return PhotoMode; }

	// --- Partner and switching (claude-docs/gameplay-semantics.md) -----------------------------

	/**
	 * Swaps control to the other Hawkeye: possesses the partner's pawn, hands the one you were
	 * playing to the partner controller, rebinds the HUD (quiver, name) and blends the camera over
	 * SwitchBlendSeconds. False, and nothing changes, when GetSwitchRefusal has a reason.
	 */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	bool SwitchCharacter();

	/**
	 * Why a switch would be refused now, or empty when it would go ahead: the chapter does not allow
	 * it, there is no partner, or either Hawkeye is mid-traversal, mid-zip, mid-takedown or down.
	 */
	UFUNCTION(BlueprintPure, Category = "Partner")
	FString GetSwitchRefusal() const;

	/** The chapter's bAllowSwitching, or bAllowSwitchingOverride. */
	UFUNCTION(BlueprintPure, Category = "Partner")
	bool IsSwitchingAllowed() const;

	/** The partner controller backing up this player's pawn (any partner with a pawn if none leads it). */
	UFUNCTION(BlueprintPure, Category = "Partner")
	AHawkeyePartnerController* FindPartnerController() const;

	/**
	 * T or R3: traces down the camera's centre (the reticle) up to MarkTraceDistance and sends the partner
	 * to the point it hits.
	 * False when there is no partner or nothing under the view.
	 */
	UFUNCTION(BlueprintCallable, Category = "Partner")
	bool MarkPoint();

	/** The name the HUD shows for the character being played; updated by every switch. */
	UFUNCTION(BlueprintPure, Category = "Partner")
	FText GetHudCharacterName() const;

	UFUNCTION(BlueprintPure, Category = "Partner")
	UBanterComponent* GetBanter() const { return Banter; }

	/** The light snow that follows this player's camera. */
	USnowfallComponent* GetSnowfall() const { return Snowfall; }

	/** X on the keyboard, LB on a pad. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner")
	TObjectPtr<UInputAction> SwitchCharacterAction;

	/** T on the keyboard. No pad binding yet (the D-pad is the quiver's). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner")
	TObjectPtr<UInputAction> PartnerMarkAction;

	/** How long the camera takes to move from one Hawkeye to the other. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner", meta = (ClampMin = "0.0"))
	float SwitchBlendSeconds = 0.3f;

	/** How far the mark trace looks, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Partner", meta = (ClampMin = "100.0"))
	float MarkTraceDistance = 5000.f;

	/** Allows switching whatever the chapter says: debug and automation tests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Partner")
	bool bAllowSwitchingOverride = false;

	// --- Audio -------------------------------------------------------------------------------------

	/** Plays one of the interface sounds below, 2D and through pause. Nothing when it is unset. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlayUISound(EHawkeyeUISound Sound);

	/** Pushes VolumeMix and overrides each sound class with the gains Settings asks for. */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void ApplyVolumeSettings(const FHawkeyeSettings& Settings);

	/** The gains the last ApplyVolumeSettings used (tests and the log read them). */
	UFUNCTION(BlueprintPure, Category = "Audio")
	FHawkeyeClassVolumes GetAppliedVolumes() const { return AppliedVolumes; }

	/** A menu button under the cursor or the pad's focus (MS_UI_Hover). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	TSoftObjectPtr<USoundBase> UIHoverSound;

	/** A menu button pressed (MS_UI_Click). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	TSoftObjectPtr<USoundBase> UIClickSound;

	/** The two-note chime when an objective completes (MS_UI_ObjectiveComplete). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	TSoftObjectPtr<USoundBase> ObjectiveCompleteSound;

	/** The tick when a new objective is shown (MS_UI_NewObjective). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	TSoftObjectPtr<USoundBase> NewObjectiveSound;

	/** Any other toast (a pickup) (MS_UI_Toast). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|UI")
	TSoftObjectPtr<USoundBase> ToastSound;

	/** The mix the volume sliders drive (SMX_Settings). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Volume")
	TSoftObjectPtr<USoundMix> VolumeMix;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Volume")
	TSoftObjectPtr<USoundClass> MasterSoundClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Volume")
	TSoftObjectPtr<USoundClass> SfxSoundClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Volume")
	TSoftObjectPtr<USoundClass> AmbientSoundClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Volume")
	TSoftObjectPtr<USoundClass> UISoundClass;

	/** The score and its stings (SCL_Music); a C++ default, so no Blueprint has to carry it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Volume")
	TSoftObjectPtr<USoundClass> MusicSoundClass;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	virtual void PlayerTick(float DeltaTime) override;

	void Input_Pause(const FInputActionValue& Value);
	void Input_Phone(const FInputActionValue& Value);
	void Input_Map(const FInputActionValue& Value);
	void Input_SwitchCharacter(const FInputActionValue& Value);
	void Input_PartnerMark(const FInputActionValue& Value);

	/** Kate and Clint's banter, shown on the HUD. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Partner")
	TObjectPtr<UBanterComponent> Banter;

	/** Snow round the camera (NS_Snowfall, set by create_blueprints.py). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Effects")
	TObjectPtr<USnowfallComponent> Snowfall;

	/** Set by SwitchCharacter; empty until the first switch, when the pawn's own name is used. */
	UPROPERTY(Transient)
	FText ActiveCharacterName;

	/** Adds PauseMappingContext to the local player's Enhanced Input subsystem. */
	void AddPauseMappingContext();

	/** A slider moved: the new volumes apply at once, before the screen closes. */
	UFUNCTION()
	void HandleSettingsChanged(FHawkeyeSettings Settings);

	UPROPERTY(Transient)
	FHawkeyeClassVolumes AppliedVolumes;

	/**
	 * The mix and classes, held while the mix is pushed: the audio device keeps raw pointers to them,
	 * and a class collected under a pushed override is warned about every frame.
	 */
	UPROPERTY(Transient)
	TObjectPtr<USoundMix> PushedMix = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundClass>> OverriddenClasses;

	/** Takes the mix back off the device (EndPlay), so nothing outlives this controller. */
	void PopVolumeMix();

	UFUNCTION()
	void HandlePauseResumeClicked();

	UFUNCTION()
	void HandlePauseSettingsClicked();

	UFUNCTION()
	void HandleSettingsBackRequested();

	UFUNCTION()
	void HandlePauseReplayFlashbacksClicked();

	UFUNCTION()
	void HandleFlashbackReplayPicked(UFlashbackDefinition* Flashback);

	UFUNCTION()
	void HandleFlashbackReplayBack();

	/** New Game once the difficulty is settled: the fresh-boot start or the clean reopen. */
	void StartNewGame();

	UFUNCTION()
	void HandlePauseRestartClicked();

	UFUNCTION()
	void HandlePauseQuitClicked();

	UFUNCTION()
	void HandlePauseQuitToMenuClicked();

	UFUNCTION()
	void HandleMainMenuSettingsClicked();

	UFUNCTION()
	void HandleMainMenuQuitClicked();

	/** Creates the main menu widget and binds it, once. */
	UHawkeyeMainMenuWidget* EnsureMainMenuWidget();

	/** Creates the safehouse widget and binds it, once. */
	UHawkeyeSafehouseWidget* EnsureSafehouseWidget();

	void SetSafehouseStatus(const FText& Status);

	/** A toast with no title under it ("[Can't fast travel now]"). */
	void PushHudToast(const FText& Heading, const FText& Title);

	UFUNCTION()
	void HandleSafehouseListPicked(int32 Index);

	UFUNCTION()
	void HandleSafehouseListBack();

	UFUNCTION()
	void HandlePauseMarkSafehouseClicked();

	UFUNCTION()
	void HandlePauseMarkChallengeClicked();

	/** Real seconds until UpdatePlaceNotices runs again. */
	double NextPlaceNoticeSeconds = 0.0;

	/** Creates PauseWidget (if needed) and adds it to the viewport. Returns the widget or null. */
	UHawkeyePauseWidget* ShowPauseWidget();

	void HidePauseWidget();

	/** Creates SettingsWidget (if needed) and adds it to the viewport. Returns the widget or null. */
	UHawkeyeSettingsWidget* ShowSettingsWidget();

	void HideSettingsWidget();

	/** Whichever menu should hold keyboard focus right now, or an invalid pointer for none. */
	TSharedPtr<SWidget> GetFocusedMenuWidget() const;

	/** UI+Game input with a cursor while paused, game-only input while playing. */
	void ApplyPauseInputMode(bool bPaused);

	UFUNCTION()
	void HandleFlashbackRequested(UFlashbackDefinition* Flashback);

	UFUNCTION()
	void HandleFlashbackFinished(UFlashbackDefinition* Flashback);

	UFUNCTION()
	void HandleMissionComplete(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleEndCardFinished(UMissionDefinition* Mission);

	/** Runs whichever beat the flow is on now. Called after every Begin and Advance. */
	void PerformCurrentFlowStep();

	/** The HUD back and the chapter-complete toast: the chapter is over and the district is open. */
	void ReturnToRoaming();

	/** The close-up's hold is over: the view blends back to the pawn. */
	void EndCloseUpView();

	/** The view is back: input returns and the end sequence (if waiting) moves on. */
	void FinishCloseUp();

	/** The chapter's opening title card and sequence, unless a load, an automation run or the menu says wait. */
	UFUNCTION()
	void HandleMissionStartedForOpening(UMissionDefinition* Mission);

	/** Shows the title (once per campaign) and queues the opening sequence. */
	void BeginChapterOpening(UMissionDefinition* Mission);

	/** Automation drives the game itself; opening cards would sit over its screenshots. */
	static bool IsAutomationRun();

	UPROPERTY(Transient)
	TObjectPtr<UChapterTitleWidget> ChapterTitleWidget = nullptr;

	/** A mission that started under the main menu; its opening waits for the menu to close. */
	UPROPERTY(Transient)
	TObjectPtr<UMissionDefinition> PendingOpening = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPhoneWidget> PhoneWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Phone")
	bool bPhoneOpen = false;

	UPROPERTY(Transient)
	TObjectPtr<UHawkeyeMapWidget> MapWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Map")
	bool bMapOpen = false;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CloseUpTarget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "End card")
	bool bCloseUpActive = false;

	float CloseUpBlendSeconds = 0.5f;
	FTimerHandle CloseUpTimer;

	/** D-pad down: a hold of PhoneHoldSeconds opens the phone, a tap (on release) is quiver slot 2. */
	FHawkeyeTapHold DPadDown;

	/** D-pad up: a hold of MapHoldSeconds opens the map, a tap (on release) is quiver slot 1. */
	FHawkeyeTapHold DPadUp;

	/** Creates the end card (if needed) and plays it. bWaitForInput is the campaign-end card. */
	UMissionEndCardWidget* ShowEndCard(UMissionDefinition* Mission, bool bWaitForInput);

	void HideEndCard();

	/** Opens the current mission's NextLevel if one is set. Returns true when travel started. */
	bool TryOpenNextLevel();

	/** Opens MenuLevelName, or FallbackMenuLevelName when that map does not exist. */
	void OpenMenuLevel();

	/** Creates the HUD from HudWidgetClass and adds it to the viewport. */
	void CreateHud();

	void SetHudVisible(bool bVisible);

	UPROPERTY(Transient, BlueprintReadOnly, Category = "HUD")
	TObjectPtr<UHawkeyeHudWidget> HudWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Flashback")
	TObjectPtr<UFlashbackWidget> ActiveFlashbackWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pause")
	TObjectPtr<UHawkeyePauseWidget> PauseWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Settings")
	TObjectPtr<UHawkeyeSettingsWidget> SettingsWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Flashback")
	TObjectPtr<UFlashbackReplayWidget> FlashbackReplayWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Flashback")
	bool bFlashbackReplayOpen = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Main menu")
	TObjectPtr<UHawkeyeDifficultyPromptWidget> DifficultyPromptWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Main menu")
	bool bDifficultyPromptOpen = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UHawkeyeInventoryWidget> InventoryWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Inventory")
	bool bInventoryOpen = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Main menu")
	TObjectPtr<UHawkeyeMainMenuWidget> MainMenuWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Main menu")
	bool bMainMenuOpen = false;

	/** The menu is up over the district as it booted, untouched: New Game can just start playing. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Main menu")
	bool bMainMenuOverFreshBoot = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<UHawkeyeSafehouseWidget> SafehouseWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Safehouse")
	TObjectPtr<ASafehouse> ActiveSafehouse = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Safehouse")
	bool bSafehouseMenuOpen = false;

	/** Which list the safehouse menu is showing. */
	ESafehouseList SafehouseListKind = ESafehouseList::None;

	/** The fast-travel list's safehouse ids, row for row. */
	TArray<FName> TravelRowIds;

	/** Chapter select's chapters, row for row (the campaign order). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMissionDefinition>> ChapterRows;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Challenge")
	TObjectPtr<UChallengeResultsWidget> ChallengeResultsWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Challenge")
	bool bChallengeResultsOpen = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "End card")
	TObjectPtr<UMissionEndCardWidget> EndCardWidget = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "End card")
	TObjectPtr<UMissionFlowController> MissionFlow = nullptr;

	/** The mission whose end sequence is running. Held because the world outlives the subsystem. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "End card")
	TObjectPtr<UMissionDefinition> CompletedMission = nullptr;

	/** The definition OnFlashbackRequested handed us, played once the end card is done. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Flashback")
	TObjectPtr<UFlashbackDefinition> PendingFlashback = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Pause")
	bool bPauseMenuOpen = false;

	/** True while the settings screen has replaced the pause menu. Implies bPauseMenuOpen. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Settings")
	bool bSettingsOpen = false;

	/**
	 * Set for the whole of a flashback, including the frames where the widget exists but has
	 * not started ticking, so Escape can never steal the pause from the slideshow.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Flashback")
	bool bFlashbackActive = false;

	/** Last device seen by InputKey: true for a Gamepad_* key, false for mouse or keyboard. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Input")
	bool bUsingGamepad = false;

	/** F12, F11 and the pad's Menu hold. True when the key was the kit's and is used up. */
	bool HandlePlaytestKey(const FInputKeyEventArgs& Params);

	/** The Menu hold and the verbose bump's 2 s, from real time. */
	void TickPlaytestKeys(double NowSeconds);

	UFUNCTION()
	void HandlePausePhotoModeClicked();

	UPROPERTY(Transient)
	TObjectPtr<UPlaytestPhotoMode> PhotoMode = nullptr;

	/** Menu on a pad in play: a tap pauses on release, a hold of NoteHoldSeconds is a note. */
	FHawkeyeTapHold MenuHold;

	/** When the held note key (F12 or Menu) went down, real s; negative when none is held. */
	double NoteKeyDownSeconds = -1.0;
	bool bVerboseBumpStarted = false;
};
