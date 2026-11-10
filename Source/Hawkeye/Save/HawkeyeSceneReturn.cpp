#include "Save/HawkeyeSceneReturn.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

void FHawkeyeSceneReturn::BeginInterior(const FString& InReturnMap, FName InReturnPoint, bool bSaved)
{
	*this = FHawkeyeSceneReturn();
	ReturnMap = InReturnMap;
	ReturnPointLabel = InReturnPoint;
	bSavedOnEntry = bSaved;
	bInterior = true;
	bFadeInOnArrival = true;
	bInScene = true;
}

bool FHawkeyeSceneReturn::Leave(FName ReturnPointOverride)
{
	if (!bInScene)
	{
		return false;
	}
	bInScene = false;
	bReturnPending = true;
	bFadeInOnArrival = false;
	if (!ReturnPointOverride.IsNone())
	{
		ReturnPointLabel = ReturnPointOverride;
	}
	return true;
}

int32 FHawkeyeSceneReturn::SelectReturnPoint(
const TArray<FHawkeyeReturnCandidate>& Candidates, FName ReturnPointLabel)
{
	if (ReturnPointLabel.IsNone())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].Label == ReturnPointLabel)
		{
			return Index;
		}
	}
	// A packaged build has no labels, so the generator tags the actor with its label as well.
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].Tags.Contains(ReturnPointLabel))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

TArray<FHawkeyeReturnCandidate> FHawkeyeSceneReturn::GatherCandidates(UWorld* World)
{
	TArray<FHawkeyeReturnCandidate> Candidates;
	if (!World)
	{
		return Candidates;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		FHawkeyeReturnCandidate& Candidate = Candidates.AddDefaulted_GetRef();
#if WITH_EDITOR
		Candidate.Label = FName(*It->GetActorLabel(false));
#else
		Candidate.Label = It->GetFName();
#endif
		Candidate.Tags = It->Tags;
		Candidate.Transform = It->GetActorTransform();
	}
	return Candidates;
}
