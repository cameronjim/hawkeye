#include "Boss/BossDefinition.h"

TArray<FBossPhase> UBossDefinition::GetPhaseRules() const
{
	TArray<FBossPhase> Out;
	Out.Reserve(Phases.Num());
	for (const FBossPhaseDefinition& Phase : Phases)
	{
		Out.Add(Phase.Phase);
	}
	return Out;
}

TArray<FName> UBossDefinition::GetPhaseObjectives() const
{
	TArray<FName> Out;
	Out.Reserve(Phases.Num());
	for (const FBossPhaseDefinition& Phase : Phases)
	{
		Out.Add(Phase.ObjectiveId);
	}
	return Out;
}

FPrimaryAssetId UBossDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("BossDefinition"), GetFName());
}
