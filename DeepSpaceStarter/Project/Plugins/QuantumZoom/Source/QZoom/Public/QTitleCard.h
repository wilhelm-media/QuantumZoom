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

	FText Title    = FText::FromString(TEXT("QUANTUM ZOOM"));
	FText Subtitle = FText::FromString(TEXT("ARS ELECTRONICA DEEP SPACE 8K"));

	bool bShowBackground = true;
	bool bShowText       = true;

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
