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
#endif
}

UStateTree* UHawkeyeThugTreeBuilder::BuildThugStateTree(const FString& PackageName)
{
#if WITH_EDITOR
	UStateTree* Tree = HawkeyeThugBuilder::LoadOrCreate<UStateTree>(PackageName);

	// Rebuilt from nothing every time, so the script is the one description of the tree.
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree, NAME_None, RF_Transactional);
	EditorData->Schema = NewObject<UStateTreeAIComponentSchema>(EditorData);
	Tree->EditorData = EditorData;

	UStateTreeState& Root = EditorData->AddSubTree(FName(TEXT("Thug")));
	struct FModeState
	{
		EThugMode Mode;
		const TCHAR* Name;
	};
	// Priority order: the root tries its children top to bottom.
	const FModeState States[] = {
		{ EThugMode::Stunned, TEXT("Stunned") },
		{ EThugMode::Reposition, TEXT("Reposition") },
		{ EThugMode::Cover, TEXT("Cover") },
		{ EThugMode::Attack, TEXT("Attack") },
		{ EThugMode::Investigate, TEXT("Investigate") },
		{ EThugMode::Patrol, TEXT("Patrol") },
	};
	for (const FModeState& Entry : States)
	{
		UStateTreeState& State = Root.AddChildState(FName(Entry.Name));
		if (Entry.Mode != EThugMode::Patrol)
		{
			State.AddEnterCondition<FHawkeyeThugNeedCondition>().GetNode().Need = Entry.Mode;
		}
		State.AddTask<FHawkeyeThugModeTask>().GetNode().Mode = Entry.Mode;
		State.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}

	FStateTreeCompilerLog Log;
	FStateTreeCompiler Compiler(Log);
	if (!Compiler.Compile(*Tree))
	{
		Log.DumpToLog(LogHawkeye);
		UE_LOG(LogHawkeye, Error, TEXT("BuildThugStateTree: %s did not compile."), *PackageName);
		return nullptr;
	}
	Tree->MarkPackageDirty();
	UE_LOG(LogHawkeye, Log, TEXT("BuildThugStateTree: %s built and compiled, %d states."), *PackageName, Root.Children.Num());
	return Tree;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildThugStateTree(%s): editor builds only."), *PackageName);
	return nullptr;
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
