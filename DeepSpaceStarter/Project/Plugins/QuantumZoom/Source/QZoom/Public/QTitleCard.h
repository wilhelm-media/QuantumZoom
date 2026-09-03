#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QTitleCard.generated.h"

class SBorder;
class STextBlock;

UCLASS()
class QZOOM_API UQTitleCard : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Drives the fade from OUTSIDE (the hosting actor's tick). A widget inside a
	 *  WidgetComponent does not get NativeTick — the first stereo card froze on its opaque
	 *  frame-0 state forever, textless, because nothing ever advanced it. Returns true when
	 *  the card is finished and the host should hide it. */
	bool ApplyPhase(float InElapsed);
	bool bExternallyDriven = false;

	/** The background colour+alpha for the CURRENT phase, written by every ApplyPhase call
	 *  whether or not this widget paints it. The surround sphere is the real background now
	 *  (a plane cannot cover a wall AND a floor frustum), and it reads this. */
	FLinearColor CurrentBackground = FLinearColor(0.f, 0.f, 0.f, 1.f);

	FText Title    = FText::FromString(TEXT("QUANTUM ZOOM"));
	FText Subtitle = FText::FromString(TEXT("ARS ELECTRONICA DEEP SPACE 8K"));

	bool bShowBackground = true;
	bool bShowText       = true;

	/** Point sizes on the widget's own 7680x4320 canvas — NOT screen pixels. The canvas is mapped
	 *  onto a world plane CardHeight tall, so a size here is a FRACTION OF THE PICTURE and stays
	 *  true at any resolution or viewing distance. The original 96 was 2% of the canvas height,
	 *  which is why the title read as a caption instead of a title. */
	float TitleFontSize    = 240.f;
	float SubtitleFontSize = 70.f;
	/** Gap under the title, same canvas units. */
	float SubtitleGap      = 55.f;

	FLinearColor BackgroundColor = FLinearColor(0.0f, 0.01f, 0.06f, 1.f);
	FLinearColor TitleColor      = FLinearColor(0.85f, 0.95f, 1.f, 1.f);
	FLinearColor SubtitleColor   = FLinearColor(0.4f, 0.6f, 0.8f, 1.f);
	FLinearColor StartColor      = FLinearColor(0.f, 0.f, 0.f, 1.f);

private:
	float Elapsed = 0.f;

	static constexpr float FadeIn  = 2.f;
	static constexpr float Hold    = 6.f;
	static constexpr float FadeOut = 2.f;
	static constexpr float Total   = FadeIn + Hold + FadeOut;  // 10s

	TSharedPtr<SBorder>    RootBorder;
	TSharedPtr<STextBlock> TitleTextBlock;
	TSharedPtr<STextBlock> SubtitleTextBlock;
};
