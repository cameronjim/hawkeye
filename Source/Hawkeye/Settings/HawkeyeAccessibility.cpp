#include "Settings/HawkeyeAccessibility.h"

#include "Settings/HawkeyeSettingsSubsystem.h"

FHawkeyePalette UHawkeyeAccessibility::GetPalette(EHawkeyeColorPalette Palette)
{
	FHawkeyePalette Out;
	switch (Palette)
	{
	case EHawkeyeColorPalette::Deuteranopia:
		// No green-red axis: the grapple green goes amber, Kate's purple goes blue, the bar orange.
		Out.Green = FLinearColor(1.f, 0.75f, 0.05f, 1.f);
		Out.Purple = FLinearColor(0.25f, 0.55f, 1.f, 1.f);
		Out.Cream = FLinearColor(1.f, 1.f, 0.9f, 1.f);
		Out.HealthBar = FLinearColor(1.f, 0.45f, 0.1f, 1.f);
		// Away from the amber, the blue and the orange bar: a hot pink, which stays bright.
		Out.Danger = FLinearColor(1.f, 0.15f, 0.5f, 1.f);
		break;
	case EHawkeyeColorPalette::Protanopia:
		// Reds read dark: the bar moves to cyan so it stays bright on a dark street.
		Out.Green = FLinearColor(1.f, 0.82f, 0.1f, 1.f);
		Out.Purple = FLinearColor(0.35f, 0.5f, 1.f, 1.f);
		Out.Cream = FLinearColor(0.95f, 0.95f, 1.f, 1.f);
		Out.HealthBar = FLinearColor(0.1f, 0.85f, 0.95f, 1.f);
		// A full orange rather than red, which reads dark; well off the yellow grapple marker.
		Out.Danger = FLinearColor(1.f, 0.4f, 0.f, 1.f);
		break;
	case EHawkeyeColorPalette::Tritanopia:
		// No blue-yellow axis: purple goes magenta, green goes cyan, cream a pinkish white.
		Out.Green = FLinearColor(0.1f, 0.95f, 0.95f, 1.f);
		Out.Purple = FLinearColor(1.f, 0.3f, 0.6f, 1.f);
		Out.Cream = FLinearColor(1.f, 0.88f, 0.9f, 1.f);
		Out.HealthBar = FLinearColor(1.f, 0.25f, 0.2f, 1.f);
		// Off the red bar and the magenta: orange toward yellow.
		Out.Danger = FLinearColor(1.f, 0.6f, 0.f, 1.f);
		break;
	default:
		break;
	}
	return Out;
}

int32 UHawkeyeAccessibility::GetSubtitleFontSize(EHawkeyeSubtitleSize Size)
{
	switch (Size)
	{
	case EHawkeyeSubtitleSize::Small:
		return 20;
	case EHawkeyeSubtitleSize::Large:
		return 34;
	default:
		return 26;
	}
}

float UHawkeyeAccessibility::GetFlashScaleFor(const UObject* WorldContextObject)
{
	return GetFlashScale(UHawkeyeSettingsSubsystem::GetCurrentSettings(WorldContextObject).bReduceFlashing);
}

FText UHawkeyeAccessibility::GetPaletteName(EHawkeyeColorPalette Palette)
{
	switch (Palette)
	{
	case EHawkeyeColorPalette::Deuteranopia:
		return NSLOCTEXT("Hawkeye", "PaletteDeuteranopia", "Deuteranopia");
	case EHawkeyeColorPalette::Protanopia:
		return NSLOCTEXT("Hawkeye", "PaletteProtanopia", "Protanopia");
	case EHawkeyeColorPalette::Tritanopia:
		return NSLOCTEXT("Hawkeye", "PaletteTritanopia", "Tritanopia");
	default:
		return NSLOCTEXT("Hawkeye", "PaletteDefault", "Default");
	}
}

FText UHawkeyeAccessibility::GetSubtitleSizeName(EHawkeyeSubtitleSize Size)
{
	switch (Size)
	{
	case EHawkeyeSubtitleSize::Small:
		return NSLOCTEXT("Hawkeye", "SubtitleSmall", "Small");
	case EHawkeyeSubtitleSize::Large:
		return NSLOCTEXT("Hawkeye", "SubtitleLarge", "Large");
	default:
		return NSLOCTEXT("Hawkeye", "SubtitleMedium", "Medium");
	}
}
