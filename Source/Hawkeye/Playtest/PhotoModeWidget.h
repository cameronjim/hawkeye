#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PhotoModeWidget.generated.h"

class UTextBlock;

/**
 * Photo mode's corner card, top left: "Photo mode, FOV 90" and the controls for the device in use. Built in
 * code, hit-test invisible. Photo captures are taken with no UI at all, so it never shows in photo_<n>.png.
 */
UCLASS()
class HAWKEYE_API UPhotoModeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Rewrites the card when the FOV (rounded), the device or the status ("photo 2 saved") changes. */
	void SetState(float Fov, bool bGamepad, const FString& Status = FString());

	/** The card's text as shown, for the tests. */
	FString GetShownText() const;

	/** The controls line for a device. */
	static FString GetControlsText(bool bGamepad);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ControlsText = nullptr;

	int32 ShownFov = -1;
	int32 ShownDevice = -1;
	FString ShownStatus;
};
