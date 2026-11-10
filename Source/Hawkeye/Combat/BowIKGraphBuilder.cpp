#include "Combat/BowIKGraphBuilder.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Hawkeye.h"

#if WITH_EDITOR
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "Animation/BlendSpace.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "Combat/CombatAnimPlayback.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Combat/BowIKAnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace HawkeyeBowIKGraph
{
	/** Where the hit lean bends: the lowest spine bone, on both mannequins. */
	static const FName HitLeanBone(TEXT("spine_01"));

	/** The upper-body clips take over from here up (the arms, the spine, the head). */
	static const FName UpperBodyRootBone(TEXT("spine_01"));

	/** The pose under the upper-body layer, evaluated once and read twice. */
	static const TCHAR* BodyCacheName = TEXT("HawkeyeBody");

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

	/** Places a node of type T at (X, Y); Setup fills its settings before the pins are made. */
	template <typename T, typename SetupFn>
	static T* AddNode(UEdGraph& Graph, int32 X, int32 Y, SetupFn Setup)
	{
		FGraphNodeCreator<T> Creator(Graph);
		T* Node = Creator.CreateNode(/*bSelectNewNode=*/false);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Setup(*Node);
		Creator.Finalize();
		return Node;
	}

	/** The node's first local- or component-space pose pin facing Direction. */
	static UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			const UObject* Type = Pin ? Pin->PinType.PinSubCategoryObject.Get() : nullptr;
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
				&& (Type == FPoseLink::StaticStruct() || Type == FComponentSpacePoseLink::StaticStruct()))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** Collects the first failure so the builder can report exactly which link did not take. */
	struct FWiring
	{
		UEdGraph& Graph;
		FString Failure;

		void Link(UEdGraphPin* From, UEdGraphPin* To, const TCHAR* What)
		{
			if (!Failure.IsEmpty())
			{
				return;
			}
			if (!From || !To)
			{
				Failure = FString::Printf(TEXT("%s: pin missing (%s, %s)"), What, From ? TEXT("from ok") : TEXT("no from"),
					To ? TEXT("to ok") : TEXT("no to"));
				return;
			}
			if (!Graph.GetSchema()->TryCreateConnection(From, To))
			{
				Failure = FString::Printf(TEXT("%s: %s -> %s refused"), What, *From->PinName.ToString(), *To->PinName.ToString());
			}
		}

		void Pose(UEdGraphNode* From, UEdGraphNode* To, const TCHAR* What)
		{
			Link(FindPosePin(From, EGPD_Output), FindPosePin(To, EGPD_Input), What);
		}

		/** A variable-get node for Member of the parent class, wired into To's pin PinName. */
		void Variable(FName Member, UEdGraphNode* To, FName PinName, int32 X, int32 Y)
		{
			UK2Node_VariableGet* Get = AddNode<UK2Node_VariableGet>(Graph, X, Y,
				[Member](UK2Node_VariableGet& Node) { Node.VariableReference.SetSelfMember(Member); });
			Link(Get->FindPin(Member, EGPD_Output), To ? To->FindPin(PinName, EGPD_Input) : nullptr, *Member.ToString());
		}
	};

	static UAnimBlueprint* LoadOrCreate(const FString& PackageName, USkeleton* Skeleton)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (UAnimBlueprint* Existing = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->ParentClass = UHawkeyeBowIKAnimInstance::StaticClass();
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

	static UAnimGraphNode_ModifyBone* AddTwist(FWiring& Wiring, FName Bone, FName Member, int32 X)
	{
		UAnimGraphNode_ModifyBone* Node = AddNode<UAnimGraphNode_ModifyBone>(Wiring.Graph, X, 0,
			[Bone](UAnimGraphNode_ModifyBone& N)
			{
				N.Node.BoneToModify.BoneName = Bone;
				N.Node.TranslationMode = BMM_Ignore;
				N.Node.ScaleMode = BMM_Ignore;
				N.Node.RotationMode = BMM_Additive;
				N.Node.RotationSpace = BCS_ComponentSpace;
			});
		Wiring.Variable(Member, Node, TEXT("Rotation"), X - 40, 220);
		return Node;
	}

	static UAnimGraphNode_TwoBoneIK* AddArm(FWiring& Wiring, FName Hand, FName Target, FName Elbow, FName Alpha, int32 X)
	{
		UAnimGraphNode_TwoBoneIK* Node = AddNode<UAnimGraphNode_TwoBoneIK>(Wiring.Graph, X, 0,
			[Hand](UAnimGraphNode_TwoBoneIK& N)
			{
				N.Node.IKBone.BoneName = Hand;
				N.Node.EffectorLocationSpace = BCS_ComponentSpace;
				N.Node.JointTargetLocationSpace = BCS_ComponentSpace;
				N.Node.bAllowStretching = false;
				N.Node.bTakeRotationFromEffectorSpace = false;
				N.Node.bMaintainEffectorRelRot = true;
			});
		Wiring.Variable(Target, Node, TEXT("EffectorLocation"), X - 40, 220);
		Wiring.Variable(Elbow, Node, TEXT("JointTargetLocation"), X - 40, 300);
		Wiring.Variable(Alpha, Node, TEXT("Alpha"), X - 40, 380);
		return Node;
	}

	/**
	 * The clip slots, in local space after the mesh's own post-process: DefaultSlot (full body; a
	 * thug's clips, whose main instance is a single sequence), cached, then UpperBody layered over it
	 * from UpperBodyRootBone up in mesh space, with the aim offset (when there is one) on the upper-body
	 * branch after its slot. Returns the layered blend, the node to carry on from.
	 */
	static UEdGraphNode* AddClipSlots(FWiring& Wiring, UEdGraphNode* Last, UBlendSpace* AimOffset)
	{
		UEdGraph& Graph = Wiring.Graph;
		UEdGraphNode* FullBody = AddNode<UAnimGraphNode_Slot>(Graph, -1800, -300,
			[](UAnimGraphNode_Slot& N) { N.Node.SlotName = HawkeyeCombatAnim::FullBodySlot; });
		Wiring.Pose(Last, FullBody, TEXT("full-body clip slot"));

		UAnimGraphNode_SaveCachedPose* Save = AddNode<UAnimGraphNode_SaveCachedPose>(Graph, -1700, -300,
			[](UAnimGraphNode_SaveCachedPose& N) { N.CacheName = BodyCacheName; });
		Wiring.Link(FindPosePin(FullBody, EGPD_Output), FindPosePin(Save, EGPD_Input), TEXT("cache the body"));

		auto UseCache = [&Graph, Save](int32 Y)
		{
			return AddNode<UAnimGraphNode_UseCachedPose>(Graph, -1650, Y, [Save](UAnimGraphNode_UseCachedPose& N)
				{
					N.SaveCachedPoseNode = Save;
					// Private; what the compiler matches the cache by if the weak pointer is ever lost.
					if (FStrProperty* Name = FindFProperty<FStrProperty>(N.GetClass(), TEXT("NameOfCache")))
					{
						Name->SetPropertyValue_InContainer(&N, Save->CacheName);
					}
				});
		};
		UEdGraphNode* Base = UseCache(-200);
		UEdGraphNode* ForUpper = UseCache(-100);
		UEdGraphNode* Upper = AddNode<UAnimGraphNode_Slot>(Graph, -1600, -100,
			[](UAnimGraphNode_Slot& N) { N.Node.SlotName = HawkeyeCombatAnim::UpperBodySlot; });
		Wiring.Pose(ForUpper, Upper, TEXT("upper-body clip slot"));
		UEdGraphNode* UpperOut = Upper;
		if (AimOffset)
		{
			// Epic's aim offset node: mesh-space additive poses picked by yaw and pitch, over the clip.
			UAnimGraphNode_RotationOffsetBlendSpace* Aim = AddNode<UAnimGraphNode_RotationOffsetBlendSpace>(Graph, -1550, -100,
				[AimOffset](UAnimGraphNode_RotationOffsetBlendSpace& N) { N.SetAnimationAsset(AimOffset); });
			Wiring.Link(FindPosePin(Upper, EGPD_Output), Aim->FindPin(TEXT("BasePose"), EGPD_Input), TEXT("aim offset base"));
			Wiring.Variable(GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, AimOffsetYaw), Aim, TEXT("X"), -1600, 60);
			Wiring.Variable(GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, AimOffsetPitch), Aim, TEXT("Y"), -1600, 120);
			Wiring.Variable(GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, AimOffsetAlpha), Aim, TEXT("Alpha"), -1600, 180);
			UpperOut = Aim;
		}

		UAnimGraphNode_LayeredBoneBlend* Layer = AddNode<UAnimGraphNode_LayeredBoneBlend>(Graph, -1500, -150,
			[](UAnimGraphNode_LayeredBoneBlend& N)
			{
				if (N.Node.LayerSetup.Num() == 0)
				{
					N.Node.LayerSetup.AddDefaulted();
				}
				FBranchFilter Filter;
				Filter.BoneName = UpperBodyRootBone;
				Filter.BlendDepth = 0;
				N.Node.LayerSetup[0].BranchFilters = { Filter };
				N.Node.bMeshSpaceRotationBlend = true;
			});
		Wiring.Link(FindPosePin(Base, EGPD_Output), Layer->FindPin(TEXT("BasePose"), EGPD_Input), TEXT("layer base"));
		Wiring.Link(FindPosePin(UpperOut, EGPD_Output), Layer->FindPin(TEXT("BlendPoses_0"), EGPD_Input), TEXT("upper-body layer"));
		return Layer;
	}

	/** Lays the graph out left to right and wires it. Empty on success, else the first failure. */
	static FString BuildGraph(UEdGraph& Graph, UAnimGraphNode_Root& Root, TSubclassOf<UAnimInstance> Chained,
		FName SpineBone, FName NeckBone, UBlendSpace* AimOffset)
	{
		FWiring Wiring{ Graph };
		UEdGraphNode* Last = AddNode<UAnimGraphNode_LinkedInputPose>(Graph, -2000, 0, [](UAnimGraphNode_LinkedInputPose&) {});
		if (Chained)
		{
			UAnimGraphNode_LinkedAnimGraph* Linked = AddNode<UAnimGraphNode_LinkedAnimGraph>(Graph, -1700, 0,
				[Chained](UAnimGraphNode_LinkedAnimGraph& N)
				{
					N.Node.InstanceClass = Chained;
					// What picking the asset in the editor does (SetupFromAsset): point the node at the
					// class's AnimGraph, whose input poses become this node's pins. Protected, hence
					// through reflection.
					const FStructProperty* Reference = FindFProperty<FStructProperty>(N.GetClass(), TEXT("FunctionReference"));
					if (Reference && Reference->Struct == FMemberReference::StaticStruct())
					{
						Reference->ContainerPtrToValuePtr<FMemberReference>(&N)->SetExternalMember(
							UEdGraphSchema_K2::GN_AnimGraph, Chained);
					}
				});
			Linked->ReconstructNode();
			Wiring.Pose(Last, Linked, TEXT("input pose to the mesh's own post-process"));
			Last = Linked;
		}
		Last = AddClipSlots(Wiring, Last, AimOffset);
		UEdGraphNode* ToComponent = AddNode<UAnimGraphNode_LocalToComponentSpace>(Graph, -1400, 0,
			[](UAnimGraphNode_LocalToComponentSpace&) {});
		Wiring.Pose(Last, ToComponent, TEXT("to component space"));

		// The lean from a hit tips the whole upper body from the lower spine, before the aim's turn.
		UEdGraphNode* Lean = AddTwist(Wiring, HitLeanBone, GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, HitLean), -1275);
		Wiring.Pose(ToComponent, Lean, TEXT("hit lean"));
		UEdGraphNode* Spine = AddTwist(Wiring, SpineBone, GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, SpineTwist), -1150);
		Wiring.Pose(Lean, Spine, TEXT("spine twist"));
		UEdGraphNode* Neck = AddTwist(Wiring, NeckBone, GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, NeckTwist), -900);
		Wiring.Pose(Spine, Neck, TEXT("neck twist"));

		UEdGraphNode* Left = AddArm(Wiring, TEXT("hand_l"), GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, LeftHandTarget),
			GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, LeftElbowTarget),
			GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, LeftArmAlpha), -650);
		Wiring.Pose(Neck, Left, TEXT("bow arm IK"));
		UEdGraphNode* Right = AddArm(Wiring, TEXT("hand_r"), GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, RightHandTarget),
			GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, RightElbowTarget),
			GET_MEMBER_NAME_CHECKED(UHawkeyeBowIKAnimInstance, RightArmAlpha), -400);
		Wiring.Pose(Left, Right, TEXT("draw arm IK"));

		UEdGraphNode* ToLocal = AddNode<UAnimGraphNode_ComponentToLocalSpace>(Graph, -150, 0,
			[](UAnimGraphNode_ComponentToLocalSpace&) {});
		Wiring.Pose(Right, ToLocal, TEXT("to local space"));
		Wiring.Pose(ToLocal, &Root, TEXT("output pose"));
		return Wiring.Failure;
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
				UE_LOG(LogHawkeye, Warning, TEXT("BuildBowIKPostProcess: %s: %s"), *Blueprint.GetName(), *Message->ToText().ToString());
			}
		}
		return Blueprint.Status != BS_Error && Results.NumErrors == 0;
	}
}
#endif

UAnimBlueprint* UHawkeyeBowIKGraphBuilder::BuildBowIKPostProcess(const FString& PackageName, USkeleton* Skeleton,
	TSubclassOf<UAnimInstance> ChainedPostProcess, FName SpineBone, FName NeckBone, UBlendSpace* AimOffset)
{
#if WITH_EDITOR
	using namespace HawkeyeBowIKGraph;
	if (!Skeleton)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): no skeleton."), *PackageName);
		return nullptr;
	}
	UAnimBlueprint* Blueprint = LoadOrCreate(PackageName, Skeleton);
	UEdGraph* Graph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
	if (!Graph)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): could not create the AnimBlueprint or find its AnimGraph."),
			*PackageName);
		return nullptr;
	}
	if (Blueprint->ParentClass != UHawkeyeBowIKAnimInstance::StaticClass() || Blueprint->TargetSkeleton != Skeleton)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): exists with parent %s and skeleton %s; delete it first."),
			*PackageName, *GetNameSafe(Blueprint->ParentClass), *GetNameSafe(Blueprint->TargetSkeleton));
		return nullptr;
	}

	Blueprint->Modify();
	UAnimGraphNode_Root* Root = ClearGraph(*Graph);
	if (!Root)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): the AnimGraph has no output pose node."), *PackageName);
		return nullptr;
	}
	if (AimOffset && AimOffset->GetSkeleton() != Skeleton)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): aim offset %s is on skeleton %s, not %s."), *PackageName,
			*GetNameSafe(AimOffset), *GetNameSafe(AimOffset->GetSkeleton()), *GetNameSafe(Skeleton));
		return nullptr;
	}
	const FString Failure = BuildGraph(*Graph, *Root, ChainedPostProcess, SpineBone, NeckBone, AimOffset);
	if (!Failure.IsEmpty())
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): %s."), *PackageName, *Failure);
		return nullptr;
	}
	if (!Compile(*Blueprint))
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): did not compile."), *PackageName);
		return nullptr;
	}
	// The instance clamps the aim offset's inputs to its range and knows to leave the spine turn to it.
	if (UHawkeyeBowIKAnimInstance* Defaults = Blueprint->GeneratedClass
		? Cast<UHawkeyeBowIKAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr)
	{
		Defaults->Modify();
		Defaults->AimOffset = AimOffset;
	}
	Blueprint->MarkPackageDirty();
	UE_LOG(LogHawkeye, Log, TEXT("BuildBowIKPostProcess: %s built and compiled, %d nodes, chained %s, aim offset %s."), *PackageName,
		Graph->Nodes.Num(), *GetNameSafe(ChainedPostProcess.Get()), *GetNameSafe(AimOffset));
	return Blueprint;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildBowIKPostProcess(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}

int32 UHawkeyeBowIKGraphBuilder::CountAnimGraphNodes(UAnimBlueprint* Blueprint)
{
#if WITH_EDITOR
	const UEdGraph* Graph = Blueprint ? HawkeyeBowIKGraph::FindAnimGraph(*Blueprint) : nullptr;
	return Graph ? Graph->Nodes.Num() : -1;
#else
	return -1;
#endif
}
