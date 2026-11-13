#pragma once

#include "CoreMinimal.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Engine/World.h"
#include "Player/HawkeyeCharacter.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The laps' shared dodge bot. Every lap fights the way a competent player would, and a player dodges a swing late
 * enough that her invulnerability covers the blow: the dodge starts at most her invulnerable seconds before the hit,
 * less a frame. A lead longer than that (the laps used 0.3 s against a 0.25 s window) lets a blow land after the
 * window has closed, and an earlier one is luck, not the rule. GrottoLapTest found it first (2026-11-10).
 */
namespace HawkeyeLapBot
{
	/** How long before a swing lands the bot may start its dodge: her invulnerable seconds less one frame. */
	inline float DodgeLeadSeconds(const AHawkeyeCharacter* Kate)
	{
		if (!Kate)
		{
			return 0.f;
		}
		const UWorld* World = Kate->GetWorld();
		const float Frame = World ? FMath::Clamp(World->GetDeltaSeconds(), 1.f / 120.f, 1.f / 30.f) : 1.f / 60.f;
		return FMath::Max(Kate->GetDodgeInvulnerableSeconds() - Frame, 0.f);
	}

	/** Which swings the bot dodges. */
	struct FDodgeRule
	{
		/** cm between them (2D) at most; with bReachFromAttack, added to the swing's own range instead. */
		float Reach = 250.f;
		bool bReachFromAttack = false;
		/** Never dodge this one's swing (the thug she is trading blows with). */
		const AThugCharacter* Except = nullptr;
		/** Only melee thugs: gunners and archers are left to the lap's own handling. */
		bool bMeleeOnly = false;
	};

	inline const AThugCharacter* AsThug(const AThugCharacter* Thug) { return Thug; }
	inline const AThugCharacter* AsThug(const TWeakObjectPtr<AThugCharacter>& Thug) { return Thug.Get(); }

	/** The thug whose swing lands within DodgeLeadSeconds and in reach, or null. */
	template <typename RangeT>
	const AThugCharacter* FindLandingSwing(const AHawkeyeCharacter* Kate, const RangeT& Thugs, const FDodgeRule& Rule)
	{
		const float Lead = DodgeLeadSeconds(Kate);
		for (const auto& Entry : Thugs)
		{
			const AThugCharacter* Thug = AsThug(Entry);
			if (!Thug || Thug == Rule.Except || Thug->IsLimp() || Thug->GetHealthComponent()->IsDead()
				|| Thug->IsIncapacitated() || (Rule.bMeleeOnly && (Thug->IsGunner() || Thug->IsArcher())))
			{
				continue;
			}
			const UMeleeComponent* Melee = Thug->GetMeleeComponent();
			if (!Melee || !Melee->IsWindingUp() || Melee->GetPhaseRemaining() > Lead)
			{
				continue;
			}
			const float Reach = Rule.bReachFromAttack ? Melee->GetCurrentAttack().Range + Rule.Reach : Rule.Reach;
			if (FVector::Dist2D(Thug->GetActorLocation(), Kate->GetActorLocation()) <= Reach)
			{
				return Thug;
			}
		}
		return nullptr;
	}

	/** Dodges sideways and back from Thug's swing: out of the sweep, not out of reach of her own next swing. */
	inline bool DodgeAway(AHawkeyeCharacter* Kate, const AThugCharacter* Thug)
	{
		const FVector Away = (Kate->GetActorLocation() - Thug->GetActorLocation()).GetSafeNormal2D();
		return Kate->TryDodge((Away + FVector::CrossProduct(FVector::UpVector, Away)).GetSafeNormal2D());
	}

	/** With the dodge ready and a swing about to land (FindLandingSwing): dodges it. True when she dodged. */
	template <typename RangeT>
	bool TryDodgeSwings(AHawkeyeCharacter* Kate, const RangeT& Thugs, const FDodgeRule& Rule)
	{
		if (!Kate || Kate->GetDodgeCooldownRemaining() > 0.f)
		{
			return false;
		}
		const AThugCharacter* Landing = FindLandingSwing(Kate, Thugs, Rule);
		return Landing && DodgeAway(Kate, Landing);
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
