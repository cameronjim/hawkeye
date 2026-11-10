#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrimePanelWidget.generated.h"

class UBorder;
class UCrimeSubsystem;
class UTextBlock;

/**
 * The street crime's HUD line, top right under the challenge panel while a crime is on: the crime's
 * name and how far it is ("[Crime: mugging] 38 m"), and under it how it stands (thugs down, the
 * victim's hits, the loot). Collapsed otherwise. Built into UHawkeyeHudWidget's overlay; reads
 * UCrimeSubsystem every frame.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UCrimePanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The two lines for the crime that is on, seen from PlayerLocation. Pure so a test can read the format. */
	static void FormatLines(const UCrimeSubsystem* Crimes, const FVector& PlayerLocation, FText& OutTitle, FText& OutStatus);

	UFUNCTION(BlueprintPure, Category = "HUD|Crime")
	bool IsPanelVisible() const { return bShown; }

	UFUNCTION(BlueprintPure, Category = "HUD|Crime")
	FText GetTitleText() const { return TitleShown; }

	UFUNCTION(BlueprintPure, Category = "HUD|Crime")
	FText GetStatusText() const { return StatusShown; }

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
	//~ End UUserWidget interface

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Panel = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText = nullptr;

	/** Tracksuit red for the crime's line, cream for its state. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Crime")
	FLinearColor TitleColor = FLinearColor(1.f, 0.42f, 0.34f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HUD|Crime")
	FLinearColor TextColor = FLinearColor(1.f, 0.93f, 0.78f, 1.f);

private:
	bool bShown = false;
	FText TitleShown;
	FText StatusShown;
};
