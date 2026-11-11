#include "STraversalDebuggerPanel.h"

#include "Editor.h"
#include "TraversalDebuggerSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "TraversalDebugger"

void STraversalDebuggerPanel::Construct(const FArguments& InArgs)
{
	if (UTraversalDebuggerSubsystem* Subsystem = GetSubsystem())
	{
		ChangedHandle = Subsystem->OnChanged.AddSP(this, &STraversalDebuggerPanel::HandleChanged);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			MakeControls()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(STextBlock).AutoWrapText(true).Text(this, &STraversalDebuggerPanel::GetStatusText)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(STextBlock).AutoWrapText(true).Text(this, &STraversalDebuggerPanel::GetSummaryText)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 8.f, 4.f, 2.f)
		[
			SNew(STextBlock).Text(LOCTEXT("ReachHeader", "Anchors in range of the last spot (reachable first)"))
		]
		+ SVerticalBox::Slot().FillHeight(0.5f).Padding(4.f)
		[
			SAssignNew(ReachList, SListView<FRowPtr>)
			.ListItemsSource(&ReachRows)
			.OnGenerateRow(this, &STraversalDebuggerPanel::MakeRow)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 8.f, 4.f, 2.f)
		[
			SNew(STextBlock).Text(LOCTEXT("RoofHeader", "Roofs the street cannot reach (red: street-facing, purple: no sidewalk in range)"))
		]
		+ SVerticalBox::Slot().FillHeight(0.5f).Padding(4.f)
		[
			SAssignNew(RoofList, SListView<FRowPtr>)
			.ListItemsSource(&RoofRows)
			.OnGenerateRow(this, &STraversalDebuggerPanel::MakeRow)
		]
	];
	HandleChanged();
}

STraversalDebuggerPanel::~STraversalDebuggerPanel()
{
	if (UTraversalDebuggerSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->OnChanged.Remove(ChangedHandle);
	}
}

UTraversalDebuggerSubsystem* STraversalDebuggerPanel::GetSubsystem()
{
	return GEditor ? GEditor->GetEditorSubsystem<UTraversalDebuggerSubsystem>() : nullptr;
}

TSharedRef<SWidget> STraversalDebuggerPanel::MakeControls()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 4.f))
			+ SWrapBox::Slot()[MakeLayerToggle(ETraversalDebugLayer::Anchors, LOCTEXT("LayerAnchors", "Grapple anchors"))]
			+ SWrapBox::Slot()[MakeLayerToggle(ETraversalDebugLayer::Ledges, LOCTEXT("LayerLedges", "Ledges"))]
			+ SWrapBox::Slot()[MakeLayerToggle(ETraversalDebugLayer::FireEscapes, LOCTEXT("LayerFireEscapes", "Fire-escape landings"))]
			+ SWrapBox::Slot()[MakeLayerToggle(ETraversalDebugLayer::UnreachedRoofs, LOCTEXT("LayerUnreached", "Unreached roofs"))]
			+ SWrapBox::Slot()[MakeLayerToggle(ETraversalDebugLayer::Reach, LOCTEXT("LayerReach", "Reach lines"))]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
			+ SWrapBox::Slot()[MakeButton(LOCTEXT("Refresh", "Refresh scene"), LOCTEXT("RefreshTip", "Re-read the city data and the player's rules."),
				[](UTraversalDebuggerSubsystem& Subsystem) { Subsystem.RefreshScene(); })]
			+ SWrapBox::Slot()[MakeButton(LOCTEXT("ReachCamera", "Reach from camera"),
				LOCTEXT("ReachCameraTip", "Stand on the floor under the viewport camera and check every anchor."),
				[](UTraversalDebuggerSubsystem& Subsystem) { Subsystem.ReachFromViewport(); })]
			+ SWrapBox::Slot()[MakeButton(LOCTEXT("ReachSelection", "Reach from selection"),
				LOCTEXT("ReachSelectionTip", "Stand on the floor under the selected actor and check every anchor."),
				[](UTraversalDebuggerSubsystem& Subsystem) { Subsystem.ReachFromSelection(); })]
			+ SWrapBox::Slot()[MakeButton(LOCTEXT("Audit", "Run audit"), LOCTEXT("AuditTip", "Run the grapple audit on this map."),
				[](UTraversalDebuggerSubsystem& Subsystem) { Subsystem.RunAudit(/*bWriteJson=*/false); })]
			+ SWrapBox::Slot()[MakeButton(LOCTEXT("AuditJson", "Run audit, write JSON"),
				LOCTEXT("AuditJsonTip", "Run the grapple audit and write Saved/Automation/grapple_audit.json."),
				[](UTraversalDebuggerSubsystem& Subsystem) { Subsystem.RunAudit(/*bWriteJson=*/true); })]
			+ SWrapBox::Slot()[MakeButton(LOCTEXT("Clear", "Clear drawing"), LOCTEXT("ClearTip", "Remove everything this tool drew."),
				[](UTraversalDebuggerSubsystem& Subsystem) { Subsystem.ClearDrawing(); })]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("Chain", "Objective chain"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("ChainHint", "objective ids whose roofs must chain, comma-separated"))
				.Text_Lambda([]()
				{
					const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
					return FText::FromString(Subsystem ? Subsystem->ObjectiveChain : FString());
				})
				.OnTextCommitted_Lambda([](const FText& Text, ETextCommit::Type)
				{
					if (UTraversalDebuggerSubsystem* Subsystem = GetSubsystem())
					{
						Subsystem->ObjectiveChain = Text.ToString();
					}
				})
			]
		];
}

TSharedRef<SWidget> STraversalDebuggerPanel::MakeLayerToggle(ETraversalDebugLayer Layer, const FText& Label)
{
	return SNew(SCheckBox)
		.IsChecked_Lambda([Layer]()
		{
			const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
			return Subsystem && Subsystem->IsLayerVisible(Layer) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([Layer](ECheckBoxState State)
		{
			if (UTraversalDebuggerSubsystem* Subsystem = GetSubsystem())
			{
				Subsystem->SetLayerVisible(Layer, State == ECheckBoxState::Checked);
			}
		})
		[
			SNew(STextBlock).Text(Label)
		];
}

TSharedRef<SWidget> STraversalDebuggerPanel::MakeButton(const FText& Label, const FText& ToolTip,
	TFunction<void(UTraversalDebuggerSubsystem&)> Action)
{
	return SNew(SButton)
		.Text(Label)
		.ToolTipText(ToolTip)
		.OnClicked_Lambda([Action]()
		{
			if (UTraversalDebuggerSubsystem* Subsystem = GetSubsystem())
			{
				Action(*Subsystem);
			}
			return FReply::Handled();
		});
}

TSharedRef<ITableRow> STraversalDebuggerPanel::MakeRow(FRowPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const int32 Roof = Item.IsValid() ? Item->Roof : INDEX_NONE;
	const int32 Anchor = Item.IsValid() ? Item->Anchor : INDEX_NONE;
	return SNew(STableRow<FRowPtr>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 1.f, 6.f, 1.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Focus", "Focus"))
				.OnClicked_Lambda([Roof, Anchor]()
				{
					if (const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem())
					{
						if (Anchor != INDEX_NONE)
						{
							Subsystem->FocusAnchor(Anchor);
						}
						else
						{
							Subsystem->FocusRoof(Roof);
						}
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Item.IsValid() ? FText::FromString(Item->Label) : FText::GetEmpty())
				.ColorAndOpacity(FSlateColor(Item.IsValid() ? Item->Color : FLinearColor::White))
			]
		];
}

void STraversalDebuggerPanel::HandleChanged()
{
	RebuildReachRows();
	RebuildRoofRows();
	if (ReachList.IsValid())
	{
		ReachList->RequestListRefresh();
	}
	if (RoofList.IsValid())
	{
		RoofList->RequestListRefresh();
	}
}

void STraversalDebuggerPanel::RebuildReachRows()
{
	ReachRows.Reset();
	const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
	if (!Subsystem || !Subsystem->HasReach())
	{
		return;
	}
	for (const FTraversalDebuggerRow& Row : FTraversalDebuggerViewModel::MakeReachRows(Subsystem->GetScene(), Subsystem->GetReachResults()))
	{
		ReachRows.Add(MakeShared<FTraversalDebuggerRow>(Row));
	}
}

void STraversalDebuggerPanel::RebuildRoofRows()
{
	RoofRows.Reset();
	const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
	for (const FTraversalDebuggerRow& Row : FTraversalDebuggerViewModel::MakeRoofRows(Subsystem ? Subsystem->GetAuditReport() : nullptr))
	{
		RoofRows.Add(MakeShared<FTraversalDebuggerRow>(Row));
	}
}

FText STraversalDebuggerPanel::GetStatusText() const
{
	const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
	return FText::FromString(FTraversalDebuggerViewModel::MakeStatusText(Subsystem != nullptr, Subsystem ? Subsystem->GetStatus() : FString()));
}

FText STraversalDebuggerPanel::GetSummaryText() const
{
	const UTraversalDebuggerSubsystem* Subsystem = GetSubsystem();
	return FText::FromString(FTraversalDebuggerViewModel::MakeSummaryText(Subsystem ? Subsystem->GetAuditReport() : nullptr));
}

#undef LOCTEXT_NAMESPACE
