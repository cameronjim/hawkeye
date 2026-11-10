#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeAIComponent.h"
#include "ThugTreeComponent.generated.h"

/**
 * Runs ST_Thug for AThugAIController and answers which of its states are active, in every build (the engine's
 * GetActiveStateNames is a gameplay-debugger extra). With hawkeye.DebugThugs 1 it draws the active path over
 * the thug's head each frame.
 */
UCLASS(ClassGroup = (Hawkeye))
class HAWKEYE_API UHawkeyeThugTreeComponent : public UStateTreeAIComponent
{
	GENERATED_BODY()

public:
	/** The active states, root first ("Thug", "Attack", "Melee", "Swing"). Empty while the tree is not running. */
	TArray<FName> GetActiveStatePath() const;

	/** True while the state called State is on the active path. */
	bool IsStateActive(FName State) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void DrawActivePath() const;
};
