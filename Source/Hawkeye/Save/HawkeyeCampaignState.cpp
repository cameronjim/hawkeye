#include "Save/HawkeyeCampaignState.h"

#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

void UHawkeyeCampaignState::ResetCampaign()
{
	Version = CurrentVersion;
	MissionPath.Reset();
	CompletedObjectives.Reset();
	DiscoveredSafehouses.Reset();
	LastSafehouse = NAME_None;
	NoticedPlaces.Reset();
	PlayTimeSeconds = 0.f;
	ControlledCharacter.Reset();
	ReceivedMessages.Reset();
	ReadMessages.Reset();
	PendingMessages.Reset();
	PendingMessageSeconds.Reset();
	SeenChapterTitles.Reset();
	PlayedDialogueSequences.Reset();
	ChallengeIds.Reset();
	ChallengeBestScores.Reset();
	ChallengeBestSeconds.Reset();
	ChallengeBestMedals.Reset();
	ChallengeCompletions.Reset();
	CrimeTypes.Reset();
	CrimeCompletions.Reset();
	HintIds.Reset();
	HintShowCounts.Reset();
}

bool UHawkeyeCampaignState::CanMigrateFrom(int32 SavedVersion)
{
	// TODO(stage4): the first format change adds its migration here (and in the subsystem's
	// post-restore fix-up) instead of throwing old saves away.
	return SavedVersion == CurrentVersion;
}

bool UHawkeyeCampaignState::DoesMissionExist(const FString& InMissionPath)
{
	if (InMissionPath.IsEmpty())
	{
		return false;
	}
	const FSoftObjectPath Path(InMissionPath);
	const FString PackageName = Path.GetLongPackageName();
	return !PackageName.IsEmpty() && FPackageName::DoesPackageExist(PackageName);
}

EHawkeyeLoadDecision UHawkeyeCampaignState::EvaluateLoad(bool bSaveExists, int32 SavedVersion, const FString& InMissionPath)
{
	if (!bSaveExists)
	{
		return EHawkeyeLoadDecision::NoSave;
	}
	if (!CanMigrateFrom(SavedVersion))
	{
		return EHawkeyeLoadDecision::VersionMismatch;
	}
	if (!DoesMissionExist(InMissionPath))
	{
		return EHawkeyeLoadDecision::StaleMission;
	}
	return EHawkeyeLoadDecision::Load;
}

FString UHawkeyeCampaignState::OverrideName_Implementation() const
{
	return TEXT("HawkeyeCampaign");
}
