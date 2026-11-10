#include "Challenge/ChallengeDefinition.h"

int32 UChallengeDefinition::GetStepCount() const
{
	return Type == EChallengeType::Archery ? Targets.Num() : Checkpoints.Num();
}

FText UChallengeDefinition::GetDisplayName() const
{
	return Name.IsEmpty() ? FText::FromString(FString::Printf(TEXT("[%s]"), *Id.ToString())) : Name;
}

FPrimaryAssetId UChallengeDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("Challenge"), GetFName());
}
