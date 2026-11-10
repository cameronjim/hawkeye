#include "UI/HawkeyeObjectiveWidget.h"
#include "Hawkeye.h"
#include "UI/HawkeyeHudGlyphs.h"
#include "Audio/HawkeyeAudioSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CanvasPanel.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "SceneView.h"
#include "Styling/CoreStyle.h"
#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "HawkeyePlayerController.h"
#include "Player/HawkeyeCharacter.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"

namespace HawkeyeObjectiveHud
{
	// The glyphs (text, diamond, house, medal) are shared with the world map.
	using namespace HawkeyeHudGlyphs;
	static constexpr float CompassHeight = 30.f;
}

TSharedRef<SWidget> UHawkeyeObjectiveWidget::RebuildWidget()
{
	// Nothing is laid out, everything is painted; an empty canvas gives the widget a body that
	// fills the HUD's overlay slot.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ObjectiveRoot"));
	}
	return Super::RebuildWidget();
}

void UHawkeyeObjectiveWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ObjectiveCompleteHeading.IsEmpty())
	{
		ObjectiveCompleteHeading = NSLOCTEXT("Hawkeye", "ObjectiveComplete", "Objective complete");
	}
	if (NewObjectiveHeading.IsEmpty())
	{
		NewObjectiveHeading = NSLOCTEXT("Hawkeye", "NewObjective", "New objective");
	}

	if (!bBound)
	{
		if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
		{
			Missions->OnMissionStarted.AddDynamic(this, &UHawkeyeObjectiveWidget::HandleMissionStarted);
			Missions->OnObjectiveUpdated.AddDynamic(this, &UHawkeyeObjectiveWidget::HandleObjectiveUpdated);
			bBound = true;
		}
	}
	NoteCurrentObjective();
}

void UHawkeyeObjectiveWidget::NativeDestruct()
{
	if (bBound)
	{
		if (UMissionSubsystem* Missions = UMissionSubsystem::Get(this))
		{
			Missions->OnMissionStarted.RemoveDynamic(this, &UHawkeyeObjectiveWidget::HandleMissionStarted);
			Missions->OnObjectiveUpdated.RemoveDynamic(this, &UHawkeyeObjectiveWidget::HandleObjectiveUpdated);
		}
		bBound = false;
	}

	Super::NativeDestruct();
}

void UHawkeyeObjectiveWidget::HandleMissionStarted(UMissionDefinition* /*Mission*/)
{
	LastCurrentObjectiveId = NAME_None;
	NoteCurrentObjective();
}

void UHawkeyeObjectiveWidget::HandleObjectiveUpdated(UMissionObjective* Objective, int32 /*ObjectiveIndex*/)
{
	if (ShouldToastCompletion(Objective))
	{
		PushToast(ObjectiveCompleteHeading, Objective->Title, EHawkeyeUISound::ObjectiveComplete);
	}
	NoteCurrentObjective();
}

bool UHawkeyeObjectiveWidget::ShouldToastCompletion(const UMissionObjective* Objective)
{
	return Objective && Objective->IsCompleted() && !Objective->IsHiddenFromHud();
}

void UHawkeyeObjectiveWidget::NoteCurrentObjective()
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const UMissionObjective* Current = Missions ? Missions->GetCurrentObjective() : nullptr;
	const FName CurrentId = Current ? Current->ObjectiveId : NAME_None;
	if (CurrentId == LastCurrentObjectiveId)
	{
		return;
	}
	LastCurrentObjectiveId = CurrentId;
	if (Current)
	{
		PushToast(NewObjectiveHeading, Current->Title, EHawkeyeUISound::NewObjective);
	}
}

void UHawkeyeObjectiveWidget::PushToast(FText Heading, FText Title, EHawkeyeUISound Sound)
{
	const bool bShowsNow = ToastQueue.Num() == 0;
	if (bShowsNow)
	{
		ToastElapsed = 0.f;
	}
	ToastQueue.Add({ Heading, Title, Sound });
	if (bShowsNow && !bToastsHeld)
	{
		UHawkeyeAudioSubsystem::PlayUI(this, Sound);
	}
}

void UHawkeyeObjectiveWidget::ClearToasts()
{
	ToastQueue.Reset();
	ToastElapsed = 0.f;
}

void UHawkeyeObjectiveWidget::SetToastsHeld(bool bHeld)
{
	if (bHeld == bToastsHeld)
	{
		return;
	}
	bToastsHeld = bHeld;
	if (bHeld)
	{
		// One cut off by the hold starts over when it comes back, rather than resuming half faded.
		ToastElapsed = 0.f;
		if (ToastQueue.Num() > 0)
		{
			UE_LOG(LogHawkeye, Verbose, TEXT("%s: %d toast(s) held."), *GetName(), ToastQueue.Num());
		}
		return;
	}
	if (ToastQueue.Num() > 0)
	{
		ToastElapsed = 0.f;
		UHawkeyeAudioSubsystem::PlayUI(this, ToastQueue[0].Sound);
		UE_LOG(LogHawkeye, Log, TEXT("%s: %d held toast(s) released: \"%s\" first."), *GetName(), ToastQueue.Num(),
			*ToastQueue[0].Heading.ToString());
	}
}

void UHawkeyeObjectiveWidget::AdvanceToasts(float DeltaSeconds)
{
	if (ToastQueue.Num() == 0 || bToastsHeld)
	{
		return;
	}
	ToastElapsed += FMath::Max(DeltaSeconds, 0.f);
	while (ToastQueue.Num() > 0 && ToastElapsed >= ToastSeconds)
	{
		ToastElapsed -= ToastSeconds;
		ToastQueue.RemoveAt(0);
		if (ToastQueue.Num() > 0)
		{
			UHawkeyeAudioSubsystem::PlayUI(this, ToastQueue[0].Sound);
		}
	}
	if (ToastQueue.Num() == 0)
	{
		ToastElapsed = 0.f;
	}
}

FText UHawkeyeObjectiveWidget::GetToastHeading() const
{
	return ToastQueue.Num() > 0 ? ToastQueue[0].Heading : FText::GetEmpty();
}

FText UHawkeyeObjectiveWidget::GetToastTitle() const
{
	return ToastQueue.Num() > 0 ? ToastQueue[0].Title : FText::GetEmpty();
}

void UHawkeyeObjectiveWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
	Super::NativeTick(MyGeometry, DeltaSeconds);

	// Under the chapter title card a toast would play behind the title, unreadable: it waits for the card.
	if (const AHawkeyePlayerController* Controller = Cast<AHawkeyePlayerController>(GetOwningPlayer()))
	{
		SetToastsHeld(Controller->IsChapterTitleShowing());
	}
	AdvanceToasts(DeltaSeconds);
	UpdateMarker(MyGeometry);
}

void UHawkeyeObjectiveWidget::UpdateMarker(const FGeometry& /*MyGeometry*/)
{
	bMarkerVisible = false;
	bCompassIconVisible = false;

	APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
	ViewBearing = UObjectiveMarkerMath::YawToBearing(PC->PlayerCameraManager->GetCameraRotation().Yaw, NorthYawDegrees);

	Secondary.Reset();
	NearestSecondary = INDEX_NONE;
	SafehouseIcons.Reset();
	ChallengeIcons.Reset();
	ChallengeMarks.Reset();
	MarkedSafehouseLabel = FText::GetEmpty();
	UpdateDowned(Pawn);

	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	FVector Target;
	const bool bHasTarget = Pawn && Missions && Missions->GetCurrentObjectiveLocation(Target);
	const float Distance = bHasTarget ? FVector::Dist(Pawn->GetActorLocation(), Target) : BIG_NUMBER;
	if (Pawn)
	{
		UpdateSafehouseIcons(Pawn->GetActorLocation(), CameraLocation);
		UpdateChallengeIcons(PC, Pawn->GetActorLocation(), CameraLocation, Distance);
	}
	if (Pawn && UpdateSecondaryMarkers(PC, Pawn->GetActorLocation(), CameraLocation))
	{
		// A challenge's markers take the objective's place until it ends.
		return;
	}

	if (!bHasTarget || Distance < HideWithinDistance)
	{
		return;
	}

	CompassIconOffset = UObjectiveMarkerMath::CompassOffset(
		UObjectiveMarkerMath::BearingBetween(CameraLocation, Target, NorthYawDegrees), ViewBearing,
		CompassWidth, CompassSpanDegrees, bCompassIconClamped);
	bCompassIconVisible = true;

	// The renderer's own view-projection, so the diamond sits exactly where the point is drawn.
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	FSceneViewProjectionData Projection;
	if (!LocalPlayer || !LocalPlayer->ViewportClient
		|| !LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, Projection))
	{
		return;
	}
	const FIntRect ViewRect = Projection.GetConstrainedViewRect();
	ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_KINDA_SMALL_NUMBER);
	ViewRectMin = FVector2D(ViewRect.Min);
	Placement = UObjectiveMarkerMath::PlaceMarker(Target, Projection.ComputeViewProjectionMatrix(),
		FVector2D(ViewRect.Width(), ViewRect.Height()), EdgeMargin * ViewportScale);
	DistanceText = UObjectiveMarkerMath::FormatDistance(Distance);
	bMarkerVisible = true;
}

bool UHawkeyeObjectiveWidget::UpdateSecondaryMarkers(APlayerController* PC, const FVector& PawnLocation, const FVector& CameraLocation)
{
	const UMissionSubsystem* Missions = UMissionSubsystem::Get(this);
	const TArray<FVector> Points = Missions ? Missions->GetSecondaryMarkers() : TArray<FVector>();
	if (Points.Num() == 0)
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	FSceneViewProjectionData Projection;
	const bool bProjects = LocalPlayer && LocalPlayer->ViewportClient
		&& LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, Projection);
	const FIntRect ViewRect = bProjects ? Projection.GetConstrainedViewRect() : FIntRect();
	ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_KINDA_SMALL_NUMBER);
	ViewRectMin = FVector2D(ViewRect.Min);
	// A marked safehouse draws as a house (with a "?" until found) and a marked pedestal as a medal.
	const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(this);
	const ASafehouse* MarkedHouse = Safehouses ? Safehouses->GetMarkedSafehouse() : nullptr;
	const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	const AChallengeStart* MarkedStart = Challenges ? Challenges->GetMarkedStart() : nullptr;
	float Nearest = BIG_NUMBER;
	for (const FVector& Point : Points)
	{
		FSecondaryMark Mark;
		Mark.Distance = FVector::Dist(PawnLocation, Point);
		if (MarkedHouse && Point.Equals(MarkedHouse->GetActorLocation() + FVector(0.f, 0.f, USafehouseSubsystem::MarkerUp), 1.f))
		{
			Mark.Kind = ESecondaryKind::Safehouse;
			Mark.bUnknown = !Safehouses->IsDiscoveredNow(MarkedHouse);
			MarkedSafehouseLabel = USafehouseSubsystem::GetMarkerName(MarkedHouse, !Mark.bUnknown);
		}
		else if (MarkedStart && Point.Equals(MarkedStart->GetActorLocation() + FVector(0.f, 0.f, UChallengeSubsystem::StartMarkerUp), 1.f))
		{
			Mark.Kind = ESecondaryKind::Challenge;
		}
		Mark.CompassOffset = UObjectiveMarkerMath::CompassOffset(UObjectiveMarkerMath::BearingBetween(CameraLocation, Point,
			NorthYawDegrees), ViewBearing, CompassWidth, CompassSpanDegrees, Mark.bCompassClamped);
		if (bProjects)
		{
			Mark.Placement = UObjectiveMarkerMath::PlaceMarker(Point, Projection.ComputeViewProjectionMatrix(),
				FVector2D(ViewRect.Width(), ViewRect.Height()), EdgeMargin * ViewportScale);
		}
		if (Mark.Distance < Nearest)
		{
			Nearest = Mark.Distance;
			NearestSecondary = Secondary.Num();
		}
		Secondary.Add(Mark);
	}
	SecondaryDistanceText = UObjectiveMarkerMath::FormatDistance(Nearest);
	return true;
}

void UHawkeyeObjectiveWidget::UpdateSafehouseIcons(const FVector& PawnLocation, const FVector& CameraLocation)
{
	const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(this);
	if (!Safehouses)
	{
		return;
	}
	for (const FHawkeyeSafehouseEntry& Entry : Safehouses->GetEntries())
	{
		if (FVector::Dist(PawnLocation, Entry.Location) < HideWithinDistance)
		{
			continue;
		}
		FSafehouseIcon& Icon = SafehouseIcons.AddDefaulted_GetRef();
		Icon.bDiscovered = Entry.bDiscovered;
		Icon.Offset = UObjectiveMarkerMath::CompassOffset(UObjectiveMarkerMath::BearingBetween(CameraLocation, Entry.Location,
			NorthYawDegrees), ViewBearing, CompassWidth, CompassSpanDegrees, Icon.bClamped);
	}
}

void UHawkeyeObjectiveWidget::UpdateChallengeIcons(APlayerController* PC, const FVector& PawnLocation,
	const FVector& CameraLocation, float ObjectiveDistance)
{
	const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(this);
	if (!Challenges || Challenges->IsRunning())
	{
		return;
	}
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	FSceneViewProjectionData Projection;
	const bool bProjects = LocalPlayer && LocalPlayer->ViewportClient
		&& LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, Projection);
	const FIntRect ViewRect = bProjects ? Projection.GetConstrainedViewRect() : FIntRect();
	for (const AChallengeStart* Start : Challenges->GetStarts())
	{
		// The icon floating over the cap, a little higher so the marker sits clear of it.
		const FVector Point = Start->GetActorLocation() + FVector(0.f, 0.f, UChallengeSubsystem::StartMarkerUp + 60.f);
		const float Distance = FVector::Dist(PawnLocation, Point);
		if (Distance < HideWithinDistance || Distance > ChallengeCompassRange)
		{
			continue;
		}
		FSafehouseIcon& Icon = ChallengeIcons.AddDefaulted_GetRef();
		Icon.Offset = UObjectiveMarkerMath::CompassOffset(UObjectiveMarkerMath::BearingBetween(CameraLocation, Point,
			NorthYawDegrees), ViewBearing, CompassWidth, CompassSpanDegrees, Icon.bClamped);
		// The world marker: near enough, nothing to do nearer, and not already the marked one.
		if (!bProjects || Distance > ChallengeMarkerRange || Distance >= ObjectiveDistance || Start == Challenges->GetMarkedStart())
		{
			continue;
		}
		ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_KINDA_SMALL_NUMBER);
		ViewRectMin = FVector2D(ViewRect.Min);
		FChallengeMark& Mark = ChallengeMarks.AddDefaulted_GetRef();
		Mark.Placement = UObjectiveMarkerMath::PlaceMarker(Point, Projection.ComputeViewProjectionMatrix(),
			FVector2D(ViewRect.Width(), ViewRect.Height()), EdgeMargin * ViewportScale);
		Mark.DistanceText = UObjectiveMarkerMath::FormatDistance(Distance);
	}
}

void UHawkeyeObjectiveWidget::UpdateDowned(const APawn* Pawn)
{
	bDownedRing = false;
	DownedText = FText::GetEmpty();
	DeathLine = FText::GetEmpty();
	const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Pawn);
	if (!Hawkeye || !Hawkeye->IsDowned())
	{
		return;
	}
	if (Hawkeye->HasDiedFromDown())
	{
		DeathLine = NSLOCTEXT("Hawkeye", "DownedDeathLine", "[You're down]");
		return;
	}
	bDownedRing = true;
	DownedFraction = Hawkeye->GetDownedFractionLeft();
	const AHawkeyeCharacter* Reviver = Cast<AHawkeyeCharacter>(Hawkeye->GetReviver());
	if (!Hawkeye->IsReviveExpected())
	{
		// Nobody is coming: only the way out (placeholder).
		DownedText = NSLOCTEXT("Hawkeye", "DownedNoRevive", "[Any key]");
	}
	else if (Reviver && FVector::Dist(Reviver->GetActorLocation(), Hawkeye->GetActorLocation()) <= ReviverBesideDistance)
	{
		DownedText = NSLOCTEXT("Hawkeye", "DownedHoldOn", "[Hold on]");
	}
	else
	{
		DownedText = FText::Format(NSLOCTEXT("Hawkeye", "DownedComing", "[{0} is coming]"),
			Reviver ? Reviver->GetCharacterName() : NSLOCTEXT("Hawkeye", "DownedPartner", "Help"));
	}
}

void UHawkeyeObjectiveWidget::PaintSafehouseIcons(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	// A house 12 px wide in the marker's cream: filled once found, hollow (and dimmer) before.
	const float CentreX = Geometry.GetLocalSize().X * 0.5f;
	const float Mid = CompassTop + HawkeyeObjectiveHud::CompassHeight * 0.5f;
	for (const FSafehouseIcon& Icon : SafehouseIcons)
	{
		FLinearColor Color = MarkerColor;
		Color.A = (Icon.bClamped ? 0.45f : 0.95f) * (Icon.bDiscovered ? 1.f : 0.8f);
		HawkeyeObjectiveHud::DrawHouse(Out, LayerId, Geometry, FVector2D(CentreX + Icon.Offset, Mid), 1.f, Color,
			Icon.bDiscovered, /*bQuestion=*/false);
	}
}

void UHawkeyeObjectiveWidget::PaintChallengeIcons(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeObjectiveHud;
	// On the strip: a small purple medal per pedestal in range.
	const float CentreX = Geometry.GetLocalSize().X * 0.5f;
	const float Mid = CompassTop + CompassHeight * 0.5f;
	for (const FSafehouseIcon& Icon : ChallengeIcons)
	{
		FLinearColor Color = SecondaryColor;
		Color.A = Icon.bClamped ? 0.45f : 0.95f;
		DrawMedal(Out, LayerId, Geometry, FVector2D(CentreX + Icon.Offset, Mid), 6.f, Color);
	}
	// In the world: the medal over the pedestal with its distance under it.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12);
	for (const FChallengeMark& Mark : ChallengeMarks)
	{
		const FVector2D Centre = (ViewRectMin + Mark.Placement.Position) / ViewportScale / FMath::Max(HudScale, 0.1f);
		FLinearColor Color = SecondaryColor;
		Color.A = Mark.Placement.bOnScreen ? 1.f : 0.6f;
		DrawMedal(Out, LayerId, Geometry, Centre, 8.f, Color);
		DrawText(Out, LayerId, Geometry, Mark.DistanceText, Font, Centre + FVector2D(0.f, 13.f), Color);
	}
}

void UHawkeyeObjectiveWidget::PaintDowned(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeObjectiveHud;
	const FVector2D Centre = Geometry.GetLocalSize() * 0.5f;
	if (!DeathLine.IsEmpty())
	{
		// Over the fade to black.
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 30);
		DrawText(Out, LayerId + 4, Geometry, DeathLine, Font, Centre - FVector2D(0.f, 20.f), FLinearColor::White);
		return;
	}
	if (!bDownedRing)
	{
		return;
	}
	// The time left as a ring that empties clockwise from the top, over a dim full one.
	const float Radius = 34.f;
	FLinearColor Track = FLinearColor::White;
	Track.A = 0.25f;
	FSlateDrawElement::MakeLines(Out, LayerId, Geometry.ToPaintGeometry(), ArcPoints(Centre, Radius, 1.f, 48),
		ESlateDrawEffect::None, Track, true, 5.f);
	if (DownedFraction > 0.f)
	{
		DrawShadowedLines(Out, LayerId + 1, Geometry, ArcPoints(Centre, Radius, DownedFraction, 48), SecondaryColor, 5.f);
	}
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16);
	DrawText(Out, LayerId + 2, Geometry, DownedText, Font, Centre + FVector2D(0.f, Radius + 10.f), FLinearColor::White);
}

void UHawkeyeObjectiveWidget::PaintSecondary(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeObjectiveHud;
	const float Half = SecondarySizePixels * 0.5f;
	const float CentreX = Geometry.GetLocalSize().X * 0.5f;
	for (int32 Index = 0; Index < Secondary.Num(); ++Index)
	{
		const FSecondaryMark& Mark = Secondary[Index];
		const FVector2D Centre = (ViewRectMin + Mark.Placement.Position) / ViewportScale / FMath::Max(HudScale, 0.1f);
		FLinearColor Color = Mark.Kind == ESecondaryKind::Safehouse ? MarkerColor : SecondaryColor;
		Color.A = Mark.Placement.bOnScreen ? 1.f : 0.6f;
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12);
		const FVector2D CompassPoint(CentreX + Mark.CompassOffset, CompassTop + HawkeyeObjectiveHud::CompassHeight * 0.5f);
		FLinearColor IconColor = Color;
		IconColor.A = Mark.bCompassClamped ? 0.5f : 1.f;
		if (Mark.Kind == ESecondaryKind::Safehouse)
		{
			// A house (a "?" in it until found) with the distance and the name under it.
			DrawHouse(Out, LayerId, Geometry, Centre, 1.6f, Color, !Mark.bUnknown, Mark.bUnknown);
			DrawText(Out, LayerId, Geometry, UObjectiveMarkerMath::FormatDistance(Mark.Distance), Font, Centre + FVector2D(0.f, 13.f), Color);
			DrawText(Out, LayerId, Geometry, MarkedSafehouseLabel, Font, Centre + FVector2D(0.f, 28.f), Color);
			DrawHouse(Out, LayerId + 2, Geometry, CompassPoint, 1.2f, IconColor, !Mark.bUnknown, /*bQuestion=*/false);
			continue;
		}
		if (Mark.Kind == ESecondaryKind::Challenge)
		{
			DrawMedal(Out, LayerId, Geometry, Centre, 8.f, Color);
			DrawText(Out, LayerId, Geometry, UObjectiveMarkerMath::FormatDistance(Mark.Distance), Font, Centre + FVector2D(0.f, 13.f), Color);
			DrawMedal(Out, LayerId + 2, Geometry, CompassPoint, 7.f, IconColor);
			continue;
		}
		DrawDiamond(Out, LayerId, Geometry, Centre, Half, MarkerLineWidth, Color);
		if (Index == NearestSecondary)
		{
			DrawText(Out, LayerId, Geometry, SecondaryDistanceText, Font, Centre + FVector2D(0.f, Half + 3.f), SecondaryColor);
		}
		DrawDiamond(Out, LayerId + 2, Geometry, CompassPoint, 6.f, 2.f, IconColor);
	}
}

void UHawkeyeObjectiveWidget::SetHudScale(float Scale)
{
	HudScale = FMath::Clamp(Scale, 0.5f, 2.f);
}

void UHawkeyeObjectiveWidget::SetAccentColors(const FLinearColor& Cream, const FLinearColor& Purple, bool bDesign)
{
	if (!bDesignColorsKept)
	{
		DesignMarkerColor = MarkerColor;
		DesignSecondaryColor = SecondaryColor;
		bDesignColorsKept = true;
	}
	MarkerColor = bDesign ? DesignMarkerColor : Cream;
	SecondaryColor = bDesign ? DesignSecondaryColor : Purple;
}

int32 UHawkeyeObjectiveWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
		bParentEnabled);
	// Everything is painted in a child geometry scaled by the HUD scale about the top-left corner: the
	// compass and toasts centre on its (smaller) local width, so they stay top centre; the markers'
	// screen points are divided by the scale on the way in, so they stay on what they mark.
	const float Scale = FMath::Max(HudScale, 0.1f);
	const FGeometry Scaled = AllottedGeometry.MakeChild(AllottedGeometry.GetLocalSize() / Scale, FSlateLayoutTransform(Scale));
	PaintCompass(Scaled, OutDrawElements, Layer + 1);
	PaintSafehouseIcons(Scaled, OutDrawElements, Layer + 3);
	PaintChallengeIcons(Scaled, OutDrawElements, Layer + 3);
	PaintMarker(Scaled, OutDrawElements, Layer + 1);
	PaintSecondary(Scaled, OutDrawElements, Layer + 1);
	PaintToast(Scaled, OutDrawElements, Layer + 1);
	PaintDowned(Scaled, OutDrawElements, Layer + 1);
	return Layer + 7;
}

void UHawkeyeObjectiveWidget::PaintMarker(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeObjectiveHud;
	if (!bMarkerVisible)
	{
		return;
	}

	const FVector2D Centre = (ViewRectMin + Placement.Position) / ViewportScale / FMath::Max(HudScale, 0.1f);
	const float Half = MarkerSizePixels * 0.5f;
	DrawDiamond(Out, LayerId, Geometry, Centre, Half, MarkerLineWidth, MarkerColor);

	// Off screen, a chevron just outside the diamond points the way to turn.
	const float Radians = FMath::DegreesToRadians(Placement.ArrowAngleDegrees);
	const FVector2D Dir(FMath::Cos(Radians), FMath::Sin(Radians));
	if (!Placement.bOnScreen)
	{
		const FVector2D Side(-Dir.Y, Dir.X);
		const FVector2D Tip = Centre + Dir * (Half + 12.f);
		const FVector2D Base = Centre + Dir * (Half + 5.f);
		const TArray<FVector2D> Chevron{ Base + Side * 5.f, Tip, Base - Side * 5.f };
		FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(), Chevron,
			ESlateDrawEffect::None, MarkerColor, true, MarkerLineWidth);
	}

	// The distance goes under the diamond, or over it when the arrow is pointing down.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);
	const bool bTextAbove = !Placement.bOnScreen && Dir.Y > 0.5f;
	const float TextHeight = MeasureText(DistanceText, Font).Y;
	const FVector2D TextTop = bTextAbove
		? Centre - FVector2D(0.f, Half + 4.f + TextHeight)
		: Centre + FVector2D(0.f, Half + 4.f);
	DrawText(Out, LayerId, Geometry, DistanceText, Font, TextTop, MarkerColor);
}

void UHawkeyeObjectiveWidget::PaintCompass(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeObjectiveHud;
	const float CentreX = Geometry.GetLocalSize().X * 0.5f;
	const float HalfWidth = CompassWidth * 0.5f;
	const float Bottom = CompassTop + CompassHeight;

	FSlateDrawElement::MakeBox(Out, LayerId,
		Geometry.ToPaintGeometry(FVector2f(CompassWidth, CompassHeight),
			FSlateLayoutTransform(FVector2f(CentreX - HalfWidth, CompassTop))),
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, CompassBackColor);

	static const TCHAR* Cardinals[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
	const FSlateFontInfo LetterFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
	const int32 TickCount = FMath::Max(1, FMath::RoundToInt(360.f / CompassTickDegrees));
	for (int32 Tick = 0; Tick < TickCount; ++Tick)
	{
		const float Bearing = Tick * CompassTickDegrees;
		const float Offset = UObjectiveMarkerMath::BearingDelta(Bearing, ViewBearing) / CompassSpanDegrees * CompassWidth;
		if (FMath::Abs(Offset) > HalfWidth - 4.f)
		{
			continue;
		}
		// Fade toward the ends so the strip has no hard cut.
		const float Edge = FMath::Abs(Offset) / HalfWidth;
		FLinearColor Color = FLinearColor::White;
		Color.A = 1.f - 0.75f * Edge * Edge;
		const float X = CentreX + Offset;
		const int32 WholeBearing = FMath::RoundToInt(Bearing);
		if (WholeBearing % 90 == 0)
		{
			DrawText(Out, LayerId + 1, Geometry, FText::FromString(Cardinals[(WholeBearing / 90) % 4]), LetterFont,
				FVector2D(X, CompassTop + 5.f), Color);
			continue;
		}
		const float Length = WholeBearing % 45 == 0 ? 12.f : 7.f;
		Color.A *= 0.8f;
		FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(),
			TArray<FVector2D>{ FVector2D(X, Bottom - 4.f - Length), FVector2D(X, Bottom - 4.f) },
			ESlateDrawEffect::None, Color, true, 2.f);
	}

	// Where the camera faces: a small caret under the strip's centre.
	FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(),
		TArray<FVector2D>{ FVector2D(CentreX - 6.f, Bottom + 8.f), FVector2D(CentreX, Bottom + 2.f),
			FVector2D(CentreX + 6.f, Bottom + 8.f) },
		ESlateDrawEffect::None, MarkerColor, true, 2.f);

	if (bCompassIconVisible)
	{
		FLinearColor IconColor = MarkerColor;
		IconColor.A = bCompassIconClamped ? 0.55f : 1.f;
		DrawDiamond(Out, LayerId + 2, Geometry, FVector2D(CentreX + CompassIconOffset, CompassTop + CompassHeight * 0.5f),
			8.f, 2.f, IconColor);
	}
}

void UHawkeyeObjectiveWidget::PaintToast(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeObjectiveHud;
	if (ToastQueue.Num() == 0 || bToastsHeld)
	{
		return;
	}

	// Quick fade in, slower fade out.
	const float FadeIn = FMath::Clamp(ToastElapsed / 0.15f, 0.f, 1.f);
	const float FadeOut = FMath::Clamp((ToastSeconds - ToastElapsed) / 0.35f, 0.f, 1.f);
	const float Alpha = FMath::Min(FadeIn, FadeOut);

	const float CentreX = Geometry.GetLocalSize().X * 0.5f;
	const float Top = CompassTop + CompassHeight + 44.f;
	FLinearColor HeadingColor = MarkerColor;
	HeadingColor.A = Alpha;
	FLinearColor TitleColor = FLinearColor::White;
	TitleColor.A = Alpha;

	const FSlateFontInfo HeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 24);
	const FText Heading = ToastQueue[0].Heading.ToUpper();
	DrawText(Out, LayerId, Geometry, Heading, HeadingFont, FVector2D(CentreX, Top), HeadingColor);
	DrawText(Out, LayerId, Geometry, ToastQueue[0].Title, TitleFont,
		FVector2D(CentreX, Top + MeasureText(Heading, HeadingFont).Y + 2.f), TitleColor);
}
