#include "QTitleCard.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UQTitleCard::RebuildWidget()
{
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(
		"Bold", FMath::Max(TitleFontSize, 8.f));
	const FSlateFontInfo SubFont = FCoreStyle::GetDefaultFontStyle(
		"Regular", FMath::Max(SubtitleFontSize, 6.f));

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	if (bShowText)
	{
		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SAssignNew(TitleTextBlock, STextBlock)
			.Font(TitleFont)
			.Text(Title)
			// Start hidden — NativeTick fades alpha in
			.ColorAndOpacity(FLinearColor(TitleColor.R, TitleColor.G, TitleColor.B, 0.f))
			.Justification(ETextJustify::Center)
		];

		Content->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.f, FMath::Max(SubtitleGap, 0.f), 0.f, 0.f))
		[
			SAssignNew(SubtitleTextBlock, STextBlock)
			.Font(SubFont)
			.Text(Subtitle)
			.ColorAndOpacity(FLinearColor(SubtitleColor.R, SubtitleColor.G, SubtitleColor.B, 0.f))
			.Justification(ETextJustify::Center)
		];
	}

	// Initial background = StartColor when bShowBackground (covers scene from frame 0);
	// otherwise transparent (TextOnly mode).
	const FLinearColor InitialBg = bShowBackground
		? StartColor
		: FLinearColor(0.f, 0.f, 0.f, 0.f);

	return SAssignNew(RootBorder, SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(InitialBg)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(0.f)
		[ Content ];
}

void UQTitleCard::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bExternallyDriven) return;      // the hosting actor owns the clock
	Elapsed += InDeltaTime;
	if (Elapsed >= Total)
	{
		RemoveFromParent();
		return;
	}
	ApplyPhase(Elapsed);
}

bool UQTitleCard::ApplyPhase(float InElapsed)
{
	Elapsed = InElapsed;
	if (Elapsed >= Total) return true;

	// Compute per-phase progress
	float BgAlpha;       // background color alpha (1 = fully opaque, 0 = scene revealed)
	float BgLerp;        // 0 = StartColor, 1 = BackgroundColor
	float TextAlpha;     // 0 = invisible, 1 = full

	if (Elapsed < FadeIn)
	{
		const float t = Elapsed / FadeIn;
		BgLerp    = t;
		BgAlpha   = 1.f;
		TextAlpha = t;
	}
	else if (Elapsed < FadeIn + Hold)
	{
		BgLerp    = 1.f;
		BgAlpha   = 1.f;
		TextAlpha = 1.f;
	}
	else
	{
		const float t = (Elapsed - FadeIn - Hold) / FadeOut;
		BgLerp    = 1.f;
		BgAlpha   = 1.f - t;
		TextAlpha = 1.f - t;
	}

	// Background: lerp StartColor → BackgroundColor (RGB), then scale alpha for fade-out.
	// Computed UNCONDITIONALLY and published, because the background is no longer necessarily
	// painted by this widget: the surround sphere reads CurrentBackground from here every frame.
	{
		const FLinearColor BgRGB = FMath::Lerp(StartColor, BackgroundColor, BgLerp);
		CurrentBackground = FLinearColor(BgRGB.R, BgRGB.G, BgRGB.B, BgRGB.A * BgAlpha);
	}
	if (RootBorder.IsValid() && bShowBackground)
	{
		RootBorder->SetBorderBackgroundColor(FSlateColor(CurrentBackground));
	}

	// Text: keep target color, modulate alpha
	if (TitleTextBlock.IsValid())
	{
		TitleTextBlock->SetColorAndOpacity(
			FSlateColor(FLinearColor(TitleColor.R, TitleColor.G, TitleColor.B, TextAlpha)));
	}
	if (SubtitleTextBlock.IsValid())
	{
		SubtitleTextBlock->SetColorAndOpacity(
			FSlateColor(FLinearColor(SubtitleColor.R, SubtitleColor.G, SubtitleColor.B, TextAlpha)));
	}
	return false;
}
