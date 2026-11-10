#include "Mission/MissionObjective.h"

bool UMissionObjective::Complete()
{
	if (bCompleted)
	{
		return false;
	}

	bCompleted = true;
	OnObjectiveCompleted.Broadcast(this);
	return true;
}

void UMissionObjective::ResetObjective()
{
	bCompleted = false;
}

bool UMissionObjective::MarkSpotted()
{
	if (!IsStealth() || bSpotted)
	{
		return false;
	}
	bSpotted = true;
	Title = SpottedTitle;
	Hint = SpottedHint;
	return true;
}

UWorld* UMissionObjective::GetWorld() const
{
	// Objectives live on a subsystem or a data asset; route through the outer when there is one
	// so Blueprint graphs on this class can use latent/world nodes. CDOs must return null.
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	if (const UObject* Owner = GetOuter())
	{
		return Owner->GetWorld();
	}

	return nullptr;
}
