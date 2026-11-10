#include "Mission/MissionSubsystem.h"

#include "Hawkeye.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"

UMissionSubsystem* UMissionSubsystem::Get(const UObject* WorldContextObject)
{
	if (const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		return World->GetSubsystem<UMissionSubsystem>();
	}

	return nullptr;
}

void UMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Tracker = NewObject<UMissionTracker>(this, TEXT("MissionTracker"));
	Tracker->OnMissionStarted.AddDynamic(this, &UMissionSubsystem::HandleMissionStarted);
	Tracker->OnObjectiveUpdated.AddDynamic(this, &UMissionSubsystem::HandleObjectiveUpdated);
	Tracker->OnMissionComplete.AddDynamic(this, &UMissionSubsystem::HandleMissionComplete);
	Tracker->OnFlashbackRequested.AddDynamic(this, &UMissionSubsystem::HandleFlashbackRequested);
}

void UMissionSubsystem::Deinitialize()
{
	OnMissionStarted.Clear();
	OnObjectiveUpdated.Clear();
	OnMissionComplete.Clear();
	OnFlashbackRequested.Clear();

	if (Tracker)
	{
		Tracker->Reset();
		Tracker = nullptr;
	}
	MarkerActorCache.Reset();
	MissingMarkerLabels.Reset();

	Super::Deinitialize();
}

void UMissionSubsystem::HandleMissionStarted(UMissionDefinition* Mission)
{
	OnMissionStarted.Broadcast(Mission);
}

void UMissionSubsystem::HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex)
{
	OnObjectiveUpdated.Broadcast(Objective, ObjectiveIndex);
}

void UMissionSubsystem::HandleMissionComplete(UMissionDefinition* Mission)
{
	OnMissionComplete.Broadcast(Mission);
}

void UMissionSubsystem::HandleFlashbackRequested(UFlashbackDefinition* Flashback)
{
	OnFlashbackRequested.Broadcast(Flashback);
}

bool UMissionSubsystem::StartMission(UMissionDefinition* MissionDefinition)
{
	return Tracker ? Tracker->StartMission(MissionDefinition) : false;
}

bool UMissionSubsystem::CompleteObjective(FName ObjectiveId)
{
	return Tracker ? Tracker->CompleteObjective(ObjectiveId) : false;
}

void UMissionSubsystem::AbortMission()
{
	if (Tracker)
	{
		Tracker->AbortMission();
	}
}

UMissionDefinition* UMissionSubsystem::GetCurrentMission() const
{
	return Tracker ? Tracker->GetCurrentMission() : nullptr;
}

TArray<UMissionObjective*> UMissionSubsystem::GetActiveObjectives() const
{
	return Tracker ? Tracker->GetActiveObjectives() : TArray<UMissionObjective*>();
}

UMissionObjective* UMissionSubsystem::GetCurrentObjective() const
{
	return Tracker ? Tracker->GetCurrentObjective() : nullptr;
}

bool UMissionSubsystem::IsMissionComplete() const
{
	return Tracker && Tracker->IsMissionComplete();
}

bool UMissionSubsystem::HasObjective(FName ObjectiveId) const
{
	return Tracker && Tracker->FindObjective(ObjectiveId) != nullptr;
}

void UMissionSubsystem::RegisterObjectiveLocation(FName ObjectiveId, FVector Location)
{
	if (Tracker)
	{
		Tracker->RegisterObjectiveLocation(ObjectiveId, Location);
	}
}

void UMissionSubsystem::UnregisterObjectiveLocation(FName ObjectiveId)
{
	if (Tracker)
	{
		Tracker->UnregisterObjectiveLocation(ObjectiveId);
	}
}

void UMissionSubsystem::SetSecondaryMarkers(FName Source, const TArray<FVector>& Locations)
{
	if (Tracker)
	{
		Tracker->SetSecondaryMarkers(Source, Locations);
	}
}

void UMissionSubsystem::ClearSecondaryMarkers(FName Source)
{
	if (Tracker)
	{
		Tracker->ClearSecondaryMarkers(Source);
	}
}

TArray<FVector> UMissionSubsystem::GetSecondaryMarkers() const
{
	return Tracker ? Tracker->GetSecondaryMarkers() : TArray<FVector>();
}

bool UMissionSubsystem::GetCurrentObjectiveLocation(FVector& OutLocation) const
{
	const UMissionObjective* Objective = GetCurrentObjective();
	if (!Objective)
	{
		return false;
	}
	if (!Objective->bHasWorldLocation && !Objective->MarkerActorLabel.IsNone())
	{
		if (const AActor* Marker = FindMarkerActor(Objective->MarkerActorLabel))
		{
			OutLocation = Marker->GetActorLocation();
			return true;
		}
	}
	return Tracker && Tracker->GetCurrentObjectiveLocation(OutLocation);
}

AActor* UMissionSubsystem::FindMarkerActor(FName Label) const
{
	if (const TWeakObjectPtr<AActor>* Cached = MarkerActorCache.Find(Label))
	{
		if (Cached->IsValid())
		{
			return Cached->Get();
		}
	}

	UWorld* World = GetWorld();
	if (!World || MissingMarkerLabels.Contains(Label))
	{
		return nullptr;
	}
	const FString LabelString = Label.ToString();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
#if WITH_EDITOR
		const bool bLabelMatches = Actor->GetActorLabel(false) == LabelString;
#else
		const bool bLabelMatches = false;
#endif
		if (bLabelMatches || Actor->GetFName() == Label || Actor->Tags.Contains(Label))
		{
			MarkerActorCache.Add(Label, Actor);
			return Actor;
		}
	}

	UE_LOG(LogHawkeye, Warning, TEXT("%s: no actor labelled, named or tagged '%s' for an objective marker."),
		*GetNameSafe(this), *LabelString);
	MissingMarkerLabels.Add(Label);
	return nullptr;
}
