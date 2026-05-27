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
	virtual void BeginPlay() override;

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

private:
	UPROPERTY()
	TObjectPtr<UQTitleCard> TitleWidget;
};
