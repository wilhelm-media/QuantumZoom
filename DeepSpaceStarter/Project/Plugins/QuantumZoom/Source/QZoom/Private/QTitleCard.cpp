#include "QTitleCard.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UQTitleCard::RebuildWidget()
{
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 96);
	const FSlateFontInfo SubFont   = FCoreStyle::GetDefaultFontStyle("Regular", 24);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	if (bShowText)
	{
		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(TitleFont)
			.Text(Title)
			.ColorAndOpacity(TitleColor)
			.Justification(ETextJustify::Center)
		];

		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.f, 18.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Font(SubFont)
			.Text(Subtitle)
			.ColorAndOpacity(SubtitleColor)
			.Justification(ETextJustify::Center)
		];
	}

	// Transparent background when bShowBackground == false (alpha 0)
	const FLinearColor BgEff = bShowBackground
		? BackgroundColor
		: FLinearColor(0.f, 0.f, 0.f, 0.f);

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(BgEff)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(0.f)
		[ Content ];
}

void UQTitleCard::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	Elapsed += InDeltaTime;

	float Alpha;
	if (Elapsed < FadeIn)
		Alpha = Elapsed / FadeIn;
	else if (Elapsed < FadeIn + Hold)
		Alpha = 1.f;
	else if (Elapsed < Total)
		Alpha = 1.f - (Elapsed - FadeIn - Hold) / FadeOut;
	else
	{
		RemoveFromParent();
		return;
	}

	SetRenderOpacity(FMath::Clamp(Alpha, 0.f, 1.f));
}
