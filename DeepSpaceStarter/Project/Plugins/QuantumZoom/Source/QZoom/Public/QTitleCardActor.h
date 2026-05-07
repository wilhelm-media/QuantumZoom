#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QTitleCard.h"
#include "QTitleCardActor.generated.h"

UCLASS()
class QZOOM_API AQTitleCardActor : public AActor
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UQTitleCard> TitleWidget;
};
