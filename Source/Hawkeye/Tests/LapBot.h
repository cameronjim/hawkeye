#pragma once

#include "CoreMinimal.h"
#include "Combat/HealthComponent.h"
#include "Combat/MeleeComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Math/RotationMatrix.h"
#include "Player/CameraRules.h"
#include "Player/HawkeyeCharacter.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The laps' shared aim bot and dodge bot.
 *
 * Aim: every lap used to set the control rotation from the LENS to the target, every frame, with no rate limit. The lens
 * is where the arm left it, a swept and lagged place that itself depends on the rotation being set: a target near or
 * behind it flipped the yaw half a turn in a frame, and the camera audit (CameraAudit.h) saw a swing no player makes.
 * AimAt aims from where the lens WOULD be for the view it is choosing (the arm's own length, offset and lift, with no
 * sweep and no lag, so nothing it did last frame feeds back), keeps the crosshair on the target the way the lens version
 * did, turns at most AimTurnRate degrees a second, and holds its yaw when the target is straight above or below that point.
 *
 * Dodge: Every lap fights the way a competent player would, and a player dodges a swing late
 * enough that her invulnerability covers the blow: the dodge starts at most her invulnerable seconds before the hit,
 * less a frame. A lead longer than that (the laps used 0.3 s against a 0.25 s window) lets a blow land after the
 * window has closed, and an earlier one is luck, not the rule. GrottoLapTest found it first (2026-11-10).
 */
namespace HawkeyeLapBot
{
	/** The fastest the bot turns the view, degrees per second: a quick flick of a mouse, not a teleport. */
	constexpr float AimTurnRate = 540.f;

	/** Within this horizontal distance of the aim origin the yaw to a target is meaningless; the bot holds it, cm. */
	constexpr float AimYawGuardDistance = 30.f;

	/** Moves Current toward Want by at most MaxDegrees on each of yaw and pitch (the short way round for yaw). */
	inline FRotator StepAim(const FRotator& Current, const FRotator& Want, float MaxDegrees)
	{
		const float Yaw = FRotator::NormalizeAxis(Want.Yaw - Current.Yaw);
		const float Pitch = FRotator::NormalizeAxis(Want.Pitch - Current.Pitch);
		return FRotator(FRotator::NormalizeAxis(Current.Pitch + FMath::Clamp(Pitch, -MaxDegrees, MaxDegrees)),
			FRotator::NormalizeAxis(Current.Yaw + FMath::Clamp(Yaw, -MaxDegrees, MaxDegrees)), 0.f);
	}

	/** Where the lens sits for View with the arm's current settings, no sweep and no lag. */
	inline FVector IdealLens(const AHawkeyeCharacter* Kate, const FRotator& View)
	{
		const UHawkeyeSpringArmComponent* Boom = Kate->GetCameraBoom();
		if (!Boom)
		{
			return Kate->GetActorLocation();
		}
		const FVector Pivot = Boom->GetComponentLocation() + Boom->TargetOffset;
		return Pivot - View.Vector() * Boom->TargetArmLength + FRotationMatrix(View).TransformVector(Boom->SocketOffset);
	}

	/** The view that puts the crosshair on Target, ignoring the rate limit. */
	inline FRotator AimWanted(const AHawkeyeCharacter* Kate, const FRotator& From, const FVector& Target)
	{
		FRotator Want = From;
		FVector Lens = IdealLens(Kate, Want);
		// The lens depends on the view and the view on the lens: a few passes settle it for any target well beyond the arm.
		for (int32 Pass = 0; Pass < 6; ++Pass)
		{
			Want = (Target - Lens).Rotation();
			Lens = IdealLens(Kate, Want);
		}
		if (FVector::Dist2D(Target, Lens) < AimYawGuardDistance)
		{
			Want.Yaw = From.Yaw;
		}
		return Want;
	}

	/** Points the camera at Target; call every frame. Rate-limited (AimTurnRate): a target behind her takes a third of a second. */
	inline void AimAt(APlayerController* PC, const AHawkeyeCharacter* Kate, const FVector& Target)
	{
		const UWorld* World = PC->GetWorld();
		const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		const float Frame = HawkeyeCameraRules::UnscaledDelta(World ? World->GetDeltaSeconds() : 0.f,
			Settings ? Settings->GetEffectiveTimeDilation() : 1.f);
		const float Seconds = Frame > 0.f ? FMath::Min(Frame, 0.1f) : 1.f / 60.f;
		const FRotator Current = PC->GetControlRotation();
		const FRotator Next = StepAim(Current, AimWanted(Kate, Current, Target), AimTurnRate * Seconds);
		PC->SetControlRotation(FRotator(Kate->ClampCameraPitch(Next.Pitch), Next.Yaw, 0.f));
	}

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
