#include "Player/RetargetGraphBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Hawkeye.h"

#if WITH_EDITOR
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "AnimGraphNode_Root.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Retargeter/IKRetargeter.h"
#include "UObject/Package.h"

namespace HawkeyeRetargetGraph
{
	static UEdGraph* FindAnimGraph(UAnimBlueprint& Blueprint)
	{
		for (UEdGraph* Graph : Blueprint.FunctionGraphs)
		{
			if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	static UAnimBlueprint* LoadOrCreate(const FString& PackageName, USkeleton* Skeleton)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (UAnimBlueprint* Existing =
				LoadObject<UAnimBlueprint>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = UAnimInstance::StaticClass();
		Factory->TargetSkeleton = Skeleton;
		Factory->BlueprintType = BPTYPE_Normal;
		UPackage* Package = CreatePackage(*PackageName);
		UAnimBlueprint* Created = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package,
			FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (Created)
		{
			FAssetRegistryModule::AssetCreated(Created);
		}
		return Created;
	}

	/** Every node but the output pose goes, so the builder is the one description of the graph. */
	static UAnimGraphNode_Root* ClearGraph(UEdGraph& Graph)
	{
		UAnimGraphNode_Root* Root = nullptr;
		const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph.Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			if (UAnimGraphNode_Root* AsRoot = Cast<UAnimGraphNode_Root>(Node))
			{
				Root = AsRoot;
				Root->BreakAllNodeLinks();
			}
			else if (Node)
			{
				Graph.RemoveNode(Node);
			}
		}
		return Root;
	}

	static UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
		{
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct &&
				Pin->PinType.PinSubCategoryObject.Get() == FPoseLink::StaticStruct())
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** The retarget node, its settings written through reflection (the node's struct is private). */
	static UAnimGraphNode_RetargetPoseFromMesh* AddRetargetNode(UEdGraph& Graph, UIKRetargeter* Retargeter)
	{
		FGraphNodeCreator<UAnimGraphNode_RetargetPoseFromMesh> Creator(Graph);
		UAnimGraphNode_RetargetPoseFromMesh* Node = Creator.CreateNode(/*bSelectNewNode=*/false);
		Node->NodePosX = -400;
		const FStructProperty* Inner = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("Node"));
		if (!Inner || Inner->Struct != FAnimNode_RetargetPoseFromMesh::StaticStruct())
		{
			return nullptr;
		}
		FAnimNode_RetargetPoseFromMesh* Settings = Inner->ContainerPtrToValuePtr<FAnimNode_RetargetPoseFromMesh>(Node);
		Settings->RetargetFrom = ERetargetSourceMode::ParentSkeletalMeshComponent;
		Settings->IKRetargeterAsset = Retargeter;
		Creator.Finalize();
		return Node;
	}

	static bool Compile(UAnimBlueprint& Blueprint)
	{
		FCompilerResultsLog Results;
		Results.SetSilentMode(true);
		FKismetEditorUtilities::CompileBlueprint(&Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			if (Message->GetSeverity() <= EMessageSeverity::Warning)
			{
				UE_LOG(LogHawkeye, Warning, TEXT("BuildRetargetFromParent: %s: %s"), *Blueprint.GetName(),
					*Message->ToText().ToString());
			}
		}
		return Blueprint.Status != BS_Error && Results.NumErrors == 0;
	}
}
#endif

UAnimBlueprint* UHawkeyeRetargetGraphBuilder::BuildRetargetFromParent(
	const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter)
{
#if WITH_EDITOR
	using namespace HawkeyeRetargetGraph;
	if (!Skeleton || !Retargeter)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): needs a skeleton (%s) and a retargeter (%s)."),
			*PackageName, *GetNameSafe(Skeleton), *GetNameSafe(Retargeter));
		return nullptr;
	}
	UAnimBlueprint* Blueprint = LoadOrCreate(PackageName, Skeleton);
	UEdGraph* Graph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
	if (!Graph || Blueprint->TargetSkeleton != Skeleton)
	{
		UE_LOG(LogHawkeye, Error,
			TEXT("BuildRetargetFromParent(%s): no AnimBlueprint with an AnimGraph on %s; delete it first."),
			*PackageName, *GetNameSafe(Skeleton));
		return nullptr;
	}
	Blueprint->Modify();
	UAnimGraphNode_Root* Root = ClearGraph(*Graph);
	UAnimGraphNode_RetargetPoseFromMesh* Retarget = AddRetargetNode(*Graph, Retargeter);
	if (!Root || !Retarget)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): %s."), *PackageName,
			Root ? TEXT("could not set up the Retarget Pose From Mesh node")
				 : TEXT("the AnimGraph has no output pose node"));
		return nullptr;
	}
	if (!Graph->GetSchema()->TryCreateConnection(FindPosePin(Retarget, EGPD_Output), FindPosePin(Root, EGPD_Input)))
	{
		UE_LOG(LogHawkeye, Error,
			TEXT("BuildRetargetFromParent(%s): the retarget pose would not connect to the output."), *PackageName);
		return nullptr;
	}
	if (!Compile(*Blueprint))
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): did not compile."), *PackageName);
		return nullptr;
	}
	Blueprint->MarkPackageDirty();
	UE_LOG(LogHawkeye, Log, TEXT("BuildRetargetFromParent: %s built, %s from the parent mesh onto %s."), *PackageName,
		*GetNameSafe(Retargeter), *GetNameSafe(Skeleton));
	return Blueprint;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}
