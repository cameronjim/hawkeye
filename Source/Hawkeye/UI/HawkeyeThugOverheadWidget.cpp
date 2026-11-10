#include "UI/HawkeyeThugOverheadWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/CombatReadability.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/CanvasPanel.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Player/HawkeyeCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Settings/HawkeyeAccessibility.h"
#include "Styling/CoreStyle.h"
#include "World/ThugCharacter.h"

TSharedRef<SWidget> UHawkeyeThugOverheadWidget::RebuildWidget()
{
	// Painted, not laid out: an empty canvas gives it a body that fills the HUD's overlay slot.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ThugOverheadRoot"));
	}
	return Super::RebuildWidget();
}

void UHawkeyeThugOverheadWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
	Super::NativeTick(MyGeometry, DeltaSeconds);
	GatherEntries();
}

void UHawkeyeThugOverheadWidget::GatherEntries()
{
	Entries.Reset();
	APlayerController* PC = GetOwningPlayer();
	UWorld* World = GetWorld();
	if (!PC || !PC->PlayerCameraManager || !World)
	{
		return;
	}
	const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
	const APawn* Viewer = PC->GetPawn();
	const AHawkeyeCharacter* Kate = Cast<AHawkeyeCharacter>(Viewer);
	for (TActorIterator<AThugCharacter> It(World); It; ++It)
	{
		const AThugCharacter* Thug = *It;
		if (Thug->IsLimp() || FVector::Dist(Thug->GetActorLocation(), Camera) > MaxGlyphDistance)
		{
			continue;
		}
		// The bar's range is from Kate, not the lens: it is about who she is fighting.
		const float FromViewer = FVector::Dist(Thug->GetActorLocation(), Viewer ? Viewer->GetActorLocation() : Camera);
		const TCHAR Glyph = Thug->GetAlertGlyph();
		const float BarAlpha = Thug->GetHealthBarAlpha(FromViewer);
		FHawkeyeThugOverhead Entry;
		const bool bCombat = GatherCombatReads(Thug, Kate, PC, Entry);
		if (!Glyph && BarAlpha <= 0.f && !bCombat)
		{
			continue;
		}
		// The whole stack sits on his head bone: the bar on it, the parry line and the glyph over the bar.
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Thug->GetGlyphLocation(), Entry.GlyphPosition, false))
		{
			continue;
		}
		Entry.Position = Entry.GlyphPosition;
		if (Entry.bTelegraph)
		{
			// The strike coming outranks the alert it came with.
			Entry.Glyph = TEXT("!");
		}
		else if (Glyph)
		{
			Entry.Glyph = FString::Chr(Glyph);
			// Full for most of its life, a quick fade in the last 0.15 s.
			Entry.GlyphAlpha = FMath::Clamp(Thug->GetAlertGlyphRemaining() / 0.15f, 0.f, 1.f);
		}
		const UHealthComponent* Health = Thug->GetHealthComponent();
		Entry.HealthFraction = Health ? Health->GetHealthPercent() : 1.f;
		Entry.BarAlpha = BarAlpha;
		Entries.Add(Entry);
	}
}

bool UHawkeyeThugOverheadWidget::GatherCombatReads(const AThugCharacter* Thug, const AHawkeyeCharacter* Kate, APlayerController* PC,
	FHawkeyeThugOverhead& Entry) const
{
	float Elapsed = 0.f;
	float Windup = 0.f;
	const FVector From = Kate ? Kate->GetActorLocation() : Thug->GetActorLocation();
	if (HawkeyeCombatReadability::GetStrikeTelegraph(Thug, From, GunnerTelegraphRange, Elapsed, Windup))
	{
		const FHawkeyeTelegraphLook Look = HawkeyeCombatReadability::ComputeTelegraphLook(Elapsed, Windup,
			UHawkeyeAccessibility::GetFlashScaleFor(this));
		Entry.bTelegraph = true;
		Entry.TelegraphScale = Look.Scale;
		Entry.GlyphAlpha = Look.Alpha;
		Entry.bParryCue = Kate && Kate->CanParryNow(Thug);
	}

	const FHawkeyeTargetMarker* Marker = Kate ? &Kate->GetMeleeTargetMarker() : nullptr;
	Entry.MarkerAlpha = Marker && Marker->GetTarget() == Thug ? Marker->GetAlpha() : 0.f;
	if (Entry.MarkerAlpha > 0.f)
	{
		// On the ground round his feet; a knocked-down body lies where the mesh went, not in the capsule.
		const UCapsuleComponent* Capsule = Thug->GetCapsuleComponent();
		const float Radius = (Capsule ? Capsule->GetScaledCapsuleRadius() : 34.f) + MarkerRingPadding;
		FVector Centre = Thug->GetActorLocation() - FVector(0.f, 0.f, Capsule ? Capsule->GetScaledCapsuleHalfHeight() - 3.f : 90.f);
		if (Thug->IsKnockedDown() && Thug->GetMesh())
		{
			Centre.X = Thug->GetMesh()->GetComponentLocation().X;
			Centre.Y = Thug->GetMesh()->GetComponentLocation().Y;
		}
		constexpr int32 Segments = 24;
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float Angle = 2.f * PI * Index / Segments;
			FVector2D Point;
			if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,
					Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius, Point, false))
			{
				Entry.MarkerRing.Reset();
				break;
			}
			Entry.MarkerRing.Add(Point);
		}
	}
	return Entry.bTelegraph || Entry.MarkerAlpha > 0.f;
}

FSlateFontInfo UHawkeyeThugOverheadWidget::GetGlyphFont() const
{
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), GlyphFontSize);
	Font.OutlineSettings.OutlineSize = GlyphOutlineSize;
	Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
	return Font;
}

void UHawkeyeThugOverheadWidget::SetAccentColors(const FLinearColor& Glyph, const FLinearColor& Bar, bool bDesign)
{
	if (!bDesignColorsKept)
	{
		DesignGlyphColor = GlyphColor;
		DesignBarColor = BarColor;
		bDesignColorsKept = true;
	}
	GlyphColor = bDesign ? DesignGlyphColor : Glyph;
	BarColor = bDesign ? DesignBarColor : Bar;
}

void UHawkeyeThugOverheadWidget::SetThreatColors(const FLinearColor& Danger, const FLinearColor& Marker, bool bDesign)
{
	if (!bDesignThreatColorsKept)
	{
		DesignTelegraphColor = TelegraphColor;
		DesignMarkerColor = MarkerColor;
		bDesignThreatColorsKept = true;
	}
	TelegraphColor = bDesign ? DesignTelegraphColor : Danger;
	MarkerColor = bDesign ? DesignMarkerColor : Marker;
}

FSlateFontInfo UHawkeyeThugOverheadWidget::GetTelegraphFont(float Scale) const
{
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FMath::Max(6, FMath::RoundToInt(TelegraphFontSize * Scale)));
	// A heavier outline than the alert glyph's: it reads by size and shape before colour.
	Font.OutlineSettings.OutlineSize = GlyphOutlineSize + 1;
	Font.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
	return Font;
}

float UHawkeyeThugOverheadWidget::PaintGlyph(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& AllottedGeometry,
	const FString& Glyph, const FSlateFontInfo& Font, const FLinearColor& Color, float X, float Bottom) const
{
	const bool bCanMeasure = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer();
	const FText Text = FText::FromString(Glyph);
	const FVector2D Size = bCanMeasure
		? FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font)
		: FVector2D(Font.Size * 0.5f, Font.Size);
	// Bottom is where the glyph's ink ends (its baseline, less its outline), not its line box: the box runs on
	// below the baseline by the font's descent, which would leave the "!" floating well over the stack under it.
	float Drop = Size.Y;
	if (bCanMeasure)
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		Drop = FMath::Clamp(static_cast<float>(Measure->GetMaxCharacterHeight(Font) + Measure->GetBaseline(Font)), Size.Y * 0.5f, Size.Y);
	}
	const FVector2D TopLeft(X - Size.X * 0.5f, Bottom - Drop - Font.OutlineSettings.OutlineSize);
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.7f * Color.A);
	FSlateDrawElement::MakeText(OutDrawElements, Layer,
		AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft + FVector2D(1.5f, 1.5f)))),
		Text, Font, ESlateDrawEffect::None, Shadow);
	FSlateDrawElement::MakeText(OutDrawElements, Layer + 1,
		AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))),
		Text, Font, ESlateDrawEffect::None, Color);
	return Size.Y;
}

int32 UHawkeyeThugOverheadWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
		bParentEnabled) + 1;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FSlateFontInfo Font = GetGlyphFont();
	auto Box = [&](int32 AtLayer, const FVector2D& TopLeft, const FVector2D& Size, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, AtLayer,
			AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))), White,
			ESlateDrawEffect::None, Color);
	};
	for (const FHawkeyeThugOverhead& Entry : Entries)
	{
		// The ring first, under everything: thin, her purple, on the ground round his feet.
		if (Entry.MarkerAlpha > 0.f && Entry.MarkerRing.Num() > 1)
		{
			TArray<FVector2f> Points;
			Points.Reserve(Entry.MarkerRing.Num());
			for (const FVector2D& Point : Entry.MarkerRing)
			{
				Points.Add(FVector2f(Point));
			}
			FLinearColor Ring = MarkerColor;
			Ring.A *= 0.9f * Entry.MarkerAlpha;
			// A dark edge under it first: her lavender alone is lost on white snow.
			if (MarkerRingEdge > 0.f)
			{
				TArray<FVector2f> Edge = Points;
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), MoveTemp(Edge),
					ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.5f * Entry.MarkerAlpha), true,
					MarkerRingThickness + 2.f * MarkerRingEdge);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points),
				ESlateDrawEffect::None, Ring, true, MarkerRingThickness);
		}

		// Bottom up, just over his head: the bar, then the parry line, then the glyph, StackGap apart.
		const bool bBar = Entry.BarAlpha > 0.f;
		if (bBar)
		{
			const FVector2D BarTopLeft(Entry.Position.X - BarSize.X * 0.5f, Entry.Position.Y - BarSize.Y);
			FLinearColor Back = BarBackColor;
			Back.A *= Entry.BarAlpha;
			// The marked thug's bar a little whiter, so the eye finds who she is hitting.
			FLinearColor Fill = FMath::Lerp(BarColor, FLinearColor::White, MarkerBarBrighten * Entry.MarkerAlpha);
			Fill.A = BarColor.A * Entry.BarAlpha;
			Box(Layer, BarTopLeft - FVector2D(1.f, 1.f), BarSize + FVector2D(2.f, 2.f), Back);
			Box(Layer + 1, BarTopLeft, FVector2D(BarSize.X * FMath::Clamp(Entry.HealthFraction, 0.f, 1.f), BarSize.Y), Fill);
		}
		if (Entry.Glyph.IsEmpty() || Entry.GlyphAlpha <= 0.f)
		{
			continue;
		}
		// The glyph sits just over his head, not over his capsule; stacked on the bar when that shows (its dark
		// edge is 1 px over the fill).
		const float X = Entry.GlyphPosition.X;
		float Bottom = HawkeyeCombatReadability::ComputeGlyphBottom(Entry.GlyphPosition.Y, bBar, BarSize.Y + 1.f, StackGap);
		if (!Entry.bTelegraph)
		{
			FLinearColor Color = GlyphColor;
			Color.A *= Entry.GlyphAlpha;
			PaintGlyph(OutDrawElements, Layer, AllottedGeometry, Entry.Glyph, Font, Color, X, Bottom);
			continue;
		}
		// The parry line: short, cream on a dark edge, only while a tap would parry him.
		if (Entry.bParryCue)
		{
			const FVector2D CueTopLeft(X - ParryCueSize.X * 0.5f, Bottom - ParryCueSize.Y - 1.f);
			Box(Layer, CueTopLeft - FVector2D(1.f, 1.f), ParryCueSize + FVector2D(2.f, 2.f), FLinearColor(0.f, 0.f, 0.f, 0.8f));
			Box(Layer + 1, CueTopLeft, ParryCueSize, GlyphColor);
			Bottom -= ParryCueSize.Y + 2.f + StackGap;
		}
		FLinearColor Color = TelegraphColor;
		Color.A *= Entry.GlyphAlpha;
		PaintGlyph(OutDrawElements, Layer, AllottedGeometry, Entry.Glyph, GetTelegraphFont(Entry.TelegraphScale), Color, X, Bottom);
	}
	return Layer + 2;
}
