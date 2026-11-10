#pragma once

#include "CoreMinimal.h"
#include "Phone/PhoneInbox.h"
#include "Subsystems/WorldSubsystem.h"
#include "PhoneSubsystem.generated.h"

class UMissionDefinition;
class UMissionObjective;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhoneMessageArrivedSignature, FName, MessageId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPhoneUnreadChangedSignature, int32, UnreadCount);

/**
 * The phone in a running world (claude-docs/gameplay-semantics.md, "Phone"). Loads DT_Messages
 * (UHawkeyeNarrativeSettings), turns the mission's events into triggers for UPhoneInbox, and when
 * a message arrives shows a 2 s "[Sender]: [first line]" toast and updates the HUD badge.
 *
 * The arrived and read ids, and the messages still waiting on their delay with the seconds they
 * have left, are mirrored into UHawkeyeCampaignState on every change and before every save, and
 * read back at world start and after a load, so SPUD saves them with the campaign. Triggers are ignored
 * while a save is being restored: the objectives it completes silently must not text anyone.
 */
UCLASS()
class HAWKEYE_API UPhoneSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Phone", meta = (WorldContext = "WorldContextObject"))
	static UPhoneSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Phone")
	UPhoneInbox* GetInbox() const { return Inbox; }

	/** A named story event (Trigger = Event rows with this TriggerEvent). Blueprints and volumes call this. */
	UFUNCTION(BlueprintCallable, Category = "Phone")
	int32 NotifyEvent(FName EventName);

	/** Opening Sender's thread: every message in it is read. Returns how many changed. */
	UFUNCTION(BlueprintCallable, Category = "Phone")
	int32 ReadThread(const FString& Sender);

	UFUNCTION(BlueprintPure, Category = "Phone")
	int32 GetUnreadCount() const { return Inbox ? Inbox->GetUnreadCount() : 0; }

	/** Writes the inbox into the campaign state (SPUD saves that), the messages still on their delay included. */
	void MirrorToCampaign() const;

	/**
	 * Schedules the ObjectiveCompleted messages of every objective already done that have neither
	 * arrived nor been scheduled. The save calls it before it mirrors: an objective's autosave can
	 * run before this subsystem has heard the same objective complete. Returns how many.
	 */
	int32 CatchUpTriggers();

	/** Reads the inbox back from the campaign state. */
	void RestoreFromCampaign();

	UPROPERTY(BlueprintAssignable, Category = "Phone")
	FOnPhoneMessageArrivedSignature OnMessageArrived;

	UPROPERTY(BlueprintAssignable, Category = "Phone")
	FOnPhoneUnreadChangedSignature OnUnreadChanged;

	/** How long the arrival notification stays up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phone", meta = (ClampMin = "0.1"))
	float NotificationSeconds = 2.f;

	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem interface

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	UFUNCTION()
	void HandleMissionStarted(UMissionDefinition* Mission);

	UFUNCTION()
	void HandleObjectiveUpdated(UMissionObjective* Objective, int32 ObjectiveIndex);

	UFUNCTION()
	void HandleCampaignLoaded(bool bSuccess);

	/** True while a save is restoring the world: triggers then are the save replaying, not play. */
	bool IsRestoring() const;

	/** Fires ObjectiveStarted when the current objective is not the one last seen. */
	void NoteCurrentObjective();

	/** The toast and the delegates for one arrival. */
	void AnnounceArrival(FName Id);

	UPROPERTY(Transient)
	TObjectPtr<UPhoneInbox> Inbox = nullptr;

	/** The running chapter's asset name, for ChapterId matching. */
	FName ChapterId;

	FName LastCurrentObjectiveId;
};
