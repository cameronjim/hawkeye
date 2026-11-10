#include "World/ThugTreeBuilder.h"

#include "Hawkeye.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_Donut.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
#include "StateTree.h"
#include "World/ThugEnvQueryContext.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "Misc/PackageName.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#include "World/ThugArcherTasks.h"
#include "World/ThugGunnerTasks.h"
#include "World/ThugMeleeTasks.h"
#include "World/ThugStateTreeNodes.h"
#endif

namespace HawkeyeThugBuilder
{
#if WITH_EDITOR
	template <typename TAsset>
	static TAsset* LoadOrCreate(const FString& PackageName)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		TAsset* Asset = LoadObject<TAsset>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Asset = NewObject<TAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Asset);
		}
		return Asset;
	}

	/** One sense a condition asks for, or its absence. */
	struct FSense
	{
		EThugSense Sense;
		bool bInvert = false;
	};

	static FSense Not(EThugSense Sense)
	{
		return { Sense, true };
	}

	/** Conditions over Senses on a state's entry or a transition, joined by Operand. */
	template <typename TOwner>
	static void AddSenses(TOwner&& AddOne, std::initializer_list<FSense> Senses, EStateTreeExpressionOperand Operand)
	{
		for (const FSense& Each : Senses)
		{
			FStateTreeEditorNode& Node = AddOne(Each);
			Node.ExpressionOperand = Operand;
		}
	}

	/** State is entered only when every one of Senses holds. */
	static void EnterWhen(UStateTreeState& State, std::initializer_list<FSense> Senses,
		EStateTreeExpressionOperand Operand = EStateTreeExpressionOperand::And)
	{
		AddSenses([&State](const FSense& Each) -> FStateTreeEditorNode&
			{ return State.AddEnterCondition<FHawkeyeThugSenseCondition>(Each.Sense, Each.bInvert); }, Senses, Operand);
	}

	/** Every tick: to Target when Senses hold (any of them with Or, all with And). */
	static void GoWhen(UStateTreeState& State, const UStateTreeState& Target, std::initializer_list<FSense> Senses,
		EStateTreeExpressionOperand Operand, EStateTreeTransitionPriority Priority = EStateTreeTransitionPriority::Normal)
	{
		FStateTreeTransition& Transition = State.AddTransition(EStateTreeTransitionTrigger::OnTick,
			EStateTreeTransitionType::GotoState, &Target);
		Transition.Priority = Priority;
		AddSenses([&State, &Transition](const FSense& Each) -> FStateTreeEditorNode&
			{ return Transition.AddConditionWithOuter<FHawkeyeThugSenseCondition>(&State, Each.Sense, Each.bInvert); },
			Senses, Operand);
	}

	/** When State's tasks finish with Trigger (succeeded, failed, either): to Target, if Sense is given and holds. */
	static void GoOn(UStateTreeState& State, EStateTreeTransitionTrigger Trigger, const UStateTreeState& Target,
		std::initializer_list<FSense> Senses = {})
	{
		FStateTreeTransition& Transition = State.AddTransition(Trigger, EStateTreeTransitionType::GotoState, &Target);
		AddSenses([&State, &Transition](const FSense& Each) -> FStateTreeEditorNode&
			{ return Transition.AddConditionWithOuter<FHawkeyeThugSenseCondition>(&State, Each.Sense, Each.bInvert); },
			Senses, EStateTreeExpressionOperand::And);
	}

	static UStateTreeState& AddState(UStateTreeState& Parent, FName Name, const TCHAR* Description)
	{
		UStateTreeState& State = Parent.AddChildState(Name);
		State.Description = Description;
		return State;
	}

	/** Stunned > Reposition > Cover > Attack > Investigate > Patrol, each leaving for the root when it should yield. */
	static void AddTopLevel(UStateTreeState& Root);

	/** The gunner's cover cycle: hide, relocate or peek, a burst, back. */
	static void AddCover(UStateTreeState& Root);

	/** The fight by weapon, under Attack. */
	static void AddAttack(UStateTreeState& Root, UStateTreeState& Cover);

	static void DescribeState(const UStateTreeState& State, FString& Out)
	{
		Out += State.Name.ToString();
		if (State.Children.IsEmpty())
		{
			return;
		}
		Out += TEXT("(");
		for (int32 Index = 0; Index < State.Children.Num(); ++Index)
		{
			Out += Index > 0 ? TEXT(", ") : TEXT("");
			DescribeState(*State.Children[Index], Out);
		}
		Out += TEXT(")");
	}
#endif
}

#if WITH_EDITOR
void HawkeyeThugBuilder::AddTopLevel(UStateTreeState& Root)
{
	using enum EThugSense;
	using enum EStateTreeExpressionOperand;
	constexpr EStateTreeTransitionPriority High = EStateTreeTransitionPriority::High;

	UStateTreeState& StunnedState = AddState(Root, HawkeyeThugStates::Stunned, TEXT("Held, staggered, down, dead or switched off."));
	EnterWhen(StunnedState, { { Stunned } });
	StunnedState.AddTask<FHawkeyeThugStunnedTask>();
	GoWhen(StunnedState, Root, { Not(Stunned) }, And);

	UStateTreeState& Reposition = AddState(Root, HawkeyeThugStates::Reposition, TEXT("Getting room: the gunner backs away, the archer zips to another roof."));
	EnterWhen(Reposition, { { Engaged } });
	GoWhen(Reposition, Root, { { Stunned } }, And, High);
	UStateTreeState& BackAway = AddState(Reposition, HawkeyeThugStates::BackAway, TEXT("She closed inside RetreatTriggerDistance."));
	EnterWhen(BackAway, { { Gunner }, { Crowded } });
	BackAway.AddTask<FHawkeyeGunnerBackAwayTask>();
	GoWhen(BackAway, Root, { Not(Engaged) }, And);
	GoOn(BackAway, EStateTreeTransitionTrigger::OnStateCompleted, Root);
	UStateTreeState& ArcherZip = AddState(Reposition, HawkeyeThugStates::ArcherZip, TEXT("She closed in, or is out of his reach."));
	EnterWhen(ArcherZip, { { RoofWanted } });
	ArcherZip.AddTask<FHawkeyeArcherZipTask>();
	GoOn(ArcherZip, EStateTreeTransitionTrigger::OnStateCompleted, Root);

	AddCover(Root);
	AddAttack(Root, *Root.Children.Last());

	UStateTreeState& Investigate = AddState(Root, HawkeyeThugStates::Investigate, TEXT("Suspicious: to the noise, look round."));
	EnterWhen(Investigate, { { Suspicious } });
	Investigate.AddTask<FHawkeyeThugInvestigateTask>();
	GoWhen(Investigate, Root, { { Stunned }, Not(Suspicious) }, Or);

	UStateTreeState& Patrol = AddState(Root, HawkeyeThugStates::Patrol, TEXT("Calm: walk the patrol points."));
	Patrol.AddTask<FHawkeyeThugPatrolTask>();
	GoWhen(Patrol, Root, { { Stunned }, Not(Calm) }, Or);
}

void HawkeyeThugBuilder::AddCover(UStateTreeState& Root)
{
	using enum EThugSense;
	using enum EStateTreeExpressionOperand;
	using enum EStateTreeTransitionTrigger;

	UStateTreeState& Cover = AddState(Root, HawkeyeThugStates::Cover, TEXT("The gunner holds a cover point."));
	EnterWhen(Cover, { { Engaged }, { Gunner }, { HasCover }, Not(TargetDown) });
	GoWhen(Cover, Root, { { Stunned }, Not(Engaged), { Crowded }, { TargetDown } }, Or, EStateTreeTransitionPriority::High);

	UStateTreeState& Hide = AddState(Cover, HawkeyeThugStates::Hide, TEXT("To the cover point; hidden CoverHideSeconds."));
	Hide.AddTask<FHawkeyeGunnerHideTask>();
	UStateTreeState& Relocate = AddState(Cover, HawkeyeThugStates::Relocate, TEXT("Held this cover RelocateSeconds: find another."));
	Relocate.AddTask<FHawkeyeGunnerRelocateTask>();
	UStateTreeState& Peek = AddState(Cover, HawkeyeThugStates::Peek, TEXT("Out for one burst."));
	Peek.AddTask<FHawkeyeGunnerStanceTask>();
	UStateTreeState& StepOut = AddState(Peek, HawkeyeThugStates::StepOut, TEXT("To the spot beside cover with a line on her."));
	StepOut.AddTask<FHawkeyeGunnerStepOutTask>();
	UStateTreeState& PeekBurst = AddState(Peek, HawkeyeThugStates::PeekBurst, TEXT("Telegraph, then the burst."));
	PeekBurst.AddTask<FHawkeyeGunnerBurstTask>();

	GoOn(Hide, OnStateSucceeded, Relocate, { { CoverStale } });
	GoOn(Hide, OnStateSucceeded, Peek);
	GoOn(Relocate, OnStateSucceeded, Hide);
	GoOn(Relocate, OnStateFailed, Peek);
	GoOn(StepOut, OnStateSucceeded, PeekBurst);
	GoOn(StepOut, OnStateFailed, Hide);
	GoOn(PeekBurst, OnStateSucceeded, Hide, { { CoverOwed } });
	GoOn(PeekBurst, OnStateSucceeded, PeekBurst);
	GoOn(PeekBurst, OnStateFailed, Hide);
}

void HawkeyeThugBuilder::AddAttack(UStateTreeState& Root, UStateTreeState& Cover)
{
	using enum EThugSense;
	using enum EStateTreeExpressionOperand;
	using enum EStateTreeTransitionTrigger;
	constexpr EStateTreeTransitionPriority High = EStateTreeTransitionPriority::High;
	constexpr EStateTreeTransitionPriority Medium = EStateTreeTransitionPriority::Medium;

	UStateTreeState& Attack = AddState(Root, HawkeyeThugStates::Attack, TEXT("Alerted: the fight, by weapon."));
	EnterWhen(Attack, { { Alerted } });
	Attack.AddTask<FHawkeyeThugEngageTask>();
	GoWhen(Attack, Root, { { Stunned }, Not(Alerted) }, Or, High);
	GoWhen(Attack, Root, { { Engaged }, { Gunner }, { Crowded } }, And, High);
	GoWhen(Attack, Root, { { Engaged }, { RoofWanted } }, And, High);

	UStateTreeState& HoldFire = AddState(Attack, HawkeyeThugStates::HoldFire, TEXT("Nothing to shoot at: no body to the noise, or smoke."));
	EnterWhen(HoldFire, { Not(Engaged) });
	HoldFire.AddTask<FHawkeyeThugHoldFireTask>();
	GoWhen(HoldFire, Root, { { Engaged } }, And);

	UStateTreeState& StandOff = AddState(Attack, HawkeyeThugStates::StandOff, TEXT("She is down: circle, nobody piles on."));
	EnterWhen(StandOff, { { TargetDown } });
	StandOff.AddTask<FHawkeyeThugStandOffTask>();
	GoWhen(StandOff, Root, { Not(TargetDown) }, And);

	UStateTreeState& GunnerState = AddState(Attack, HawkeyeThugStates::Gunner, TEXT("Pistol."));
	EnterWhen(GunnerState, { { Gunner } });
	GunnerState.AddTask<FHawkeyeGunnerStanceTask>();
	GoWhen(GunnerState, Attack, { Not(Engaged), { TargetDown } }, Or, Medium);
	UStateTreeState& Approach = AddState(GunnerState, HawkeyeThugStates::Approach, TEXT("Beyond EngageRange: close in."));
	EnterWhen(Approach, { { OutOfRange } });
	Approach.AddTask<FHawkeyeGunnerApproachTask>();
	GoWhen(Approach, GunnerState, { Not(OutOfRange) }, And);
	UStateTreeState& TakeCover = AddState(GunnerState, HawkeyeThugStates::TakeCover, TEXT("Aimed at, or a burst's worth of shots since cover."));
	EnterWhen(TakeCover, { { AimedAt }, { CoverOwed } }, Or);
	TakeCover.AddTask<FHawkeyeGunnerTakeCoverTask>();
	UStateTreeState& Burst = AddState(GunnerState, HawkeyeThugStates::Burst, TEXT("Telegraph, then the burst, from where he stands."));
	Burst.AddTask<FHawkeyeGunnerBurstTask>();
	GoOn(TakeCover, OnStateSucceeded, Cover);
	GoOn(TakeCover, OnStateFailed, Burst);
	GoWhen(Burst, GunnerState, { { OutOfRange } }, And);
	GoWhen(Burst, TakeCover, { { AimedAt }, { CanSearchCover } }, And);
	GoOn(Burst, OnStateCompleted, GunnerState);

	UStateTreeState& ArcherState = AddState(Attack, HawkeyeThugStates::Archer, TEXT("Bow."));
	EnterWhen(ArcherState, { { Archer } });
	GoWhen(ArcherState, Attack, { Not(Engaged), { TargetDown } }, Or, Medium);
	UStateTreeState& Draw = AddState(ArcherState, HawkeyeThugStates::Draw, TEXT("Keep the band, draw, loose with lead."));
	Draw.AddTask<FHawkeyeArcherDrawTask>();
	GoOn(Draw, OnStateCompleted, ArcherState);

	UStateTreeState& Melee = AddState(Attack, HawkeyeThugStates::Melee, TEXT("Fists, bat or shield."));
	GoWhen(Melee, Attack, { Not(Engaged), { TargetDown } }, Or, Medium);
	UStateTreeState& BackOff = AddState(Melee, HawkeyeThugStates::BackOff, TEXT("A pair of swings done: step back."));
	EnterWhen(BackOff, { { BackOffOwed } });
	BackOff.AddTask<FHawkeyeMeleeBackOffTask>();
	GoOn(BackOff, OnStateCompleted, Melee);
	UStateTreeState& Swing = AddState(Melee, HawkeyeThugStates::Swing, TEXT("His turn: close, wind up, swing."));
	EnterWhen(Swing, { { HoldsToken } });
	Swing.AddTask<FHawkeyeMeleeSwingTask>();
	GoOn(Swing, OnStateCompleted, Melee);
	UStateTreeState& HoldRing = AddState(Melee, HawkeyeThugStates::HoldRing, TEXT("Not his turn: a place on the ring round her."));
	HoldRing.AddTask<FHawkeyeMeleeHoldRingTask>();
	GoOn(HoldRing, OnStateCompleted, Melee);
}
#endif

UStateTree* UHawkeyeThugTreeBuilder::BuildThugStateTree(const FString& PackageName)
{
#if WITH_EDITOR
	UStateTree* Tree = HawkeyeThugBuilder::LoadOrCreate<UStateTree>(PackageName);
	if (!FillThugStateTree(Tree))
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildThugStateTree: %s did not compile."), *PackageName);
		return nullptr;
	}
	Tree->MarkPackageDirty();
	UE_LOG(LogHawkeye, Log, TEXT("BuildThugStateTree: %s built and compiled: %s."), *PackageName, *DescribeThugStateTree(Tree));
	return Tree;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildThugStateTree(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}

bool UHawkeyeThugTreeBuilder::FillThugStateTree(UStateTree* Tree)
{
#if WITH_EDITOR
	if (!Tree)
	{
		return false;
	}
	// Rebuilt from nothing every time, so this is the one description of the tree.
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree, NAME_None, RF_Transactional);
	EditorData->Schema = NewObject<UStateTreeAIComponentSchema>(EditorData);
	Tree->EditorData = EditorData;
	UStateTreeState& Root = EditorData->AddSubTree(HawkeyeThugStates::Root);
	Root.Description = TEXT("Tries its children by priority; each leaves for here when a higher one is wanted.");
	HawkeyeThugBuilder::AddTopLevel(Root);

	FStateTreeCompilerLog Log;
	FStateTreeCompiler Compiler(Log);
	if (!Compiler.Compile(*Tree))
	{
		Log.DumpToLog(LogHawkeye);
		return false;
	}
	return true;
#else
	return false;
#endif
}

int32 UHawkeyeThugTreeBuilder::CountThugStates(UStateTree* Tree)
{
#if WITH_EDITOR
	const UStateTreeEditorData* EditorData = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
	if (!EditorData || EditorData->SubTrees.IsEmpty() || !EditorData->SubTrees[0])
	{
		return -1;
	}
	return EditorData->SubTrees[0]->Children.Num();
#else
	return -1;
#endif
}

FString UHawkeyeThugTreeBuilder::DescribeThugStateTree(UStateTree* Tree)
{
	FString Out;
#if WITH_EDITOR
	const UStateTreeEditorData* EditorData = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
	if (EditorData && !EditorData->SubTrees.IsEmpty() && EditorData->SubTrees[0])
	{
		HawkeyeThugBuilder::DescribeState(*EditorData->SubTrees[0], Out);
	}
#endif
	return Out;
}

void UHawkeyeThugTreeBuilder::FillCoverQuery(UEnvQuery* Query, float OuterRadius)
{
	if (!Query)
	{
		return;
	}
	TArray<TObjectPtr<UEnvQueryOption>>& Options = Query->GetOptionsMutable();
	Options.Reset();

	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query, NAME_None, RF_Transactional);

	// The same candidates the C++ ring uses: three rings of twelve round him, on the navmesh.
	UEnvQueryGenerator_Donut* Donut = NewObject<UEnvQueryGenerator_Donut>(Option, NAME_None, RF_Transactional);
	Donut->InnerRadius.DefaultValue = OuterRadius * 0.15f;
	Donut->OuterRadius.DefaultValue = OuterRadius;
	Donut->NumberOfRings.DefaultValue = 3;
	Donut->PointOnRingSpacingMethod = EEnvQueryPointSpacingMethod::ByNumberOfPoints;
	Donut->PointsPerRing.DefaultValue = 12;
	Donut->Center = UEnvQueryContext_Querier::StaticClass();
	Option->Generator = Donut;

	// Kept only where the line from the target to the point is blocked.
	UEnvQueryTest_Trace* Trace = NewObject<UEnvQueryTest_Trace>(Option, NAME_None, RF_Transactional);
	Trace->Context = UHawkeyeEnvQueryContext_ThugTarget::StaticClass();
	Trace->TraceFromContext.DefaultValue = true;
	Trace->ItemHeightOffset.DefaultValue = 90.f;
	Trace->ContextHeightOffset.DefaultValue = 60.f;
	Trace->TestPurpose = EEnvTestPurpose::Filter;
	Trace->FilterType = EEnvTestFilterType::Match;
	Trace->BoolValue.DefaultValue = true;
	Trace->TestOrder = 0;

	// Nearest first.
	UEnvQueryTest_Distance* Distance = NewObject<UEnvQueryTest_Distance>(Option, NAME_None, RF_Transactional);
	Distance->DistanceTo = UEnvQueryContext_Querier::StaticClass();
	Distance->TestPurpose = EEnvTestPurpose::Score;
	Distance->ScoringEquation = EEnvTestScoreEquation::InverseLinear;
	Distance->TestOrder = 1;

	Option->Tests = { Trace, Distance };
	Options.Add(Option);
}

UEnvQuery* UHawkeyeThugTreeBuilder::BuildCoverQuery(const FString& PackageName)
{
#if WITH_EDITOR
	UEnvQuery* Query = HawkeyeThugBuilder::LoadOrCreate<UEnvQuery>(PackageName);
	FillCoverQuery(Query, 800.f);
	Query->MarkPackageDirty();
	UE_LOG(LogHawkeye, Log, TEXT("BuildCoverQuery: %s built: %s."), *PackageName, *DescribeCoverQuery(Query));
	return Query;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildCoverQuery(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}

FString UHawkeyeThugTreeBuilder::DescribeCoverQuery(UEnvQuery* Query)
{
	if (!Query || Query->GetOptions().IsEmpty() || !Query->GetOptions()[0])
	{
		return TEXT("empty");
	}
	const UEnvQueryOption* Option = Query->GetOptions()[0];
	TArray<FString> Tests;
	for (const UEnvQueryTest* Test : Option->Tests)
	{
		Tests.Add(Test ? Test->GetClass()->GetName().Replace(TEXT("EnvQueryTest_"), TEXT("")) : TEXT("null"));
	}
	const UEnvQueryGenerator_Donut* Donut = Cast<UEnvQueryGenerator_Donut>(Option->Generator);
	return FString::Printf(TEXT("generator %s (outer %.0f cm), tests %s"),
		Option->Generator ? *Option->Generator->GetClass()->GetName().Replace(TEXT("EnvQueryGenerator_"), TEXT("")) : TEXT("null"),
		Donut ? Donut->OuterRadius.DefaultValue : 0.f, *FString::Join(Tests, TEXT("+")));
}
