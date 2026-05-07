#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QTitleCard.generated.h"

UCLASS()
class QZOOM_API UQTitleCard : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	float Elapsed = 0.f;

	static constexpr float FadeIn  = 2.f;
	static constexpr float Hold    = 6.f;
	static constexpr float FadeOut = 2.f;
	static constexpr float Total   = FadeIn + Hold + FadeOut;  // 10s
};
