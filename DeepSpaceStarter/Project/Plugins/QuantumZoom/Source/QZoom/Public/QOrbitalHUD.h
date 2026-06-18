#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QOrbitalHUD.generated.h"

class STextBlock;

/**
 * QOrbitalHUD
 *
 * Minimal operator readout for gamepad live-control of the orbital Niagara system.
 * Pure C++/Slate (no .uasset). Rendered through Slate, so it is NOT suppressed by
 * the cluster's `DisableAllScreenMessages` (unlike GEngine->AddOnScreenDebugMessage).
 *
 * Created + driven by AQOrbitalController on the PRIMARY node only.
 */
UCLASS()
class QZOOM_API UQOrbitalHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Update the readout. Header e.g. "ORBITAL  4/10", Value e.g. "LayerGradient: 50.0". */
	void SetReadout(const FString& Header, const FString& Value);

private:
	TSharedPtr<STextBlock> HeaderText;
	TSharedPtr<STextBlock> ValueText;
};
