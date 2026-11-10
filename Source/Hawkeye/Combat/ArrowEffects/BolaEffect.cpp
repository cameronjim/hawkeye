#include "Combat/ArrowEffects/BolaEffect.h"

#include "Hawkeye.h"
#include "Combat/HealthComponent.h"
#include "World/ThugCharacter.h"

void ABolaEffect::Activate()
{
	Super::Activate();

	AThugCharacter* Thug = Cast<AThugCharacter>(GetHitActor());
	const UHealthComponent* Health = Thug ? Thug->GetHealthComponent() : nullptr;
	if (Thug && !Thug->IsLimp() && (!Health || Health->IsAlive()))
	{
		Thug->KnockdownFor(GetShooter(), TripSeconds, TripLaunchSpeed);
		bTripped = true;
		UE_LOG(LogHawkeye, Log, TEXT("%s: bola trips %s for %.1f s."), *GetName(), *Thug->GetName(), TripSeconds);
	}
	else
	{
		UE_LOG(LogHawkeye, Log, TEXT("%s: bola missed (hit %s)."), *GetName(), *GetNameSafe(GetHitActor()));
	}
	// The trip is the thug's own state from here; nothing left for this actor to do.
	SetLifeSpan(0.1f);
}
