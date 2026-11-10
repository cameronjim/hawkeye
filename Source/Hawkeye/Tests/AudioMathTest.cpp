#include "Audio/HawkeyeAudioMath.h"
#include "Audio/HawkeyeAudioSubsystem.h"
#include "Audio/HawkeyeAudioTypes.h"
#include "HawkeyePlayerController.h"
#include "Misc/AutomationTest.h"
#include "Settings/HawkeyeSettings.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Tests/HawkeyeTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace HawkeyeAudioTest
{
	static constexpr EAutomationTestFlags Flags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioFootstepWalkStride, "Hawkeye.Audio.FootstepWalkStride",
	HawkeyeAudioTest::Flags)

bool FHawkeyeAudioFootstepWalkStride::RunTest(const FString& Parameters)
{
	FHawkeyeFootstepTracker Tracker;
	int32 Steps = 0;
	// 69 cm in 10 cm frames and one of 9: not yet a stride.
	for (int32 Frame = 0; Frame < 6; ++Frame)
	{
		Steps += Tracker.Advance(10.f, false, true) ? 1 : 0;
	}
	Steps += Tracker.Advance(9.f, false, true) ? 1 : 0;
	TestEqual(TEXT("69 cm walked is no step"), Steps, 0);
	Steps += Tracker.Advance(1.f, false, true) ? 1 : 0;
	TestEqual(TEXT("70 cm walked is one step"), Steps, 1);

	// 700 cm more at 10 cm a frame: ten more.
	for (int32 Frame = 0; Frame < 70; ++Frame)
	{
		Steps += Tracker.Advance(10.f, false, true) ? 1 : 0;
	}
	TestEqual(TEXT("770 cm walked is eleven steps"), Steps, 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioFootstepSprintStride, "Hawkeye.Audio.FootstepSprintStride",
	HawkeyeAudioTest::Flags)

bool FHawkeyeAudioFootstepSprintStride::RunTest(const FString& Parameters)
{
	FHawkeyeFootstepTracker Tracker;
	int32 Steps = 0;
	for (int32 Frame = 0; Frame < 55; ++Frame)
	{
		Steps += Tracker.Advance(10.f, true, true) ? 1 : 0;
	}
	TestEqual(TEXT("550 cm sprinted is ten 55 cm strides"), Steps, 10);

	// The remainder carries across frames: two 40 cm frames make one step with 25 cm left over.
	FHawkeyeFootstepTracker Carry;
	TestFalse(TEXT("40 cm sprinting is no step"), Carry.Advance(40.f, true, true));
	TestTrue(TEXT("80 cm sprinting has stepped"), Carry.Advance(40.f, true, true));
	TestEqual(TEXT("25 cm carried"), Carry.Accumulated, 25.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioFootstepAirAndTeleport, "Hawkeye.Audio.FootstepAirAndTeleport",
	HawkeyeAudioTest::Flags)

bool FHawkeyeAudioFootstepAirAndTeleport::RunTest(const FString& Parameters)
{
	FHawkeyeFootstepTracker Tracker;
	Tracker.Advance(60.f, false, true);
	TestFalse(TEXT("off the ground there are no steps"), Tracker.Advance(60.f, false, false));
	TestEqual(TEXT("the air resets the count"), Tracker.Accumulated, 0.f);
	TestFalse(TEXT("60 cm after landing is not a step yet"), Tracker.Advance(60.f, false, true));

	FHawkeyeFootstepTracker Jumpy;
	TestFalse(TEXT("a 5 m teleport is not a step"), Jumpy.Advance(500.f, false, true));
	TestEqual(TEXT("and leaves nothing to carry"), Jumpy.Accumulated, 0.f);

	// A long frame steps once and carries at most a stride, never a queue of steps.
	FHawkeyeFootstepTracker Hitch;
	TestTrue(TEXT("a 250 cm hitch steps"), Hitch.Advance(250.f, false, true));
	TestTrue(TEXT("carry is capped at one stride"), Hitch.Accumulated <= Hitch.WalkStrideCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioVolumeMapping, "Hawkeye.Audio.VolumeMapping", HawkeyeAudioTest::Flags)

bool FHawkeyeAudioVolumeMapping::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("slider 0 is silence"), HawkeyeAudioMath::SliderToGain(0.f), 0.f);
	TestEqual(TEXT("slider 1 is unity"), HawkeyeAudioMath::SliderToGain(1.f), 1.f);
	TestEqual(TEXT("slider 0.5 is a quarter (squared)"), HawkeyeAudioMath::SliderToGain(0.5f), 0.25f, 1e-5f);
	TestEqual(TEXT("above 1 clamps"), HawkeyeAudioMath::SliderToGain(3.f), 1.f);
	TestEqual(TEXT("below 0 clamps"), HawkeyeAudioMath::SliderToGain(-1.f), 0.f);

	FHawkeyeSettings Settings;
	const FHawkeyeClassVolumes Defaults = HawkeyeAudioMath::ComputeClassVolumes(Settings);
	TestEqual(TEXT("default master is full"), Defaults.Master, 1.f);
	TestEqual(TEXT("default sfx is full"), Defaults.Sfx, 1.f);
	TestEqual(TEXT("default ambience is 0.8 squared"), Defaults.Ambient, 0.64f, 1e-5f);

	Settings.MasterVolume = 0.5f;
	Settings.SfxVolume = 1.f;
	Settings.AmbientVolume = 0.f;
	const FHawkeyeClassVolumes Half = HawkeyeAudioMath::ComputeClassVolumes(Settings);
	TestEqual(TEXT("master applies"), Half.Master, 0.25f, 1e-5f);
	TestEqual(TEXT("sfx includes the master"), Half.Sfx, 0.25f, 1e-5f);
	TestEqual(TEXT("ambience at 0 is silent whatever the master"), Half.Ambient, 0.f);
	TestEqual(TEXT("ui follows sfx"), Half.UI, Half.Sfx);

	Settings.MasterVolume = 0.f;
	Settings.SfxVolume = 1.f;
	Settings.AmbientVolume = 1.f;
	const FHawkeyeClassVolumes Muted = HawkeyeAudioMath::ComputeClassVolumes(Settings);
	TestEqual(TEXT("master 0 silences sfx"), Muted.Sfx, 0.f);
	TestEqual(TEXT("master 0 silences ambience"), Muted.Ambient, 0.f);
	TestEqual(TEXT("master 0 silences ui"), Muted.UI, 0.f);

	// The controller computes the same gains even with no mix asset to push them into.
	FHawkeyeTestWorld World;
	AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(
		World.SpawnActor(AHawkeyePlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator));
	if (!TestNotNull(TEXT("a controller to apply the settings on"), PC))
	{
		return false;
	}
	Settings.MasterVolume = 0.8f;
	Settings.SfxVolume = 0.5f;
	PC->ApplyVolumeSettings(Settings);
	TestEqual(TEXT("controller sfx = 0.64 * 0.25"), PC->GetAppliedVolumes().Sfx, 0.16f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioAmbienceMix, "Hawkeye.Audio.AmbienceMix", HawkeyeAudioTest::Flags)

bool FHawkeyeAudioAmbienceMix::RunTest(const FString& Parameters)
{
	float Wind = 0.f;
	float Street = 0.f;
	HawkeyeAudioMath::ComputeAmbienceMix(0.f, Wind, Street);
	TestEqual(TEXT("street level: wind at its floor"), Wind, 0.15f, 1e-4f);
	TestEqual(TEXT("street level: street full"), Street, 1.f, 1e-4f);

	HawkeyeAudioMath::ComputeAmbienceMix(2500.f, Wind, Street);
	TestEqual(TEXT("a 25 m roof: wind full"), Wind, 1.f, 1e-4f);
	TestEqual(TEXT("a 25 m roof: street at its floor"), Street, 0.35f, 1e-4f);

	float LastWind = -1.f;
	bool bRising = true;
	for (float Height = 0.f; Height <= 1200.f; Height += 50.f)
	{
		HawkeyeAudioMath::ComputeAmbienceMix(Height, Wind, Street);
		bRising &= Wind >= LastWind;
		LastWind = Wind;
	}
	TestTrue(TEXT("wind never falls as she climbs"), bRising);

	TestEqual(TEXT("a kerb makes no landing sound"), HawkeyeAudioMath::ComputeLandingIntensity(20.f), 0.f);
	TestEqual(TEXT("a 40 cm drop is the quietest landing"), HawkeyeAudioMath::ComputeLandingIntensity(40.f), 0.1f);
	TestEqual(TEXT("6 m and up is full"), HawkeyeAudioMath::ComputeLandingIntensity(900.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioArrowSurface, "Hawkeye.Audio.ArrowSurface", HawkeyeAudioTest::Flags)

bool FHawkeyeAudioArrowSurface::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("anything with health is flesh"),
		HawkeyeAudioMath::ClassifyArrowSurface(true, TEXT("City_WaterTower_3")) == EHawkeyeArrowSurface::Flesh);
	TestTrue(TEXT("a water tower is wood"),
		HawkeyeAudioMath::ClassifyArrowSurface(false, TEXT("City_WaterTower_3")) == EHawkeyeArrowSurface::Wood);
	TestTrue(TEXT("a building is stone"),
		HawkeyeAudioMath::ClassifyArrowSurface(false, TEXT("City_Bldg_W248142394")) == EHawkeyeArrowSurface::Stone);
	TestTrue(TEXT("nothing is stone"),
		HawkeyeAudioMath::ClassifyArrowSurface(false, FString()) == EHawkeyeArrowSurface::Stone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHawkeyeAudioUnsetIsSilent, "Hawkeye.Audio.UnsetIsSilent", HawkeyeAudioTest::Flags)

bool FHawkeyeAudioUnsetIsSilent::RunTest(const FString& Parameters)
{
	// Tests build components with no sounds set and no world: every entry point must shrug.
	const TSoftObjectPtr<USoundBase> None;
	TestNull(TEXT("PlayAt with nothing set"),
		UHawkeyeAudioSubsystem::PlayAt(nullptr, None, FVector::ZeroVector, TEXT("t")));
	TestNull(TEXT("Play2D with nothing set"), UHawkeyeAudioSubsystem::Play2D(nullptr, None, TEXT("t")));
	TestNull(TEXT("PlayAttached with nothing"), UHawkeyeAudioSubsystem::PlayAttached(None, nullptr, TEXT("t")));
	UHawkeyeAudioSubsystem::PlayUI(nullptr, EHawkeyeUISound::Click);
	TObjectPtr<UAudioComponent> Loop = nullptr;
	UHawkeyeAudioSubsystem::StopLoop(Loop, TEXT("t"));
	TestNull(TEXT("StopLoop on null leaves null"), Loop.Get());
	return true;
}

#endif
