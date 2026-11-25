#include "Player/RetargetGraphBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Hawkeye.h"
#include "Player/BodyPoseNode.h"
#include "Player/LedgeIKNode.h"

#if WITH_EDITOR
#include "AnimGraphNode_Base.h"
#include "AnimNode_KawaiiPhysics.h"
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
#include "Modules/ModuleManager.h"
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

	/** True when Bone is on the skeleton (a none name counts: it is not asked for). */
	static bool HasBone(const USkeleton& Skeleton, FName Bone)
	{
		return Bone.IsNone() || Skeleton.GetReferenceSkeleton().FindBoneIndex(Bone) != INDEX_NONE;
	}

	/** The plugin's anim graph node class (the editor half of Plugins/KawaiiPhysics; its node class is not exported, so it
	 * is found by name, not linked). Null when the plugin is not enabled. */
	static UClass* FindKawaiiNodeClass()
	{
		FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("KawaiiPhysicsEd"));
		return FindObject<UClass>(nullptr, TEXT("/Script/KawaiiPhysicsEd.AnimGraphNode_KawaiiPhysics"));
	}

	static bool IsKawaiiNode(const UEdGraphNode* Node)
	{
		return Node && Node->GetClass()->GetName() == TEXT("AnimGraphNode_KawaiiPhysics");
	}

	/** One Kawaii Physics node for Chain: the chain's settings in the plugin's own fields, the shapes it stays outside of
	 * laid in the bones' axes, and the first-frame guards (a warm-up that settles it, the thresholds that make a cut a
	 * teleport and not a whip). Null, with the node left out of the graph, when the node class or its data is not what
	 * this was written against. */
	static UEdGraphNode* AddKawaiiNode(UEdGraph& Graph, UClass& NodeClass, const FHawkeyeDynamicsChain& Chain,
		const USkeleton& Skeleton, int32 X)
	{
		FGraphNodeCreator<UAnimGraphNode_Base> Creator(Graph);
		UAnimGraphNode_Base* Node =
			Creator.CreateNode(/*bSelectNewNode=*/false, TSubclassOf<UAnimGraphNode_Base>(&NodeClass));
		Node->NodePosX = X;
		Node->NodePosY = 0;
		const FStructProperty* Inner = FindFProperty<FStructProperty>(Node->GetClass(), TEXT("Node"));
		FAnimNode_KawaiiPhysics* Settings = (Inner && Inner->Struct == FAnimNode_KawaiiPhysics::StaticStruct())
			? Inner->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(Node)
			: nullptr;
		if (Settings)
		{
			const FReferenceSkeleton& Reference = Skeleton.GetReferenceSkeleton();
			Settings->RootBone.BoneName = Chain.BoundBone;
			for (const FName& Excluded : HawkeyeSecondary::ExcludedBones(Reference, Chain))
			{
				FBoneReference Bone;
				Bone.BoneName = Excluded;
				Settings->ExcludeBones.Add(Bone);
			}
			// The chain ends in a real leaf bone (ponytail_05, quiver_02), so no dummy bone is added past it.
			Settings->DummyBoneLength = 0.f;
			Settings->PhysicsSettings.Damping = Chain.Damping;
			Settings->PhysicsSettings.Stiffness = Chain.Stiffness;
			Settings->PhysicsSettings.WorldDampingLocation = Chain.WorldDampingLocation;
			Settings->PhysicsSettings.WorldDampingRotation = Chain.WorldDampingRotation;
			Settings->PhysicsSettings.Radius = Chain.Radius;
			Settings->PhysicsSettings.LimitAngle = Chain.LimitAngleDegrees;
			Settings->SimulationSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;
			Settings->TargetFramerate = FMath::Max(1, Chain.TargetFramerate);
			// Real gravity, world down, applied to velocity (the plugin's default), not the project setting's multiple.
			Settings->Gravity = FVector(0., 0., -980. * Chain.GravityScale);
			Settings->bUseWorldSpaceGravity = true;
			Settings->bUseLegacyGravity = false;
			Settings->bUseDefaultGravityZProjectSetting = false;
			// First-frame and after-reset guard: the plugin skips a step with no time passed (DeltaTime <= 0), and settles
			// the chain with these steps on its first frame and after every ResetDynamics (a teleport).
			Settings->WarmUpFrames = FMath::Max(0, Chain.WarmUpFrames);
			Settings->bNeedWarmUp = Chain.WarmUpFrames > 0;
			Settings->bUseWarmUpWhenResetDynamics = true;
			Settings->TeleportDistanceThreshold = Chain.TeleportDistance;
			Settings->TeleportRotationThreshold = Chain.TeleportRotationDegrees;
			for (const FHawkeyeDynamicsCollider& Collider : Chain.Colliders)
			{
				FVector Offset;
				FRotator Rotation;
				HawkeyeSecondary::ColliderInBoneSpace(Collider,
					HawkeyeSecondary::RefBoneInComponent(Reference, Reference.FindBoneIndex(Collider.Bone)), Offset,
					Rotation);
				if (Collider.Length > 0.f)
				{
					FCapsuleLimit Limit;
					Limit.DrivingBone.BoneName = Collider.Bone;
					Limit.OffsetLocation = Offset;
					Limit.OffsetRotation = Rotation;
					Limit.Radius = Collider.Radius;
					Limit.Length = Collider.Length;
					Settings->CapsuleLimits.Add(Limit);
				}
				else
				{
					FSphericalLimit Limit;
					Limit.DrivingBone.BoneName = Collider.Bone;
					Limit.OffsetLocation = Offset;
					Limit.Radius = Collider.Radius;
					Limit.LimitType = ESphericalLimitType::Outer;
					Settings->SphericalLimits.Add(Limit);
				}
			}
		}
		// Finalize either way: the creator asserts in its destructor on a node left unplaced.
		Creator.Finalize();
		if (!Settings)
		{
			Graph.RemoveNode(Node);
			return nullptr;
		}
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

	/** The Hawkeye Ledge IK graph node (a class of the HawkeyeAnimEditor module, found by path), or null when it is not loaded. */
	static UEdGraphNode* AddLedgeIKNode(UEdGraph& Graph, int32 X)
	{
		UClass* NodeClass = FindObject<UClass>(nullptr, TEXT("/Script/HawkeyeAnimEditor.AnimGraphNode_HawkeyeLedgeIK"));
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

UAnimBlueprint* UHawkeyeRetargetGraphBuilder::BuildRetargetWithLedgeIK(const FString& PackageName, USkeleton* Skeleton,
	UIKRetargeter* Retargeter, const TArray<FHawkeyeDynamicsChain>& Chains)
{
	return BuildGraph(PackageName, Skeleton, Retargeter, Chains, /*bBodyPose=*/false, /*bLedgeIK=*/true);
}

int32 UHawkeyeRetargetGraphBuilder::CountLedgeIKNodes(UAnimBlueprint* Blueprint)
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
		Count += Node && Node->GetClass()->GetName() == TEXT("AnimGraphNode_HawkeyeLedgeIK") ? 1 : 0;
	}
	return Count;
#else
	return -1;
#endif
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
		Count += HawkeyeRetargetGraph::IsKawaiiNode(Node) ? 1 : 0;
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
	UIKRetargeter* Retargeter, const TArray<FHawkeyeDynamicsChain>& Chains, bool bBodyPose, bool bLedgeIK)
{
#if WITH_EDITOR
	using namespace HawkeyeRetargetGraph;
	if (!Skeleton || !Retargeter)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): needs a skeleton (%s) and a retargeter (%s)."),
			*PackageName, *GetNameSafe(Skeleton), *GetNameSafe(Retargeter));
		return nullptr;
	}
	UClass* ParentClass = UAnimInstance::StaticClass();
	if (bLedgeIK)
	{
		ParentClass = UHawkeyeKateBodyAnimInstance::StaticClass();
	}
	else if (bBodyPose)
	{
		ParentClass = UHawkeyeBodyAnimInstance::StaticClass();
	}
	UAnimBlueprint* Blueprint = LoadOrCreate(PackageName, Skeleton, ParentClass);
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
	// The chains the look's skeleton can run (HawkeyeSecondary::DescribeProblem); the others are left out with a line, never an
	// error: a Kate built before the simulation bones existed is the plain graph.
	TArray<const FHawkeyeDynamicsChain*> Usable;
	UClass* KawaiiNodeClass = Chains.Num() > 0 ? FindKawaiiNodeClass() : nullptr;
	if (Chains.Num() > 0 && !KawaiiNodeClass)
	{
		UE_LOG(LogHawkeye, Warning,
			TEXT("BuildRetargetFromParent(%s): the Kawaii Physics plugin's node is not loaded (enable Plugins/KawaiiPhysics); "
				 "no dynamics."),
			*PackageName);
	}
	const TArray<FHawkeyeDynamicsChain> NoChains;
	for (const FHawkeyeDynamicsChain& Chain : KawaiiNodeClass ? Chains : NoChains)
	{
		const FString Problem = HawkeyeSecondary::DescribeProblem(Skeleton->GetReferenceSkeleton(), Chain);
		if (Problem.IsEmpty())
		{
			Usable.Add(&Chain);
		}
		else
		{
			// A bone the skeleton lacks is the expected case (Log); anything else is a chain written wrong (Warning).
			const bool bMissingBone = !HasBone(*Skeleton, Chain.BoundBone) || !HasBone(*Skeleton, Chain.ChainEnd);
			UE_LOG(LogHawkeye, Log, TEXT("BuildRetargetFromParent(%s): no dynamics for %s..%s on %s: %s."), *PackageName,
				*Chain.BoundBone.ToString(), *Chain.ChainEnd.ToString(), *GetNameSafe(Skeleton), *Problem);
			if (!bMissingBone)
			{
				UE_LOG(LogHawkeye, Warning, TEXT("BuildRetargetFromParent(%s): the %s chain was rejected: %s."), *PackageName,
					*Chain.BoundBone.ToString(), *Problem);
			}
		}
	}
	UEdGraphNode* Last = Retarget;
	if (Usable.Num() > 0 || bBodyPose || bLedgeIK)
	{
		// Local pose to component space, one Kawaii Physics node a chain, back to local.
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
			UEdGraphNode* Node = AddKawaiiNode(*Graph, *KawaiiNodeClass, *Chain, *Skeleton, X);
			if (!Node)
			{
				UE_LOG(LogHawkeye, Error,
					TEXT("BuildRetargetFromParent(%s): the Kawaii Physics node's data is not the struct this was written against."),
					*PackageName);
				return nullptr;
			}
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
		if (bLedgeIK)
		{
			// Kate's hands on the edge, the body hung from them, the feet on the wall: after the dynamics, so the ponytail
			// and quiver ride the body as it is moved.
			UEdGraphNode* LedgeNode = AddLedgeIKNode(*Graph, X);
			if (LedgeNode)
			{
				Link(Last, LedgeNode, TEXT("the ledge IK"));
				Last = LedgeNode;
				X += 60;
			}
			else if (Failure.IsEmpty())
			{
				Failure = TEXT("the Hawkeye Ledge IK graph node is not loaded (the HawkeyeAnimEditor module)");
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
		*GetNameSafe(Retargeter), *GetNameSafe(Skeleton), Usable.Num(),
		bLedgeIK ? TEXT(", with the ledge IK node") : (bBodyPose ? TEXT(", with the body pose node") : TEXT("")));
	return Blueprint;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildRetargetFromParent(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}
