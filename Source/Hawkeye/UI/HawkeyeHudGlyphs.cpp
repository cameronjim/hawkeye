#include "UI/HawkeyeHudGlyphs.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace HawkeyeHudGlyphs
{
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.6f);

	FVector2D MeasureText(const FText& Text, const FSlateFontInfo& Font)
	{
		if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
		{
			return FVector2D::ZeroVector;
		}
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
	}

	/** Text centred horizontally on CentreTop, with a one-pixel drop shadow so it reads on sky and snow. */
	void DrawText(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FText& Text,
		const FSlateFontInfo& Font, const FVector2D& CentreTop, const FLinearColor& Color)
	{
		const FVector2D Size = MeasureText(Text, Font);
		const FVector2D TopLeft(CentreTop.X - Size.X * 0.5f, CentreTop.Y);
		FLinearColor ShadowColor = Shadow;
		ShadowColor.A *= Color.A;
		FSlateDrawElement::MakeText(Out, LayerId,
			Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft + FVector2D(1.f, 1.f)))),
			Text, Font, ESlateDrawEffect::None, ShadowColor);
		FSlateDrawElement::MakeText(Out, LayerId + 1,
			Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))),
			Text, Font, ESlateDrawEffect::None, Color);
	}

	/** A hollow diamond HalfSize from the centre to each point, shadowed like the text. */
	void DrawDiamond(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry,
		const FVector2D& Centre, float HalfSize, float Thickness, const FLinearColor& Color)
	{
		auto Points = [HalfSize](const FVector2D& C)
		{
			return TArray<FVector2D>{ C + FVector2D(0.f, -HalfSize), C + FVector2D(HalfSize, 0.f),
				C + FVector2D(0.f, HalfSize), C + FVector2D(-HalfSize, 0.f), C + FVector2D(0.f, -HalfSize) };
		};
		FLinearColor ShadowColor = Shadow;
		ShadowColor.A *= Color.A;
		FSlateDrawElement::MakeLines(Out, LayerId, Geometry.ToPaintGeometry(), Points(Centre + FVector2D(1.f, 1.f)),
			ESlateDrawEffect::None, ShadowColor, true, Thickness);
		FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(), Points(Centre),
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	/** Points round a circle of Radius about Centre, from straight up clockwise, Fraction of the way. */
	TArray<FVector2D> ArcPoints(const FVector2D& Centre, float Radius, float Fraction, int32 Segments)
	{
		TArray<FVector2D> Points;
		const int32 Count = FMath::Max(1, FMath::CeilToInt(Segments * FMath::Clamp(Fraction, 0.f, 1.f)));
		for (int32 Index = 0; Index <= Count; ++Index)
		{
			const float Angle = 2.f * PI * Fraction * Index / Count - 0.5f * PI;
			Points.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}

	/** Lines with the one-pixel drop shadow everything else has. */
	void DrawShadowedLines(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry,
		const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness)
	{
		FLinearColor ShadowColor = Shadow;
		ShadowColor.A *= Color.A;
		TArray<FVector2D> ShadowPoints;
		for (const FVector2D& Point : Points)
		{
			ShadowPoints.Add(Point + FVector2D(1.f, 1.f));
		}
		FSlateDrawElement::MakeLines(Out, LayerId, Geometry.ToPaintGeometry(), ShadowPoints, ESlateDrawEffect::None, ShadowColor, true, Thickness);
		FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	}

	/**
	 * A house Scale times 12 px wide: a body under a pitched roof. Filled for a safehouse that has been
	 * found, only outlined before; bQuestion puts a "?" in it (a marked one not found yet).
	 */
	void DrawHouse(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FVector2D& C,
		float Scale, const FLinearColor& Color, bool bFilled, bool bQuestion)
	{
		const float S = Scale;
		const TArray<FVector2D> House{ C + FVector2D(-5.f, 6.f) * S, C + FVector2D(-5.f, -1.f) * S, C + FVector2D(-7.f, -1.f) * S,
			C + FVector2D(0.f, -7.f) * S, C + FVector2D(7.f, -1.f) * S, C + FVector2D(5.f, -1.f) * S, C + FVector2D(5.f, 6.f) * S,
			C + FVector2D(-5.f, 6.f) * S };
		if (bFilled)
		{
			FSlateDrawElement::MakeBox(Out, LayerId + 1,
				Geometry.ToPaintGeometry(FVector2f(10.f * S, 7.f * S), FSlateLayoutTransform(FVector2f(C + FVector2D(-5.f, -1.f) * S))),
				FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Color);
			for (float Row = 0.f; Row < 6.f; Row += 1.f)
			{
				const float Half = Row;
				FSlateDrawElement::MakeLines(Out, LayerId + 1, Geometry.ToPaintGeometry(),
					TArray<FVector2D>{ C + FVector2D(-Half, -6.f + Row) * S, C + FVector2D(Half, -6.f + Row) * S },
					ESlateDrawEffect::None, Color, true, 1.5f * S);
			}
		}
		DrawShadowedLines(Out, LayerId, Geometry, House, Color, 2.f);
		if (bQuestion)
		{
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FMath::RoundToInt(7.f * S));
			DrawText(Out, LayerId + 2, Geometry, FText::FromString(TEXT("?")), Font, C + FVector2D(0.f, -2.f * S), Color);
		}
	}

	/** A medal Radius across the disc: a ring on a V of ribbon, the challenge pedestals' glyph. */
	void DrawMedal(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FVector2D& C,
		float Radius, const FLinearColor& Color)
	{
		const FVector2D Disc = C + FVector2D(0.f, Radius * 0.45f);
		DrawShadowedLines(Out, LayerId, Geometry, TArray<FVector2D>{ C + FVector2D(-Radius * 0.7f, -Radius * 1.2f),
			Disc + FVector2D(0.f, -Radius * 0.9f), C + FVector2D(Radius * 0.7f, -Radius * 1.2f) }, Color, 2.f);
		DrawShadowedLines(Out, LayerId, Geometry, ArcPoints(Disc, Radius * 0.75f, 1.f, 16), Color, 2.f);
		FSlateDrawElement::MakeBox(Out, LayerId + 1,
			Geometry.ToPaintGeometry(FVector2f(Radius * 0.6f, Radius * 0.6f), FSlateLayoutTransform(FVector2f(Disc - FVector2D(Radius * 0.3f)))),
			FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, Color);
	}
}
