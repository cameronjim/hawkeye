#include "Traversal/LedgeBodyRules.h"

namespace HawkeyeLedge
{
	namespace
	{
		/** A stage and where it ends; it starts where the one before ended. */
		struct FEntry
		{
			ELedgeStage Stage;
			float End;
		};

		FLedgeSequence Make(std::initializer_list<FEntry> StagesAndEnds)
		{
			FLedgeSequence Out;
			float Start = 0.f;
			for (const FEntry& Entry : StagesAndEnds)
			{
				FLedgePhase& Phase = Out.Phases.AddDefaulted_GetRef();
				Phase.Stage = Entry.Stage;
				Phase.Start = Start;
				Phase.End = Entry.End;
				Start = Entry.End;
			}
			return Out;
		}

		float S(float X)
		{
			return FMath::SmoothStep(0.f, 1.f, FMath::Clamp(X, 0.f, 1.f));
		}
	}

	FLedgeSequence GetSequence(EHawkeyeParkourMove Move, bool bFalling)
	{
		using E = ELedgeStage;
		switch (Move)
		{
		case EHawkeyeParkourMove::DropToHang:
			return Make({{E::Plant, 0.30f}, {E::Lower, 0.78f}, {E::Settle, 1.f}});
		case EHawkeyeParkourMove::LedgeGrab:
			// A grab from the ground jumps up to it; a catch of a fall is over in 0.15 s: reach, catch, nothing more.
			return bFalling ? Make({{E::Reach, 0.40f}, {E::Catch, 1.f}})
							: Make({{E::Reach, 0.45f}, {E::Catch, 0.80f}, {E::Settle, 1.f}});
		case EHawkeyeParkourMove::Climb:
			return Make({{E::Pull, 0.38f}, {E::ChestOver, 0.62f}, {E::KneeUp, 0.84f}, {E::Stand, 1.f}});
		case EHawkeyeParkourMove::Mantle:
			return Make({{E::Reach, 0.30f}, {E::Pull, 0.50f}, {E::ChestOver, 0.75f}, {E::KneeUp, 0.90f}, {E::Stand, 1.f}});
		case EHawkeyeParkourMove::Vault:
			return Make({{E::VaultPlant, 0.45f}, {E::VaultSwing, 1.f}});
		case EHawkeyeParkourMove::HangLeap:
		case EHawkeyeParkourMove::HangHop:
			return Make({{E::Release, 0.25f}, {E::Reach, 0.70f}, {E::Catch, 1.f}});
		case EHawkeyeParkourMove::HangCorner:
			return Make({{E::Hang, 1.f}});
		default:
			return FLedgeSequence();
		}
	}

	ELedgeStage StageAt(const FLedgeSequence& Sequence, float Alpha, float& OutStageAlpha)
	{
		OutStageAlpha = 0.f;
		if (Sequence.IsEmpty())
		{
			return ELedgeStage::None;
		}
		const float A = FMath::Clamp(Alpha, 0.f, 1.f);
		for (const FLedgePhase& Phase : Sequence.Phases)
		{
			if (A < Phase.End || &Phase == &Sequence.Phases.Last())
			{
				const float Length = FMath::Max(Phase.End - Phase.Start, KINDA_SMALL_NUMBER);
				OutStageAlpha = FMath::Clamp((A - Phase.Start) / Length, 0.f, 1.f);
				return Phase.Stage;
			}
		}
		return ELedgeStage::None;
	}

	float TurnFraction(EHawkeyeParkourMove Move, const FHawkeyeLedgeBodySettings& Settings)
	{
		return Move == EHawkeyeParkourMove::DropToHang ? FMath::Clamp(Settings.DropTurnFraction, 0.05f, 1.f) : 1.f;
	}

	FLedgeStageWeights StageWeights(ELedgeStage Stage, float StageAlpha)
	{
		const float A = FMath::Clamp(StageAlpha, 0.f, 1.f);
		FLedgeStageWeights W;
		switch (Stage)
		{
		case ELedgeStage::Plant:
			W.Hands = S(A);
			W.Body = 0.3f * S(A);
			W.Grip = S(A);
			break;
		case ELedgeStage::Lower:
			W.Hands = 1.f;
			W.Feet = 0.8f * S(A);
			W.Body = 0.3f + 0.7f * S(A);
			W.Grip = 1.f;
			break;
		case ELedgeStage::Hang:
		case ELedgeStage::Settle:
			W.Hands = 1.f;
			W.Feet = 1.f;
			W.Body = 1.f;
			W.Grip = 1.f;
			break;
		case ELedgeStage::Reach:
			// The arms are up for the edge in the clip; the IK takes the hands over only as they come to it.
			W.Hands = S((A - 0.4f) / 0.6f);
			W.Grip = 0.35f * S((A - 0.4f) / 0.6f);
			break;
		case ELedgeStage::Catch:
			W.Hands = 1.f;
			W.Feet = 1.f;
			W.Body = S(A * 1.5f);
			W.Grip = 1.f;
			break;
		case ELedgeStage::Release:
			W.Hands = 1.f - S(A);
			W.Feet = 1.f - S(A);
			W.Body = 1.f - S(A);
			W.Grip = 1.f - S(A * 1.5f);
			break;
		case ELedgeStage::Pull:
			W.Hands = 1.f;
			W.Body = 0.f;
			W.Grip = 1.f;
			break;
		case ELedgeStage::ChestOver:
			W.Hands = 1.f;
			W.Grip = 0.4f;
			break;
		case ELedgeStage::KneeUp:
			W.Hands = 1.f - S(A);
			W.Grip = 0.3f * (1.f - S(A));
			break;
		case ELedgeStage::VaultPlant:
			W.Hands = S(A);
			W.Grip = 0.5f * S(A);
			break;
		case ELedgeStage::VaultSwing:
			W.Hands = 1.f - S(A * 1.2f);
			W.Grip = 0.5f * (1.f - S(A * 1.2f));
			break;
		default:
			break;
		}
		return W;
	}

	FVector WristAt(const FLedgeEdge& Edge, float OffsetAlong, float PushBackCm, const FHawkeyeLedgeBodySettings& Settings)
	{
		const FVector Along = Edge.Along.GetSafeNormal();
		const FVector Normal = Edge.Normal.GetSafeNormal();
		const float Lo = Edge.MinAlong + Settings.HandEndMargin;
		const float Hi = Edge.MaxAlong - Settings.HandEndMargin;
		const float Offset = Lo <= Hi ? FMath::Clamp(OffsetAlong, Lo, Hi) : 0.5f * (Edge.MinAlong + Edge.MaxAlong);
		return Edge.Point + Along * Offset + Normal * (Settings.WristOut - PushBackCm) + FVector(0.f, 0.f, Settings.WristHeight);
	}

	FLedgeHandPlan PlanHands(const FLedgeEdge& Edge, float PushBackCm, const FHawkeyeLedgeBodySettings& Settings)
	{
		FLedgeHandPlan Plan;
		const float Lo = Edge.MinAlong + Settings.HandEndMargin;
		const float Hi = Edge.MaxAlong - Settings.HandEndMargin;
		const float Available = FMath::Max(Hi - Lo, 0.f);
		const float Wanted = 2.f * Settings.HandHalfGap;
		Plan.bCramped = Available < Settings.MinHandGap;
		Plan.Spacing = FMath::Min(Wanted, Available);
		// Both hands inside the ends: the pair is slid along until they are.
		float Centre = 0.f;
		const float CentreLo = Lo + 0.5f * Plan.Spacing;
		const float CentreHi = Hi - 0.5f * Plan.Spacing;
		Centre = CentreLo <= CentreHi ? FMath::Clamp(0.f, CentreLo, CentreHi) : 0.5f * (Edge.MinAlong + Edge.MaxAlong);
		Plan.AlongOffset[0] = Centre - 0.5f * Plan.Spacing;
		Plan.AlongOffset[1] = Centre + 0.5f * Plan.Spacing;
		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			Plan.Hand[Hand] = WristAt(Edge, Plan.AlongOffset[Hand], PushBackCm, Settings);
		}
		return Plan;
	}

	float CatchKick(float DownSpeed, const FHawkeyeLedgeBodySettings& Settings)
	{
		return FMath::Clamp(Settings.CatchKickBase + Settings.CatchKickPerSpeed * FMath::Max(DownSpeed, 0.f), 0.f, Settings.MaxKick);
	}

	FTwoBoneSolution SolveTwoBone(const FVector& Root, float Upper, float Lower, const FVector& Target, const FVector& Pole)
	{
		FTwoBoneSolution Out;
		const double A = FMath::Max(static_cast<double>(Upper), 0.01);
		const double B = FMath::Max(static_cast<double>(Lower), 0.01);
		const FVector ToTarget = Target - Root;
		const double Distance = ToTarget.Size();
		const double MaxReach = A + B - 0.01;
		const double MinReach = FMath::Abs(A - B) + 0.01;
		const double Reach = FMath::Clamp(Distance, MinReach, MaxReach);
		Out.bClamped = Distance > MaxReach || Distance < MinReach;
		const FVector Direction = Distance > UE_KINDA_SMALL_NUMBER ? ToTarget / Distance : FVector::DownVector;
		Out.End = Root + Direction * Reach;
		const double CosAngle = FMath::Clamp((A * A + Reach * Reach - B * B) / (2.0 * A * Reach), -1.0, 1.0);
		const double Along = A * CosAngle;
		const double Height = A * FMath::Sqrt(FMath::Max(0.0, 1.0 - CosAngle * CosAngle));
		FVector Bend = Pole - Root;
		Bend -= Direction * FVector::DotProduct(Bend, Direction);
		if (Bend.SizeSquared() < 1e-6)
		{
			// A pole straight along the limb says nothing: bend it in any plane.
			Bend = FVector::CrossProduct(Direction, FVector::UpVector);
			if (Bend.SizeSquared() < 1e-6)
			{
				Bend = FVector::CrossProduct(Direction, FVector::RightVector);
			}
		}
		Out.Joint = Root + Direction * Along + Bend.GetSafeNormal() * Height;
		return Out;
	}

	float ReachWeight(float Distance, float Length, const FHawkeyeLedgeBodySettings& Settings)
	{
		if (Length <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float Ratio = Distance / Length;
		if (Ratio <= Settings.ReachStart)
		{
			return 1.f;
		}
		if (Ratio >= Settings.ReachEnd)
		{
			return 0.f;
		}
		return 1.f - S((Ratio - Settings.ReachStart) / FMath::Max(Settings.ReachEnd - Settings.ReachStart, KINDA_SMALL_NUMBER));
	}

	FVector ShoulderTarget(const FVector& HandMid, const FVector& Normal, float ArmLength, const FHawkeyeLedgeBodySettings& Settings)
	{
		const float Length = ArmLength * Settings.ArmStraightness;
		const float Out = Settings.ShoulderOut;
		const float Down = FMath::Sqrt(FMath::Max(Length * Length - Out * Out, 1.f));
		return HandMid + Normal.GetSafeNormal() * Out - FVector(0.f, 0.f, Down);
	}

	FQuat SwingTurn(const FVector& Normal, float Degrees)
	{
		const FVector Axis = FVector::CrossProduct(Normal.GetSafeNormal(), FVector::UpVector).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			return FQuat::Identity;
		}
		return FQuat(Axis, FMath::DegreesToRadians(Degrees));
	}

	FQuat AlignFrames(const FVector& FromA, const FVector& FromB, const FVector& ToA, const FVector& ToB)
	{
		const FVector A0 = FromA.GetSafeNormal();
		const FVector A1 = ToA.GetSafeNormal();
		if (A0.IsNearlyZero() || A1.IsNearlyZero())
		{
			return FQuat::Identity;
		}
		const FQuat First = FQuat::FindBetweenNormals(A0, A1);
		const FVector B1 = First.RotateVector(FromB);
		const FVector Flat1 = (B1 - A1 * FVector::DotProduct(B1, A1)).GetSafeNormal();
		const FVector FlatTo = (ToB - A1 * FVector::DotProduct(ToB, A1)).GetSafeNormal();
		if (Flat1.IsNearlyZero() || FlatTo.IsNearlyZero())
		{
			return First;
		}
		const double Angle = FMath::Atan2(FVector::DotProduct(A1, FVector::CrossProduct(Flat1, FlatTo)), FVector::DotProduct(Flat1, FlatTo));
		return FQuat(A1, Angle) * First;
	}

	void HandAxes(const FVector& Normal, float FingerDownDegrees, FVector& OutFingers, FVector& OutPalm)
	{
		const FVector N = Normal.GetSafeNormal();
		const FQuat Tip = SwingTurn(N, FingerDownDegrees);
		OutFingers = Tip.RotateVector(-N);
		OutPalm = Tip.RotateVector(-FVector::UpVector);
	}

	FVector BallOnPlane(const FVector& Ball, const FVector& Normal, float PlaneD, float BallFromWall)
	{
		const FVector N = Normal.GetSafeNormal();
		const double Off = FVector::DotProduct(Ball, N) - PlaneD;
		return Ball - N * (Off - BallFromWall);
	}
}

void FLedgePendulum::Step(float DeltaSeconds, float Omega, float Damping, float WallDegrees, float Restitution)
{
	if (DeltaSeconds <= 0.f)
	{
		return;
	}
	// Semi-implicit Euler in small steps: stable well past the frequency (5 rad/s) and the frame times used.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds * 120.f), 1, 240);
	const float H = DeltaSeconds / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const float Acceleration = -Omega * Omega * Angle - 2.f * Damping * Omega * Velocity;
		Velocity += Acceleration * H;
		Angle += Velocity * H;
		if (Angle < WallDegrees)
		{
			Angle = WallDegrees;
			if (Velocity < 0.f)
			{
				Velocity = -Velocity * Restitution;
			}
		}
	}
}
