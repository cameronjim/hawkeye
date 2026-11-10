#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PhoneMessage.generated.h"

/** What makes a phone message arrive. */
UENUM(BlueprintType)
enum class EHawkeyeMessageTrigger : uint8
{
	/** TriggerObjectiveId was completed. */
	ObjectiveCompleted,
	/** TriggerObjectiveId became the current objective (the chapter's first one does at its start). */
	ObjectiveStarted,
	/** The chapter (ChapterId, or any chapter when that is None) started. */
	ChapterStart,
	/** UPhoneSubsystem::NotifyEvent(TriggerEvent), from a Blueprint, a volume or another system. */
	Event
};

/**
 * One text on the phone: a row of DT_Messages. The row name is the message's id, which the save
 * records once it has arrived and once it has been read.
 */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyePhoneMessage : public FTableRowBase
{
	GENERATED_BODY()

	/** Who sent it. Messages are grouped into contacts by this text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone")
	FText Sender;

	/** The message. The notification shows its first line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone")
	EHawkeyeMessageTrigger Trigger = EHawkeyeMessageTrigger::ObjectiveCompleted;

	/** The objective id for ObjectiveCompleted and ObjectiveStarted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone")
	FName TriggerObjectiveId;

	/** The event name for Event. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone")
	FName TriggerEvent;

	/**
	 * The chapter's data asset name (DA_CH01_Rooftops) this message belongs to. None matches any
	 * chapter, which is only safe for events: objective ids repeat between chapters.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone")
	FName ChapterId;

	/** Seconds between the trigger and the message arriving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone", meta = (ClampMin = "0.0"))
	float DelaySeconds = 0.f;

	/** Arrives already read: no notification, not counted on the badge (old texts in the history). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phone")
	bool bRead = false;
};
