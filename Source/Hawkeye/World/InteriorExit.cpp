#include "World/InteriorExit.h"

#include "Hawkeye.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Save/HawkeyeSaveSubsystem.h"

AInteriorExit::AInteriorExit()
{
	// In a wall's centre line; the sign reads EXIT in green, as every exit in the kit does.
	LayOutParts(3.f);
	SignText->SetText(NSLOCTEXT("Hawkeye", "InteriorExitSign", "EXIT"));
	SignText->SetTextRenderColor(FColor(150, 255, 170));
}

FText AInteriorExit::GetInteractPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Hawkeye", "InteriorLeave", "[E] {0}"),
		ExitName.IsEmpty() ? NSLOCTEXT("Hawkeye", "InteriorLeaveDefault", "[Leave]") : ExitName);
}

bool AInteriorExit::Travel()
{
	UHawkeyeSaveSubsystem* Save = UHawkeyeSaveSubsystem::Get(this);
	if (!Save)
	{
		return false;
	}
	if (Save->IsInInterior())
	{
		return Save->ReturnFromInterior(ReturnPointOverride);
	}
	if (FallbackDistrict.IsNull())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: not entered through a door and no FallbackDistrict set."), *GetName());
		return false;
	}
	// Opened straight into the interior: pretend it was entered from the fallback's doorstep, with no save.
	const FString District = FallbackDistrict.ToSoftObjectPath().GetLongPackageName();
	Save->BeginInteriorWithoutEntry(District, FallbackReturnPoint);
	return Save->ReturnFromInterior(ReturnPointOverride);
}
