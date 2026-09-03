#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QHudText.generated.h"

class STextBlock;

/**
 * UQHudText — the readout and the bisect menu, drawn in SCREEN SPACE.
 *
 * WHY THIS EXISTS AT ALL
 * The interface was UTextRenderComponent: real geometry, standing in the scene. Post processing
 * runs over the finished frame, so it necessarily runs over the text as well — there is no flag
 * on a mesh that says "grade everything except me". Every attempt to work around that treated a
 * symptom: pre-dividing the emissive by the tint (a constant, so a positional vignette walks
 * straight through it), suspending vignette and bloom while the menu is open, raising the
 * compensation to a power. All of it was arithmetic against a pass that had already happened.
 *
 * Screen-space UMG is drawn AFTER the tonemapper. Tint, exposure, contrast, vignette and bloom
 * cannot reach it — not because they are compensated, but because they are already finished.
 * The problem stops existing rather than being balanced out.
 *
 * THE PRICE, stated: screen space has no stereo depth. It is painted identically into both eyes,
 * so on the wall it sits ON the screen rather than in the room. That is exactly why the intro
 * CARD was moved the other way, from viewport to a world plane — a title is content and wants
 * depth. A diagnostic panel is not content; it belongs on the glass.
 */
UCLASS()
class QZOOM_API UQHudText : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Push a block of text. Safe before the Slate tree exists — the values are held and applied
	 *  in RebuildWidget, so BeginPlay ordering cannot lose the first frame's content. */
	void SetLine(const FString& InText, const FLinearColor& InColor, float InSize,
	             const FVector2D& InPadding);

private:
	TSharedPtr<STextBlock> Block;
	FText          Pending;
	FLinearColor   PendingColor = FLinearColor::White;
	float          PendingSize = 44.f;
	FVector2D      PendingPad = FVector2D(64.f, 40.f);
	TSharedPtr<class SBox> Pad;
};
