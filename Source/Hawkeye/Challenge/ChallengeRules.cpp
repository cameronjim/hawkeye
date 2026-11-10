#include "Challenge/ChallengeRules.h"

#include "Challenge/ChallengeDefinition.h"

int32 UChallengeRules::ScoreForRadius(float RadialCm, float BullseyeRadius, float RingRadius, float OuterRadius,
	int32 CentrePoints, int32 RingPoints, int32 OuterPoints)
{
	const float Radial = FMath::Abs(RadialCm);
	if (Radial <= BullseyeRadius)
	{
		return CentrePoints;
	}
	if (Radial <= RingRadius)
	{
		return RingPoints;
	}
	return Radial <= OuterRadius ? OuterPoints : 0;
}

int32 UChallengeRules::ScoreHit(const UChallengeDefinition* Definition, float RadialCm)
{
	if (!Definition)
	{
		return 0;
	}
	return ScoreForRadius(RadialCm, Definition->BullseyeRadius, Definition->RingRadius, Definition->OuterRadius,
		Definition->CentrePoints, Definition->RingPoints, Definition->OuterPoints);
}

EChallengeMedal UChallengeRules::ComputeMedal(EChallengeType Type, float Value, float BronzeLine, float SilverLine, float GoldLine)
{
	auto Earns = [Type, Value](float Line)
	{
		if (Line <= 0.f)
		{
			return false;
		}
		return Type == EChallengeType::Archery ? Value >= Line : Value < Line;
	};
	if (Earns(GoldLine))
	{
		return EChallengeMedal::Gold;
	}
	if (Earns(SilverLine))
	{
		return EChallengeMedal::Silver;
	}
	return Earns(BronzeLine) ? EChallengeMedal::Bronze : EChallengeMedal::None;
}

EChallengeMedal UChallengeRules::MedalForRun(const UChallengeDefinition* Definition, int32 Score, float Seconds)
{
	if (!Definition)
	{
		return EChallengeMedal::None;
	}
	const float Value = Definition->Type == EChallengeType::Archery ? static_cast<float>(Score) : Seconds;
	return ComputeMedal(Definition->Type, Value, Definition->BronzeValue, Definition->SilverValue, Definition->GoldValue);
}

bool UChallengeRules::CheckFailure(float ElapsedSeconds, float TimeLimitSeconds, const FVector& PlayerLocation,
	const FVector& StartLocation, float AreaRadius, EChallengeEndReason& OutReason)
{
	if (TimeLimitSeconds > 0.f && ElapsedSeconds >= TimeLimitSeconds)
	{
		OutReason = EChallengeEndReason::TimeUp;
		return true;
	}
	if (AreaRadius > 0.f && FVector::Dist2D(PlayerLocation, StartLocation) > AreaRadius)
	{
		OutReason = EChallengeEndReason::LeftArea;
		return true;
	}
	return false;
}

bool UChallengeRules::IsNewBest(EChallengeType Type, int32 Score, float Seconds, const FChallengeRecord& Old)
{
	if (!Old.HasCompleted())
	{
		return true;
	}
	return Type == EChallengeType::Archery ? Score > Old.BestScore : Seconds < Old.BestSeconds;
}

FText UChallengeRules::FormatSeconds(float Seconds)
{
	const float Clamped = FMath::Max(Seconds, 0.f);
	// Tenths, rounded down, so a clock never shows a time the run has not reached.
	const int32 Tenths = FMath::FloorToInt(Clamped * 10.f + KINDA_SMALL_NUMBER);
	const int32 Minutes = Tenths / 600;
	const int32 WholeSeconds = (Tenths / 10) % 60;
	return FText::FromString(FString::Printf(TEXT("%d:%02d.%d"), Minutes, WholeSeconds, Tenths % 10));
}

FText UChallengeRules::MedalText(EChallengeMedal Medal)
{
	switch (Medal)
	{
	case EChallengeMedal::Gold:
		return NSLOCTEXT("Hawkeye", "MedalGold", "Gold");
	case EChallengeMedal::Silver:
		return NSLOCTEXT("Hawkeye", "MedalSilver", "Silver");
	case EChallengeMedal::Bronze:
		return NSLOCTEXT("Hawkeye", "MedalBronze", "Bronze");
	default:
		return NSLOCTEXT("Hawkeye", "MedalNone", "No medal");
	}
}

FText UChallengeRules::EndReasonText(EChallengeEndReason Reason)
{
	switch (Reason)
	{
	case EChallengeEndReason::Completed:
		return NSLOCTEXT("Hawkeye", "ChallengeCompleted", "Complete");
	case EChallengeEndReason::TimeUp:
		return NSLOCTEXT("Hawkeye", "ChallengeTimeUp", "Time up");
	case EChallengeEndReason::LeftArea:
		return NSLOCTEXT("Hawkeye", "ChallengeLeftArea", "Left the area");
	default:
		return NSLOCTEXT("Hawkeye", "ChallengeAborted", "Abandoned");
	}
}

float UChallengeRules::RadialDistanceOnFace(const FVector& Point, const FVector& FaceCentre, const FVector& FaceNormal)
{
	const FVector Normal = FaceNormal.GetSafeNormal();
	const FVector Offset = Point - FaceCentre;
	return (Offset - Normal * FVector::DotProduct(Offset, Normal)).Size();
}
