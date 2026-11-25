// The ledge body of UParkourComponent (claude-docs/gameplay-semantics.md, "Ledge body"): which stage of its move or hang
// she is in, where her hands go on the edge, how she swings under them, and the authored clips that play over it. The
// capsule's path and the move's timings are ParkourComponent.cpp's and are not touched here; this is what the visible
// body does while they run (the Hawkeye Ledge IK node reads GetLedgeBodyPose).

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/CombatAnimSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Hawkeye.h"
#include "Player/HawkeyeCharacter.h"
#include "Player/HawkeyeMovementComponent.h"
#include "Player/ParkourComponent.h"

namespace HawkeyeLedgeBody
{
	static TAutoConsoleVariable<int32> CVarLedgePose(TEXT("hawkeye.LedgePose"), 1,
		TEXT("1: the ledge body (the hands on the edge, the body hung from them, the authored ledge clips); 0: the sample's pose, as before."),
		ECVF_Default);

	/** The master weight moves this fast toward 1 and toward 0, per second. */
	static constexpr float MasterRiseRate = 10.f;
	static constexpr float MasterFallRate = 8.f;
	/** A move with no ledge line of its own has the edge this long either way, past the hands' margin, cm. */
	static constexpr float SyntheticHalfLength = 120.f;
	/** The vault's hand lands this far onto the top and this far to the right of the middle, cm. */
	static constexpr float VaultHandBack = 10.f;
	static constexpr float VaultHandSide = 8.f;
	/** The authored clips never play faster than this or slower than a half, whatever time they are fitted to. */
	static constexpr float MaxClipRate = 2.f;
	static constexpr float MinClipRate = 0.5f;

	static float Smooth(float X)
	{
		return FMath::SmoothStep(0.f, 1.f, FMath::Clamp(X, 0.f, 1.f));
	}
}

bool UParkourComponent::IsLedgeBodyOn() const
{
	return bLedgeBodyPose && HawkeyeLedgeBody::CVarLedgePose.GetValueOnGameThread() != 0;
}

bool UParkourComponent::GetLedgeBodyPose(FLedgeBodyPose& OutPose) const
{
	OutPose = LedgePose;
	return OutPose.IsActive();
}

FLedgeEdge UParkourComponent::MakeEdgeFromObstacle(const FHawkeyeParkourObstacle& Obstacle) const
{
	FLedgeEdge Edge;
	Edge.Normal = Obstacle.WallNormal.GetSafeNormal2D();
	if (Edge.Normal.IsNearlyZero())
	{
		Edge.Normal = -FVector::ForwardVector;
	}
	// Her right as she faces the wall (the hang's Along).
	Edge.Along = FVector::CrossProduct(FVector::UpVector, -Edge.Normal).GetSafeNormal2D();
	Edge.Point = Obstacle.LedgePoint;
	const float Half = HawkeyeLedgeBody::SyntheticHalfLength;
	Edge.MinAlong = -Half;
	Edge.MaxAlong = Half;
	return Edge;
}

FLedgeEdge UParkourComponent::MakeEdgeFromHang(float Along) const
{
	FLedgeEdge Edge;
	Edge.Point = HangLine.PointAt(Along);
	Edge.Along = HangLine.Along;
	Edge.Normal = HangLine.Normal;
	Edge.MinAlong = HangLine.MinAlong - Along;
	Edge.MaxAlong = HangLine.MaxAlong - Along;
	if (HangLine.Length() < 1.f)
	{
		// "No edge found here: climb or drop only": the hands still go on the point she holds, a gap apart.
		const float Half = LedgeBody.HandHalfGap + LedgeBody.HandEndMargin;
		Edge.MinAlong = -Half;
		Edge.MaxAlong = Half;
	}
	return Edge;
}

void UParkourComponent::FillLedgePose(FLedgeBodyPose& Out, const FLedgeEdge& Edge, ELedgeStage Stage, float StageAlpha,
	const FLedgeStageWeights& Weights) const
{
	Out.Stage = Stage;
	Out.StageAlpha = StageAlpha;
	Out.Normal = Edge.Normal.GetSafeNormal();
	Out.Along = Edge.Along.GetSafeNormal();
	Out.PlaneD = static_cast<float>(FVector::DotProduct(Out.Normal, Edge.Point));
	Out.EdgeZ = static_cast<float>(Edge.Point.Z);
	Out.SwingDegrees = Swing.Angle;
	Out.Tuning = LedgeBody;
	Out.FootAlpha = Weights.Feet;
	Out.BodyAlpha = Weights.Body;
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		Out.HandAlpha[Hand] = Weights.Hands;
		Out.Grip[Hand] = Weights.Grip;
	}
}

void UParkourComponent::BeginLedgeMove(EHawkeyeParkourMove Move)
{
	const UCharacterMovementComponent* Movement = GetMovement();
	LedgeMoveElapsed = 0.f;
	ReleaseElapsed = -1.f;
	bSwingKicked = false;
	// A grab of a fall is the quick catch; a jump still rising reaches up the grab's way.
	bMoveIsCatch = Move == EHawkeyeParkourMove::LedgeGrab && Movement && Movement->IsFalling() && Movement->Velocity.Z <= 0.f;
	MoveEntryDownSpeed = Movement ? FMath::Max(0.f, -static_cast<float>(Movement->Velocity.Z)) : 0.f;
	MoveSequence = HawkeyeLedge::GetSequence(Move, bMoveIsCatch);
	if (Move == EHawkeyeParkourMove::Climb)
	{
		MoveEdge = MakeEdgeFromHang(GetHangAlong());
	}
	else
	{
		MoveEdge = MakeEdgeFromObstacle(LastObstacle);
	}
	// A move from one hang to another lets go of the old ledge first, on the pose it was held in.
	if (IsHangTransfer(Move) || Move == EHawkeyeParkourMove::Climb)
	{
		HeldPose = LedgePose;
	}
	else
	{
		HeldPose = FLedgeBodyPose();
	}
}

void UParkourComponent::BeginLedgeHang(EHawkeyeParkourMove FromMove)
{
	HangElapsed = 0.f;
	bHangSettled = false;
	ReleaseElapsed = -1.f;
	switch (FromMove)
	{
	case EHawkeyeParkourMove::DropToHang:
		Swing.Kick(LedgeBody.DropKick);
		break;
	case EHawkeyeParkourMove::LedgeGrab:
	case EHawkeyeParkourMove::HangLeap:
	case EHawkeyeParkourMove::HangHop:
		// The catch's own kick was given as its hands took the edge and the swing has been running since.
		if (!bSwingKicked)
		{
			Swing.Kick(HawkeyeLedge::CatchKick(MoveEntryDownSpeed, LedgeBody));
		}
		break;
	default:
		break;
	}
	bSwingKicked = false;
}

void UParkourComponent::BeginLedgeRelease()
{
	if (!LedgePose.IsActive())
	{
		ReleaseElapsed = -1.f;
		return;
	}
	HeldPose = LedgePose;
	HeldPose.Stage = ELedgeStage::Release;
	ReleaseElapsed = 0.f;
}

bool UParkourComponent::ComposeHangPose(float DeltaSeconds, FLedgeBodyPose& Out)
{
	const float Here = GetHangAlong();
	const FLedgeEdge Edge = MakeEdgeFromHang(Here);
	HangElapsed += DeltaSeconds;
	if (!bHangSettled && (HangElapsed >= LedgeBody.SettleSeconds || (HangElapsed > 0.25f && Swing.IsSettled(1.f, 8.f))))
	{
		bHangSettled = true;
	}
	const ELedgeStage Stage = bHangSettled ? ELedgeStage::Hang : ELedgeStage::Settle;
	const float StageAlpha = bHangSettled ? 1.f : FMath::Clamp(HangElapsed / FMath::Max(LedgeBody.SettleSeconds, 0.01f), 0.f, 1.f);
	const FLedgeStageWeights W = HawkeyeLedge::StageWeights(Stage, StageAlpha);

	// Hand over hand once she has shimmied (the driver's hands step along the edge), else the shoulders' width apart.
	float Offset[2];
	if (HandsAlpha > 0.f)
	{
		Offset[0] = HandAlong[0] - Here;
		Offset[1] = HandAlong[1] - Here;
	}
	else
	{
		const FLedgeHandPlan Plan = HawkeyeLedge::PlanHands(Edge, 0.f, LedgeBody);
		Offset[0] = Plan.AlongOffset[0];
		Offset[1] = Plan.AlongOffset[1];
	}
	FillLedgePose(Out, Edge, Stage, StageAlpha, W);
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		Out.Hand[Hand] = HawkeyeLedge::WristAt(Edge, Offset[Hand], 0.f, LedgeBody);
	}
	return true;
}

bool UParkourComponent::ComposeMovePose(float DeltaSeconds, FLedgeBodyPose& Out)
{
	using namespace HawkeyeLedge;
	if (MoveSequence.IsEmpty())
	{
		return false;
	}
	LedgeMoveElapsed += DeltaSeconds;
	const float Alpha = MoveSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(LedgeMoveElapsed / MoveSeconds, 0.f, 1.f) : 1.f;
	float StageAlpha = 0.f;
	const ELedgeStage Stage = StageAt(MoveSequence, Alpha, StageAlpha);
	const FLedgeStageWeights W = StageWeights(Stage, StageAlpha);

	// Let go of the old ledge first (a leap or a hop), on the pose it was held in.
	if (Stage == ELedgeStage::Release && HeldPose.IsActive())
	{
		Out = HeldPose;
		Out.Stage = Stage;
		Out.StageAlpha = StageAlpha;
		Out.SwingDegrees = Swing.Angle;
		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			Out.HandAlpha[Hand] = W.Hands;
			Out.Grip[Hand] = W.Grip;
		}
		Out.FootAlpha = W.Feet;
		Out.BodyAlpha = W.Body;
		return true;
	}

	// The body swings from the moment the hands take the edge.
	if (Stage == ELedgeStage::Catch && !bSwingKicked && (ActiveMove == EHawkeyeParkourMove::LedgeGrab || IsHangTransfer(ActiveMove)))
	{
		bSwingKicked = true;
		Swing.Kick(ActiveMove == EHawkeyeParkourMove::LedgeGrab ? CatchKick(MoveEntryDownSpeed, LedgeBody) : LedgeBody.DropKick * 2.f);
	}

	float PushBack = 0.f;
	if (Stage == ELedgeStage::ChestOver)
	{
		PushBack = LedgeBody.PushBack * HawkeyeLedgeBody::Smooth(StageAlpha);
	}
	else if (Stage == ELedgeStage::KneeUp || Stage == ELedgeStage::Stand)
	{
		PushBack = LedgeBody.PushBack;
	}
	FillLedgePose(Out, MoveEdge, Stage, StageAlpha, W);

	if (Stage == ELedgeStage::VaultPlant || Stage == ELedgeStage::VaultSwing)
	{
		// One hand, the right, on the top a little in from the edge; the other stays the clip's.
		const int32 Plant = 1;
		Out.Hand[Plant] = WristAt(MoveEdge, HawkeyeLedgeBody::VaultHandSide, HawkeyeLedgeBody::VaultHandBack, LedgeBody);
		Out.Hand[1 - Plant] = Out.Hand[Plant];
		Out.HandAlpha[1 - Plant] = 0.f;
		Out.Grip[1 - Plant] = 0.f;
		return true;
	}
	const FLedgeHandPlan Plan = PlanHands(MoveEdge, PushBack, LedgeBody);
	Out.Hand[0] = Plan.Hand[0];
	Out.Hand[1] = Plan.Hand[1];
	return true;
}

bool UParkourComponent::ComposeReleasePose(float DeltaSeconds, FLedgeBodyPose& Out)
{
	ReleaseElapsed += DeltaSeconds;
	const float Alpha = ReleaseElapsed / FMath::Max(LedgeBody.ReleaseSeconds, 0.02f);
	if (Alpha >= 1.f || !HeldPose.IsActive())
	{
		ReleaseElapsed = -1.f;
		return false;
	}
	const FLedgeStageWeights W = HawkeyeLedge::StageWeights(ELedgeStage::Release, Alpha);
	Out = HeldPose;
	Out.Stage = ELedgeStage::Release;
	Out.StageAlpha = Alpha;
	Out.SwingDegrees = Swing.Angle;
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		Out.HandAlpha[Hand] = HeldPose.HandAlpha[Hand] > 0.f ? W.Hands : 0.f;
		Out.Grip[Hand] = W.Grip;
	}
	Out.FootAlpha = W.Feet;
	Out.BodyAlpha = W.Body;
	return true;
}

void UParkourComponent::UpdateLedgeBody(float DeltaSeconds)
{
	using namespace HawkeyeLedgeBody;
	FLedgeBodyPose Wanted;
	bool bHave = false;
	CurrentLedgeStage = ELedgeStage::None;
	Swing.Step(DeltaSeconds, LedgeBody.SwingOmega, LedgeBody.SwingDamping, LedgeBody.SwingWallDegrees, LedgeBody.SwingRestitution);
	if (IsLedgeBodyOn())
	{
		if (bHanging)
		{
			bHave = ComposeHangPose(DeltaSeconds, Wanted);
		}
		else if (IsPerformingMove())
		{
			bHave = ComposeMovePose(DeltaSeconds, Wanted);
		}
		else if (ReleaseElapsed >= 0.f)
		{
			bHave = ComposeReleasePose(DeltaSeconds, Wanted);
		}
	}
	if (!bHanging && !IsPerformingMove() && ReleaseElapsed < 0.f)
	{
		Swing.Reset();
	}

	if (bHave)
	{
		CurrentLedgeStage = Wanted.Stage;
		LedgeLastPose = Wanted;
		LedgeMasterWeight = FMath::Min(1.f, LedgeMasterWeight + DeltaSeconds * MasterRiseRate);
	}
	else
	{
		// Nothing holds her to a ledge now: the last pose is let go of over about a tenth of a second, not dropped.
		LedgeMasterWeight = FMath::Max(0.f, LedgeMasterWeight - DeltaSeconds * MasterFallRate);
		if (LedgeMasterWeight <= 0.f)
		{
			LedgeLastPose = FLedgeBodyPose();
		}
	}
	FLedgeBodyPose Pose = LedgeLastPose;
	const float Master = LedgeMasterWeight;
	float Weight = 0.f;
	for (int32 Hand = 0; Hand < 2; ++Hand)
	{
		Pose.HandAlpha[Hand] *= Master;
		Weight = FMath::Max(Weight, Pose.HandAlpha[Hand]);
	}
	Pose.FootAlpha *= Master;
	Pose.BodyAlpha *= Master;
	Weight = FMath::Max(Weight, FMath::Max(Pose.FootAlpha, Pose.BodyAlpha));
	Pose.Weight = Weight;
	if (!bHave)
	{
		Pose.Stage = ELedgeStage::None;
	}
	LedgePose = Pose;
}

bool UParkourComponent::PlayLedgeRole(ECombatAnimRole Role, float FitSeconds)
{
	if (!bUseAuthoredLedgeClips || !IsLedgeBodyOn())
	{
		return false;
	}
	const ACharacter* Character = GetCharacter();
	const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(GetOwner());
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = Kate ? UCombatAnimSet::Resolve(Kate->GetCombatAnimSet(), Role) : nullptr;
	if (!Anim || !Montage)
	{
		return false;
	}
	float Rate = 1.f;
	if (FitSeconds > KINDA_SMALL_NUMBER)
	{
		Rate = FMath::Clamp(Montage->GetPlayLength() / FitSeconds, HawkeyeLedgeBody::MinClipRate, HawkeyeLedgeBody::MaxClipRate);
	}
	// Authored in place: the capsule is moved by the path, never by a clip.
	SetClipRootMotion(false);
	if (Anim->Montage_Play(Montage, Rate) <= 0.f)
	{
		return false;
	}
	ActiveMontage = Montage;
	RootMotionRestoreIn = 0.f;
	UE_LOG(LogHawkeye, Log, TEXT("%s: ledge clip %s (%s) at rate %.2f"), *GetNameSafe(GetOwner()), *UEnum::GetValueAsString(Role),
		*GetNameSafe(Montage), Rate);
	return true;
}

bool UParkourComponent::PlayAuthoredMoveClip(EHawkeyeParkourMove Move, float Seconds)
{
	switch (Move)
	{
	case EHawkeyeParkourMove::DropToHang:
		return PlayLedgeRole(ECombatAnimRole::LedgeDropToHang, Seconds);
	case EHawkeyeParkourMove::LedgeGrab:
	case EHawkeyeParkourMove::HangLeap:
	case EHawkeyeParkourMove::HangHop:
		return PlayLedgeRole(ECombatAnimRole::LedgeCatch, Seconds);
	case EHawkeyeParkourMove::Climb:
		return PlayLedgeRole(ECombatAnimRole::LedgeClimbUp, Seconds);
	default:
		return false;
	}
}
