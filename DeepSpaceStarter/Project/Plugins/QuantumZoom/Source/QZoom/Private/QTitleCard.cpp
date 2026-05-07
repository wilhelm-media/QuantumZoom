#include "QTitleCard.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UQTitleCard::RebuildWidget()
{
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 96);
	const FSlateFontInfo SubFont   = FCoreStyle::GetDefaultFontStyle("Regular", 24);

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.01f, 0.06f, 1.f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Font(TitleFont)
				.Text(FText::FromString(TEXT("QUANTUM ZOOM")))
				.ColorAndOpacity(FLinearColor(0.85f, 0.95f, 1.f, 1.f))
				.Justification(ETextJustify::Center)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 18.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Font(SubFont)
				.Text(FText::FromString(TEXT("ARS ELECTRONICA DEEP SPACE 8K")))
				.ColorAndOpacity(FLinearColor(0.4f, 0.6f, 0.8f, 1.f))
				.Justification(ETextJustify::Center)
			]
		];
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
