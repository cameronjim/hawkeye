#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeSubsystem.h"
#include "Challenge/ChallengeTracker.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeRules.h"
#include "Crime/CrimeSubsystem.h"
#include "Crime/CrimeTracker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Save/HawkeyeCampaignState.h"
#include "Save/HawkeyeSaveSubsystem.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"
#include "World/ThugCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeFastTravelTest
{
	static UHawkeyeSaveSubsystem* MakeSave()
	{
		// UGameInstanceSubsystem declares ClassWithin = UGameInstance, so it needs one for an outer.
		UGameInstance* Outer = NewObject<UGameInstance>(GEngine);
		UHawkeyeSaveSubsystem* Save = NewObject<UHawkeyeSaveSubsystem>(Outer);
		Save->SlotNameOverride = TEXT("HawkeyeCampaignFastTravelTest");
		return Save;
	}

	static ASafehouse* SpawnSafehouse(const FHawkeyeTestWorld& TestWorld, FName Id, const FVector& At, float Yaw)
	{
		ASafehouse* Safehouse = Cast<ASafehouse>(TestWorld.SpawnActor(ASafehouse::StaticClass(), At, FRotator(0.f, Yaw, 0.f)));
		if (Safehouse)
		{
			Safehouse->SafehouseId = Id;
			Safehouse->DisplayName = FText::FromName(Id);
		}
		return Safehouse;
	}

	static FHawkeyeSafehouseEntry Entry(const TCHAR* Id)
	{
		FHawkeyeSafehouseEntry Out;
		Out.SafehouseId = Id;
		Out.DisplayName = FText::FromString(Id);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFastTravelDiscovery, "Hawkeye.FastTravel.DiscoveryByWalkingIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFastTravelDiscovery::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFastTravelTest;
	FHawkeyeTestWorld TestWorld;
	UHawkeyeSaveSubsystem* Save = MakeSave();
	ASafehouse* Safehouse = SpawnSafehouse(TestWorld, TEXT("sh_test_a"), FVector::ZeroVector, 0.f);
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(2000.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Safehouse spawned"), Safehouse) || !TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}
	USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(TestWorld.Get());
	TestTrue(TEXT("The safehouse registered at BeginPlay"), Safehouses && Safehouses->FindSafehouse(TEXT("sh_test_a")) == Safehouse);

	TestFalse(TEXT("A pawn nobody plays does not discover it"), Safehouse->DiscoverBy(Kate, Save));
	TestFalse(TEXT("Nor does something that is not a Hawkeye"), Safehouse->DiscoverBy(Safehouse, Save));
	TestFalse(TEXT("Not discovered yet"), Save->IsSafehouseDiscovered(TEXT("sh_test_a")));

	APlayerController* PC = Cast<APlayerController>(TestWorld.SpawnActor(APlayerController::StaticClass(), FVector::ZeroVector,
		FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Player controller spawned"), PC))
	{
		return false;
	}
	PC->Possess(Kate);
	TestTrue(TEXT("The player walking in discovers it"), Safehouse->DiscoverBy(Kate, Save));
	TestTrue(TEXT("And the save has it"), Save->IsSafehouseDiscovered(TEXT("sh_test_a")));
	TestFalse(TEXT("Walking in again is not a new discovery"), Safehouse->DiscoverBy(Kate, Save));
	TestEqual(TEXT("Listed once"), Save->GetDiscoveredSafehouses().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFastTravelListFiltering, "Hawkeye.FastTravel.ListFiltering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFastTravelListFiltering::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFastTravelTest;
	const TArray<FHawkeyeSafehouseEntry> All = { Entry(TEXT("sh_c")), Entry(TEXT("sh_a")), Entry(TEXT("sh_b")) };
	const TArray<FHawkeyeSafehouseEntry> List = USafehouseSubsystem::BuildTravelList(All, TEXT("sh_b"), { TEXT("sh_b"), TEXT("sh_c") });
	if (!TestEqual(TEXT("Every safehouse but the one you are at"), List.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("In id order"), List[0].SafehouseId, FName(TEXT("sh_a")));
	TestEqual(TEXT("In id order, second"), List[1].SafehouseId, FName(TEXT("sh_c")));
	TestFalse(TEXT("An undiscovered one is listed but not travellable"), List[0].bDiscovered);
	TestTrue(TEXT("A discovered one can be picked"), List[1].bDiscovered);
	TestEqual(TEXT("Nothing else when none is found"), USafehouseSubsystem::BuildTravelList(All, TEXT("sh_a"), {}).Num(), 2);
	TestEqual(TEXT("From nowhere every safehouse is listed"), USafehouseSubsystem::BuildTravelList(All, NAME_None, {}).Num(), 3);
	TestEqual(TEXT("One safehouse alone lists nothing"), USafehouseSubsystem::BuildTravelList({ Entry(TEXT("sh_a")) }, TEXT("sh_a"),
		{ TEXT("sh_a") }).Num(), 0);

	// The nearest discovered one, for "Mark nearest safehouse".
	FHawkeyeTestWorld TestWorld;
	SpawnSafehouse(TestWorld, TEXT("sh_near"), FVector(1000.f, 0.f, 0.f), 0.f);
	ASafehouse* Far = SpawnSafehouse(TestWorld, TEXT("sh_far"), FVector(30000.f, 0.f, 0.f), 0.f);
	const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(TestWorld.Get());
	if (!TestNotNull(TEXT("Safehouse subsystem"), Safehouses))
	{
		return false;
	}
	TestEqual(TEXT("Both registered"), Safehouses->GetSafehouses().Num(), 2);
	TestNull(TEXT("Nothing to mark before one is found"), Safehouses->FindNearest(FVector::ZeroVector, {}));
	TestEqual(TEXT("Only discovered ones count, however far"), Safehouses->FindNearest(FVector::ZeroVector, { TEXT("sh_far") }), Far);
	TestEqual(TEXT("The nearer of two found"), Safehouses->FindNearest(FVector::ZeroVector, { TEXT("sh_far"), TEXT("sh_near") })->SafehouseId,
		FName(TEXT("sh_near")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFastTravelRefusal, "Hawkeye.FastTravel.RefusedDuringCrimeOrChallenge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFastTravelRefusal::RunTest(const FString& Parameters)
{
	const FString Refused = TEXT("[Can't fast travel now]");
	TestTrue(TEXT("Free to travel when nothing is on"), USafehouseSubsystem::GetTravelRefusal(false, false, false).IsEmpty());
	TestEqual(TEXT("A crime refuses"), USafehouseSubsystem::GetTravelRefusal(true, false, false).ToString(), Refused);
	TestEqual(TEXT("A challenge refuses"), USafehouseSubsystem::GetTravelRefusal(false, true, false).ToString(), Refused);
	TestEqual(TEXT("A travel under way refuses"), USafehouseSubsystem::GetTravelRefusal(false, false, true).ToString(), Refused);
	TestEqual(TEXT("A fight (or being down) refuses"), USafehouseSubsystem::GetTravelRefusal(false, false, false, true).ToString(), Refused);

	// The same from a world's own crime and challenge.
	FHawkeyeTestWorld TestWorld;
	const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(TestWorld.Get());
	UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(TestWorld.Get());
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(TestWorld.Get());
	if (!TestNotNull(TEXT("Subsystems"), Safehouses) || !Crimes || !Crimes->GetTracker() || !Challenges || !Challenges->GetTracker())
	{
		AddError(TEXT("No crime or challenge subsystem in the test world."));
		return false;
	}
	TestTrue(TEXT("The world is free to travel"), Safehouses->GetTravelRefusalNow().IsEmpty());

	UCrimeDefinition* Mugging = NewObject<UCrimeDefinition>();
	Mugging->Type = ECrimeType::Mugging;
	Mugging->Id = UCrimeRules::TypeKey(ECrimeType::Mugging);
	Mugging->TimeToFailSeconds = 45.f;
	Mugging->VictimHitsToFail = 3;
	TestTrue(TEXT("A crime starts"), Crimes->GetTracker()->StartCrime(Mugging, FVector::ZeroVector, 2));
	TestEqual(TEXT("A crime on refuses travel"), Safehouses->GetTravelRefusalNow().ToString(), Refused);
	Crimes->GetTracker()->AbortCrime();
	TestTrue(TEXT("Once it is over travel is back"), Safehouses->GetTravelRefusalNow().IsEmpty());

	UChallengeDefinition* Route = NewObject<UChallengeDefinition>();
	Route->Id = TEXT("traversal_test");
	Route->Type = EChallengeType::Traversal;
	Route->TimeLimitSeconds = 150.f;
	Route->AreaRadius = 10000.f;
	Route->Checkpoints.Add(FTransform(FVector(1000.f, 0.f, 100.f)));
	Route->CheckpointLegs.Add(EChallengeLeg::Run);
	TestTrue(TEXT("A challenge starts"), Challenges->GetTracker()->StartRun(Route));
	TestEqual(TEXT("A challenge running refuses travel"), Safehouses->GetTravelRefusalNow().ToString(), Refused);
	Challenges->GetTracker()->AbortRun();
	TestTrue(TEXT("Once it is over travel is back"), Safehouses->GetTravelRefusalNow().IsEmpty());

	// A thug alerted is a fight: found by Hawkeye.Lap.Campaign, where the archers downed Kate during the fade.
	AThugCharacter* Thug = Cast<AThugCharacter>(TestWorld.SpawnActor(AThugCharacter::StaticClass(), FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator));
	if (TestNotNull(TEXT("Thug spawned"), Thug))
	{
		Thug->SetAlertState(EThugAlertState::Alerted);
		TestEqual(TEXT("A thug alerted refuses travel"), Safehouses->GetTravelRefusalNow().ToString(), Refused);
		Thug->SetAlertState(EThugAlertState::Calm);
		TestTrue(TEXT("Calm again, travel is back"), Safehouses->GetTravelRefusalNow().IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFastTravelArrival, "Hawkeye.FastTravel.ArrivesAtTheDoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFastTravelArrival::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFastTravelTest;
	FHawkeyeTestWorld TestWorld;
	const FVector Door(12000.f, -3000.f, 15.f);
	ASafehouse* Destination = SpawnSafehouse(TestWorld, TEXT("sh_dest"), Door, -151.f);
	AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(-10000.f, 8000.f, 200.f), FRotator::ZeroRotator));
	AHawkeyeCharacter* Clint = Cast<AHawkeyeCharacter>(
		TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(-10500.f, 8000.f, 200.f), FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Spawned"), Destination) || !Kate || !Clint)
	{
		return false;
	}
	TestFalse(TEXT("No destination, no travel"), USafehouseSubsystem::PlaceAtArrival(nullptr, Kate, Clint));
	TestTrue(TEXT("Both are put at the door"), USafehouseSubsystem::PlaceAtArrival(Destination, Kate, Clint));
	const float KateOff = FVector::Dist(Kate->GetActorLocation(), Door);
	const float ClintOff = FVector::Dist(Clint->GetActorLocation(), Door);
	TestTrue(FString::Printf(TEXT("Kate within 300 cm of the door (%.0f)"), KateOff), KateOff <= 300.f);
	TestTrue(FString::Printf(TEXT("Clint within 300 cm of the door (%.0f)"), ClintOff), ClintOff <= 300.f);
	TestTrue(TEXT("Side by side, not in each other"), FVector::Dist2D(Kate->GetActorLocation(), Clint->GetActorLocation()) >= 100.f);
	TestTrue(TEXT("Both out in the street, in front of the door"),
		FVector::DotProduct(Kate->GetActorLocation() - Door, Destination->GetActorForwardVector()) > 100.f
		&& FVector::DotProduct(Clint->GetActorLocation() - Door, Destination->GetActorForwardVector()) > 100.f);
	TestEqual(TEXT("Kate faces the street"), static_cast<float>(FRotator::NormalizeAxis(Kate->GetActorRotation().Yaw)), -151.f, 0.5f);
	TestTrue(TEXT("Without a partner she still arrives"), USafehouseSubsystem::PlaceAtArrival(Destination, Kate, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeFastTravelLastUsedSaved, "Hawkeye.FastTravel.LastUsedSaved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeFastTravelLastUsedSaved::RunTest(const FString& Parameters)
{
	using namespace HawkeyeFastTravelTest;
	UHawkeyeSaveSubsystem* Save = MakeSave();
	TestEqual(TEXT("None before any"), Save->GetLastSafehouse(), FName(NAME_None));
	Save->SetLastSafehouse(TEXT("ch01_east_7th"));
	Save->SetLastSafehouse(NAME_None);
	TestEqual(TEXT("Recorded, and None does not clear it"), Save->GetCampaignState()->LastSafehouse, FName(TEXT("ch01_east_7th")));
	Save->SetLastSafehouse(TEXT("avenue_b"));
	TestEqual(TEXT("The latest wins"), Save->GetLastSafehouse(), FName(TEXT("avenue_b")));
	const FProperty* Property = UHawkeyeCampaignState::StaticClass()->FindPropertyByName(TEXT("LastSafehouse"));
	TestTrue(TEXT("It is a SaveGame field, so SPUD writes it"), Property && Property->HasAnyPropertyFlags(CPF_SaveGame));
	Save->GetCampaignState()->ResetCampaign();
	TestEqual(TEXT("A new game forgets it"), Save->GetLastSafehouse(), FName(NAME_None));
	return true;
}

#endif
