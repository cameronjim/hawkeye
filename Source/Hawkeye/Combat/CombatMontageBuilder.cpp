#include "Combat/CombatMontageBuilder.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Combat/AnimNotifyState_ComboWindow.h"
#include "Combat/AnimNotifyState_HitWindow.h"
#include "Combat/CombatAnimPlayback.h"
#include "Hawkeye.h"

#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"

#if WITH_EDITOR
#include "AnimNotifyState_MotionWarping.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/AnimMontageFactory.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "RootMotionModifier_SkewWarp.h"
#include "UObject/Package.h"

namespace HawkeyeCombatMontage
{
	static const FName NotifyTrack(TEXT("Hawkeye"));

	static UAnimMontage* LoadOrCreate(UAnimSequence& Sequence, const FString& PackageName)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (UAnimMontage* Existing = LoadObject<UAnimMontage>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}
		UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
		Factory->SourceAnimation = &Sequence;
		Factory->TargetSkeleton = Sequence.GetSkeleton();
		UPackage* Package = CreatePackage(*PackageName);
		UAnimMontage* Created = Cast<UAnimMontage>(Factory->FactoryCreateNew(UAnimMontage::StaticClass(), Package,
			FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (Created)
		{
			FAssetRegistryModule::AssetCreated(Created);
		}
		return Created;
	}

	static const UAnimSequenceBase* FirstSegmentAnim(const UAnimMontage& Montage)
	{
		if (Montage.SlotAnimTracks.Num() == 0 || Montage.SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 0)
		{
			return nullptr;
		}
		return Montage.SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
	}

	/** How the montage's one segment plays its sequence: the stretch kept and its rate. */
	struct FClipMap
	{
		float SequenceLength = 0.f;
		float Start = 0.f;
		float Rate = 1.f;
		float MontageLength = 0.f;

		/** A fraction of the sequence as montage seconds, clamped inside the montage. */
		float ToMontage(float Fraction) const
		{
			return FMath::Clamp((FMath::Clamp(Fraction, 0.f, 1.f) * SequenceLength - Start) / Rate, 0.f, MontageLength);
		}
	};

	/** One window from Start to End (fractions of the sequence), mapped into the montage. Null when Start < 0. */
	static UAnimNotifyState* AddWindow(UAnimMontage& Montage, float Start, float End, const FClipMap& Map,
		TSubclassOf<UAnimNotifyState> Class)
	{
		if (Start < 0.f || Map.MontageLength <= 0.f)
		{
			return nullptr;
		}
		const float MinSpan = 1.f / 60.f;
		const float From = FMath::Min(Map.ToMontage(Start), Map.MontageLength - MinSpan);
		const float To = FMath::Max(Map.ToMontage(FMath::Max(End, Start + 0.01f)), From + MinSpan);
		return UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(&Montage, NotifyTrack, FMath::Max(From, 0.f),
			To - FMath::Max(From, 0.f), Class);
	}

	/**
	 * Plays only [Spec.ClipStartSeconds, Spec.ClipEndSeconds] of the sequence at Spec.PlayRate, through the
	 * montage's own segment (what the montage editor's Start Time, End Time and Play Rate set), and makes
	 * the montage as long as that. Every build sets all three, so a relayout with no trim restores the whole clip.
	 */
	static FClipMap ApplyClip(UAnimMontage& Montage, const UAnimSequence& Sequence, const FHawkeyeCombatMontageSpec& Spec)
	{
		FClipMap Map;
		Map.SequenceLength = Sequence.GetPlayLength();
		Map.Start = FMath::Clamp(Spec.ClipStartSeconds, 0.f, FMath::Max(Map.SequenceLength - 1.f / 30.f, 0.f));
		const float End = Spec.ClipEndSeconds > Map.Start ? FMath::Min(Spec.ClipEndSeconds, Map.SequenceLength) : Map.SequenceLength;
		Map.Rate = FMath::Max(Spec.PlayRate, 0.05f);
		FAnimSegment& Segment = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		Segment.StartPos = 0.f;
		Segment.AnimStartTime = Map.Start;
		Segment.AnimEndTime = FMath::Max(End, Map.Start + 1.f / 30.f);
		Segment.AnimPlayRate = Map.Rate;
		Segment.LoopingCount = 1;
		Montage.SetCompositeLength(Montage.CalculateSequenceLength());
		if (Montage.CompositeSections.Num() > 0)
		{
			Montage.CompositeSections[0].SetTime(0.f);
		}
		Map.MontageLength = Montage.GetPlayLength();
		return Map;
	}

	static FString WindowText(UAnimSequenceBase* Animation, const UClass* Class)
	{
		float Start = 0.f;
		float End = 0.f;
		return HawkeyeCombatAnim::FindNotifyWindow(Animation, Class, Start, End)
			? FString::Printf(TEXT("%.2f-%.2f"), Start, End) : FString(TEXT("-"));
	}
}
#endif

UAnimMontage* UHawkeyeCombatMontageBuilder::BuildCombatMontage(UAnimSequence* Sequence, const FString& PackageName,
	const FHawkeyeCombatMontageSpec& Spec)
{
#if WITH_EDITOR
	using namespace HawkeyeCombatMontage;
	USkeleton* Skeleton = Sequence ? Sequence->GetSkeleton() : nullptr;
	if (!Skeleton)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildCombatMontage(%s): no sequence, or one without a skeleton."), *PackageName);
		return nullptr;
	}
	UAnimMontage* Montage = LoadOrCreate(*Sequence, PackageName);
	if (!Montage || Montage->SlotAnimTracks.Num() == 0)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildCombatMontage(%s): could not create the montage."), *PackageName);
		return nullptr;
	}
	if (FirstSegmentAnim(*Montage) != Sequence)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildCombatMontage(%s): exists over %s, not %s; delete it first."), *PackageName,
			*GetNameSafe(FirstSegmentAnim(*Montage)), *Sequence->GetName());
		return nullptr;
	}

	Montage->Modify();
	Montage->SlotAnimTracks[0].SlotName = Spec.SlotName;
	Skeleton->RegisterSlotNode(Spec.SlotName);

	// The builder is the one description of the notifies: whatever was there goes.
	Montage->Notifies.Reset();
	Montage->AnimNotifyTracks.Reset();
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, NotifyTrack);
	const FClipMap Map = ApplyClip(*Montage, *Sequence, Spec);
	AddWindow(*Montage, Spec.HitStart, Spec.HitEnd, Map, UAnimNotifyState_HitWindow::StaticClass());
	AddWindow(*Montage, Spec.ComboStart, Spec.ComboEnd, Map, UAnimNotifyState_ComboWindow::StaticClass());
	if (Spec.WarpEnd > 0.f)
	{
		// From the montage's first frame, whatever stretch of the clip it starts on.
		UAnimNotifyState_MotionWarping* Warp = Cast<UAnimNotifyState_MotionWarping>(
			AddWindow(*Montage, Map.Start / FMath::Max(Map.SequenceLength, UE_KINDA_SMALL_NUMBER), Spec.WarpEnd, Map,
				UAnimNotifyState_MotionWarping::StaticClass()));
		if (Warp)
		{
			URootMotionModifier_SkewWarp* Skew = NewObject<URootMotionModifier_SkewWarp>(Warp, NAME_None, RF_Transactional);
			Skew->WarpTargetName = Spec.WarpTargetName;
			Skew->bWarpTranslation = true;
			Skew->bIgnoreZAxis = true;
			Skew->bWarpRotation = true;
			Warp->RootMotionModifier = Skew;
		}
	}

	Montage->bEnableAutoBlendOut = !Spec.bHoldLastFrame;
	Montage->BlendIn.SetBlendTime(Spec.BlendInSeconds);
	Montage->BlendOut.SetBlendTime(Spec.BlendOutSeconds);
	if (Montage->CompositeSections.Num() > 0)
	{
		FCompositeSection& Section = Montage->CompositeSections[0];
		Section.NextSectionName = Spec.bLoop ? Section.SectionName : NAME_None;
	}
	Montage->RefreshCacheData();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	UE_LOG(LogHawkeye, Log, TEXT("BuildCombatMontage: %s over %s: %s"), *PackageName, *Sequence->GetName(),
		*DescribeMontage(Montage));
	return Montage;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildCombatMontage(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}

FString UHawkeyeCombatMontageBuilder::DescribeMontage(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return TEXT("none");
	}
#if WITH_EDITOR
	using namespace HawkeyeCombatMontage;
	const FName Slot = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].SlotName : NAME_None;
	const bool bLoop = Montage->CompositeSections.Num() > 0
		&& Montage->CompositeSections[0].NextSectionName == Montage->CompositeSections[0].SectionName;
	FString Clip(TEXT("-"));
	if (Montage->SlotAnimTracks.Num() > 0 && Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0)
	{
		const FAnimSegment& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		Clip = FString::Printf(TEXT("%.2f-%.2fx%.2f"), Segment.AnimStartTime, Segment.AnimEndTime, Segment.AnimPlayRate);
	}
	return FString::Printf(TEXT("slot=%s hit=%s combo=%s warp=%s loop=%d hold=%d rootmotion=%d clip=%s length=%.2f"), *Slot.ToString(),
		*WindowText(Montage, UAnimNotifyState_HitWindow::StaticClass()),
		*WindowText(Montage, UAnimNotifyState_ComboWindow::StaticClass()),
		*WindowText(Montage, UAnimNotifyState_MotionWarping::StaticClass()), bLoop ? 1 : 0,
		Montage->bEnableAutoBlendOut ? 0 : 1, Montage->HasRootMotion() ? 1 : 0, *Clip, Montage->GetPlayLength());
#else
	return Montage->GetName();
#endif
}

FVector UHawkeyeCombatMontageBuilder::GetRawBoneLocation(UAnimSequence* Sequence, FName Bone, float Time, bool bRootLocked)
{
#if WITH_EDITOR
	const USkeleton* Skeleton = Sequence ? Sequence->GetSkeleton() : nullptr;
	const IAnimationDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
	if (!Skeleton || !Model)
	{
		return FVector::ZeroVector;
	}
	const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
	const FFrameTime Frame = Model->GetFrameRate().AsFrameTime(FMath::Max(Time, 0.f));
	FTransform Accumulated = FTransform::Identity;
	for (int32 Index = Reference.FindBoneIndex(Bone); Index != INDEX_NONE; Index = Reference.GetParentIndex(Index))
	{
		const FName Name = Reference.GetBoneName(Index);
		const bool bLockedRoot = bRootLocked && Reference.GetParentIndex(Index) == INDEX_NONE;
		const FTransform Local = !bLockedRoot && Model->IsValidBoneTrackName(Name)
			? Model->EvaluateBoneTrackTransform(Name, Frame, EAnimInterpolationType::Linear)
			: Reference.GetRefBonePose()[Index];
		Accumulated = Accumulated * Local;
	}
	return Accumulated.GetLocation();
#else
	return FVector::ZeroVector;
#endif
}

TArray<FName> UHawkeyeCombatMontageBuilder::GetSkeletonBoneNames(UAnimSequence* Sequence)
{
	TArray<FName> Names;
	if (const USkeleton* Skeleton = Sequence ? Sequence->GetSkeleton() : nullptr)
	{
		const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			Names.Add(Reference.GetBoneName(Index));
		}
	}
	return Names;
}

bool UHawkeyeCombatMontageBuilder::GetNotifyWindow(UAnimSequenceBase* Animation, TSubclassOf<UAnimNotifyState> NotifyClass,
	float& Start, float& End)
{
	return HawkeyeCombatAnim::FindNotifyWindow(Animation, NotifyClass.Get(), Start, End);
}

bool UHawkeyeCombatMontageBuilder::MakeMeshSpaceAdditive(UAnimSequence* Pose, UAnimSequence* BasePose)
{
#if WITH_EDITOR
	if (!Pose || !BasePose)
	{
		return false;
	}
	if (Pose->AdditiveAnimType == AAT_RotationOffsetMeshSpace && Pose->RefPoseType == ABPT_AnimFrame
		&& Pose->RefPoseSeq == BasePose && Pose->RefFrameIndex == 0)
	{
		return false;
	}
	Pose->Modify();
	Pose->AdditiveAnimType = AAT_RotationOffsetMeshSpace;
	Pose->RefPoseType = ABPT_AnimFrame;
	Pose->RefPoseSeq = BasePose;
	Pose->RefFrameIndex = 0;
	// What the editor's additive settings do on a change: the compressed (delta) data is rebuilt.
	Pose->PostEditChange();
	Pose->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

UBlendSpace* UHawkeyeCombatMontageBuilder::BuildAimOffset(const FString& PackageName, UBlendSpace* Layout,
	const TArray<UAnimSequence*>& LayoutPoses, const TArray<UAnimSequence*>& Poses)
{
#if WITH_EDITOR
	USkeleton* Skeleton = Poses.Num() > 0 && Poses[0] ? Poses[0]->GetSkeleton() : nullptr;
	if (!Layout || !Skeleton || LayoutPoses.Num() != Poses.Num())
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildAimOffset(%s): needs a layout, poses on a skeleton, and one pose per layout pose (%d, %d)."),
			*PackageName, LayoutPoses.Num(), Poses.Num());
		return nullptr;
	}
	const FString AssetName = FPackageName::GetShortName(PackageName);
	UAimOffsetBlendSpace* Space = LoadObject<UAimOffsetBlendSpace>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr,
		LOAD_NoWarn | LOAD_Quiet);
	if (!Space)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Space = NewObject<UAimOffsetBlendSpace>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Space);
	}
	Space->Modify();
	Space->SetSkeleton(Skeleton);
	// The source's axes, ranges and grid: protected, so through reflection, the whole three-axis array.
	if (const FProperty* Axes = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters")))
	{
		Axes->CopyCompleteValue(Axes->ContainerPtrToValuePtr<void>(Space), Axes->ContainerPtrToValuePtr<void>(Layout));
	}
	while (Space->GetNumberOfBlendSamples() > 0)
	{
		Space->DeleteSample(Space->GetNumberOfBlendSamples() - 1);
	}
	int32 Added = 0;
	for (const FBlendSample& Sample : Layout->GetBlendSamples())
	{
		const int32 Index = LayoutPoses.IndexOfByKey(Sample.Animation.Get());
		UAnimSequence* Pose = Poses.IsValidIndex(Index) ? Poses[Index] : nullptr;
		if (!Pose)
		{
			continue;
		}
		if (!Space->IsAnimationCompatible(Pose) || Space->AddSample(Pose, Sample.SampleValue) == INDEX_NONE)
		{
			UE_LOG(LogHawkeye, Error, TEXT("BuildAimOffset(%s): %s refused at (%s): additive %d, skeleton %s."), *PackageName,
				*GetNameSafe(Pose), *Sample.SampleValue.ToCompactString(), static_cast<int32>(Pose->AdditiveAnimType.GetValue()),
				*GetNameSafe(Pose->GetSkeleton()));
			return nullptr;
		}
		++Added;
	}
	if (Added == 0)
	{
		UE_LOG(LogHawkeye, Error, TEXT("BuildAimOffset(%s): none of the layout's %d samples has a pose."), *PackageName,
			Layout->GetBlendSamples().Num());
		return nullptr;
	}
	Space->ValidateSampleData();
	Space->ResampleData();
	Space->PostEditChange();
	Space->MarkPackageDirty();
	return Space;
#else
	UE_LOG(LogHawkeye, Error, TEXT("BuildAimOffset(%s): editor builds only."), *PackageName);
	return nullptr;
#endif
}

FString UHawkeyeCombatMontageBuilder::DescribeBlendSpace(UBlendSpace* BlendSpace)
{
	if (!BlendSpace)
	{
		return TEXT("none");
	}
	FString Text;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const FBlendParameter& Parameter = BlendSpace->GetBlendParameter(Axis);
		Text += FString::Printf(TEXT("%s%s %.0f..%.0f (%d)"), Axis ? TEXT(", ") : TEXT(""), *Parameter.DisplayName, Parameter.Min,
			Parameter.Max, Parameter.GridNum);
	}
	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
	Text += FString::Printf(TEXT("; %d samples:"), Samples.Num());
	for (const FBlendSample& Sample : Samples)
	{
		Text += FString::Printf(TEXT(" %s (%.0f, %.0f)"), *GetNameSafe(Sample.Animation.Get()), Sample.SampleValue.X, Sample.SampleValue.Y);
	}
	return Text;
}
