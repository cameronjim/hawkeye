#pragma once

#include "CoreMinimal.h"

class FSlateWindowElementList;
struct FGeometry;
struct FSlateFontInfo;

/**
 * The painted HUD glyphs the compass, the world markers and the world map share: shadowed text, the
 * objective diamond, the safehouse house and the challenge medal. Everything draws with a one-pixel drop
 * shadow so it reads on sky, snow and the map's dark ground.
 */
namespace HawkeyeHudGlyphs
{
	extern const FLinearColor Shadow;

	/** The size Text takes in Font, or zero when Slate has no renderer (headless). */
	FVector2D MeasureText(const FText& Text, const FSlateFontInfo& Font);

	/** Text centred horizontally on CentreTop, with a one-pixel drop shadow. */
	void DrawText(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FText& Text,
		const FSlateFontInfo& Font, const FVector2D& CentreTop, const FLinearColor& Color);

	/** A hollow diamond HalfSize from the centre to each point, shadowed like the text. */
	void DrawDiamond(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry,
		const FVector2D& Centre, float HalfSize, float Thickness, const FLinearColor& Color);

	/** Points round a circle of Radius about Centre, from straight up clockwise, Fraction of the way. */
	TArray<FVector2D> ArcPoints(const FVector2D& Centre, float Radius, float Fraction, int32 Segments = 32);

	/** Lines with the one-pixel drop shadow everything else has. */
	void DrawShadowedLines(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry,
		const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness);

	/**
	 * A house Scale times 12 px wide: a body under a pitched roof. Filled for a safehouse that has been
	 * found, only outlined before; bQuestion puts a "?" in it (a marked one not found yet).
	 */
	void DrawHouse(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FVector2D& C,
		float Scale, const FLinearColor& Color, bool bFilled, bool bQuestion);

	/** A medal Radius across the disc: a ring on a V of ribbon, the challenge pedestals' glyph. */
	void DrawMedal(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& Geometry, const FVector2D& C,
		float Radius, const FLinearColor& Color);
}
