#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHawkeye, Log, All);

/**
 * Trace channel every bullet uses, declared as "Weapon" in Config/DefaultEngine.ini.
 *
 * It exists because the stock Pawn and CharacterMesh profiles ignore ECC_Visibility, so a
 * Visibility hitscan passes through every character. Everything blocks this channel by
 * default; a character's capsule and mesh both block it explicitly.
 */
#define ECC_HawkeyeWeapon ECC_GameTraceChannel1

/**
 * "Is this an automation run?", asked one way everywhere. The questions differ on purpose, so each caller
 * picks the one it means by name.
 */
namespace HawkeyeAutomation
{
	/** A test is running right now (GIsAutomationTesting). */
	HAWKEYE_API bool IsTestRunning();

	/**
	 * The command line asks for tests ("RunTests" in -ExecCmds). True from the first frame of a -game run,
	 * before the first test sets GIsAutomationTesting.
	 */
	HAWKEYE_API bool IsLaunchedForTests();

	/** IsTestRunning or IsLaunchedForTests. */
	HAWKEYE_API bool IsAutomationRun();

	/** IsAutomationRun, or a commandlet. */
	HAWKEYE_API bool IsAutomationRunOrCommandlet();

	/**
	 * Test hook (Hawkeye.Lap.Campaign): an automation run that plays as a new player gets what automation
	 * otherwise skips: the boot menu, the difficulty prompt, the chapter opening, the "nearby" notices and
	 * the crime schedule. Off in the game and in every other test.
	 */
	HAWKEYE_API bool PlaysAsPlayer();
	HAWKEYE_API void SetPlaysAsPlayer(bool bPlays);

	/** IsAutomationRun and not PlaysAsPlayer: automation drives the game itself, so the player's flow stays out. */
	HAWKEYE_API bool SkipsPlayerFlow();
}
