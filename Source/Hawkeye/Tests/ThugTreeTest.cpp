#include "Combat/ArrowDefinition.h"
#include "Combat/BowComponent.h"
#include "Combat/BowDefinition.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "StateTree.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/GrappleAnchor.h"
#include "World/ThugAIController.h"
#include "World/ThugCharacter.h"
#include "World/ThugTreeBuilder.h"
#include "World/ThugTreeComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * ST_Thug is the one place a thug's decisions live (claude-docs/architecture.md, "Thugs"): the tree compiles and
 * matches what the builder writes, every thug runs it, each sensed situation lands on the leaf the design names,
 * and a thug never runs two fight tasks at once.
 */
namespace HawkeyeThugTreeTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	static const TCHAR* TreePath = TEXT("/Game/Blueprints/AI/Thug/ST_Thug.ST_Thug");

	/** The leaves a fight runs; a thug's active path holds at most one. */
	static const FName FightLeaves[] = { HawkeyeThugStates::BackAway, HawkeyeThugStates::ArcherZip, HawkeyeThugStates::Hide,
		HawkeyeThugStates::Relocate, HawkeyeThugStates::StepOut, HawkeyeThugStates::PeekBurst, HawkeyeThugStates::HoldFire,
		HawkeyeThugStates::StandOff, HawkeyeThugStates::Approach, HawkeyeThugStates::TakeCover, HawkeyeThugStates::Burst,
		HawkeyeThugStates::Draw, HawkeyeThugStates::BackOff, HawkeyeThugStates::Swing, HawkeyeThugStates::HoldRing };

	static AThugAIController* SpawnThug(const FHawkeyeTestWorld& TestWorld, const FVector& Location, EThugWeapon Weapon,
		AThugCharacter*& OutThug)
	{
		OutThug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), Location, FRotator::ZeroRotator));
		AThugAIController* Brain = Cast<AThugAIController>(
			TestWorld.SpawnActor(AThugAIController::StaticClass(), Location, FRotator::ZeroRotator));
		if (!OutThug || !Brain)
		{
			return nullptr;
		}
		OutThug->Weapon = Weapon;
		Brain->Possess(OutThug);
		return Brain;
	}

	static AHawkeyeCharacter* SpawnKate(const FHawkeyeTestWorld& TestWorld, const FVector& Location)
	{
		return Cast<AHawkeyeCharacter>(TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), Location, FRotator::ZeroRotator));
	}

	static void Alert(AThugAIController* Brain, AActor* Target)
	{
		Brain->SetTarget(Target);
		Brain->ReportStimulus(EStimulusKind::Hearing, Target->GetActorLocation(), true, Brain->GunshotLoudnessThreshold);
	}

	static FName LeafOf(const AThugAIController* Brain)
	{
		const TArray<FName> Path = Brain->StateTreeComponent->GetActiveStatePath();
		return Path.IsEmpty() ? NAME_None : Path.Last();
	}

	static FString PathOf(const AThugAIController* Brain)
	{
		TArray<FString> Names;
		for (const FName& Name : Brain->StateTreeComponent->GetActiveStatePath())
		{
			Names.Add(Name.ToString());
		}
		return FString::Join(Names, TEXT(" > "));
	}

	/** Thinks in 0.25 s steps until Leaf is active or MaxSteps run out; true when it was reached. */
	static bool StepUntil(AThugAIController* Brain, FName Leaf, int32 MaxSteps)
	{
		for (int32 Step = 0; Step < MaxSteps && LeafOf(Brain) != Leaf; ++Step)
		{
			ThinkThug(Brain, 0.25f);
		}
		return LeafOf(Brain) == Leaf;
	}

	/** BP_Archer's bow, as EnemyTypesTest arms him. */
	static void ArmArcher(AThugCharacter* Thug)
	{
		UBowDefinition* Bow = NewObject<UBowDefinition>(Thug);
		Bow->FullDrawSeconds = 1.2f;
		Bow->MaxSpeed = 5000.f;
		UArrowDefinition* Arrow = NewObject<UArrowDefinition>(Thug);
		Arrow->Slot = 1;
		Arrow->Damage = 30.f;
		Thug->GetBowComponent()->OwnBow = Bow;
		Thug->GetBowComponent()->OwnArrow = Arrow;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugTreeCompiles, "Hawkeye.ThugTree.Compiles", HawkeyeThugTreeTest::Flags)

bool FHawkeyeThugTreeCompiles::RunTest(const FString& Parameters)
{
	UStateTree* Built = NewObject<UStateTree>(GetTransientPackage());
	if (!TestTrue(TEXT("The builder's tree compiles (errors are in LogHawkeye)"), UHawkeyeThugTreeBuilder::FillThugStateTree(Built)))
	{
		return false;
	}
	TestTrue(TEXT("And is ready to run"), Built->IsReadyToRun());
	TestEqual(TEXT("Its shape: priority at the root, the fight by weapon under Attack"),
		UHawkeyeThugTreeBuilder::DescribeThugStateTree(Built),
		FString(TEXT("Thug(Stunned, Reposition(BackAway, ArcherZip), Cover(Hide, Relocate, Peek(StepOut, PeekBurst)), "
					 "Attack(HoldFire, StandOff, Gunner(Approach, TakeCover, Burst), Archer(Draw), Melee(BackOff, Swing, HoldRing)), "
					 "Investigate, Patrol)")));

	UStateTree* Asset = LoadObject<UStateTree>(nullptr, HawkeyeThugTreeTest::TreePath);
	if (!TestNotNull(TEXT("ST_Thug exists"), Asset))
	{
		return false;
	}
	TestTrue(TEXT("ST_Thug is ready to run"), Asset->IsReadyToRun());
	TestEqual(TEXT("ST_Thug on disk is what the builder writes (if not, run Tools/Editor/create_enemies.py)"),
		UHawkeyeThugTreeBuilder::DescribeThugStateTree(Asset), UHawkeyeThugTreeBuilder::DescribeThugStateTree(Built));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugTreeCppThug, "Hawkeye.ThugTree.CppThugRunsTree", HawkeyeThugTreeTest::Flags)

bool FHawkeyeThugTreeCppThug::RunTest(const FString& Parameters)
{
	using namespace HawkeyeThugTreeTest;
	FHawkeyeTestWorld TestWorld;
	AThugCharacter* Thug = nullptr;
	AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, EThugWeapon::Fists, Thug);
	if (!Brain)
	{
		AddError(TEXT("Could not spawn a thug."));
		return false;
	}
	TestNull(TEXT("A thug spawned from C++ names no tree of his own"), Thug->ThugStateTree.Get());
	TestTrue(TEXT("He runs a tree all the same"), Brain->StateTreeComponent->IsRunning());
	TestEqual(TEXT("ST_Thug"), Brain->DefaultStateTree.ToSoftObjectPath().ToString(), FString(TreePath));
	ThinkThug(Brain, 0.25f);
	TestEqual(TEXT("Calm, he patrols"), PathOf(Brain), FString(TEXT("Thug > Patrol")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugTreeLeaves, "Hawkeye.ThugTree.LeafFromSituation", HawkeyeThugTreeTest::Flags)

bool FHawkeyeThugTreeLeaves::RunTest(const FString& Parameters)
{
	using namespace HawkeyeThugTreeTest;
	struct FSituation
	{
		const TCHAR* Name;
		EThugWeapon Weapon;
		FVector Kate;
		/** Sets the scene after the thug, Kate (and her anchors) are in; false when it could not. */
		TFunction<bool(const FHawkeyeTestWorld&, AThugAIController*, AThugCharacter*, AHawkeyeCharacter*)> Setup;
		FName Leaf;
	};
	const TArray<FSituation> Situations = {
		{ TEXT("calm"), EThugWeapon::Bat, FVector(1000.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld&, AThugAIController*, AThugCharacter*, AHawkeyeCharacter*) { return true; },
			HawkeyeThugStates::Patrol },
		{ TEXT("suspicious"), EThugWeapon::Bat, FVector(1000.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld&, AThugAIController* Brain, AThugCharacter*, AHawkeyeCharacter* Kate)
			{
				Brain->ReportStimulus(EStimulusKind::Hearing, Kate->GetActorLocation(), true, 1.f);
				return true;
			},
			HawkeyeThugStates::Investigate },
		{ TEXT("alerted melee off the ring, no token"), EThugWeapon::Bat, FVector(1000.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld&, AThugAIController* Brain, AThugCharacter*, AHawkeyeCharacter* Kate)
			{
				Alert(Brain, Kate);
				return true;
			},
			HawkeyeThugStates::HoldRing },
		{ TEXT("alerted melee in reach, given the token"), EThugWeapon::Bat, FVector(200.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld&, AThugAIController* Brain, AThugCharacter*, AHawkeyeCharacter* Kate)
			{
				Alert(Brain, Kate);
				return true;
			},
			HawkeyeThugStates::Swing },
		{ TEXT("target down"), EThugWeapon::Bat, FVector(200.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld&, AThugAIController* Brain, AThugCharacter* Thug, AHawkeyeCharacter* Kate)
			{
				Alert(Brain, Kate);
				ThinkThug(Brain, 0.25f);
				Kate->GetHealthComponent()->ApplyDamage(1000.f, Thug);
				return AThugAIController::IsTargetDown(Kate);
			},
			HawkeyeThugStates::StandOff },
		{ TEXT("stunned mid-swing"), EThugWeapon::Bat, FVector(200.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld&, AThugAIController* Brain, AThugCharacter*, AHawkeyeCharacter* Kate)
			{
				Alert(Brain, Kate);
				const bool bSwinging = StepUntil(Brain, HawkeyeThugStates::Swing, 4);
				Brain->SetHeld(true);
				return bSwinging;
			},
			HawkeyeThugStates::Stunned },
		{ TEXT("gunner in the open, aimed at"), EThugWeapon::Pistol, FVector(-500.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld& TestWorld, AThugAIController* Brain, AThugCharacter*, AHawkeyeCharacter* Kate)
			{
				// A crate off to his side to duck behind; she faces him and raises her aim.
				AHawkeyeTestBlocker* Crate = Cast<AHawkeyeTestBlocker>(
					TestWorld.SpawnActor(AHawkeyeTestBlocker::StaticClass(), FVector(-200.f, 350.f, 0.f), FRotator::ZeroRotator));
				Crate->SetExtent(FVector(100.f, 150.f, 200.f));
				Kate->StartAim();
				Alert(Brain, Kate);
				ThinkThug(Brain, 0.25f);
				return Brain->IsAimedAt();
			},
			HawkeyeThugStates::Hide },
		{ TEXT("archer crowded"), EThugWeapon::Bow, FVector(500.f, 0.f, 0.f),
			[](const FHawkeyeTestWorld& TestWorld, AThugAIController* Brain, AThugCharacter* Thug, AHawkeyeCharacter* Kate)
			{
				ArmArcher(Thug);
				TestWorld.SpawnActor(AGrappleAnchor::StaticClass(), FVector(-1500.f, 0.f, 0.f), FRotator::ZeroRotator);
				Alert(Brain, Kate);
				return true;
			},
			HawkeyeThugStates::ArcherZip },
	};

	for (const FSituation& Situation : Situations)
	{
		FHawkeyeTestWorld TestWorld;
		AThugCharacter* Thug = nullptr;
		AThugAIController* Brain = SpawnThug(TestWorld, FVector::ZeroVector, Situation.Weapon, Thug);
		AHawkeyeCharacter* Kate = SpawnKate(TestWorld, Situation.Kate);
		if (!Brain || !Kate)
		{
			AddError(FString::Printf(TEXT("%s: could not spawn the thug and Kate."), Situation.Name));
			continue;
		}
		// She faces him, so her aim (when she raises it) is on him.
		Kate->SetActorRotation((Thug->GetActorLocation() - Kate->GetActorLocation()).Rotation());
		if (!TestTrue(FString::Printf(TEXT("%s: the scene is set"), Situation.Name), Situation.Setup(TestWorld, Brain, Thug, Kate)))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: %s within 2 s (at %s)"), Situation.Name, *Situation.Leaf.ToString(), *PathOf(Brain)),
			StepUntil(Brain, Situation.Leaf, 8));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeThugTreeOneTask, "Hawkeye.ThugTree.OneFightTaskAtATime", HawkeyeThugTreeTest::Flags)

bool FHawkeyeThugTreeOneTask::RunTest(const FString& Parameters)
{
	using namespace HawkeyeThugTreeTest;
	FHawkeyeTestWorld TestWorld;
	AHawkeyeCharacter* Kate = SpawnKate(TestWorld, FVector::ZeroVector);
	const EThugWeapon Weapons[] = { EThugWeapon::Fists, EThugWeapon::Bat, EThugWeapon::Fists, EThugWeapon::Pistol };
	TArray<AThugAIController*> Brains;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Weapons); ++Index)
	{
		AThugCharacter* Thug = nullptr;
		const FVector At = FRotator(0.f, Index * 90.f, 0.f).Vector() * (Weapons[Index] == EThugWeapon::Pistol ? 700.f : 250.f);
		if (AThugAIController* Brain = SpawnThug(TestWorld, At, Weapons[Index], Thug))
		{
			Brains.Add(Brain);
		}
	}
	if (!Kate || Brains.Num() != UE_ARRAY_COUNT(Weapons))
	{
		AddError(TEXT("Could not spawn Kate and four thugs."));
		return false;
	}
	for (AThugAIController* Brain : Brains)
	{
		Alert(Brain, Kate);
	}

	int32 Violations = 0;
	TSet<FName> Seen;
	for (int32 Step = 0; Step < 40; ++Step)
	{
		TestWorld.AdvanceClock(0.25f);
		for (AThugAIController* Brain : Brains)
		{
			ThinkThug(Brain, 0.25f);
			const TArray<FName> Path = Brain->StateTreeComponent->GetActiveStatePath();
			int32 FightTasks = 0;
			for (const FName& Name : Path)
			{
				FightTasks += UE_ARRAY_COUNT(FightLeaves) > 0 && TArrayView<const FName>(FightLeaves).Contains(Name) ? 1 : 0;
			}
			Seen.Add(Path.IsEmpty() ? NAME_None : Path.Last());
			if (FightTasks > 1)
			{
				++Violations;
				AddError(FString::Printf(TEXT("Step %d: %s runs %d fight tasks (%s)."), Step, *GetNameSafe(Brain->GetPawn()), FightTasks,
					*PathOf(Brain)));
			}
		}
	}
	TestEqual(TEXT("No thug ever runs two fight tasks"), Violations, 0);
	TestTrue(TEXT("The melee thugs took turns: some held the ring while one swung"),
		Seen.Contains(HawkeyeThugStates::HoldRing) && Seen.Contains(HawkeyeThugStates::Swing));
	return true;
}

#endif
