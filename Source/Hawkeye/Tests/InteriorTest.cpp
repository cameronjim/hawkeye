#include "Audio/HawkeyeAudioMath.h"
#include "HawkeyeGameMode.h"
#include "Misc/AutomationTest.h"
#include "Player/HawkeyeCharacter.h"
#include "Save/HawkeyeSceneReturn.h"
#include "Sound/SoundBase.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The interior rules that need no map (claude-docs/gameplay-semantics.md, "Interiors"): the footstep
 * surface a floor's tags pick, the indoor camera arm, and the way into and out of an interior as the
 * scene-return state records it. The map-level checks (the stairs are walkable, the sample interior
 * loads) are in InteriorMapTest.cpp; the layout rules are Python (Tools/Editor/test_interior.py).
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorFootstepSurface, "Hawkeye.Interior.FootstepSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInteriorFootstepSurface::RunTest(const FString& Parameters)
{
	using namespace HawkeyeAudioMath;
	TestTrue(TEXT("No tags: snow"), ClassifyFootstepSurface({}) == EHawkeyeFootstepSurface::Snow);
	TestTrue(TEXT("A city slab: snow"), ClassifyFootstepSurface({ FName(TEXT("City")), FName(TEXT("CitySidewalk")) }) == EHawkeyeFootstepSurface::Snow);
	TestTrue(TEXT("Boards"), ClassifyFootstepSurface({ FName(TEXT("Interior")), WoodSurfaceTag }) == EHawkeyeFootstepSurface::Wood);
	TestTrue(TEXT("Carpet"), ClassifyFootstepSurface({ CarpetSurfaceTag }) == EHawkeyeFootstepSurface::Carpet);
	TestTrue(TEXT("Carpet laid over boards is carpet"), ClassifyFootstepSurface({ WoodSurfaceTag, CarpetSurfaceTag }) == EHawkeyeFootstepSurface::Carpet);

	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}
	Kate->FootstepSounds = { TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/SFX/MS_Foot_Snow_01.MS_Foot_Snow_01"))) };
	Kate->WoodFootstepSounds.Reset();
	Kate->CarpetFootstepSounds = { TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/SFX/MS_Footstep_Carpet.MS_Footstep_Carpet"))) };
	TestTrue(TEXT("A surface with no sounds of its own falls back to the snow set"),
		Kate->GetFootstepSoundsFor(EHawkeyeFootstepSurface::Wood)[0].ToSoftObjectPath() == Kate->FootstepSounds[0].ToSoftObjectPath());
	TestTrue(TEXT("Carpet uses the carpet set"),
		Kate->GetFootstepSoundsFor(EHawkeyeFootstepSurface::Carpet)[0].ToSoftObjectPath() == Kate->CarpetFootstepSounds[0].ToSoftObjectPath());
	TestTrue(TEXT("Standing on nothing is snow"), Kate->GetFootstepSurface() == EHawkeyeFootstepSurface::Snow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorCameraArm, "Hawkeye.Interior.CameraArm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInteriorCameraArm::RunTest(const FString& Parameters)
{
	const FHawkeyeTestWorld TestWorld;
	AHawkeyeAimTestCharacter* Kate = Cast<AHawkeyeAimTestCharacter>(
		TestWorld.SpawnActor(AHawkeyeAimTestCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("Kate spawned"), Kate))
	{
		return false;
	}
	TestFalse(TEXT("No Hawkeye game mode: not indoors"), AHawkeyeGameMode::WantsInteriorCamera(TestWorld.Get()));
	TestFalse(TEXT("Outdoors by default"), Kate->IsIndoorCamera());
	TestEqual(TEXT("Outdoors the hip arm is 350"), Kate->ComputeCameraTargets(false).ArmLength, 350.f);

	Kate->SetIndoorCamera(true);
	TestEqual(TEXT("Indoors the hip arm is 250"), Kate->ComputeCameraTargets(false).ArmLength, 250.f);
	TestEqual(TEXT("Indoors, halfway up the look-up blend (285) is still capped at 250"),
		Kate->ComputeCameraTargets(false, 40.f).ArmLength, 250.f);
	TestEqual(TEXT("A steep look up shortens it further, to 220"), Kate->ComputeCameraTargets(false, 60.f).ArmLength, 220.f);
	TestEqual(TEXT("Aiming is unchanged at 180"), Kate->ComputeCameraTargets(true).ArmLength, 180.f);
	TestEqual(TEXT("The shoulder offset is unchanged"), Kate->ComputeCameraTargets(false).SocketOffset, FVector(0.f, 70.f, 60.f));

	Kate->SetIndoorCamera(false);
	TestEqual(TEXT("Back outdoors: 350 again"), Kate->ComputeCameraTargets(false).ArmLength, 350.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeInteriorReturnRoundTrip, "Hawkeye.Interior.ReturnPointRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHawkeyeInteriorReturnRoundTrip::RunTest(const FString& Parameters)
{
	static const FName Doorstep(TEXT("City_InteriorReturn_Sample"));
	static const FName Roof(TEXT("City_InteriorRoof_Sample"));
	const FString District(TEXT("/Game/Maps/L_District_EastVillage"));

	FHawkeyeSceneReturn Return;
	TestFalse(TEXT("Leaving with nothing running does nothing"), Return.Leave());
	TestFalse(TEXT("And leaves nothing pending"), Return.bReturnPending);

	Return.BeginInterior(District, Doorstep, /*bSaved=*/true);
	TestTrue(TEXT("In: a scene is running"), Return.bInScene);
	TestTrue(TEXT("It is an interior"), Return.bInterior);
	TestTrue(TEXT("Its first frame fades in"), Return.bFadeInOnArrival);
	TestTrue(TEXT("The entry save is remembered"), Return.bSavedOnEntry);
	TestEqual(TEXT("Back to the district"), Return.ReturnMap, District);
	TestFalse(TEXT("An interior leaves the chapter's end sequence alone"), Return.ShouldResumeMissionFlow());

	TestTrue(TEXT("Out through the front door"), Return.Leave());
	TestTrue(TEXT("The return is pending"), Return.bReturnPending && !Return.bInScene);
	TestEqual(TEXT("To the doorstep it went in by"), Return.ReturnPointLabel, Doorstep);
	TestFalse(TEXT("A second exit does nothing"), Return.Leave(Roof));
	TestEqual(TEXT("And does not move the return point"), Return.ReturnPointLabel, Doorstep);

	Return.BeginInterior(District, Doorstep, /*bSaved=*/false);
	TestFalse(TEXT("Going in again starts clean"), Return.bReturnPending);
	TestTrue(TEXT("Out through the roof door"), Return.Leave(Roof));
	TestEqual(TEXT("Comes out on the roof"), Return.ReturnPointLabel, Roof);
	TestFalse(TEXT("No save was made, so the way back opens the district fresh"), Return.bSavedOnEntry);

	FHawkeyeSceneReturn Scene;
	Scene.bInScene = true;
	TestTrue(TEXT("A flashback's scene resumes the chapter's end sequence"), Scene.ShouldResumeMissionFlow());

	// The district's candidates: the doorstep point by label, the roof point by tag (a packaged build).
	auto Candidate = [](const TCHAR* Label, TArray<FName> Tags, const FVector& Where)
	{
		FHawkeyeReturnCandidate Out;
		Out.Label = FName(Label);
		Out.Tags = MoveTemp(Tags);
		Out.Transform = FTransform(Where);
		return Out;
	};
	const TArray<FHawkeyeReturnCandidate> Candidates = {
		Candidate(TEXT("City_InteriorEntrance_Sample"), { FName(TEXT("City")) }, FVector(0.f, 0.f, 15.f)),
		Candidate(TEXT("City_InteriorReturn_Sample"), { FName(TEXT("City")), Doorstep }, FVector(200.f, 0.f, 115.f)),
		Candidate(TEXT("TargetPoint_12"), { FName(TEXT("City")), Roof }, FVector(-600.f, 300.f, 1200.f)),
	};
	TestEqual(TEXT("The doorstep by its label"), FHawkeyeSceneReturn::SelectReturnPoint(Candidates, Doorstep), 1);
	TestEqual(TEXT("The roof by its tag"), FHawkeyeSceneReturn::SelectReturnPoint(Candidates, Roof), 2);
	return true;
}

#endif
