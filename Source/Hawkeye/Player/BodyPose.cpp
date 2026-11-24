#include "Player/BodyPose.h"

#include "ReferenceSkeleton.h"

namespace HawkeyeBodyPose
{
	namespace
	{
		FFingerAngles Make(float A, float B, float C, float Spread = 0.f)
		{
			FFingerAngles Out;
			Out.Curl[0] = A;
			Out.Curl[1] = B;
			Out.Curl[2] = C;
			Out.Spread = Spread;
			return Out;
		}

		/** The reference pose's transform of a bone in the skeleton's root space (bone transforms chained down from the root). */
		FTransform RefComponent(const FReferenceSkeleton& Reference, int32 BoneIndex)
		{
			FTransform Out = FTransform::Identity;
			for (int32 Bone = BoneIndex; Bone != INDEX_NONE; Bone = Reference.GetParentIndex(Bone))
			{
				Out = Out * Reference.GetRefBonePose()[Bone];
			}
			return Out;
		}
	}

	FFingerAngles Angles(EHandPose Pose, EFinger Finger)
	{
		// Rows: thumb, index, middle, ring, pinky. Staggered, a little more toward the little finger, as a hand hangs.
		static const FFingerAngles Relaxed[5] = {Make(0.f, 6.f, 6.f, -4.f), Make(14.f, 22.f, 12.f), Make(20.f, 28.f, 16.f),
			Make(26.f, 32.f, 18.f), Make(32.f, 36.f, 20.f)};
		static const FFingerAngles Fist[5] = {Make(10.f, 25.f, 20.f, 100.f), Make(78.f, 100.f, 62.f), Make(82.f, 102.f, 65.f),
			Make(86.f, 100.f, 62.f), Make(88.f, 98.f, 60.f)};
		static const FFingerAngles Grip[5] = {Make(10.f, 20.f, 15.f, 75.f), Make(58.f, 78.f, 42.f), Make(62.f, 82.f, 44.f),
			Make(66.f, 80.f, 42.f), Make(70.f, 78.f, 40.f)};
		const int32 Row = FMath::Clamp(static_cast<int32>(Finger), 0, 4);
		switch (Pose)
		{
		case EHandPose::Fist:
			return Fist[Row];
		case EHandPose::Grip:
			return Grip[Row];
		default:
			return Relaxed[Row];
		}
	}

	FFingerAngles Blend(EFinger Finger, float FistAlpha, float GripAlpha)
	{
		const FFingerAngles Relaxed = Angles(EHandPose::Relaxed, Finger);
		const FFingerAngles Fist = Angles(EHandPose::Fist, Finger);
		const FFingerAngles Grip = Angles(EHandPose::Grip, Finger);
		const float F = FMath::Clamp(FistAlpha, 0.f, 1.f);
		const float G = FMath::Clamp(GripAlpha, 0.f, 1.f);
		FFingerAngles Out;
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			Out.Curl[Joint] = FMath::Lerp(FMath::Lerp(Relaxed.Curl[Joint], Fist.Curl[Joint], F), Grip.Curl[Joint], G);
		}
		Out.Spread = FMath::Lerp(FMath::Lerp(Relaxed.Spread, Fist.Spread, F), Grip.Spread, G);
		return Out;
	}

	float StepAlpha(float Current, float Target, float DeltaSeconds, float BlendSeconds)
	{
		const float Step = BlendSeconds > UE_KINDA_SMALL_NUMBER ? DeltaSeconds / BlendSeconds : 1.f;
		const float Delta = FMath::Clamp(Target - Current, -Step, Step);
		return FMath::Clamp(Current + Delta, 0.f, 1.f);
	}

	FArmRest ArmRestDirections(const FVector& Up, const FVector& Outward, const FVector& Forward, float OutwardDegrees,
		float ElbowDegrees)
	{
		const float Out = FMath::DegreesToRadians(OutwardDegrees);
		const float Elbow = FMath::DegreesToRadians(ElbowDegrees);
		FArmRest Rest;
		Rest.Upper = (-Up * FMath::Cos(Out) + Outward * FMath::Sin(Out)).GetSafeNormal();
		Rest.Lower = (Rest.Upper * FMath::Cos(Elbow) + Forward * FMath::Sin(Elbow)).GetSafeNormal();
		return Rest;
	}

	const TCHAR* SideSuffix(int32 Side)
	{
		return Side == HandLeft ? TEXT("l") : TEXT("r");
	}

	FHandFrame ComputeHandFrame(const FReferenceSkeleton& Reference, int32 Side)
	{
		const FString Suffix = SideSuffix(Side);
		const int32 Hand = Reference.FindBoneIndex(*(TEXT("hand_") + Suffix));
		const int32 Middle = Reference.FindBoneIndex(*(TEXT("middle_01_") + Suffix));
		const int32 Index = Reference.FindBoneIndex(*(TEXT("index_01_") + Suffix));
		const int32 Pinky = Reference.FindBoneIndex(*(TEXT("pinky_01_") + Suffix));
		const int32 Thumb = Reference.FindBoneIndex(*(TEXT("thumb_01_") + Suffix));
		FHandFrame Frame;
		if (Hand == INDEX_NONE || Middle == INDEX_NONE || Index == INDEX_NONE || Pinky == INDEX_NONE || Thumb == INDEX_NONE)
		{
			return Frame;
		}
		const FTransform HandT = RefComponent(Reference, Hand);
		const FVector HandPos = HandT.GetLocation();
		const FVector MiddlePos = RefComponent(Reference, Middle).GetLocation();
		const FVector IndexPos = RefComponent(Reference, Index).GetLocation();
		const FVector PinkyPos = RefComponent(Reference, Pinky).GetLocation();
		const FVector ThumbPos = RefComponent(Reference, Thumb).GetLocation();

		// The same formula as Tools/Editor/_handpose.palm_frame: along the middle finger, across the knuckles, their normal,
		// signed to the side the thumb's base lies on.
		const FVector Along = (MiddlePos - HandPos).GetSafeNormal();
		FVector Across = IndexPos - PinkyPos;
		Across = (Across - Along * FVector::DotProduct(Across, Along)).GetSafeNormal();
		const FVector Normal = FVector::CrossProduct(Along, Across).GetSafeNormal();
		const FVector Knuckles = (IndexPos + PinkyPos) * 0.5;
		const FVector Palm = FVector::DotProduct(Normal, ThumbPos - Knuckles) > 0.0 ? Normal : -Normal;

		const FQuat ToLocal = HandT.GetRotation().Inverse();
		Frame.Along = ToLocal.RotateVector(Along);
		Frame.Across = ToLocal.RotateVector(Across);
		Frame.Palm = ToLocal.RotateVector(Palm);
		Frame.Knuckles = ToLocal.RotateVector(Knuckles - HandPos);
		Frame.bValid = true;
		return Frame;
	}

	FVector2D ThumbRestAngles(const FReferenceSkeleton& Reference, int32 Side)
	{
		const FHandFrame Frame = ComputeHandFrame(Reference, Side);
		const FString Suffix = SideSuffix(Side);
		const int32 Thumb1 = Reference.FindBoneIndex(*(TEXT("thumb_01_") + Suffix));
		const int32 Thumb2 = Reference.FindBoneIndex(*(TEXT("thumb_02_") + Suffix));
		const int32 Hand = Reference.FindBoneIndex(*(TEXT("hand_") + Suffix));
		if (!Frame.bValid || Thumb1 == INDEX_NONE || Thumb2 == INDEX_NONE)
		{
			return FVector2D::ZeroVector;
		}
		const FQuat ToLocal = RefComponent(Reference, Hand).GetRotation().Inverse();
		const FVector Dir = ToLocal.RotateVector(
			(RefComponent(Reference, Thumb2).GetLocation() - RefComponent(Reference, Thumb1).GetLocation()).GetSafeNormal());
		const float ToAlong = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Dir, Frame.Along)), -1.f, 1.f)));
		const float OutOfPlane = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(static_cast<float>(FVector::DotProduct(Dir, Frame.Palm)), -1.f, 1.f)));
		return FVector2D(ToAlong, OutOfPlane);
	}

	FVector ClosedThumbDirection(EHandPose Pose, int32 Joint)
	{
		static const FVector Fist[3] = {FVector(0.8, 0.2, 0.55), FVector(0.2, -0.6, 0.75), FVector(0.15, -0.85, 0.5)};
		static const FVector Grip[3] = {FVector(0.85, 0.2, 0.45), FVector(0.45, -0.4, 0.8), FVector(0.3, -0.65, 0.7)};
		const int32 Row = FMath::Clamp(Joint, 0, 2);
		return (Pose == EHandPose::Grip ? Grip[Row] : Fist[Row]).GetSafeNormal();
	}

	float RestArmWeight(bool bActing, float SpeedCmPerSecond)
	{
		if (bActing)
		{
			return 0.f;
		}
		return 1.f - FMath::Clamp((SpeedCmPerSecond - 30.f) / 120.f, 0.f, 1.f);
	}
}
