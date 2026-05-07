#include "QTitleCardActor.h"
#include "Blueprint/UserWidget.h"

void AQTitleCardActor::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		TitleWidget = CreateWidget<UQTitleCard>(PC, UQTitleCard::StaticClass());
		if (TitleWidget)
			TitleWidget->AddToViewport(99);
	}
}
