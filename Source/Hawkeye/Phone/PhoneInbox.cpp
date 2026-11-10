#include "Phone/PhoneInbox.h"

#include "Hawkeye.h"
#include "Engine/DataTable.h"

bool UPhoneInbox::LoadTable(const UDataTable* Table)
{
	if (!Table || Table->GetRowStruct() != FHawkeyePhoneMessage::StaticStruct())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: %s is not a table of FHawkeyePhoneMessage; the phone is empty."),
			*GetNameSafe(this), *GetNameSafe(Table));
		return false;
	}
	TMap<FName, FHawkeyePhoneMessage> Rows;
	Table->ForeachRow<FHawkeyePhoneMessage>(TEXT("UPhoneInbox"),
		[&Rows](const FName& Key, const FHawkeyePhoneMessage& Row) { Rows.Add(Key, Row); });
	SetMessages(Rows);
	return true;
}

void UPhoneInbox::SetMessages(const TMap<FName, FHawkeyePhoneMessage>& InMessages)
{
	Messages = InMessages;
	Scheduled.Reset();
}

bool UPhoneInbox::IsScheduled(FName Id) const
{
	return Scheduled.ContainsByPredicate([Id](const FScheduled& Entry) { return Entry.Id == Id; });
}

int32 UPhoneInbox::HandleTrigger(EHawkeyeMessageTrigger Trigger, FName Key, FName Chapter)
{
	int32 Count = 0;
	for (const TPair<FName, FHawkeyePhoneMessage>& Pair : Messages)
	{
		const FHawkeyePhoneMessage& Row = Pair.Value;
		if (Row.Trigger != Trigger || Received.Contains(Pair.Key) || IsScheduled(Pair.Key))
		{
			continue;
		}
		if (!Row.ChapterId.IsNone() && Row.ChapterId != Chapter)
		{
			continue;
		}
		const bool bKeyMatches = Trigger == EHawkeyeMessageTrigger::ChapterStart
			|| (Trigger == EHawkeyeMessageTrigger::Event ? Row.TriggerEvent == Key : Row.TriggerObjectiveId == Key);
		if (!bKeyMatches)
		{
			continue;
		}
		Scheduled.Add({ Pair.Key, FMath::Max(Row.DelaySeconds, 0.f) });
		++Count;
	}
	return Count;
}

TArray<FName> UPhoneInbox::Advance(float DeltaSeconds)
{
	TArray<FName> Arrived;
	const float Step = FMath::Max(DeltaSeconds, 0.f);
	for (FScheduled& Entry : Scheduled)
	{
		Entry.SecondsLeft -= Step;
	}
	// Soonest first, so two messages due in the same frame keep the order of their delays.
	Scheduled.StableSort([](const FScheduled& A, const FScheduled& B) { return A.SecondsLeft < B.SecondsLeft; });
	while (Scheduled.Num() > 0 && Scheduled[0].SecondsLeft <= KINDA_SMALL_NUMBER)
	{
		const FName Id = Scheduled[0].Id;
		Scheduled.RemoveAt(0);
		Arrive(Id);
		Arrived.Add(Id);
	}
	return Arrived;
}

void UPhoneInbox::Arrive(FName Id)
{
	Received.Remove(Id);
	Received.Add(Id);
	if (const FHawkeyePhoneMessage* Row = Messages.Find(Id); Row && Row->bRead)
	{
		Read.Add(Id);
	}
}

bool UPhoneInbox::MarkRead(FName Id)
{
	if (!Received.Contains(Id) || Read.Contains(Id))
	{
		return false;
	}
	Read.Add(Id);
	return true;
}

int32 UPhoneInbox::MarkThreadRead(const FString& Sender)
{
	int32 Count = 0;
	for (const FName Id : Received)
	{
		const FHawkeyePhoneMessage* Row = Messages.Find(Id);
		if (Row && Row->GetThreadName() == Sender && MarkRead(Id))
		{
			++Count;
		}
	}
	return Count;
}

int32 UPhoneInbox::GetUnreadCount() const
{
	int32 Count = 0;
	for (const FName Id : Received)
	{
		Count += Read.Contains(Id) ? 0 : 1;
	}
	return Count;
}

int32 UPhoneInbox::GetUnreadCountFrom(const FString& Sender) const
{
	int32 Count = 0;
	for (const FName Id : Received)
	{
		const FHawkeyePhoneMessage* Row = Messages.Find(Id);
		Count += Row && !Read.Contains(Id) && Row->GetThreadName() == Sender ? 1 : 0;
	}
	return Count;
}

TArray<FString> UPhoneInbox::GetContacts() const
{
	TArray<FString> Contacts;
	for (int32 Index = Received.Num() - 1; Index >= 0; --Index)
	{
		if (const FHawkeyePhoneMessage* Row = Messages.Find(Received[Index]))
		{
			Contacts.AddUnique(Row->GetThreadName());
		}
	}
	return Contacts;
}

TArray<FHawkeyeReceivedMessage> UPhoneInbox::GetThread(const FString& Sender) const
{
	TArray<FHawkeyeReceivedMessage> Thread;
	for (const FName Id : Received)
	{
		const FHawkeyePhoneMessage* Row = Messages.Find(Id);
		if (Row && Row->GetThreadName() == Sender)
		{
			FHawkeyeReceivedMessage& Entry = Thread.AddDefaulted_GetRef();
			Entry.Id = Id;
			Entry.Sender = Row->Sender;
			Entry.Text = Row->Text;
			Entry.bRead = Read.Contains(Id);
		}
	}
	return Thread;
}

FText UPhoneInbox::FirstLine(const FText& Text)
{
	FString Line = Text.ToString();
	int32 Break = INDEX_NONE;
	if (Line.FindChar(TEXT('\n'), Break))
	{
		Line.LeftInline(Break);
	}
	return FText::FromString(Line.TrimEnd());
}

void UPhoneInbox::Export(TArray<FName>& OutReceived, TArray<FName>& OutRead) const
{
	OutReceived = Received;
	OutRead.Reset();
	for (const FName Id : Received)
	{
		if (Read.Contains(Id))
		{
			OutRead.Add(Id);
		}
	}
}

void UPhoneInbox::Import(const TArray<FName>& InReceived, const TArray<FName>& InRead)
{
	Received.Reset();
	Read.Reset();
	Scheduled.Reset();
	for (const FName Id : InReceived)
	{
		if (Messages.Contains(Id))
		{
			Received.AddUnique(Id);
		}
	}
	for (const FName Id : InRead)
	{
		if (Received.Contains(Id))
		{
			Read.Add(Id);
		}
	}
}

void UPhoneInbox::ExportPending(TArray<FName>& OutIds, TArray<float>& OutSecondsLeft) const
{
	OutIds.Reset();
	OutSecondsLeft.Reset();
	for (const FScheduled& Entry : Scheduled)
	{
		OutIds.Add(Entry.Id);
		OutSecondsLeft.Add(FMath::Max(Entry.SecondsLeft, 0.f));
	}
}

void UPhoneInbox::ImportPending(const TArray<FName>& InIds, const TArray<float>& InSecondsLeft)
{
	for (int32 Index = 0; Index < InIds.Num(); ++Index)
	{
		const FName Id = InIds[Index];
		if (!Messages.Contains(Id) || Received.Contains(Id) || IsScheduled(Id))
		{
			continue;
		}
		const float SecondsLeft = InSecondsLeft.IsValidIndex(Index) ? FMath::Max(InSecondsLeft[Index], 0.f) : 0.f;
		Scheduled.Add({ Id, SecondsLeft });
	}
}

float UPhoneInbox::GetSecondsLeft(FName Id) const
{
	const FScheduled* Entry = Scheduled.FindByPredicate([Id](const FScheduled& Candidate) { return Candidate.Id == Id; });
	return Entry ? Entry->SecondsLeft : -1.f;
}
