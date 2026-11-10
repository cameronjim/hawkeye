#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/AnimNotifyState_HitWindow.h"
#include "Combat/CombatAnimPlayback.h"
#include "Combat/CombatAnimSet.h"
#include "Combat/MeleeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/AutomationCommon.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The combat clips on the real characters in the district: whatever clips the anim sets carry
 * (none on a fresh clone, where every check is skipped with a note) play on the instance and slot
 * claude-docs/animation.md says: Kate's full-body clips on the sample's AnimBP, her bow clips on the
 * post-process graph's UpperBody slot, a thug's clips on the post-process graph's DefaultSlot, an
 * archer's bow clips on his UpperBody slot. It reads the slot weight after a few frames, which is
 * the graph actually blending the clip in, not only the montage being started. Then Kate throws a
 * light: with a Light1 clip that carries ANS_HitWindow the swing must still be winding up before the
 * window and recovering after it, which is the notify arriving from a real montage.
 *
 * Like the other smoke tests it loads the map; the rule-level tests are in CombatAnimTest.cpp.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSmokeCombatClipsPlay, "Hawkeye.Smoke.CombatClipsPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace HawkeyeCombatClipSmoke
{
	struct FPlayed
	{
		FString Who;
		TWeakObjectPtr<UAnimInstance> Instance;
		TWeakObjectPtr<UAnimMontage> Montage;
		FName Slot;
		bool bExpectMain = false;
	};

	static TArray<FPlayed> Played;

	static UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/** Plays Role from Set on Mesh and remembers where, or notes that there is no clip for it. */
	static void Play(FAutomationTestBase& Test, const FString& Who, USkeletalMeshComponent* Mesh, const UCombatAnimSet* Set,
		ECombatAnimRole Role, bool bExpectMain)
	{
		UAnimMontage* Montage = UCombatAnimSet::Resolve(Set, Role);
		if (!Montage || !Mesh)
		{
			Test.AddInfo(FString::Printf(TEXT("%s has no %s clip; its fallback is covered by CombatAnimTest."), *Who,
				*UEnum::GetValueAsString(Role)));
			return;
		}
		// Headless nothing is rendered; make sure the pose still updates so the slot weight can be read.
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		UAnimInstance* Instance = HawkeyeCombatAnim::Play(Mesh, Montage);
		Test.TestNotNull(*FString::Printf(TEXT("%s plays %s"), *Who, *Montage->GetName()), Instance);
		if (!Instance)
		{
			return;
		}
		const bool bOnMain = Instance == Mesh->GetAnimInstance();
		Test.TestEqual(*FString::Printf(TEXT("%s's %s plays on the %s instance"), *Who, *Montage->GetName(),
			bExpectMain ? TEXT("main") : TEXT("post-process")), bOnMain, bExpectMain);
		FPlayed Entry;
		Entry.Who = Who;
		Entry.Instance = Instance;
		Entry.Montage = Montage;
		Entry.Slot = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].SlotName : NAME_None;
		Entry.bExpectMain = bExpectMain;
		Played.Add(Entry);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeStartCombatClips, FAutomationTestBase*, Test);

bool FHawkeyeStartCombatClips::Update()
{
	using namespace HawkeyeCombatClipSmoke;
	Played.Reset();
	UWorld* World = FindGameWorld();
	if (!World)
	{
		Test->AddError(TEXT("No game world after opening the district."));
		return true;
	}
	AHawkeyeCharacter* Kate = nullptr;
	for (TActorIterator<AHawkeyeCharacter> It(World); It; ++It)
	{
		if (It->IsPlayerControlled())
		{
			Kate = *It;
		}
	}
	if (Test->TestNotNull(TEXT("Kate is in the district"), Kate))
	{
		Test->TestNotNull(TEXT("She wears an anim set"), Kate->GetCombatAnimSet());
		Play(*Test, TEXT("Kate"), Kate->GetMesh(), Kate->GetCombatAnimSet(), ECombatAnimRole::HitFront, true);
		Play(*Test, TEXT("Kate"), Kate->GetMesh(), Kate->GetCombatAnimSet(), ECombatAnimRole::BowDraw, false);
	}
	bool bThug = false;
	bool bArcher = false;
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const bool bIsArcher = It->ActorHasTag(FName(TEXT("ArcherPair")));
		if (bIsArcher && !bArcher)
		{
			bArcher = true;
			Play(*Test, TEXT("An archer"), It->GetMesh(), It->CombatAnimSet, ECombatAnimRole::BowDraw, false);
		}
		else if (!bIsArcher && !bThug && !It->IsHeavy())
		{
			bThug = true;
			Play(*Test, TEXT("A thug"), It->GetMesh(), It->CombatAnimSet, ECombatAnimRole::HitFront, false);
		}
	}
	Test->TestTrue(TEXT("A thug and an archer were found"), bThug && bArcher);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeCheckCombatClips, FAutomationTestBase*, Test);

bool FHawkeyeCheckCombatClips::Update()
{
	using namespace HawkeyeCombatClipSmoke;
	for (const FPlayed& Entry : Played)
	{
		UAnimInstance* Instance = Entry.Instance.Get();
		UAnimMontage* Montage = Entry.Montage.Get();
		if (!Instance || !Montage)
		{
			Test->AddError(FString::Printf(TEXT("%s's clip went away."), *Entry.Who));
			continue;
		}
		const float Weight = Instance->GetSlotMontageGlobalWeight(Entry.Slot);
		Test->TestTrue(*FString::Printf(TEXT("%s's %s is blended in through the %s slot (weight %.2f)"), *Entry.Who,
			*Montage->GetName(), *Entry.Slot.ToString(), Weight), Weight > 0.f);
		HawkeyeCombatAnim::Stop(Instance, Montage, 0.f);
	}
	Played.Reset();
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FHawkeyeStartClipSwing, FAutomationTestBase*, Test);

bool FHawkeyeStartClipSwing::Update()
{
	using namespace HawkeyeCombatClipSmoke;
	UWorld* World = FindGameWorld();
	AHawkeyeCharacter* Kate = nullptr;
	for (TActorIterator<AHawkeyeCharacter> It(World); World && It; ++It)
	{
		Kate = It->IsPlayerControlled() ? *It : Kate;
	}
	UMeleeComponent* Melee = Kate ? Kate->GetMeleeComponent() : nullptr;
	if (!Melee || !UCombatAnimSet::Resolve(Kate->GetCombatAnimSet(), ECombatAnimRole::Light1))
	{
		Test->AddInfo(TEXT("Kate has no Light1 clip; the notify-timed swing is covered by CombatAnimTest."));
		return true;
	}
	Kate->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Test->TestTrue(TEXT("Kate throws a light"), Kate->StartLightAttack());
	Test->TestNotNull(TEXT("It plays her Light1 clip"), Melee->GetCurrentMontage());
	Test->TestTrue(TEXT("Timed by the clip's ANS_HitWindow"), Melee->IsHitFromNotify());
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FHawkeyeCheckClipSwing, FAutomationTestBase*, Test, bool, bAfterWindow);

bool FHawkeyeCheckClipSwing::Update()
{
	using namespace HawkeyeCombatClipSmoke;
	UWorld* World = FindGameWorld();
	AHawkeyeCharacter* Kate = nullptr;
	for (TActorIterator<AHawkeyeCharacter> It(World); World && It; ++It)
	{
		Kate = It->IsPlayerControlled() ? *It : Kate;
	}
	UMeleeComponent* Melee = Kate ? Kate->GetMeleeComponent() : nullptr;
	if (!Melee || !Melee->IsHitFromNotify())
	{
		return true;
	}
	float Start = 0.f;
	float End = 0.f;
	HawkeyeCombatAnim::FindNotifyWindow(Melee->GetCurrentMontage(), UAnimNotifyState_HitWindow::StaticClass(), Start, End);
	if (bAfterWindow)
	{
		Test->TestTrue(*FString::Printf(TEXT("Past the hit window (%.2f s) the swing has struck and is recovering"), Start),
			Melee->GetPhase() == EMeleePhase::Recover || !Melee->IsAttacking());
	}
	else
	{
		Test->TestTrue(*FString::Printf(TEXT("Before the hit window (%.2f s) the swing is still winding up"), Start),
			Melee->IsWindingUp());
	}
	return true;
}

/**
 * A thug's strike clips, on the real thugs in the district, open their ANS_HitWindow when his telegraph
 * ends: the fists, the bat, the heavy's bash and his slow swing, each fitted by the melee component
 * (a rate between 0.8x and 1.3x, the start of a long wind-up skipped or the first frame of a short one
 * held). Swings them one at a time with the thugs not thinking, and measures in game time from the
 * swing starting to the wind-up ending. A thug with no clip for the role is noted and skipped.
 */
class FHawkeyeThugSwingTiming : public IAutomationLatentCommand
{
public:
	explicit FHawkeyeThugSwingTiming(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		using namespace HawkeyeCombatClipSmoke;
		UWorld* World = FindGameWorld();
		if (!World)
		{
			Test->AddError(TEXT("No game world for the thug swings."));
			return true;
		}
		if (!bListed)
		{
			List(World);
			bListed = true;
		}
		const double Now = World->GetTimeSeconds();
		if (Current >= 0)
		{
			FSwing& Swing = Swings[Current];
			AThugCharacter* Thug = Swing.Thug.Get();
			UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
			const float Elapsed = static_cast<float>(Now - Start);
			if (Melee && Melee->IsWindingUp() && Elapsed < Swing.Attack.WindupSeconds + 2.f)
			{
				MaxStep = FMath::Max(MaxStep, World->GetDeltaSeconds());
				return false;
			}
			// A frame either side: the notify and this poll both land on tick boundaries.
			const float Tolerance = FMath::Max(0.05f, MaxStep * 1.5f);
			Test->TestTrue(*FString::Printf(TEXT("%s's %s (%s) strikes at the end of its %.2f s telegraph (%.3f s, within %.3f)"),
				*GetNameSafe(Thug), *Swing.Attack.Name.ToString(), *Swing.Clip, Swing.Attack.WindupSeconds, Elapsed, Tolerance),
				FMath::Abs(Elapsed - Swing.Attack.WindupSeconds) <= Tolerance);
			if (Melee)
			{
				Melee->CancelAttack();
			}
			if (Thug)
			{
				Thug->Weapon = Swing.WeaponBefore;
			}
			Current = -1;
		}
		// The next swing, once the last one's clip has blended away.
		if (Now < NextAt)
		{
			return false;
		}
		while (++Next < Swings.Num())
		{
			FSwing& Swing = Swings[Next];
			AThugCharacter* Thug = Swing.Thug.Get();
			UMeleeComponent* Melee = Thug ? Thug->GetMeleeComponent() : nullptr;
			if (!Melee || !Melee->CanStartAttack())
			{
				continue;
			}
			Thug->Weapon = Swing.Weapon;
			if (!Melee->StartAttack(Swing.Attack) || !Melee->GetCurrentMontage() || !Melee->IsHitFromNotify())
			{
				Test->AddInfo(FString::Printf(TEXT("%s's %s has no clip with a hit window; its timer is covered by MeleeCombatTest."),
					*GetNameSafe(Thug), *Swing.Attack.Name.ToString()));
				Melee->CancelAttack();
				Thug->Weapon = Swing.WeaponBefore;
				continue;
			}
			Swing.Clip = Melee->GetCurrentMontage()->GetName();
			++Measured;
			Current = Next;
			Start = Now;
			MaxStep = 0.f;
			NextAt = Now + Swing.Attack.WindupSeconds + 0.6f;
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("Measured %d thug swings with clips."), Measured));
		return true;
	}

private:
	struct FSwing
	{
		TWeakObjectPtr<AThugCharacter> Thug;
		EThugWeapon Weapon = EThugWeapon::Fists;
		EThugWeapon WeaponBefore = EThugWeapon::Fists;
		FHawkeyeMeleeAttack Attack;
		FString Clip;
	};

	void List(UWorld* World)
	{
		AThugCharacter* Street = nullptr;
		AThugCharacter* Heavy = nullptr;
		for (TActorIterator<AThugCharacter> It(World); It; ++It)
		{
			if (AThugAIController* Brain = Cast<AThugAIController>(It->GetController()))
			{
				Brain->SetThinkingEnabled(false);
			}
			It->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			if (It->ActorHasTag(FName(TEXT("ArcherPair"))))
			{
				continue;
			}
			if (It->IsHeavy())
			{
				Heavy = Heavy ? Heavy : *It;
			}
			else if (!Street)
			{
				Street = *It;
			}
		}
		auto Add = [this](AThugCharacter* Thug, EThugWeapon Weapon, int32 SwingIndex)
		{
			if (!Thug)
			{
				return;
			}
			FSwing Swing;
			Swing.Thug = Thug;
			Swing.WeaponBefore = Thug->Weapon;
			Swing.Weapon = Weapon;
			Thug->Weapon = Weapon;
			Swing.Attack = Thug->GetMeleeAttack(SwingIndex);
			Thug->Weapon = Swing.WeaponBefore;
			Swings.Add(Swing);
		};
		Add(Street, EThugWeapon::Fists, 0);
		// Twice: the second fists swing takes the next Light1 variant (Jab To Elbow after Standing Melee Punch).
		Add(Street, EThugWeapon::Fists, 0);
		Add(Street, EThugWeapon::Bat, 0);
		Add(Heavy, EThugWeapon::Shield, 0);
		Add(Heavy, EThugWeapon::Shield, 1);
		Test->TestTrue(TEXT("A street thug and the heavy were found"), Street && Heavy);
	}

	FAutomationTestBase* Test;
	TArray<FSwing> Swings;
	bool bListed = false;
	int32 Current = -1;
	int32 Next = -1;
	int32 Measured = 0;
	double Start = 0.0;
	double NextAt = 0.0;
	float MaxStep = 0.f;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeSmokeThugClipTiming, "Hawkeye.Smoke.ThugClipsStrikeOnTheTelegraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeSmokeThugClipTiming::RunTest(const FString& Parameters)
{
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeThugSwingTiming(this));
	return true;
}

bool FHawkeyeSmokeCombatClipsPlay::RunTest(const FString& Parameters)
{
	AutomationOpenMap(TEXT("/Game/Maps/L_District_EastVillage"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(5.f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeStartCombatClips(this));
	// Past the 0.1 s blend in, inside the shortest clip (the 0.6 s hit reactions).
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.25f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCheckCombatClips(this));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.5f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeStartClipSwing(this));
	// Kate's Light1 clip opens its hit window 0.2 to 0.3 s in (the manifest's hit_s, start_s and rate;
	// a clip on the role defaults opens it at a quarter of its length or later): 0.1 s is before it,
	// and 1.3 s is past the end of the clip.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(0.1f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCheckClipSwing(this, false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FHawkeyeCheckClipSwing(this, true));
	return true;
}

#endif
