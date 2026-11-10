#include "Combat/HealthComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;
	bIsDead = false;

	if (AActor* Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.AddDynamic(this, &UHealthComponent::HandleTakeAnyDamage);
	}
}

void UHealthComponent::HandleTakeAnyDamage(AActor* /*DamagedActor*/, float Damage, const UDamageType* /*DamageType*/, AController* InstigatedBy, AActor* DamageCauser)
{
	AActor* DamageInstigator = InstigatedBy ? Cast<AActor>(InstigatedBy->GetPawn()) : DamageCauser;
	ApplyDamage(Damage, DamageInstigator ? DamageInstigator : DamageCauser);
}

float UHealthComponent::ApplyMeleeDamage(float DamageAmount, AActor* DamageInstigator, bool bStagger)
{
	const float Taken = ApplyDamage(DamageAmount, DamageInstigator);

	// Damage that was ignored (invulnerable, already dead, non-positive) never staggers.
	if (bStagger && Taken > StaggerThreshold)
	{
		OnStaggered.Broadcast(this, DamageInstigator);
	}

	return Taken;
}

void UHealthComponent::Stagger(AActor* DamageInstigator)
{
	if (!bIsDead)
	{
		OnStaggered.Broadcast(this, DamageInstigator);
	}
}

float UHealthComponent::ApplyDamage(float DamageAmount, AActor* DamageInstigator)
{
	if (DamageAmount <= 0.f || bInvulnerable || bIsDead)
	{
		return 0.f;
	}

	const float OldHealth = CurrentHealth;
	// The partner's floor: one point short of dead, whatever the hit.
	const float Floor = bCannotDie ? FMath::Min(1.f, OldHealth) : 0.f;
	CurrentHealth = FMath::Clamp(CurrentHealth - DamageAmount, Floor, MaxHealth);

	const float ActualDelta = CurrentHealth - OldHealth;
	if (FMath::IsNearlyZero(ActualDelta))
	{
		return 0.f;
	}
	SecondsSinceDamage = 0.f;

	// Death is latched before the change is broadcast so listeners (the boss phase component)
	// already see IsDead() when they react to the hit that killed the actor.
	const bool bJustDied = CurrentHealth <= 0.f;
	bIsDead = bJustDied;

	OnHealthChanged.Broadcast(this, CurrentHealth, ActualDelta, DamageInstigator);

	if (bJustDied)
	{
		OnDeath.Broadcast(this, DamageInstigator);
	}

	return -ActualDelta;
}

float UHealthComponent::Heal(float HealAmount, AActor* Healer)
{
	if (HealAmount <= 0.f || bIsDead)
	{
		return 0.f;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth + HealAmount, 0.f, MaxHealth);

	const float ActualDelta = CurrentHealth - OldHealth;
	if (FMath::IsNearlyZero(ActualDelta))
	{
		return 0.f;
	}

	OnHealthChanged.Broadcast(this, CurrentHealth, ActualDelta, Healer);
	return ActualDelta;
}

void UHealthComponent::AdvanceRegen(float DeltaSeconds)
{
	const float Step = FMath::Max(DeltaSeconds, 0.f);
	SecondsSinceDamage += Step;
	if (!bRegenerates || bIsDead || CurrentHealth >= MaxHealth || SecondsSinceDamage < RegenDelay)
	{
		return;
	}
	// Only the part of this step past the delay heals, so the first tick after it is not a lump.
	const float Healing = FMath::Min(Step, SecondsSinceDamage - RegenDelay) * RegenPerSecond;
	Heal(Healing, GetOwner());
}

void UHealthComponent::Revive(float NewHealth)
{
	const float OldHealth = CurrentHealth;

	bIsDead = false;
	CurrentHealth = FMath::Clamp(NewHealth, KINDA_SMALL_NUMBER, MaxHealth);

	OnHealthChanged.Broadcast(this, CurrentHealth, CurrentHealth - OldHealth, nullptr);
}

void UHealthComponent::SetMaxHealth(float NewMaxHealth, bool bResetCurrent)
{
	MaxHealth = FMath::Max(NewMaxHealth, KINDA_SMALL_NUMBER);

	const float OldHealth = CurrentHealth;
	CurrentHealth = bResetCurrent ? MaxHealth : FMath::Clamp(CurrentHealth, 0.f, MaxHealth);

	if (bResetCurrent)
	{
		bIsDead = false;
	}

	if (!FMath::IsNearlyEqual(OldHealth, CurrentHealth))
	{
		OnHealthChanged.Broadcast(this, CurrentHealth, CurrentHealth - OldHealth, nullptr);
	}
}
