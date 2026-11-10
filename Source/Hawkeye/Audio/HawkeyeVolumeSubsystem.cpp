#include "Audio/HawkeyeVolumeSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UHawkeyeVolumeSubsystem* UHawkeyeVolumeSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = (GEngine && WorldContext)
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UHawkeyeVolumeSubsystem>() : nullptr;
}

void UHawkeyeVolumeSubsystem::Hold(UObject* Object)
{
	if (Object)
	{
		Held.AddUnique(Object);
	}
}
