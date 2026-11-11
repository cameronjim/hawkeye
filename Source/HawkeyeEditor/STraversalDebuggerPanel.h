#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class STableViewBase;
class UTraversalDebuggerSubsystem;
enum class ETraversalDebugLayer : uint8;

/**
 * The traversal debugger's tab: layer toggles, "reach from here", the audit buttons, a summary, and two lists with a
 * focus button per row (the anchors checked from the last spot, the roofs the street cannot reach). All state is in
 * UTraversalDebuggerSubsystem; this widget rebuilds its rows when the subsystem says something changed.
 */
class STraversalDebuggerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STraversalDebuggerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~STraversalDebuggerPanel() override;

private:
	/** One list row: a roof or an anchor of the subsystem's scene, and how to show it. */
	struct FRow
	{
		int32 Roof = INDEX_NONE;
		int32 Anchor = INDEX_NONE;
		FText Label;
		FLinearColor Color = FLinearColor::White;
	};
	using FRowPtr = TSharedPtr<FRow>;

	static UTraversalDebuggerSubsystem* GetSubsystem();

	TSharedRef<SWidget> MakeControls();
	TSharedRef<SWidget> MakeLayerToggle(ETraversalDebugLayer Layer, const FText& Label);
	TSharedRef<SWidget> MakeButton(const FText& Label, const FText& ToolTip, TFunction<void(UTraversalDebuggerSubsystem&)> Action);
	TSharedRef<ITableRow> MakeRow(FRowPtr Item, const TSharedRef<STableViewBase>& Owner);

	void HandleChanged();
	void RebuildReachRows();
	void RebuildRoofRows();

	FText GetStatusText() const;
	FText GetSummaryText() const;

	TArray<FRowPtr> ReachRows;
	TArray<FRowPtr> RoofRows;
	TSharedPtr<SListView<FRowPtr>> ReachList;
	TSharedPtr<SListView<FRowPtr>> RoofList;
	FDelegateHandle ChangedHandle;
};
