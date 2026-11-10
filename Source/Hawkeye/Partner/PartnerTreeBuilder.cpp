#include "Partner/PartnerTreeBuilder.h"

#include "Hawkeye.h"
#include "StateTree.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "Misc/PackageName.h"
#include "Partner/PartnerStateTreeNodes.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"
#endif

UStateTree* UHawkeyePartnerTreeBuilder::BuildPartnerStateTree(const FString& PackageName)
{
#if WITH_EDITOR
	const FString AssetName = FPackageName::GetShortName(PackageName);
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;

	UStateTree* Tree = LoadObject<UStateTree>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Tree)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Tree = NewObject<UStateTree>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Tree);
	}

	// Rebuilt from nothing every time, so the script is the one description of the tree.
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree, NAME_None, RF_Transactional);
	EditorData->Schema = NewObject<UStateTreeAIComponentSchema>(EditorData);
	Tree->EditorData = EditorData;

	UStateTreeState& Root = EditorData->AddSubTree(FName(TEXT("Partner")));
	struct FModeState
	{
		EHawkeyePartnerMode Mode;
		const TCHAR* Name;
	};
	// Priority order: the root tries its children top to bottom.
	const FModeState States[] = {
		{ EHawkeyePartnerMode::Revive, TEXT("Revive") },
		{ EHawkeyePartnerMode::GoToMark, TEXT("GoToMark") },
		{ EHawkeyePartnerMode::Cover, TEXT("Cover") },
		{ EHawkeyePartnerMode::Attack, TEXT("Attack") },
		{ EHawkeyePartnerMode::Follow, TEXT("Follow") },
	};
	for (const FModeState& Entry : States)
	{
		UStateTreeState& State = Root.AddChildState(FName(Entry.Name));
		if (Entry.Mode != EHawkeyePartnerMode::Follow)
		{
			State.AddEnterCondition<FHawkeyePartnerNeedCondition>().GetNode().Need = Entry.Mode;
		}
		State.AddTask<FHawkeyePartnerModeTask>().GetNode().Mode = Entry.Mode;
		State.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}

	FStateTreeCompilerLog Log;
	FStateTreeCompiler Compiler(Log);
	if (!Compiler.Compile(*Tree))
	{
		Log.DumpToLog(LogHawkeye);
		UE_LOG(LogHawkeye, Error, TEXT("BuildPartnerStateTree: %s did not compile."), *ObjectPath);
		return nullptr;
	}
	Tree->MarkPackageDirty();
	UE_LOG(LogHawkeye, Log, TEXT("BuildPartnerStateTree: %s built and compiled, %d states."), *ObjectPath,
		Root.Children.Num());
	return Tree;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildPartnerStateTree(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}

int32 UHawkeyePartnerTreeBuilder::CountPartnerStates(UStateTree* Tree)
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
