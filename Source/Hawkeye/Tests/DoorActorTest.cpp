#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Mission/MissionDefinition.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Player/HawkeyeCharacter.h"
#include "Tests/HawkeyeTestUtils.h"
#include "World/DoorActor.h"
#include "World/InteractionComponent.h"
#include "World/Interactable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeDoorTest
{
	static ADoorActor* SpawnLockedDoor(const FHawkeyeTestWorld& TestWorld)
	{
		ADoorActor* Door = Cast<ADoorActor>(
			TestWorld.SpawnActor(ADoorActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
		if (Door)
		{
			Door->bLocked = true;
			Door->RequiredKeycardId = FName(TEXT("red"));
		}
		return Door;
	}

	static AHawkeyeCharacter* SpawnPlayer(const FHawkeyeTestWorld& TestWorld)
	{
		return Cast<AHawkeyeCharacter>(
			TestWorld.SpawnActor(AHawkeyeCharacter::StaticClass(), FVector(300.f, 0.f, 0.f), FRotator::ZeroRotator));
	}

	/** A one-objective mission started on the test world's subsystem, so the door can complete it. */
	static UMissionSubsystem* StartMissionWith(UWorld* World, FName ObjectiveId)
	{
		UMissionSubsystem* Missions = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
		if (!Missions)
		{
			return nullptr;
		}

		UMissionDefinition* Mission = NewObject<UMissionDefinition>();
		UMissionObjective* Objective = NewObject<UMissionObjective>(Mission);
		Objective->ObjectiveId = ObjectiveId;
		Mission->Objectives.Add(Objective);
		Missions->StartMission(Mission);
		return Missions;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDoorRefusesWithoutKeycard, "Hawkeye.Door.RefusesWithoutKeycard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDoorRefusesWithoutKeycard::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	ADoorActor* Door = HawkeyeDoorTest::SpawnLockedDoor(TestWorld);
	AHawkeyeCharacter* Player = HawkeyeDoorTest::SpawnPlayer(TestWorld);
	if (!Door || !Player)
	{
		AddError(TEXT("Failed to spawn the door or the player."));
		return false;
	}

	TestFalse(TEXT("The door is locked for an empty-handed player"), Door->IsUnlockedFor(Player));
	TestFalse(TEXT("TryOpen refuses"), Door->TryOpen(Player));
	TestFalse(TEXT("And the door stays shut"), Door->IsOpen());
	TestEqual(TEXT("The prompt says why"),
		IInteractable::Execute_GetInteractPrompt(Door).ToString(), FString(TEXT("Locked: keycard required")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDoorOpensWithKeycard, "Hawkeye.Door.OpensWithKeycard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDoorOpensWithKeycard::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	ADoorActor* Door = HawkeyeDoorTest::SpawnLockedDoor(TestWorld);
	AHawkeyeCharacter* Player = HawkeyeDoorTest::SpawnPlayer(TestWorld);
	if (!Door || !Player)
	{
		AddError(TEXT("Failed to spawn the door or the player."));
		return false;
	}

	Player->GiveKeycard(FName(TEXT("red")));

	TestTrue(TEXT("The door is unlocked now"), Door->IsUnlockedFor(Player));
	TestTrue(TEXT("TryOpen opens it"), Door->TryOpen(Player));
	TestTrue(TEXT("It is open"), Door->IsOpen());
	TestFalse(TEXT("Opening it again does nothing"), Door->TryOpen(Player));
	TestTrue(TEXT("An open door is no longer interactable"),
		!IInteractable::Execute_CanInteract(Door, Player));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDoorCompletesObjectiveOnce, "Hawkeye.Door.CompletesObjectiveOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDoorCompletesObjectiveOnce::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	const FName ObjectiveId(TEXT("security_door"));

	UMissionSubsystem* Missions = HawkeyeDoorTest::StartMissionWith(TestWorld.Get(), ObjectiveId);
	if (!Missions)
	{
		AddError(TEXT("The test world has no UMissionSubsystem."));
		return false;
	}

	UHawkeyeTestListener* Listener = NewObject<UHawkeyeTestListener>();
	Missions->OnObjectiveUpdated.AddDynamic(Listener, &UHawkeyeTestListener::HandleObjectiveUpdated);

	ADoorActor* Door = HawkeyeDoorTest::SpawnLockedDoor(TestWorld);
	AHawkeyeCharacter* Player = HawkeyeDoorTest::SpawnPlayer(TestWorld);
	if (!Door || !Player)
	{
		AddError(TEXT("Failed to spawn the door or the player."));
		return false;
	}
	Door->CompletesObjectiveId = ObjectiveId;

	TestFalse(TEXT("A refused open completes nothing"), Door->TryOpen(Player));
	TestEqual(TEXT("No objective update yet"), Listener->ObjectiveUpdatedCount, 0);

	Player->GiveKeycard(FName(TEXT("red")));
	TestTrue(TEXT("The door opens"), Door->TryOpen(Player));
	TestEqual(TEXT("The objective completed once"), Listener->ObjectiveUpdatedCount, 1);

	// A second open is refused outright, so the objective can never fire twice.
	Door->TryOpen(Player);
	TestEqual(TEXT("Still once"), Listener->ObjectiveUpdatedCount, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDoorUnlockedNeedsNoKeycard, "Hawkeye.Door.UnlockedNeedsNoKeycard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDoorUnlockedNeedsNoKeycard::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;
	ADoorActor* Door = HawkeyeDoorTest::SpawnLockedDoor(TestWorld);
	AHawkeyeCharacter* Player = HawkeyeDoorTest::SpawnPlayer(TestWorld);
	if (!Door || !Player)
	{
		AddError(TEXT("Failed to spawn the door or the player."));
		return false;
	}

	Door->bLocked = false;

	TestEqual(TEXT("The prompt is the plain one"),
		IInteractable::Execute_GetInteractPrompt(Door).ToString(), FString(TEXT("[E] Open")));
	TestTrue(TEXT("It opens with no keycard at all"), Door->TryOpen(Player));

	return true;
}

/**
 * "The door to the second room only opens with the keycard on the top... I think the whole door
 * has to unlock???"
 *
 * It did only answer at the top. The frame used to be the actor's root, and a root component's
 * relative location is thrown away by the spawn transform, so the 130 cm the content script wrote
 * on it never arrived: the frame sank half under the floor and the leaf, whose offset is measured
 * from the frame, floated up out of reach. What was left at eye height was a gap, and the
 * interaction sweep went straight through it into corridor 2.
 *
 * So: the leaf must sit on the floor whatever the actor is spawned at, and a sweep at any height
 * up it has to come back with the door.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeDoorWholeSlabInteracts, "Hawkeye.Door.WholeSlabInteracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeDoorWholeSlabInteracts::RunTest(const FString& Parameters)
{
	FHawkeyeTestWorld TestWorld;

	// Spawned well away from the origin on purpose: this is what caught the root-component bug.
	const FVector DoorLocation(2900.f, 0.f, 0.f);
	ADoorActor* Door = Cast<ADoorActor>(
		TestWorld.SpawnActor(ADoorActor::StaticClass(), DoorLocation, FRotator::ZeroRotator));
	if (!Door || !Door->DoorMesh || !Door->FrameMesh)
	{
		AddError(TEXT("Failed to spawn the door."));
		return false;
	}

	// The leaf is 220 cm tall and stands on the threshold, so its centre is at 110 - not at 286,
	// which is where the old hierarchy put it.
	TestEqual(TEXT("The leaf stands on the floor, wherever the door was placed"),
		static_cast<float>(Door->GetLeafWorldCentre().Z),
		static_cast<float>(DoorLocation.Z) + Door->LeafHeight * 0.5f, 0.5f);
	TestEqual(TEXT("And the frame with it"),
		static_cast<float>(Door->FrameMesh->GetComponentLocation().Z),
		static_cast<float>(DoorLocation.Z) + Door->FrameHeight * 0.5f, 0.5f);

	// The frame is a solid cube as wide as the opening: if it collided it would plug the doorway
	// the door just cleared, and swallow the interaction sweep on the way.
	TestTrue(TEXT("The frame is scenery and never collides"),
		Door->FrameMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("The leaf blocks the interaction channel"),
		Door->DoorMesh->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);

	// Now the reproduction itself. The leaf needs geometry to be hit, and an engine shape is the
	// one mesh a test may reach for; without it the transform assertions above still stand.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		AddWarning(TEXT("/Engine/BasicShapes/Cube is not available; skipped the sweep."));
		return true;
	}

	// The shipped leaf: 10 cm thick, 100 wide, 220 tall.
	Door->DoorMesh->SetStaticMesh(Cube);
	Door->DoorMesh->SetRelativeScale3D(FVector(0.1f, 1.0f, 2.2f));

	UWorld* World = TestWorld.Get();
	const UInteractionComponent* Defaults = GetDefault<UInteractionComponent>();
	const float Radius = Defaults->TraceRadius;

	// 150 cm back from the slab, level, at the four heights a player's eyes can be: crouched,
	// looking down, standing, looking up. Every one has to find the door.
	for (const float Height : { 50.f, 100.f, 150.f, 200.f })
	{
		const FVector Start = DoorLocation + FVector(-150.f, 0.f, Height);
		const FVector End = DoorLocation + FVector(Defaults->InteractRange - 150.f, 0.f, Height);

		TArray<FHitResult> Hits;
		World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(Radius));

		const AActor* Found = nullptr;
		for (const FHitResult& Hit : Hits)
		{
			if (Hit.GetActor() == Door)
			{
				Found = Hit.GetActor();
				break;
			}
		}

		TestTrue(FString::Printf(TEXT("A sweep at %.0f cm finds the door"), Height), Found == Door);
	}

	return true;
}

#endif
