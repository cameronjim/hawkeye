#include "UI/HawkeyeMapWidget.h"

#include "Challenge/ChallengeDefinition.h"
#include "Challenge/ChallengeRules.h"
#include "Challenge/ChallengeStart.h"
#include "Challenge/ChallengeSubsystem.h"
#include "CompGeom/PolygonTriangulation.h"
#include "Crime/CrimeDefinition.h"
#include "Crime/CrimeSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "Hawkeye.h"
#include "HawkeyePlayerController.h"
#include "InputCoreTypes.h"
#include "Mission/MissionObjective.h"
#include "Mission/MissionSubsystem.h"
#include "Partner/HawkeyePartnerController.h"
#include "Player/HawkeyeCharacter.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Settings/HawkeyeSettingsSubsystem.h"
#include "Styling/CoreStyle.h"
#include "UI/HawkeyeHudGlyphs.h"
#include "UI/ObjectiveMarkerMath.h"
#include "World/CityMapData.h"
#include "World/InteriorEntrance.h"
#include "World/Safehouse.h"
#include "World/SafehouseSubsystem.h"

namespace HawkeyeMapStyle
{
	static constexpr float StickDeadZone = 0.2f;
	static const FLinearColor TextColor(0.85f, 0.85f, 0.88f, 1.f);
	static const FLinearColor Dim(0.55f, 0.55f, 0.6f, 1.f);
	static const FLinearColor Panel(0.f, 0.f, 0.f, 0.75f);

	static FSlateFontInfo Font(int32 Size, const float Scale, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), FMath::RoundToInt(Size * Scale));
	}

	/** Text with its top left at TopLeft (the glyph helper centres; the legend and tooltip read left to right). */
	static void DrawTextLeft(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FText& Text,
		const FSlateFontInfo& FontInfo, const FVector2D& TopLeft, const FLinearColor& Color)
	{
		const FVector2D Size = HawkeyeHudGlyphs::MeasureText(Text, FontInfo);
		HawkeyeHudGlyphs::DrawText(Out, LayerId, Geometry, Text, FontInfo, TopLeft + FVector2D(Size.X * 0.5f, 0.f), Color);
	}

	static void DrawBox(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FVector2D& TopLeft,
		const FVector2D& Size, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Out, LayerId, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))),
			FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Color);
	}

	static float DeadZoned(float Value)
	{
		return FMath::Abs(Value) < StickDeadZone ? 0.f : FMath::Sign(Value) * (FMath::Abs(Value) - StickDeadZone) / (1.f - StickDeadZone);
	}
}

UHawkeyeMapWidget::UHawkeyeMapWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MapDataAsset = TSoftObjectPtr<UCityMapData>(FSoftObjectPath(UCityMapData::DefaultPath));
}

void UHawkeyeMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
}

void UHawkeyeMapWidget::Open()
{
	bOpen = true;
	SetVisibility(ESlateVisibility::Visible);
	EnsureMapData();
	ReadSettings();
	View.Reset();
	LeftStick = RightStick = FVector2D::ZeroVector;
	bDragging = bDragMoved = false;
	StatusSeconds = 0.f;
	RefreshIcons();
	// The cursor starts on the player once the widget knows its size (the first tick).
	bPendingCentre = true;
	SetKeyboardFocus();
	UE_LOG(LogHawkeye, Log, TEXT("%s: map open, %d icons, %d footprints."), *GetName(), Icons.Num(),
		MapData ? MapData->Footprints.Num() : 0);
}

void UHawkeyeMapWidget::Close()
{
	bOpen = false;
	bDragging = false;
	RemoveFromParent();
}

void UHawkeyeMapWidget::RequestClose()
{
	if (AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer()))
	{
		PC->SetMapOpen(false);
		return;
	}
	Close();
}

void UHawkeyeMapWidget::EnsureMapData()
{
	if (MapData)
	{
		return;
	}
	MapData = MapDataAsset.LoadSynchronous();
	if (!MapData || !MapData->IsUsable())
	{
		UE_LOG(LogHawkeye, Warning, TEXT("%s: no usable map data at %s; run Tools\\generate-city.ps1."), *GetName(),
			*MapDataAsset.ToString());
		return;
	}
	// Triangulated once: the outlines are simple polygons (courtyards were never cut into the footprints).
	auto Triangulate = [](const TArray<FCityMapPolygon>& Polygons, TArray<FVector2D>& Out)
	{
		for (const FCityMapPolygon& Polygon : Polygons)
		{
			if (Polygon.Points.Num() < 3)
			{
				continue;
			}
			TArray<UE::Geometry::FIndex3i> Triangles;
			PolygonTriangulation::TriangulateSimplePolygon<double>(Polygon.Points, Triangles, false);
			for (const UE::Geometry::FIndex3i& Triangle : Triangles)
			{
				Out.Add(Polygon.Points[Triangle.A]);
				Out.Add(Polygon.Points[Triangle.B]);
				Out.Add(Polygon.Points[Triangle.C]);
			}
		}
	};
	FootprintTriangles.Reset();
	ParkTriangles.Reset();
	Triangulate(MapData->Footprints, FootprintTriangles);
	Triangulate(MapData->Parks, ParkTriangles);
}

void UHawkeyeMapWidget::ReadSettings()
{
	const FHawkeyeSettings Settings = UHawkeyeSettingsSubsystem::GetCurrentSettings(this);
	HudScale = UHawkeyeSettingsSubsystem::ClampHudScale(Settings.HudScale);
	Palette = UHawkeyeAccessibility::GetPalette(Settings.ColorPalette);
}

bool UHawkeyeMapWidget::IsUsingGamepad() const
{
	const AHawkeyePlayerController* PC = Cast<AHawkeyePlayerController>(GetOwningPlayer());
	return PC && PC->IsUsingGamepad();
}

void UHawkeyeMapWidget::UpdateFrame(const FVector2D& Size)
{
	LastSize = Size;
	const FVector2D TopLeft(Gutter, Gutter);
	const FVector2D Area(Size.X - 2.f * Gutter, Size.Y - 2.f * Gutter - LegendHeight * HudScale);
	if (MapData && MapData->IsUsable())
	{
		View.SetFrame(MapData->BoundsMin, MapData->BoundsMax, TopLeft, Area);
	}
	else
	{
		View.SetFrame(FVector2D(-50000.0), FVector2D(50000.0), TopLeft, Area);
	}
}

void UHawkeyeMapWidget::RefreshIcons()
{
	State = GatherState(this, GetOwningPlayer());
	Icons = FHawkeyeMapIcons::Build(State);
}

bool UHawkeyeMapWidget::MoveCursorToIcon(EHawkeyeMapIconKind Kind, int32 Nth)
{
	for (const FHawkeyeMapIcon& Icon : Icons)
	{
		if (Icon.Kind == Kind && Nth-- == 0)
		{
			Cursor = View.WorldToMap(FVector2D(Icon.Location));
			bPendingCentre = false;
			HoveredIndex = FHawkeyeMapIcons::FindAt(Icons, View, Cursor, HoverRadius * HudScale);
			return true;
		}
	}
	return false;
}

void UHawkeyeMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bOpen)
	{
		return;
	}
	UpdateFrame(MyGeometry.GetLocalSize());
	RefreshIcons();
	if (bPendingCentre)
	{
		bPendingCentre = false;
		Cursor = State.bHasPlayer ? View.WorldToMap(FVector2D(State.PlayerLocation)) : View.AreaTopLeft + View.AreaSize * 0.5;
	}
	ApplySticks(InDeltaTime);
	StatusSeconds = FMath::Max(0.f, StatusSeconds - InDeltaTime);
	HoveredIndex = FHawkeyeMapIcons::FindAt(Icons, View, Cursor, HoverRadius * HudScale);
}

void UHawkeyeMapWidget::ApplySticks(float DeltaSeconds)
{
	using namespace HawkeyeMapStyle;
	const FVector2D Move(DeadZoned(LeftStick.X), -DeadZoned(LeftStick.Y));
	if (!Move.IsNearlyZero())
	{
		Cursor += Move * CursorSpeed * HudScale * DeltaSeconds;
		// Past the edge of the map area the cursor pushes the map instead.
		const FVector2D Low = View.AreaTopLeft;
		const FVector2D High = View.AreaTopLeft + View.AreaSize;
		const FVector2D Clamped(FMath::Clamp(Cursor.X, Low.X, High.X), FMath::Clamp(Cursor.Y, Low.Y, High.Y));
		View.PanPixels(Clamped - Cursor);
		Cursor = Clamped;
	}
	const float Zoom = DeadZoned(RightStick.Y);
	if (Zoom != 0.f)
	{
		View.ZoomAbout(Cursor, View.Zoom * FMath::Pow(StickZoomRate, Zoom * DeltaSeconds));
	}
}

bool UHawkeyeMapWidget::MarkHovered()
{
	if (!Icons.IsValidIndex(HoveredIndex) || !Icons[HoveredIndex].IsMarkable())
	{
		return false;
	}
	const FHawkeyeMapIcon Icon = Icons[HoveredIndex];
	if (!MarkIcon(this, Icon))
	{
		return false;
	}
	const bool bSafehouse = Icon.Kind == EHawkeyeMapIconKind::Safehouse;
	StatusText = Icon.bMarked ? NSLOCTEXT("Hawkeye", "MapUnmarked", "[Marker cleared]")
		: bSafehouse ? NSLOCTEXT("Hawkeye", "SafehouseMarked", "[Safehouse marked]")
		: NSLOCTEXT("Hawkeye", "ChallengeMarked", "[Challenge marked]");
	StatusSeconds = 2.f;
	RefreshIcons();
	return true;
}

FHawkeyeMapState UHawkeyeMapWidget::GatherState(const UObject* WorldContextObject, const APlayerController* PC)
{
	FHawkeyeMapState Out;
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return Out;
	}
	auto NameOf = [](const APawn* Pawn)
	{
		const AHawkeyeCharacter* Hawkeye = Cast<AHawkeyeCharacter>(Pawn);
		return Hawkeye ? Hawkeye->GetCharacterName() : FText::FromString(GetNameSafe(Pawn));
	};
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		Out.bHasPlayer = true;
		Out.PlayerLocation = Pawn->GetActorLocation();
		Out.PlayerYaw = Pawn->GetActorRotation().Yaw;
		Out.PlayerName = NameOf(Pawn);
	}
	const AHawkeyePlayerController* HawkeyePC = Cast<AHawkeyePlayerController>(PC);
	const AHawkeyePartnerController* Partner = HawkeyePC ? HawkeyePC->FindPartnerController() : nullptr;
	if (const APawn* PartnerPawn = Partner ? Partner->GetPawn() : nullptr)
	{
		Out.bHasPartner = true;
		Out.PartnerLocation = PartnerPawn->GetActorLocation();
		Out.PartnerYaw = PartnerPawn->GetActorRotation().Yaw;
		Out.PartnerName = NameOf(PartnerPawn);
	}
	if (const UMissionSubsystem* Missions = UMissionSubsystem::Get(WorldContextObject))
	{
		Out.bHasObjective = Missions->GetCurrentObjectiveLocation(Out.ObjectiveLocation);
		const UMissionObjective* Objective = Missions->GetCurrentObjective();
		Out.ObjectiveName = Objective && !Objective->Title.IsEmpty() ? Objective->Title : NSLOCTEXT("Hawkeye", "MapObjective", "[Objective]");
		Out.SecondaryMarkers = Missions->GetSecondaryMarkers();
	}
	if (const USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(WorldContextObject))
	{
		for (const ASafehouse* Safehouse : Safehouses->GetSafehouses())
		{
			FHawkeyeMapPlace& Place = Out.Safehouses.AddDefaulted_GetRef();
			Place.Id = Safehouse->SafehouseId;
			Place.Location = Safehouse->GetActorLocation();
			Place.bDiscovered = Safehouses->IsDiscoveredNow(Safehouse);
			Place.Name = USafehouseSubsystem::GetMarkerName(Safehouse, Place.bDiscovered);
			Place.bMarked = Safehouse == Safehouses->GetMarkedSafehouse();
		}
	}
	if (const UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(WorldContextObject))
	{
		for (const AChallengeStart* Start : Challenges->GetStarts())
		{
			FHawkeyeMapPlace& Place = Out.Challenges.AddDefaulted_GetRef();
			Place.Id = Start->Definition->Id;
			Place.Name = Start->Definition->GetDisplayName();
			Place.Location = Start->GetActorLocation();
			Place.bDiscovered = true;
			Place.Medal = Challenges->GetRecord(Place.Id).BestMedal;
			Place.bMarked = Start == Challenges->GetMarkedStart();
		}
	}
	if (const UCrimeSubsystem* Crimes = UCrimeSubsystem::Get(WorldContextObject); Crimes && Crimes->IsCrimeActive())
	{
		Out.bCrimeActive = true;
		Out.CrimeLocation = Crimes->GetCrimePoint();
		const UCrimeDefinition* Definition = Crimes->GetActiveDefinition();
		Out.CrimeName = Definition ? Definition->GetDisplayName() : NSLOCTEXT("Hawkeye", "MapCrime", "[Crime]");
	}
	for (TActorIterator<AInteriorEntrance> It(const_cast<UWorld*>(World)); It; ++It)
	{
		FHawkeyeMapPlace& Door = Out.Doors.AddDefaulted_GetRef();
		Door.Location = It->GetActorLocation();
		Door.Name = It->DisplayName.IsEmpty() ? NSLOCTEXT("Hawkeye", "MapDoor", "[Door]") : It->DisplayName;
	}
	return Out;
}

bool UHawkeyeMapWidget::MarkIcon(const UObject* WorldContextObject, const FHawkeyeMapIcon& Icon)
{
	USafehouseSubsystem* Safehouses = USafehouseSubsystem::Get(WorldContextObject);
	UChallengeSubsystem* Challenges = UChallengeSubsystem::Get(WorldContextObject);
	if (!Safehouses || !Challenges)
	{
		return false;
	}
	if (Icon.Kind == EHawkeyeMapIconKind::Safehouse)
	{
		ASafehouse* Safehouse = Safehouses->FindSafehouse(Icon.Id);
		if (!Safehouse)
		{
			return false;
		}
		if (Safehouses->GetMarkedSafehouse() == Safehouse)
		{
			Safehouses->ClearSafehouseMarker();
			return true;
		}
		Challenges->ClearChallengeMarker();
		return Safehouses->MarkSafehouse(Safehouse);
	}
	if (Icon.Kind == EHawkeyeMapIconKind::Challenge)
	{
		for (AChallengeStart* Start : Challenges->GetStarts())
		{
			if (Start->Definition->Id != Icon.Id)
			{
				continue;
			}
			if (Challenges->GetMarkedStart() == Start)
			{
				Challenges->ClearChallengeMarker();
				return true;
			}
			Safehouses->ClearSafehouseMarker();
			return Challenges->MarkChallenge(Start);
		}
	}
	return false;
}

FText UHawkeyeMapWidget::GetDistanceText(const FHawkeyeMapIcon& Icon) const
{
	return State.bHasPlayer ? UObjectiveMarkerMath::FormatDistance(FVector::Dist(State.PlayerLocation, Icon.Location)) : FText::GetEmpty();
}

FText UHawkeyeMapWidget::GetHoverText(const FHawkeyeMapIcon& Icon) const
{
	FString Line = Icon.Name.ToString();
	if (Icon.Kind == EHawkeyeMapIconKind::Challenge)
	{
		Line += TEXT("  ") + UChallengeRules::MedalText(Icon.Medal).ToString();
	}
	if (Icon.Kind != EHawkeyeMapIconKind::Player)
	{
		Line += TEXT("  ") + GetDistanceText(Icon).ToString();
	}
	if (Icon.IsMarkable())
	{
		Line += IsUsingGamepad() ? TEXT("\n[A] ") : TEXT("\n[Enter] ");
		Line += Icon.bMarked ? TEXT("[Clear marker]") : TEXT("[Mark]");
	}
	return FText::FromString(Line);
}

// --- Input ---------------------------------------------------------------------------------------------

FReply UHawkeyeMapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bOpen)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::M || Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right
		|| Key == EKeys::Gamepad_DPad_Up)
	{
		RequestClose();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		MarkHovered();
		return FReply::Handled();
	}
	// Everything else is swallowed: the game is paused under the map.
	return FReply::Handled();
}

FReply UHawkeyeMapWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	const FKey Key = InAnalogEvent.GetKey();
	const float Value = InAnalogEvent.GetAnalogValue();
	if (Key == EKeys::Gamepad_LeftX)
	{
		LeftStick.X = Value;
	}
	else if (Key == EKeys::Gamepad_LeftY)
	{
		LeftStick.Y = Value;
	}
	else if (Key == EKeys::Gamepad_RightX)
	{
		RightStick.X = Value;
	}
	else if (Key == EKeys::Gamepad_RightY)
	{
		RightStick.Y = Value;
	}
	else
	{
		return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
	}
	// Handled, so Slate's navigation never turns the stick into focus moves.
	return FReply::Handled();
}

FReply UHawkeyeMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}
	bDragging = true;
	bDragMoved = false;
	DragLast = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	Cursor = DragLast;
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UHawkeyeMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bDragging)
	{
		return FReply::Handled();
	}
	bDragging = false;
	if (!bDragMoved)
	{
		// A click, not a drag: mark what is under it.
		HoveredIndex = FHawkeyeMapIcons::FindAt(Icons, View, Cursor, HoverRadius * HudScale);
		MarkHovered();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UHawkeyeMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Slate synthesises a move every frame the widget under a still mouse changes; only a real one
	// takes the cursor from the stick (or from where the map put it on opening).
	if (!bDragging && InMouseEvent.GetCursorDelta().IsNearlyZero())
	{
		return FReply::Handled();
	}
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	if (bDragging)
	{
		const FVector2D Delta = Local - DragLast;
		if (bDragMoved || Delta.Size() > 3.f)
		{
			bDragMoved = true;
			View.PanPixels(Delta);
			DragLast = Local;
		}
	}
	Cursor = Local;
	bPendingCentre = false;
	return FReply::Handled();
}

FReply UHawkeyeMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Cursor = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	View.ZoomAbout(Cursor, View.Zoom * FMath::Pow(WheelZoomStep, InMouseEvent.GetWheelDelta()));
	return FReply::Handled();
}

// --- Paint ---------------------------------------------------------------------------------------------

int32 UHawkeyeMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Base = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen)
	{
		return Base;
	}
	HawkeyeMapStyle::DrawBox(OutDrawElements, Base + 1, AllottedGeometry, FVector2D::ZeroVector, AllottedGeometry.GetLocalSize(),
		BackgroundColor);
	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.ToPaintGeometry(FVector2f(View.AreaSize),
		FSlateLayoutTransform(FVector2f(View.AreaTopLeft)))));
	PaintGround(AllottedGeometry, OutDrawElements, Base + 2);
	PaintIcons(AllottedGeometry, OutDrawElements, Base + 10);
	OutDrawElements.PopClip();
	PaintCursor(AllottedGeometry, OutDrawElements, Base + 30);
	PaintLegend(AllottedGeometry, OutDrawElements, Base + 40);
	return Base + 50;
}

void UHawkeyeMapWidget::PaintTriangles(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId,
	const TArray<FVector2D>& Triangles, const FLinearColor& Color) const
{
	if (Triangles.IsEmpty() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return;
	}
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(
		*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
	const FColor Packed = Color.ToFColor(true);
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	Vertices.Reserve(Triangles.Num());
	Indices.Reserve(Triangles.Num());
	for (const FVector2D& World : Triangles)
	{
		Indices.Add(Vertices.Num());
		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(View.WorldToMap(World)),
			FVector2f(0.5f, 0.5f), Packed));
	}
	FSlateDrawElement::MakeCustomVerts(Out, LayerId, Handle, Vertices, Indices, nullptr, 0, 0);
}

void UHawkeyeMapWidget::PaintGround(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	PaintTriangles(Geometry, Out, LayerId, ParkTriangles, ParkColor);
	if (MapData)
	{
		const double Scale = View.GetScale();
		for (const FCityMapStreet& Street : MapData->Streets)
		{
			TArray<FVector2D> Points;
			Points.Reserve(Street.Points.Num());
			for (const FVector2D& World : Street.Points)
			{
				Points.Add(View.WorldToMap(World));
			}
			// The carriageway's real width, so the blocks sit between them as they do on the street.
			const float Thickness = FMath::Max(2.f, static_cast<float>(Street.WidthM * 100.0 * Scale));
			FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				StreetColor, true, Thickness);
		}
	}
	PaintTriangles(Geometry, Out, LayerId + 2, FootprintTriangles, FootprintColor);
}

void UHawkeyeMapWidget::PaintArrow(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FVector2D& At,
	float AngleRadians, float Length, const FLinearColor& Color) const
{
	const FVector2D Forward(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians));
	const FVector2D Side(-Forward.Y, Forward.X);
	const FVector2D Tip = At + Forward * Length * 0.6f;
	const FVector2D Left = At - Forward * Length * 0.4f + Side * Length * 0.4f;
	const FVector2D Notch = At - Forward * Length * 0.15f;
	const FVector2D Right = At - Forward * Length * 0.4f - Side * Length * 0.4f;
	// Two triangles make the notched arrowhead; the map's inverse view turns them back into world points.
	TArray<FVector2D> Triangles;
	for (const FVector2D& Point : { Tip, Left, Notch, Tip, Notch, Right })
	{
		Triangles.Add(View.MapToWorld(Point));
	}
	PaintTriangles(Geometry, Out, LayerId + 1, Triangles, Color);
	HawkeyeHudGlyphs::DrawShadowedLines(Out, LayerId + 2, Geometry, { Tip, Left, Notch, Right, Tip },
		FLinearColor(0.f, 0.f, 0.f, 0.8f), 1.5f);
}

void UHawkeyeMapWidget::PaintIcon(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId, const FHawkeyeMapIcon& Icon,
	const FVector2D& At, float Scale) const
{
	using namespace HawkeyeHudGlyphs;
	switch (Icon.Kind)
	{
	case EHawkeyeMapIconKind::Player:
		PaintArrow(Geometry, Out, LayerId, At, FHawkeyeMapView::YawToMapAngle(Icon.Yaw), 22.f * Scale, Palette.Purple);
		break;
	case EHawkeyeMapIconKind::Partner:
		PaintArrow(Geometry, Out, LayerId, At, FHawkeyeMapView::YawToMapAngle(Icon.Yaw), 14.f * Scale, PartnerColor);
		break;
	case EHawkeyeMapIconKind::Objective:
		DrawDiamond(Out, LayerId, Geometry, At, 9.f * Scale, 2.f, Palette.Cream);
		break;
	case EHawkeyeMapIconKind::Safehouse:
	{
		FLinearColor Color = Palette.Cream;
		Color.A = Icon.bDiscovered ? 1.f : 0.85f;
		DrawHouse(Out, LayerId, Geometry, At, 1.3f * Scale, Color, Icon.bDiscovered, /*bQuestion=*/false);
		break;
	}
	case EHawkeyeMapIconKind::Challenge:
		DrawMedal(Out, LayerId, Geometry, At, 8.f * Scale, FHawkeyeMapIcons::GetMedalColor(Icon.Medal, Palette.Purple));
		break;
	case EHawkeyeMapIconKind::Crime:
		DrawDiamond(Out, LayerId, Geometry, At, 9.f * Scale, 2.5f, Palette.Danger);
		DrawText(Out, LayerId + 2, Geometry, FText::FromString(TEXT("!")), HawkeyeMapStyle::Font(10, Scale, true),
			At - FVector2D(0.f, 7.f * Scale), Palette.Danger);
		break;
	case EHawkeyeMapIconKind::InteriorDoor:
	{
		const FVector2D Half(4.f * Scale, 6.f * Scale);
		DrawShadowedLines(Out, LayerId, Geometry, { At + FVector2D(-Half.X, Half.Y), At + FVector2D(-Half.X, -Half.Y),
			At + FVector2D(Half.X, -Half.Y), At + FVector2D(Half.X, Half.Y), At + FVector2D(-Half.X, Half.Y) }, DoorColor, 2.f);
		HawkeyeMapStyle::DrawBox(Out, LayerId + 2, Geometry, At + FVector2D(1.5f, 0.f) * Scale, FVector2D(1.5f * Scale), DoorColor);
		break;
	}
	default:
		DrawDiamond(Out, LayerId, Geometry, At, 5.f * Scale, 2.f, Palette.Purple);
		break;
	}
}

void UHawkeyeMapWidget::PaintIcons(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeHudGlyphs;
	for (int32 Index = 0; Index < Icons.Num(); ++Index)
	{
		const FHawkeyeMapIcon& Icon = Icons[Index];
		const FVector2D At = View.WorldToMap(FVector2D(Icon.Location));
		if (Icon.bMarked)
		{
			DrawShadowedLines(Out, LayerId, Geometry, ArcPoints(At, 15.f * HudScale, 1.f, 24), Palette.Cream, 2.f);
		}
		if (Index == HoveredIndex)
		{
			DrawShadowedLines(Out, LayerId, Geometry, ArcPoints(At, 12.f * HudScale, 1.f, 24), FLinearColor(1.f, 1.f, 1.f, 0.7f), 1.5f);
		}
		PaintIcon(Geometry, Out, LayerId + 1, Icon, At, HudScale);
	}
}

void UHawkeyeMapWidget::PaintCursor(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeHudGlyphs;
	const float S = HudScale;
	const FLinearColor Color(1.f, 1.f, 1.f, 0.85f);
	DrawShadowedLines(Out, LayerId, Geometry, ArcPoints(Cursor, 7.f * S, 1.f, 20), Color, 1.5f);
	for (const FVector2D& Direction : { FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f), FVector2D(0.f, 1.f), FVector2D(0.f, -1.f) })
	{
		DrawShadowedLines(Out, LayerId, Geometry, { Cursor + Direction * 9.f * S, Cursor + Direction * 14.f * S }, Color, 1.5f);
	}
	if (!Icons.IsValidIndex(HoveredIndex))
	{
		return;
	}
	// The label: name, medal and distance, and how to mark it, in a dark panel beside the cursor.
	const FSlateFontInfo Font = HawkeyeMapStyle::Font(13, S);
	const FText Label = GetHoverText(Icons[HoveredIndex]);
	const FVector2D Size = MeasureText(Label, Font) + FVector2D(16.f, 10.f) * S;
	FVector2D TopLeft = Cursor + FVector2D(18.f, 12.f) * S;
	TopLeft.X = FMath::Min(TopLeft.X, LastSize.X - Gutter - Size.X);
	TopLeft.Y = FMath::Min(TopLeft.Y, LastSize.Y - Gutter - LegendHeight * S - Size.Y);
	HawkeyeMapStyle::DrawBox(Out, LayerId + 2, Geometry, TopLeft, Size, HawkeyeMapStyle::Panel);
	FSlateDrawElement::MakeText(Out, LayerId + 3, Geometry.ToPaintGeometry(FVector2f(Size),
		FSlateLayoutTransform(FVector2f(TopLeft + FVector2D(8.f, 5.f) * S))), Label, Font, ESlateDrawEffect::None, HawkeyeMapStyle::TextColor);
}

void UHawkeyeMapWidget::PaintLegend(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 LayerId) const
{
	using namespace HawkeyeMapStyle;
	const float S = HudScale;
	const FSlateFontInfo LabelFont = Font(13, S);
	const float Top = LastSize.Y - Gutter - LegendHeight * S;
	const float Mid = Top + LegendHeight * S * 0.5f;
	DrawBox(Out, LayerId, Geometry, FVector2D(Gutter, Top), FVector2D(LastSize.X - 2.f * Gutter, LegendHeight * S), Panel);

	// The title and zoom sit over the map's top left corner.
	DrawTextLeft(Out, LayerId + 1, Geometry, NSLOCTEXT("Hawkeye", "MapTitle", "[East Village]"), Font(18, S, true),
		FVector2D(Gutter + 12.f, Gutter + 8.f), TextColor);
	DrawTextLeft(Out, LayerId + 1, Geometry, FText::FromString(FString::Printf(TEXT("%.1fx"), View.Zoom)), Font(12, S),
		FVector2D(Gutter + 12.f, Gutter + 8.f + 26.f * S), Dim);
	if (StatusSeconds > 0.f)
	{
		DrawTextLeft(Out, LayerId + 1, Geometry, StatusText, Font(14, S, true), FVector2D(Gutter + 12.f, Gutter + 8.f + 44.f * S),
			Palette.Cream);
	}

	struct FEntry
	{
		EHawkeyeMapIconKind Kind;
		FText Label;
	};
	const FEntry Entries[] = {
		{ EHawkeyeMapIconKind::Player, State.bHasPlayer ? State.PlayerName : NSLOCTEXT("Hawkeye", "MapYou", "[You]") },
		{ EHawkeyeMapIconKind::Partner, State.bHasPartner ? State.PartnerName : NSLOCTEXT("Hawkeye", "MapPartner", "[Partner]") },
		{ EHawkeyeMapIconKind::Objective, NSLOCTEXT("Hawkeye", "MapLegendObjective", "[Objective]") },
		{ EHawkeyeMapIconKind::Safehouse, NSLOCTEXT("Hawkeye", "MapLegendSafehouse", "[Safehouse]") },
		{ EHawkeyeMapIconKind::Challenge, NSLOCTEXT("Hawkeye", "MapLegendChallenge", "[Challenge]") },
		{ EHawkeyeMapIconKind::Crime, NSLOCTEXT("Hawkeye", "MapLegendCrime", "[Crime]") },
		{ EHawkeyeMapIconKind::InteriorDoor, NSLOCTEXT("Hawkeye", "MapLegendDoor", "[Door]") },
		{ EHawkeyeMapIconKind::Secondary, NSLOCTEXT("Hawkeye", "MapLegendMarker", "[Marker]") },
	};
	float X = Gutter + 20.f * S;
	for (const FEntry& Entry : Entries)
	{
		FHawkeyeMapIcon Sample;
		Sample.Kind = Entry.Kind;
		Sample.Yaw = -90.f;
		Sample.bDiscovered = true;
		PaintIcon(Geometry, Out, LayerId + 1, Sample, FVector2D(X, Mid), S * 0.85f);
		const FVector2D Size = HawkeyeHudGlyphs::MeasureText(Entry.Label, LabelFont);
		DrawTextLeft(Out, LayerId + 1, Geometry, Entry.Label, LabelFont, FVector2D(X + 14.f * S, Mid - Size.Y * 0.5f), TextColor);
		X += 14.f * S + Size.X + 22.f * S;
	}

	const FText Keys = IsUsingGamepad()
		? NSLOCTEXT("Hawkeye", "MapKeysGamepad", "[LS] Cursor   [A] Mark   [RS] Zoom   [B] Close")
		: NSLOCTEXT("Hawkeye", "MapKeys", "[Enter] Mark   [Wheel] Zoom   [Drag] Pan   [M] Close");
	const FVector2D KeysSize = HawkeyeHudGlyphs::MeasureText(Keys, LabelFont);
	DrawTextLeft(Out, LayerId + 1, Geometry, Keys, LabelFont,
		FVector2D(LastSize.X - Gutter - 16.f * S - KeysSize.X, Mid - KeysSize.Y * 0.5f), Dim);
}
