#pragma once

#include "CoreMinimal.h"
#include "Settings/HawkeyeSettings.h"

/** Why a toggled sprint ended (the log names it). */
enum class EHawkeyeSprintStop : uint8
{
	None,
	/** A second press of the sprint button. */
	SecondPress,
	/** The stick sat in its dead zone for longer than CentreSeconds. */
	StickCentred,
	Aim,
	Crouch,
	SlideEnded,
	Menu,
	/** Control left the pawn, or a hold press took over. */
	Released,
};

/**
 * The sprint toggle (gameplay-semantics.md, "Movement"; revised 2026-09-29 after "sprint on controller should be
 * pressing the left joystick once and the character continues to sprint the whole time"). One press turns it on; it
 * stays on through turns and short stops, and goes off on a second press, when the stick has been centred for more
 * than CentreSeconds, or when the character says so (aim, a crouch, a slide's end, a menu). No world, no input
 * system: the character feeds it presses, ticks and stops, and the tests feed it numbers.
 */
struct FHawkeyeSprintToggle
{
	/** The stick in its dead zone this long (s) turns the sprint off; a shorter stop keeps it. */
	float CentreSeconds = 0.6f;

	bool IsOn() const { return bOn; }

	/** Seconds the stick has been centred while on. */
	float GetCentredSeconds() const { return CentredSeconds; }

	/** A press: on when off; off (SecondPress) when on. Returns whether it is on now. */
	bool Press()
	{
		if (bOn)
		{
			Stop();
			return false;
		}
		bOn = true;
		CentredSeconds = 0.f;
		return true;
	}

	/**
	 * One frame. bStickCentred: no move input this frame. Returns StickCentred on the frame the centred time passes
	 * CentreSeconds (and turns off), None otherwise.
	 */
	EHawkeyeSprintStop Tick(float DeltaSeconds, bool bStickCentred)
	{
		if (!bOn)
		{
			return EHawkeyeSprintStop::None;
		}
		if (!bStickCentred)
		{
			CentredSeconds = 0.f;
			return EHawkeyeSprintStop::None;
		}
		CentredSeconds += FMath::Max(DeltaSeconds, 0.f);
		if (CentredSeconds > CentreSeconds)
		{
			Stop();
			return EHawkeyeSprintStop::StickCentred;
		}
		return EHawkeyeSprintStop::None;
	}

	/** Turns it off. Returns whether it was on. */
	bool Stop()
	{
		const bool bWasOn = bOn;
		bOn = false;
		CentredSeconds = 0.f;
		return bWasOn;
	}

	/**
	 * Whether a sprint press toggles: the setting's Default holds on a keyboard and toggles on a pad; Hold and Toggle
	 * apply to both.
	 */
	static bool UsesToggle(EHawkeyeSprintMode Mode, bool bFromGamepad)
	{
		switch (Mode)
		{
		case EHawkeyeSprintMode::Hold:
			return false;
		case EHawkeyeSprintMode::Toggle:
			return true;
		default:
			return bFromGamepad;
		}
	}

	/** The settings row's value: "Hold on keys, toggle on pad", "Hold", "Toggle". */
	static FText GetModeName(EHawkeyeSprintMode Mode)
	{
		switch (Mode)
		{
		case EHawkeyeSprintMode::Hold:
			return NSLOCTEXT("Hawkeye", "SprintModeHold", "Hold");
		case EHawkeyeSprintMode::Toggle:
			return NSLOCTEXT("Hawkeye", "SprintModeToggle", "Toggle");
		default:
			return NSLOCTEXT("Hawkeye", "SprintModeDefault", "Hold on keys, toggle on pad");
		}
	}

	static const TCHAR* StopName(EHawkeyeSprintStop Why)
	{
		switch (Why)
		{
		case EHawkeyeSprintStop::SecondPress: return TEXT("second press");
		case EHawkeyeSprintStop::StickCentred: return TEXT("stick centred");
		case EHawkeyeSprintStop::Aim: return TEXT("aim");
		case EHawkeyeSprintStop::Crouch: return TEXT("crouch");
		case EHawkeyeSprintStop::SlideEnded: return TEXT("slide ended");
		case EHawkeyeSprintStop::Menu: return TEXT("menu");
		case EHawkeyeSprintStop::Released: return TEXT("released");
		default: return TEXT("none");
		}
	}

private:
	bool bOn = false;
	float CentredSeconds = 0.f;
};
