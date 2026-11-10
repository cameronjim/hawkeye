#include "World/InteriorEntrance.h"

#include "Hawkeye.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Save/HawkeyeSaveSubsystem.h"

AInteriorEntrance::AInteriorEntrance()
{
	// On a facade: the leaf stands just proud of the wall, the surround behind it shows as a border.
	LayOutParts(8.f);
}

void AInteriorEntrance::BeginPlay()
{
	Super::BeginPlay();
	if (!DisplayName.IsEmpty())
	{
		SetSignText(DisplayName);
	}
}

FText AInteriorEntrance::GetInteractPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Hawkeye", "InteriorEnter", "[E] Enter {0}"),
		DisplayName.IsEmpty() ? NSLOCTEXT("Hawkeye", "InteriorBuilding", "[Building]") : DisplayName);
}

bool AInteriorEntrance::Travel()
{
	if (Interior.IsNull())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no Interior set."), *GetName());
		return false;
	}
	if (UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this))
	{
		return Save->EnterInterior(Interior, ReturnPointLabel);
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, Interior);
	return true;
}
