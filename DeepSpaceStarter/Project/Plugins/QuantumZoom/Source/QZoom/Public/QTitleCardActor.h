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

private:
	UPROPERTY()
	TObjectPtr<UQTitleCard> TitleWidget;
	UPROPERTY()
	TObjectPtr<class UWidgetComponent> CardComp;
	float CardElapsed = 0.f;
};
