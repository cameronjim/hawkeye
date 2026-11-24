#include "Combat/CombatAnimPlayback.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Combat/CombatAnimSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Hawkeye.h"

namespace HawkeyeCombatAnim
{
	const FName UpperBodySlot(TEXT("UpperBody"));
	const FName FullBodySlot(TEXT("DefaultSlot"));

	bool IsUpperBodyMontage(const UAnimMontage* Montage)
	{
		return Montage && Montage->SlotAnimTracks.Num() > 0 && Montage->SlotAnimTracks[0].SlotName == UpperBodySlot;
	}

	UAnimInstance* PickInstance(const USkeletalMeshComponent* Mesh, const UAnimMontage* Montage)
	{
		if (!Mesh || !Montage)
		{
			return nullptr;
		}
		UAnimInstance* PostProcess = Mesh->GetPostProcessInstance();
		if (IsUpperBodyMontage(Montage))
		{
			return PostProcess;
		}
		// A single-clip instance has no slot to play into; the post-process graph's DefaultSlot does.
		UAnimInstance* Main = Mesh->GetAnimInstance();
		if (Main && !Main->IsA<UAnimSingleNodeInstance>())
		{
			return Main;
		}
		return PostProcess;
	}

	UAnimInstance* Play(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, float PlayRate, float StartAtSeconds)
	{
		UAnimInstance* Instance = PickInstance(Mesh, Montage);
		if (!Instance)
		{
			return nullptr;
		}
		// Montage_Play refuses a montage for another skeleton and returns 0, which is the fallback.
		const float Length = Instance->Montage_Play(Montage, FMath::Max(PlayRate, 0.01f), EMontagePlayReturnType::MontageLength,
			FMath::Max(StartAtSeconds, 0.f));
		return Length > 0.f ? Instance : nullptr;
	}

	UAnimInstance* PlayWithBlend(USkeletalMeshComponent* Mesh, UAnimMontage* Montage, float PlayRate, float StartAtSeconds,
		float BlendInSeconds)
	{
		UAnimInstance* Instance = PickInstance(Mesh, Montage);
		if (!Instance)
		{
			return nullptr;
		}
		const float Length = Instance->Montage_PlayWithBlendSettings(Montage, FMontageBlendSettings(FMath::Max(BlendInSeconds, 0.f)),
			FMath::Max(PlayRate, 0.01f), EMontagePlayReturnType::MontageLength, FMath::Max(StartAtSeconds, 0.f));
		return Length > 0.f ? Instance : nullptr;
	}

	UAnimMontage* PlayReactionRole(USkeletalMeshComponent* Mesh, const UCombatAnimSet* Set, ECombatAnimRole Role,
		const FHawkeyeWeldTuning& Tuning, float* OutKnockbackCm)
	{
		if (OutKnockbackCm)
		{
			*OutKnockbackCm = -1.f;
		}
		UAnimMontage* Montage = UCombatAnimSet::Resolve(Set, Role);
		if (!Montage || !Mesh)
		{
			return nullptr;
		}
		const FCombatClipData* Data = Set->FindClipData(Montage);
		const FHawkeyeReactionStart Start = UHawkeyeHitContactRules::ComputeReactionStart(Data ? Data->HitPeakTime : -1.f,
			Montage->GetPlayLength(), Tuning);
		if (!PlayWithBlend(Mesh, Montage, 1.f, Start.StartAtSeconds, Start.BlendInSeconds))
		{
			return nullptr;
		}
		if (OutKnockbackCm && Data)
		{
			*OutKnockbackCm = Data->KnockbackCm;
		}
		return Montage;
	}

	FReactionPlayed PlayHitReaction(USkeletalMeshComponent* Mesh, const UCombatAnimSet* Set, const FHawkeyeStrikeImpact* Blow,
		EHawkeyeHitDirection Side, float HeavyWeight, const FHawkeyeWeldTuning& Tuning, bool bAttackerInFront)
	{
		FReactionPlayed Result;
		const EHawkeyeContactRegion Region = Blow ? UHawkeyeHitContactRules::ClassifyBoneRegion(Blow->Bone) : EHawkeyeContactRegion::Torso;
		const float Weight = Blow ? Blow->Weight : 0.f;
		ECombatAnimRole Role = UCombatAnimSet::HitRoleForImpact(Set, bAttackerInFront ? EHawkeyeHitDirection::Front : Side, Region, Weight,
			HeavyWeight);
		if (bAttackerInFront && Role == ECombatAnimRole::HitFront && Side != EHawkeyeHitDirection::Front)
		{
			// No zone clip for it: the side the blow really came from, as before.
			Role = UCombatAnimSet::HitRoleForBlow(Set, Side, Weight, HeavyWeight);
		}
		float KnockbackCm = -1.f;
		UAnimMontage* Played = PlayReactionRole(Mesh, Set, Role, Tuning, &KnockbackCm);
		if (!Played && Role != UCombatAnimSet::HitRoleFor(Side))
		{
			Role = UCombatAnimSet::HitRoleFor(Side);
			Played = PlayReactionRole(Mesh, Set, Role, Tuning, &KnockbackCm);
		}
		if (Played && Mesh && !Mesh->IsSimulatingPhysics())
		{
			// The victim is frozen the same frame (his clock stops): one evaluation now, so the pose the freeze holds is the
			// montage's hit pose and not the idle he was in a frame ago. A millisecond of play, then the bones.
			Mesh->TickAnimation(0.001f, /*bNeedsValidRootMotion=*/false);
			Mesh->RefreshBoneTransforms();
		}
		UE_LOG(LogHawkeye, Log, TEXT("%s: hit reaction %s (%s) from %s: %s."), *GetNameSafe(Mesh ? Mesh->GetOwner() : nullptr),
			*UEnum::GetValueAsString(Role), Blow ? *Blow->Bone.ToString() : TEXT("no blow"), *GetNameSafe(Played),
			Played ? TEXT("played") : TEXT("did not play"));
		Result.bPlayed = Played != nullptr;
		Result.Role = Role;
		Result.bHeavy = UCombatAnimSet::IsHeavyHitRole(Role);
		Result.KnockbackCm = KnockbackCm;
		return Result;
	}

	UAnimInstance* PlayRole(USkeletalMeshComponent* Mesh, const UCombatAnimSet* Set, ECombatAnimRole Role, float FitToSeconds)
	{
		UAnimMontage* Montage = UCombatAnimSet::Resolve(Set, Role);
		if (!Montage || !Mesh)
		{
			return nullptr;
		}
		const float Rate = FitToSeconds > 0.f ? FitRate(Montage->GetPlayLength(), FitToSeconds) : 1.f;
		return Play(Mesh, Montage, Rate);
	}

	void Stop(UAnimInstance* Instance, const UAnimMontage* Montage, float BlendOutSeconds)
	{
		if (Instance && Montage && Instance->Montage_IsActive(Montage))
		{
			Instance->Montage_Stop(FMath::Max(BlendOutSeconds, 0.f), Montage);
		}
	}

	bool IsPlaying(const UAnimInstance* Instance, const UAnimMontage* Montage)
	{
		return Instance && Montage && Instance->Montage_IsPlaying(Montage);
	}

	float FitRate(float ClipSeconds, float WantedSeconds)
	{
		if (ClipSeconds <= 0.f || WantedSeconds <= 0.f)
		{
			return 1.f;
		}
		return FMath::Clamp(ClipSeconds / WantedSeconds, 0.25f, 4.f);
	}

	FHitFit FitHitToWindup(float HitSeconds, float WindupSeconds, float MinRate, float MaxRate)
	{
		FHitFit Fit;
		if (HitSeconds <= 0.f || WindupSeconds <= 0.f)
		{
			return Fit;
		}
		const float Low = FMath::Max(MinRate, 0.05f);
		const float High = FMath::Max(MaxRate, Low);
		const float Wanted = HitSeconds / WindupSeconds;
		if (Wanted > High)
		{
			Fit.Rate = High;
			Fit.StartAtSeconds = HitSeconds - WindupSeconds * High;
		}
		else if (Wanted < Low)
		{
			Fit.Rate = Low;
			Fit.HoldSeconds = WindupSeconds - HitSeconds / Low;
		}
		else
		{
			Fit.Rate = Wanted;
		}
		return Fit;
	}

	bool FindNotifyWindow(const UAnimSequenceBase* Animation, const UClass* NotifyClass, float& OutStart, float& OutEnd)
	{
		OutStart = 0.f;
		OutEnd = 0.f;
		if (!Animation || !NotifyClass)
		{
			return false;
		}
		for (const FAnimNotifyEvent& Event : Animation->Notifies)
		{
			if (Event.NotifyStateClass && Event.NotifyStateClass->IsA(NotifyClass))
			{
				OutStart = Event.GetTriggerTime();
				OutEnd = Event.GetEndTriggerTime();
				return true;
			}
		}
		return false;
	}

	bool DrivesRootMotion(const USkeletalMeshComponent* Mesh, const UAnimMontage* Montage)
	{
		const UAnimInstance* Main = Mesh ? Mesh->GetAnimInstance() : nullptr;
		return Montage && Montage->HasRootMotion() && Main && PickInstance(Mesh, Montage) == Main
			&& Main->RootMotionMode != ERootMotionMode::IgnoreRootMotion && Main->RootMotionMode != ERootMotionMode::NoRootMotionExtraction;
	}
}
