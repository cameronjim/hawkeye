#include "Player/RetargetGraphBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Hawkeye.h"
#include "Player/BodyPoseNode.h"

#if WITH_EDITOR
#include "AnimGraphNode_AnimDynamics.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_RetargetPoseFromMesh.h"
#include "AnimGraphNode_Root.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
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

	static UAnimBlueprint* LoadOrCreate(const FString& PackageName, USkeleton* Skeleton, UClass* ParentClass)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (UAnimBlueprint* Existing =
				LoadObject<UAnimBlueprint>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			if (Existing->ParentClass != ParentClass)
			{
				Existing->ParentClass = ParentClass;
			}
			return Existing;
		}
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = ParentClass;
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

	/** The first local- or component-space pose pin of Node facing Direction. */
	static UEdGraphPin* FindAnyPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
		{
			const UObject* Type = Pin ? Pin->PinType.PinSubCategoryObject.Get() : nullptr;
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct &&
				(Type == FPoseLink::StaticStruct() || Type == FComponentSpacePoseLink::StaticStruct()))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** The way a bone points, in its own frame: the dominant axis (with its sign) of its first child's rest offset. */
	static FVector AlongBoneAxis(
		const FReferenceSkeleton& Reference, FName Bone, AnimPhysTwistAxis& OutAxis, const FVector& Fallback)
	{
		FVector Along = Fallback;
		const int32 Index = Reference.FindBoneIndex(Bone);
		for (int32 Child = Index + 1; Index != INDEX_NONE && Child < Reference.GetNum(); ++Child)
		{
			if (Reference.GetParentIndex(Child) == Index)
			{
				const FVector Offset = Reference.GetRefBonePose()[Child].GetTranslation();
				const double Largest = FMath::Max3(FMath::Abs(Offset.X), FMath::Abs(Offset.Y), FMath::Abs(Offset.Z));
				if (Largest > UE_KINDA_SMALL_NUMBER)
				{
					if (FMath::Abs(Offset.X) == Largest)
					{
						Along = FVector(FMath::Sign(Offset.X), 0., 0.);
					}
					else
					{
						Along = FMath::Abs(Offset.Y) == Largest ? FVector(0., FMath::Sign(Offset.Y), 0.)
																: FVector(0., 0., FMath::Sign(Offset.Z));
					}
				}
				break;
			}
		}
		OutAxis = FMath::Abs(Along.X) > 0.5
			? AnimPhysTwistAxis::AxisX
			: (FMath::Abs(Along.Y) > 0.5 ? AnimPhysTwistAxis::AxisY : AnimPhysTwistAxis::AxisZ);
		return Along;
	}

	/** True when Bone is on the skeleton (a none name counts: it is not asked for). */
	static bool HasBone(const USkeleton& Skeleton, FName Bone)
	{
		return Bone.IsNone() || Skeleton.GetReferenceSkeleton().FindBoneIndex(Bone) != INDEX_NONE;
	}

	/** One AnimDynamics node for Chain, its bodies set up from the skeleton: a cone round each bone's own axis and a
	 * spring back. */
	static UAnimGraphNode_AnimDynamics* AddDynamicsNode(
		UEdGraph& Graph, const FHawkeyeDynamicsChain& Chain, const USkeleton& Skeleton, int32 X)
	{
		FGraphNodeCreator<UAnimGraphNode_AnimDynamics> Creator(Graph);
		UAnimGraphNode_AnimDynamics* Node = Creator.CreateNode(/*bSelectNewNode=*/false);
		Node->NodePosX = X;
		Node->NodePosY = 0;
		FAnimNode_AnimDynamics& Dynamics = Node->Node;
		Dynamics.BoundBone.BoneName = Chain.BoundBone;
		Dynamics.bChain = !Chain.ChainEnd.IsNone();
		Dynamics.ChainEnd.BoneName = Chain.ChainEnd;
		Dynamics.SimulationSpace = AnimPhysSimSpaceType::Component;
		Dynamics.GravityScale = Chain.GravityScale;
		Dynamics.bOverrideLinearDamping = true;
		Dynamics.LinearDampingOverride = Chain.Damping;
		Dynamics.bAngularSpring = true;
		Dynamics.AngularSpringConstant = Chain.AngularSpring;
		Dynamics.ComponentLinearAccScale = FVector(Chain.AccelerationScale);
		Dynamics.ComponentLinearVelScale = FVector(Chain.VelocityDrag);
		Dynamics.ComponentAppliedLinearAccClamp = FVector(Chain.MaxAcceleration);
		Dynamics.SimSpaceSettings.MaxAngularVelocity = Chain.MaxAngularVelocity;
		Dynamics.SimSpaceSettings.MaxAngularAcceleration = Chain.MaxAngularAcceleration;

		const FReferenceSkeleton& Reference = Skeleton.GetReferenceSkeleton();
		if (Dynamics.bChain)
		{
			Dynamics.UpdateChainPhysicsBodyDefinitions(Reference);
		}
		else
		{
			Dynamics.PhysicsBodyDefinitions.SetNum(1);
			Dynamics.PhysicsBodyDefinitions[0].BoundBone.BoneName = Chain.BoundBone;
		}
		FVector Along = FVector(1., 0., 0.);
		for (FAnimPhysBodyDefinition& Body : Dynamics.PhysicsBodyDefinitions)
		{
			Body.BoxExtents = Chain.BoxExtents;
			FAnimPhysConstraintSetup& Constraint = Body.ConstraintSetup;
			Constraint.AngularConstraintType = AnimPhysAngularConstraintType::Cone;
			Constraint.ConeAngle = Chain.ConeDegrees;
			AnimPhysTwistAxis Axis = AnimPhysTwistAxis::AxisX;
			Along = AlongBoneAxis(Reference, Body.BoundBone.BoneName, Axis, Along);
			Constraint.TwistAxis = Axis;
			Constraint.AngularTargetAxis = Axis;
			Constraint.AngularTarget = Along;
		}
		Creator.Finalize();
		return Node;
	}

	/** The Hawkeye Body Pose graph node (a class of the HawkeyeAnimEditor module, found by path), or null when it is not loaded. */
	static UEdGraphNode* AddBodyPoseNode(UEdGraph& Graph, int32 X)
	{
		UClass* NodeClass = FindObject<UClass>(nullptr, TEXT("/Script/HawkeyeAnimEditor.AnimGraphNode_HawkeyeBodyPose"));
		if (!NodeClass)
		{
			return nullptr;
		}
		UEdGraphNode* Node = NewObject<UEdGraphNode>(&Graph, NodeClass, NAME_None, RF_Transactional);
		Graph.AddNode(Node, /*bFromUI=*/false, /*bSelectNewNode=*/false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Node->NodePosX = X;
		return Node;
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
	return BuildRetargetWithDynamics(PackageName, Skeleton, Retargeter, TArray<FHawkeyeDynamicsChain>());
}

UAnimBlueprint* UHawkeyeRetargetGraphBuilder::BuildRetargetWithBodyPose(
	const FString& PackageName, USkeleton* Skeleton, UIKRetargeter* Retargeter)
{
	return BuildGraph(PackageName, Skeleton, Retargeter, TArray<FHawkeyeDynamicsChain>(), /*bBodyPose=*/true);
}

int32 UHawkeyeRetargetGraphBuilder::CountBodyPoseNodes(UAnimBlueprint* Blueprint)
{
#if WITH_EDITOR
	const UEdGraph* Graph = Blueprint ? HawkeyeRetargetGraph::FindAnimGraph(*Blueprint) : nullptr;
	if (!Graph)
	{
		return -1;
	}
	int32 Count = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		Count += Node && Node->GetClass()->GetName() == TEXT("AnimGraphNode_HawkeyeBodyPose") ? 1 : 0;
	}
	return Count;
#else
	return -1;
#endif
}

int32 UHawkeyeRetargetGraphBuilder::CountDynamicsNodes(UAnimBlueprint* Blueprint)
{
#if WITH_EDITOR
	const UEdGraph* Graph = Blueprint ? HawkeyeRetargetGraph::FindAnimGraph(*Blueprint) : nullptr;
	if (!Graph)
	{
		return -1;
	}
	int32 Count = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		Count += Cast<UAnimGraphNode_AnimDynamics>(Node) ? 1 : 0;
	}
	return Count;
#else
	return -1;
#endif
}

UAnimBlueprint* UHawkeyeRetargetGraphBuilder::BuildRetargetWithDynamics(const FString& PackageName, USkeleton* Skeleton,
	UIKRetargeter* Retargeter, const TArray<FHawkeyeDynamicsChain>& Chains)
{
	return BuildGraph(PackageName, Skeleton, Retargeter, Chains, /*bBodyPose=*/false);
}

UAnimBlueprint* UHawkeyeRetargetGraphBuilder::BuildGraph(const FString& PackageName, USkeleton* Skeleton,
	UIKRetargeter* Retargeter, const TArray<FHawkeyeDynamicsChain>& Chains, bool bBodyPose)
{
#if WITH_EDITOR
	using namespace HawkeyeRetargetGraph;
	if (!Skeleton || !Retargeter)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): needs a skeleton (%s) and a retargeter (%s)."),
			*PackageName, *GetNameSafe(Skeleton), *GetNameSafe(Retargeter));
		return nullptr;
	}
	UAnimBlueprint* Blueprint = LoadOrCreate(
		PackageName, Skeleton, bBodyPose ? UHawkeyeBodyAnimInstance::StaticClass() : UAnimInstance::StaticClass());
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
	// The chains whose bones the look's skeleton has; the others are left out with a line, never an error.
	TArray<const FHawkeyeDynamicsChain*> Usable;
	for (const FHawkeyeDynamicsChain& Chain : Chains)
	{
		if (!Chain.BoundBone.IsNone() && HasBone(*Skeleton, Chain.BoundBone) && HasBone(*Skeleton, Chain.ChainEnd))
		{
			Usable.Add(&Chain);
		}
		else
		{
			UE_LOG(LogHawkeye, Log, TEXT("BuildRetargetFromParent(%s): no dynamics for %s..%s: %s is missing a bone."),
				*PackageName, *Chain.BoundBone.ToString(), *Chain.ChainEnd.ToString(), *GetNameSafe(Skeleton));
		}
	}
	UEdGraphNode* Last = Retarget;
	if (Usable.Num() > 0 || bBodyPose)
	{
		// Local pose to component space, one AnimDynamics node a chain, back to local.
		FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> ToComponentCreator(*Graph);
		UAnimGraphNode_LocalToComponentSpace* ToComponent = ToComponentCreator.CreateNode(/*bSelectNewNode=*/false);
		ToComponent->NodePosX = -300;
		ToComponentCreator.Finalize();
		FString Failure;
		UEdGraph* TargetGraph = Graph;
		auto Link = [TargetGraph, &Failure](UEdGraphNode* From, UEdGraphNode* To, const FString& What)
		{
			UEdGraphPin* Out = FindAnyPosePin(From, EGPD_Output);
			UEdGraphPin* In = FindAnyPosePin(To, EGPD_Input);
			if (Failure.IsEmpty() && (!Out || !In || !TargetGraph->GetSchema()->TryCreateConnection(Out, In)))
			{
				Failure = What + TEXT(" would not connect");
			}
		};
		Link(Last, ToComponent, TEXT("the retarget pose to component space"));
		Last = ToComponent;
		int32 X = -200;
		for (const FHawkeyeDynamicsChain* Chain : Usable)
		{
			UAnimGraphNode_AnimDynamics* Node = AddDynamicsNode(*Graph, *Chain, *Skeleton, X);
			Link(Last, Node, FString::Printf(TEXT("dynamics %s"), *Chain->BoundBone.ToString()));
			Last = Node;
			X += 60;
		}
		if (bBodyPose)
		{
			// The visible body's hands and rest arms from our own table, after everything the retarget did.
			UEdGraphNode* PoseNode = AddBodyPoseNode(*Graph, X);
			if (PoseNode)
			{
				Link(Last, PoseNode, TEXT("the body pose"));
				Last = PoseNode;
				X += 60;
			}
			else if (Failure.IsEmpty())
			{
				Failure = TEXT("the Hawkeye Body Pose graph node is not loaded (the HawkeyeAnimEditor module)");
			}
		}
		FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> ToLocalCreator(*Graph);
		UAnimGraphNode_ComponentToLocalSpace* ToLocal = ToLocalCreator.CreateNode(/*bSelectNewNode=*/false);
		ToLocal->NodePosX = -100;
		ToLocalCreator.Finalize();
		Link(Last, ToLocal, TEXT("the dynamics to local space"));
		Last = ToLocal;
		if (!Failure.IsEmpty())
		{
			UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): %s."), *PackageName, *Failure);
			return nullptr;
		}
	}
	if (!Graph->GetSchema()->TryCreateConnection(FindAnyPosePin(Last, EGPD_Output), FindPosePin(Root, EGPD_Input)))
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
	UE_LOG(LogHawkeye, Log,
		TEXT("BuildRetargetFromParent: %s built, %s from the parent mesh onto %s, %d dynamics chain(s)%s."), *PackageName,
		*GetNameSafe(Retargeter), *GetNameSafe(Skeleton), Usable.Num(), bBodyPose ? TEXT(", with the body pose node") : TEXT(""));
	return Blueprint;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}
