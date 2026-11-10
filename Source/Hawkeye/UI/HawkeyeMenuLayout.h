#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class UWidgetTree;

/**
 * The plain layout the script-free menus share (main menu, safehouse): a full-screen dimmer and a
 * centred stack of a title, text lines and buttons. Only for widgets whose Blueprint has no
 * designer layout; see UHawkeyePauseWidget for the same approach written out inline.
 */
namespace HawkeyeMenuLayout
{
	/** Sets WidgetTree's root to a dimmer at DimAlpha with an empty centred stack, and returns the stack. */
	HAWKEYE_API UVerticalBox* BuildFrame(UWidgetTree* WidgetTree, float DimAlpha);

	/** Appends a centred text line of FontSize with BottomPadding under it. */
	HAWKEYE_API UTextBlock* AddText(UWidgetTree* WidgetTree, UVerticalBox* Stack, const TCHAR* Name, const FText& Text,
		int32 FontSize, float BottomPadding);

	/**
	 * Base with every check glyph (unchecked, checked, undetermined, each plain, hovered and pressed)
	 * drawn BoxSize px square, and LabelGap px between the glyph and the checkbox's content. The
	 * engine default is 16 px, which read as a speck at 1080p.
	 */
	HAWKEYE_API FCheckBoxStyle MakeCheckBoxStyle(const FCheckBoxStyle& Base, float BoxSize, float LabelGap);

	/** Appends a full-width button labelled Label, creating it into Button when the layout had none. */
	HAWKEYE_API void AddButton(UWidgetTree* WidgetTree, UVerticalBox* Stack, TObjectPtr<UButton>& Button, const TCHAR* Name,
		const FText& Label);
}
