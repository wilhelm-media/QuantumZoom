#include "QHudText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UQHudText::RebuildWidget()
{
	// MONO, because the readout is a table of numbers that must line up between frames — a
	// proportional font makes a changing value shift every column beside it.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Mono", FMath::Max(PendingSize, 6.f));

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	Row->AddSlot()
	.AutoWidth()
	.Padding(FMargin(PendingPad.X, PendingPad.Y, 0.f, 0.f))
	[
		SAssignNew(Block, STextBlock)
		.Font(Font)
		.Text(Pending)
		.ColorAndOpacity(FSlateColor(PendingColor))
		// A thin dark outline instead of a background panel: the interface has to stay legible
		// over a bright mushroom AND over black space, and a panel would cover the show.
		.ShadowOffset(FVector2D(2.f, 2.f))
		.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
	];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[ Row ];
}

void UQHudText::SetLine(const FString& InText, const FLinearColor& InColor, float InSize,
                        const FVector2D& InPadding)
{
	Pending      = FText::FromString(InText);
	PendingColor = InColor;
	PendingPad   = InPadding;

	if (Block.IsValid())
	{
		Block->SetText(Pending);
		Block->SetColorAndOpacity(FSlateColor(PendingColor));
		// Rebuild the font only when the size actually changed — SetFont allocates, and this
		// runs every frame.
		if (!FMath::IsNearlyEqual(InSize, PendingSize))
		{
			PendingSize = InSize;
			Block->SetFont(FCoreStyle::GetDefaultFontStyle("Mono", FMath::Max(InSize, 6.f)));
		}
	}
	else
	{
		PendingSize = InSize;   // held for RebuildWidget
	}
}
