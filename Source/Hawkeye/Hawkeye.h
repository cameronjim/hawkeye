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
