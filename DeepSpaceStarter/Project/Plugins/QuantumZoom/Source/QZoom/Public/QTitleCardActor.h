#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QTitleCard.h"
#include "QTitleCardActor.generated.h"

UENUM(BlueprintType)
enum class EQTitleCardMode : uint8
{
	Hidden         UMETA(DisplayName="Hidden"),
	BackgroundOnly UMETA(DisplayName="Background only"),
	TextOnly       UMETA(DisplayName="Text only"),
	Full           UMETA(DisplayName="Background + Text"),
};

UCLASS()
class QZOOM_API AQTitleCardActor : public AActor
{
	GENERATED_BODY()

public:
	AQTitleCardActor();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;

	UPROPERTY(EditAnywhere, Category="QTitleCard")
	FText Title = FText::FromString(TEXT("QUANTUM ZOOM"));

	UPROPERTY(EditAnywhere, Category="QTitleCard")
	FText Subtitle = FText::FromString(TEXT("ARS ELECTRONICA DEEP SPACE 8K"));

	/** Per-node visibility — primary (Wall) node */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Display")
	EQTitleCardMode WallMode = EQTitleCardMode::Full;

	/** Per-node visibility — secondary (Floor) node */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Display")
	EQTitleCardMode FloorMode = EQTitleCardMode::Hidden;

	UPROPERTY(EditAnywhere, Category="QTitleCard|Colors")
	FLinearColor BackgroundColor = FLinearColor(0.0f, 0.01f, 0.06f, 1.f);

	UPROPERTY(EditAnywhere, Category="QTitleCard|Colors")
	FLinearColor TitleColor = FLinearColor(0.85f, 0.95f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, Category="QTitleCard|Colors")
	FLinearColor SubtitleColor = FLinearColor(0.4f, 0.6f, 0.8f, 1.f);

	/** Color the background fades FROM at t=0. Default opaque black — keeps scene invisible at start. */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Colors")
	FLinearColor StartColor = FLinearColor(0.f, 0.f, 0.f, 1.f);

	/** ═══ STEREO. AddToViewport painted the card at SCREEN depth — identical in both eyes, zero
	 *  parallax, which on the Deep Space wall reads as "not stereo" (it wasn't). The card now
	 *  lives on a world-space widget plane at a real distance in front of the show camera, so it
	 *  has depth like everything else, and the grade applies to it (neutral P0 at the start). ═══ */

	/** How far in front of the show camera the card plane hangs. Comfortable stereo depth. */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Display", meta=(ClampMin="100.0"))
	float CardDistance = 120.f;

	/** World height of the plane. Default fills the wall frustum at CardDistance with margin. */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Display", meta=(ClampMin="50.0"))
	float CardHeight = 260.f;

	/** ═══ THE FLOOR CUT-OFF. The plane above solved stereo and created a second problem: a plane
	 *  covers ONE frustum. Wall and floor look ~90° apart, so the floor node saw the plane edge-on
	 *  and the background simply stopped mid-picture. A sphere around the camera has no outside —
	 *  it covers every direction, for any number of nodes, and stays real geometry so it keeps its
	 *  depth. The TEXT stays on its plane in front (sort priority 100 vs 90): text is the part that
	 *  needs parallax, a flat colour has none to give. ═══ */

	/** ═══ TEXT SIZE. Point sizes on the card's own 7680x4320 canvas, not screen pixels — the
	 *  canvas is mapped onto the world plane, so these are FRACTIONS OF THE PICTURE and hold at
	 *  any resolution. 520 puts the title at ~12% of the picture height; the old hard-coded 96
	 *  was 2%, which is caption size, not title size. ═══ */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Text", meta=(ClampMin="8.0"))
	float TitleFontSize = 240.f;      // ~5.5% of picture height. 96 was a caption, 520 a poster.

	UPROPERTY(EditAnywhere, Category="QTitleCard|Text", meta=(ClampMin="6.0"))
	float SubtitleFontSize = 70.f;

	/** Gap between title and subtitle, same canvas units. */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Text", meta=(ClampMin="0.0"))
	float SubtitleGap = 55.f;

	/** Nudge the text plane inside its own plane, in world units: X = right, Y = up.
	 *  The plane is placed on the show camera's forward axis, which is the centre of the WALL only
	 *  as long as that node's frustum is on-axis. An nDisplay wall is usually off-axis, so this is
	 *  the correction — and it moves the text alone, never the surround. */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Text")
	FVector2D CardOffset = FVector2D::ZeroVector;

	/** Radius of the surround sphere. Must stay LARGER than CardDistance or it swallows the text. */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Display", meta=(ClampMin="150.0"))
	float BackgroundRadius = 400.f;

	/** Unlit two-sided translucent material with a 'Color' vector parameter (RGB = colour,
	 *  A = coverage). Empty = load M_QZ_CardBG, which is exactly that. */
	UPROPERTY(EditAnywhere, Category="QTitleCard|Display")
	TObjectPtr<class UMaterialInterface> BackgroundMaterial;

private:
	UPROPERTY()
	TObjectPtr<UQTitleCard> TitleWidget;
	UPROPERTY()
	TObjectPtr<class UWidgetComponent> CardComp;
	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> BgComp;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> BgMID;
	float CardElapsed = 0.f;
};
