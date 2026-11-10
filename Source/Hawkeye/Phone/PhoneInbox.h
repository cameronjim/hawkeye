#pragma once

#include "CoreMinimal.h"
#include "Phone/PhoneMessage.h"
#include "UObject/Object.h"
#include "PhoneInbox.generated.h"

class UDataTable;

/** One message that has arrived, as a thread shows it. */
USTRUCT(BlueprintType)
struct HAWKEYE_API FHawkeyeReceivedMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FText Sender;

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "Phone")
	bool bRead = false;
};

/**
 * The phone's rules, with no world and no UMG. UPhoneSubsystem owns one and feeds it the
 * mission's events; automation tests create one with NewObject and hand it rows.
 *
 * A trigger schedules every matching message that has neither arrived nor been scheduled yet, to
 * arrive after its DelaySeconds; Advance moves the clock and returns what arrived. A message
 * arrives at most once per campaign. Reading a contact's thread marks its messages read.
 */
UCLASS(BlueprintType)
class HAWKEYE_API UPhoneInbox : public UObject
{
	GENERATED_BODY()

public:
	/** Every row of Table (FHawkeyePhoneMessage), keyed by row name. A table of another struct is refused. */
	bool LoadTable(const UDataTable* Table);

	/** Replaces the message rows. Tests hand theirs in here. */
	void SetMessages(const TMap<FName, FHawkeyePhoneMessage>& InMessages);

	/**
	 * Schedules each message for this trigger. Key is the objective id or event name (None for
	 * ChapterStart); Chapter is the running chapter's asset name. Returns how many were scheduled.
	 */
	int32 HandleTrigger(EHawkeyeMessageTrigger Trigger, FName Key, FName Chapter);

	/** Runs the delay clock. Returns the ids that arrived, in arrival order. */
	TArray<FName> Advance(float DeltaSeconds);

	/** Marks one message read. False when it has not arrived or was already read. */
	bool MarkRead(FName Id);

	/** Marks every arrived message from Sender read. Returns how many changed. */
	int32 MarkThreadRead(const FString& Sender);

	/** Arrived and not read. The badge shows this. */
	UFUNCTION(BlueprintPure, Category = "Phone")
	int32 GetUnreadCount() const;

	/** Unread messages from Sender. */
	int32 GetUnreadCountFrom(const FString& Sender) const;

	/** Everyone who has written, most recent first. */
	TArray<FString> GetContacts() const;

	/** Sender's arrived messages, oldest first. */
	TArray<FHawkeyeReceivedMessage> GetThread(const FString& Sender) const;

	bool HasArrived(FName Id) const { return Received.Contains(Id); }
	bool IsScheduled(FName Id) const;
	int32 GetScheduledCount() const { return Scheduled.Num(); }
	int32 GetArrivedCount() const { return Received.Num(); }

	/** The message row Id, or null. */
	const FHawkeyePhoneMessage* FindMessage(FName Id) const { return Messages.Find(Id); }

	/** The first line of a message's text: what the notification shows. */
	static FText FirstLine(const FText& Text);

	/** What the save keeps: arrived ids in order, and the read ones. */
	void Export(TArray<FName>& OutReceived, TArray<FName>& OutRead) const;

	/** Back from a save. Unknown ids are dropped; anything scheduled is forgotten (ImportPending restores it). */
	void Import(const TArray<FName>& InReceived, const TArray<FName>& InRead);

	/** What the save keeps of the messages still on their delay: ids, and the seconds each has left. */
	void ExportPending(TArray<FName>& OutIds, TArray<float>& OutSecondsLeft) const;

	/**
	 * Back from a save, after Import: each id is scheduled again with the delay it had left. Ids that
	 * are unknown, already arrived or already scheduled are dropped; a missing or negative delay is 0.
	 */
	void ImportPending(const TArray<FName>& InIds, const TArray<float>& InSecondsLeft);

	/** Seconds until Id arrives, or -1 when it is not scheduled. */
	float GetSecondsLeft(FName Id) const;

private:
	/** Adds Id to the arrived list (read at once if its row says so). */
	void Arrive(FName Id);

	struct FScheduled
	{
		FName Id;
		float SecondsLeft = 0.f;
	};

	TMap<FName, FHawkeyePhoneMessage> Messages;
	TArray<FName> Received;
	TSet<FName> Read;
	TArray<FScheduled> Scheduled;
};
