#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PhoneWidget.generated.h"

class UBorder;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;

/**
 * The phone: a panel that slides in from the right with the contacts who have written (most
 * recent first, unread counts beside them) and the selected contact's thread. Opening it shows the
 * newest thread; showing a thread reads it (UPhoneSubsystem::ReadThread, saved). Up and down
 * (W/S, arrows, D-pad, left stick, mouse wheel, a click) pick a contact; P, Escape, Tab or B close.
 *
 * AHawkeyePlayerController opens it on P or a 0.4 s hold of D-pad down and pauses under it, the
 * way the inventory does. Built in RebuildWidget; no designer layout.
 */
UCLASS(Blueprintable, BlueprintType)
class HAWKEYE_API UPhoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Refreshes from the phone, picks the newest contact and slides in. */
	UFUNCTION(BlueprintCallable, Category = "Phone")
	void Open();

	/** Slides out, then leaves the viewport. */
	UFUNCTION(BlueprintCallable, Category = "Phone")
	void Close();

	/** Rebuilds the contact list and the thread from UPhoneSubsystem. */
	UFUNCTION(BlueprintCallable, Category = "Phone")
	void Refresh();

	/** Shows contact Index's thread (and so reads it). Clamped to the list. */
	UFUNCTION(BlueprintCallable, Category = "Phone")
	void SelectContact(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Phone")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category = "Phone")
	int32 GetSelectedContactIndex() const { return SelectedIndex; }

	UFUNCTION(BlueprintPure, Category = "Phone")
	int32 GetContactCount() const { return Contacts.Num(); }

	/** The selected contact's name, or empty. */
	UFUNCTION(BlueprintPure, Category = "Phone")
	FString GetSelectedContact() const;

	/** How many messages the thread shows. */
	UFUNCTION(BlueprintPure, Category = "Phone")
	int32 GetThreadLength() const { return ThreadLength; }

	/** Seconds the slide takes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Phone", meta = (ClampMin = "0.0"))
	float SlideSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Phone", meta = (ClampMin = "200.0"))
	float PanelWidth = 420.f;

protected:
	//~ Begin UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget interface

	/** One contact line: the name, and the unread count when there is one. */
	void AddContactRow(int32 Index);

	/** The selected contact's messages as bubbles. */
	void RebuildThread();

	/** Asks the controller to close (it owns the pause). */
	void RequestClose();

	/** Swaps the footer between keyboard and gamepad key names. */
	void RefreshHint();

	/** A text block in the panel's style. */
	UTextBlock* MakeText(const FText& Text, int32 Size, const FLinearColor& Color) const;

	/** "[P] [Close]", or the gamepad equivalent. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> Panel = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ContactList = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ThreadScroll = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ThreadList = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> ContactRows;

	UPROPERTY(Transient)
	TArray<FString> Contacts;

	int32 SelectedIndex = INDEX_NONE;
	int32 ThreadLength = 0;
	bool bOpen = false;

	/** 0 fully off screen to the right, 1 in place. */
	float SlideAlpha = 0.f;
};
