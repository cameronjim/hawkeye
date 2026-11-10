#include "World/ThugTreeComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"

static TAutoConsoleVariable<int32> CVarHawkeyeDebugThugs(TEXT("hawkeye.DebugThugs"), 0,
	TEXT("1: draw each thug's active ST_Thug states over his head."));

TArray<FName> UHawkeyeThugTreeComponent::GetActiveStatePath() const
{
	const UStateTree* Tree = StateTreeRef.GetStateTree();
	if (!IsRunning() || !Tree || !GetOwner())
	{
		return {};
	}
	// A read-only context only reads the instance data; it takes it by reference for the writable kinds.
	FStateTreeReadOnlyExecutionContext Context(GetOwner(), Tree, const_cast<FStateTreeInstanceData&>(InstanceData));
	return Context.GetActiveStateNames();
}

bool UHawkeyeThugTreeComponent::IsStateActive(FName State) const
{
	return GetActiveStatePath().Contains(State);
}

void UHawkeyeThugTreeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (CVarHawkeyeDebugThugs.GetValueOnGameThread() != 0)
	{
		DrawActivePath();
	}
}

void UHawkeyeThugTreeComponent::DrawActivePath() const
{
	const AController* Controller = Cast<AController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		return;
	}
	TArray<FString> Names;
	for (const FName& Name : GetActiveStatePath())
	{
		Names.Add(Name.ToString());
	}
	DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.f, 0.f, 130.f), FString::Join(Names, TEXT(" > ")), nullptr,
		FColor::Cyan, 0.f, true, 1.1f);
}
