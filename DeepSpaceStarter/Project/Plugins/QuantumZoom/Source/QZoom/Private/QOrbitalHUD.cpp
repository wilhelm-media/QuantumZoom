#include "QOrbitalHUD.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UQOrbitalHUD::RebuildWidget()
{
	const FSlateFontInfo HeadFont  = FCoreStyle::GetDefaultFontStyle("Bold", 22);
	const FSlateFontInfo ValueFont = FCoreStyle::GetDefaultFontStyle("Bold", 34);
	const FSlateFontInfo HintFont  = FCoreStyle::GetDefaultFontStyle("Regular", 15);

	const FLinearColor Cyan(0.30f, 0.85f, 1.00f, 1.f);
	const FLinearColor Dim (0.50f, 0.70f, 0.85f, 1.f);
	const FLinearColor Hint(0.40f, 0.55f, 0.65f, 1.f);

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(HeaderText, STextBlock)
			.Font(HeadFont)
			.Text(FText::FromString(TEXT("ORBITAL")))
			.ColorAndOpacity(FSlateColor(Dim))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
		[
			SAssignNew(ValueText, STextBlock)
			.Font(ValueFont)
			.Text(FText::FromString(TEXT("-")))
			.ColorAndOpacity(FSlateColor(Cyan))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Font(HintFont)
			.Text(FText::FromString(TEXT("RT hold   -   B / X cycle   -   L-Stick adjust   -   R-Stick reset")))
			.ColorAndOpacity(FSlateColor(Hint))
		];

	// Outer border = full-screen, fully transparent container that pins the panel to the
	// top-left corner. Inner border = the visible dark panel.
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f)))
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(48.f, 48.f, 0.f, 0.f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.f, 0.03f, 0.06f, 0.72f)))
			.Padding(FMargin(24.f, 16.f))
			[ Panel ]
		];
}

void UQOrbitalHUD::SetReadout(const FString& Header, const FString& Value)
{
	if (HeaderText.IsValid()) HeaderText->SetText(FText::FromString(Header));
	if (ValueText.IsValid())  ValueText->SetText(FText::FromString(Value));
}
