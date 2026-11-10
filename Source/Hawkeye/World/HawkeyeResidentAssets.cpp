#include "World/HawkeyeResidentAssets.h"

#include "Hawkeye.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UObjectIterator.h"

UHawkeyeResidentAssets* UHawkeyeResidentAssets::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine && WorldContext
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UHawkeyeResidentAssets>() : nullptr;
}

bool UHawkeyeResidentAssets::ShouldKeep(const UObject* Object, const TSet<const UObject*>& AlreadyKept)
{
	return Object != nullptr && !AlreadyKept.Contains(Object);
}

void UHawkeyeResidentAssets::Keep(UObject* Object)
{
	if (ShouldKeep(Object, KeptSet))
	{
		KeptSet.Add(Object);
		Kept.Add(Object);
	}
}

int32 UHawkeyeResidentAssets::KeepWorldContent(const UWorld* World)
{
	if (!World)
	{
		return 0;
	}
	const int32 Before = Kept.Num();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		// The class, not the pawn: its defaults hold the mesh, the animation Blueprint and through it
		// the sample's choosers and databases.
		Keep(It->GetClass());
	}
	for (TObjectIterator<UPoseSearchDatabase> It; It; ++It)
	{
		if (IsValid(*It) && !It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			Keep(*It);
		}
	}
	const int32 Added = Kept.Num() - Before;
	UE_LOG(LogHawkeye, Log, TEXT("%s: keeping %d more object(s) loaded across level changes (%d in all)."), *GetName(),
		Added, Kept.Num());
	return Added;
}
