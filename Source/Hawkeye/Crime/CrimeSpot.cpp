#include "Crime/CrimeSpot.h"

#include "Components/SceneComponent.h"
#include "Crime/CrimeDefinition.h"

ACrimeSpot::ACrimeSpot()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
	SetCanBeDamaged(false);
}

UCrimeDefinition* ACrimeSpot::FindCrime(ECrimeType Type) const
{
	for (UCrimeDefinition* Crime : Crimes)
	{
		if (Crime && Crime->Type == Type)
		{
			return Crime;
		}
	}
	return nullptr;
}
