#include "Combat/CombatAnimSet.h"

#include "Animation/AnimMontage.h"
#include "Hawkeye.h"

const TSoftObjectPtr<UAnimMontage>* UCombatAnimSet::FindSlot(ECombatAnimRole Role) const
{
	switch (Role)
	{
	case ECombatAnimRole::Light1: return &Light1;
	case ECombatAnimRole::Light2: return &Light2;
	case ECombatAnimRole::Light3: return &Light3;
	case ECombatAnimRole::Heavy: return &Heavy;
	case ECombatAnimRole::Kick: return &Kick;
	case ECombatAnimRole::Parry: return &Parry;
	case ECombatAnimRole::DodgeForward: return &DodgeForward;
	case ECombatAnimRole::DodgeBack: return &DodgeBack;
	case ECombatAnimRole::DodgeLeft: return &DodgeLeft;
	case ECombatAnimRole::DodgeRight: return &DodgeRight;
	case ECombatAnimRole::HitFront: return &HitFront;
	case ECombatAnimRole::HitBack: return &HitBack;
	case ECombatAnimRole::HitLeft: return &HitLeft;
	case ECombatAnimRole::HitRight: return &HitRight;
	case ECombatAnimRole::Knockdown: return &Knockdown;
	case ECombatAnimRole::GetUp: return &GetUp;
	case ECombatAnimRole::FinisherAttacker: return &FinisherAttacker;
	case ECombatAnimRole::FinisherVictim: return &FinisherVictim;
	case ECombatAnimRole::FinisherBow: return &FinisherBow;
	case ECombatAnimRole::BowDraw: return &BowDraw;
	case ECombatAnimRole::BowAimIdle: return &BowAimIdle;
	case ECombatAnimRole::BowFire: return &BowFire;
	case ECombatAnimRole::BowNock: return &BowNock;
	case ECombatAnimRole::GetUpFront: return &GetUpFront;
	case ECombatAnimRole::HitHeavy: return &HitHeavy;
	case ECombatAnimRole::TakedownAttacker: return &TakedownAttacker;
	case ECombatAnimRole::TakedownVictim: return &TakedownVictim;
	case ECombatAnimRole::HitHeadLight: return &HitHeadLight;
	case ECombatAnimRole::HitHeadHeavy: return &HitHeadHeavy;
	case ECombatAnimRole::HitGutLight: return &HitGutLight;
	case ECombatAnimRole::HitGutHeavy: return &HitGutHeavy;
	case ECombatAnimRole::HitLegsLight: return &HitLegsLight;
	case ECombatAnimRole::HitLegsHeavy: return &HitLegsHeavy;
	case ECombatAnimRole::LandStandLight: return &LandStandLight;
	case ECombatAnimRole::LandStandHeavy: return &LandStandHeavy;
	case ECombatAnimRole::LandWalkLight: return &LandWalkLight;
	case ECombatAnimRole::LandWalkHeavy: return &LandWalkHeavy;
	case ECombatAnimRole::LandRunLight: return &LandRunLight;
	case ECombatAnimRole::LandRunHeavy: return &LandRunHeavy;
	case ECombatAnimRole::LandSprintLight: return &LandSprintLight;
	case ECombatAnimRole::LandSprintHeavy: return &LandSprintHeavy;
	case ECombatAnimRole::LandRoll: return &LandRoll;
	case ECombatAnimRole::LandStumble: return &LandStumble;
	case ECombatAnimRole::FallLoop: return &FallLoop;
	case ECombatAnimRole::LedgeDropToHang: return &LedgeDropToHang;
	case ECombatAnimRole::LedgeCatch: return &LedgeCatch;
	case ECombatAnimRole::LedgeHangIdle: return &LedgeHangIdle;
	case ECombatAnimRole::LedgeHangDrop: return &LedgeHangDrop;
	case ECombatAnimRole::LedgeClimbUp: return &LedgeClimbUp;
	case ECombatAnimRole::LedgeVaultPlant: return &LedgeVaultPlant;
	case ECombatAnimRole::LedgeFallLoop: return &LedgeFallLoop;
	case ECombatAnimRole::LedgeFallBrace: return &LedgeFallBrace;
	case ECombatAnimRole::LedgeVaultCrouch: return &LedgeVaultCrouch;
	default: return nullptr;
	}
}

ECombatAnimRole UCombatAnimSet::LandingRoleFor(ELandingClip Clip)
{
	switch (Clip)
	{
	case ELandingClip::StandLight: return ECombatAnimRole::LandStandLight;
	case ELandingClip::StandHeavy: return ECombatAnimRole::LandStandHeavy;
	case ELandingClip::WalkLight: return ECombatAnimRole::LandWalkLight;
	case ELandingClip::WalkHeavy: return ECombatAnimRole::LandWalkHeavy;
	case ELandingClip::RunLight: return ECombatAnimRole::LandRunLight;
	case ELandingClip::RunHeavy: return ECombatAnimRole::LandRunHeavy;
	case ELandingClip::SprintLight: return ECombatAnimRole::LandSprintLight;
	case ELandingClip::SprintHeavy: return ECombatAnimRole::LandSprintHeavy;
	case ELandingClip::Roll: return ECombatAnimRole::LandRoll;
	case ELandingClip::Stumble: return ECombatAnimRole::LandStumble;
	default: return ECombatAnimRole::None;
	}
}

TSoftObjectPtr<UAnimMontage> UCombatAnimSet::GetMontage(ECombatAnimRole Role) const
{
	const TSoftObjectPtr<UAnimMontage>* Slot = FindSlot(Role);
	if (Slot && !Slot->IsNull())
	{
		return *Slot;
	}
	// The chain's ender reads as well as a kick as a third punch, so a set with only a kick uses it.
	if (Role == ECombatAnimRole::Light3)
	{
		return Kick;
	}
	return TSoftObjectPtr<UAnimMontage>();
}

UAnimMontage* UCombatAnimSet::ResolveMontage(ECombatAnimRole Role) const
{
	const TSoftObjectPtr<UAnimMontage> Soft = GetMontage(Role);
	if (Soft.IsNull())
	{
		return nullptr;
	}
	UAnimMontage* Montage = Soft.LoadSynchronous();
	if (!Montage)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: the %s montage %s does not load; using the procedural fallback."), *GetName(),
			*UEnum::GetValueAsString(Role), *Soft.ToString());
	}
	return Montage;
}

UAnimMontage* UCombatAnimSet::Resolve(const UCombatAnimSet* Set, ECombatAnimRole Role)
{
	return Set ? Set->ResolveMontage(Role) : nullptr;
}

ECombatAnimRole UCombatAnimSet::SourceRole(ECombatAnimRole Role) const
{
	const TSoftObjectPtr<UAnimMontage>* Slot = FindSlot(Role);
	if (Role == ECombatAnimRole::Light3 && (!Slot || Slot->IsNull()))
	{
		return ECombatAnimRole::Kick;
	}
	return Role;
}

const FCombatAnimVariants* UCombatAnimSet::FindVariants(ECombatAnimRole Role) const
{
	return MoreVariants.FindByPredicate([Role](const FCombatAnimVariants& Entry) { return Entry.Role == Role; });
}

int32 UCombatAnimSet::GetVariantCount(ECombatAnimRole Role) const
{
	if (GetMontage(Role).IsNull())
	{
		return 0;
	}
	const FCombatAnimVariants* More = FindVariants(SourceRole(Role));
	return 1 + (More ? More->Montages.Num() : 0);
}

TSoftObjectPtr<UAnimMontage> UCombatAnimSet::GetVariant(ECombatAnimRole Role, int32 Index) const
{
	if (Index <= 0)
	{
		return Index == 0 ? GetMontage(Role) : TSoftObjectPtr<UAnimMontage>();
	}
	if (GetMontage(Role).IsNull())
	{
		return TSoftObjectPtr<UAnimMontage>();
	}
	const FCombatAnimVariants* More = FindVariants(SourceRole(Role));
	return More && More->Montages.IsValidIndex(Index - 1) ? More->Montages[Index - 1] : TSoftObjectPtr<UAnimMontage>();
}

UAnimMontage* UCombatAnimSet::ResolveVariant(ECombatAnimRole Role, int32 Index) const
{
	const TSoftObjectPtr<UAnimMontage> Soft = GetVariant(Role, Index);
	if (Soft.IsNull())
	{
		return nullptr;
	}
	UAnimMontage* Montage = Soft.LoadSynchronous();
	if (!Montage)
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: the %s variant %d montage %s does not load."), *GetName(),
			*UEnum::GetValueAsString(Role), Index, *Soft.ToString());
	}
	return Montage;
}

UAnimMontage* UCombatAnimSet::ResolveVariantIn(const UCombatAnimSet* Set, ECombatAnimRole Role, int32 Index)
{
	return Set ? Set->ResolveVariant(Role, Index) : nullptr;
}

int32 UCombatAnimSet::PickNextVariant(int32& Cursor, int32 Count)
{
	if (Count <= 1)
	{
		Cursor = 0;
		return 0;
	}
	const int32 Pick = ((Cursor % Count) + Count) % Count;
	Cursor = (Pick + 1) % Count;
	return Pick;
}

int32 UCombatAnimSet::CountAssigned() const
{
	int32 Count = 0;
	for (uint8 Value = static_cast<uint8>(ECombatAnimRole::Light1); Value <= static_cast<uint8>(LastCombatAnimRole); ++Value)
	{
		const TSoftObjectPtr<UAnimMontage>* Slot = FindSlot(static_cast<ECombatAnimRole>(Value));
		Count += (Slot && !Slot->IsNull()) ? 1 : 0;
	}
	return Count;
}

void UCombatAnimSet::Preload()
{
	Preloaded.Reset();
	for (uint8 Value = static_cast<uint8>(ECombatAnimRole::Light1); Value <= static_cast<uint8>(LastCombatAnimRole); ++Value)
	{
		const TSoftObjectPtr<UAnimMontage>* Slot = FindSlot(static_cast<ECombatAnimRole>(Value));
		if (Slot && !Slot->IsNull())
		{
			if (UAnimMontage* Montage = Slot->LoadSynchronous())
			{
				Preloaded.AddUnique(Montage);
			}
		}
	}
	int32 Variants = 0;
	for (const FCombatAnimVariants& Entry : MoreVariants)
	{
		for (const TSoftObjectPtr<UAnimMontage>& Soft : Entry.Montages)
		{
			if (!Soft.IsNull())
			{
				++Variants;
				if (UAnimMontage* Montage = Soft.LoadSynchronous())
				{
					Preloaded.AddUnique(Montage);
				}
			}
		}
	}
	UE_LOG(LogHawkeye, Log, TEXT("%s: %d combat montages loaded of %d assigned and %d more variants."), *GetName(), Preloaded.Num(),
		CountAssigned(), Variants);
}

const FCombatClipData* UCombatAnimSet::FindClipData(const UAnimMontage* Montage) const
{
	if (!Montage)
	{
		return nullptr;
	}
	return FindClipDataByPath(FSoftObjectPath(Montage));
}

const FCombatClipData* UCombatAnimSet::FindClipDataByPath(const FSoftObjectPath& Path) const
{
	if (Path.IsNull())
	{
		return nullptr;
	}
	return ClipData.FindByPredicate([&Path](const FCombatClipData& Entry) { return Entry.Montage.ToSoftObjectPath() == Path; });
}

ECombatAnimRole UCombatAnimSet::ZoneHitRole(EHawkeyeContactRegion Region, bool bHeavy)
{
	switch (Region)
	{
	case EHawkeyeContactRegion::Head: return bHeavy ? ECombatAnimRole::HitHeadHeavy : ECombatAnimRole::HitHeadLight;
	case EHawkeyeContactRegion::Legs: return bHeavy ? ECombatAnimRole::HitLegsHeavy : ECombatAnimRole::HitLegsLight;
	default: return bHeavy ? ECombatAnimRole::HitGutHeavy : ECombatAnimRole::HitGutLight;
	}
}

bool UCombatAnimSet::IsHeavyHitRole(ECombatAnimRole Role)
{
	return Role == ECombatAnimRole::HitHeavy || Role == ECombatAnimRole::HitHeadHeavy || Role == ECombatAnimRole::HitGutHeavy
		|| Role == ECombatAnimRole::HitLegsHeavy;
}

ECombatAnimRole UCombatAnimSet::HitRoleForImpact(const UCombatAnimSet* Set, EHawkeyeHitDirection Side, EHawkeyeContactRegion Region,
	float Weight, float HeavyWeight)
{
	if (Set && Side == EHawkeyeHitDirection::Front)
	{
		const ECombatAnimRole Zone = ZoneHitRole(Region, Weight >= HeavyWeight);
		if (Set->HasMontage(Zone))
		{
			return Zone;
		}
	}
	return HitRoleForBlow(Set, Side, Weight, HeavyWeight);
}

ECombatAnimRole UCombatAnimSet::HitRoleFor(EHawkeyeHitDirection Side)
{
	switch (Side)
	{
	case EHawkeyeHitDirection::Back: return ECombatAnimRole::HitBack;
	case EHawkeyeHitDirection::Left: return ECombatAnimRole::HitLeft;
	case EHawkeyeHitDirection::Right: return ECombatAnimRole::HitRight;
	default: return ECombatAnimRole::HitFront;
	}
}

ECombatAnimRole UCombatAnimSet::HitRoleForBlow(const UCombatAnimSet* Set, EHawkeyeHitDirection Side, float Weight, float HeavyWeight)
{
	if (Set && Weight >= HeavyWeight && Set->HasMontage(ECombatAnimRole::HitHeavy))
	{
		return ECombatAnimRole::HitHeavy;
	}
	return HitRoleFor(Side);
}

ECombatAnimRole UCombatAnimSet::GetUpRoleFor(const UCombatAnimSet* Set, bool bOnBack)
{
	if (!bOnBack && Set && Set->HasMontage(ECombatAnimRole::GetUpFront))
	{
		return ECombatAnimRole::GetUpFront;
	}
	return ECombatAnimRole::GetUp;
}

ECombatAnimRole UCombatAnimSet::DodgeRoleFor(EHawkeyeHitDirection Side)
{
	switch (Side)
	{
	case EHawkeyeHitDirection::Back: return ECombatAnimRole::DodgeBack;
	case EHawkeyeHitDirection::Left: return ECombatAnimRole::DodgeLeft;
	case EHawkeyeHitDirection::Right: return ECombatAnimRole::DodgeRight;
	default: return ECombatAnimRole::DodgeForward;
	}
}

FPrimaryAssetId UCombatAnimSet::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("CombatAnimSet"), GetFName());
}
