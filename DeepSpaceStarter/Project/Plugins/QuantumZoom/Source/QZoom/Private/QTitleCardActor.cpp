#include "QTitleCardActor.h"
#include "Blueprint/UserWidget.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"

void AQTitleCardActor::BeginPlay()
{
	Super::BeginPlay();

	// Determine node role — primary = wall, secondary = floor.
	// Single-process / PIE counts as wall.
	const bool bIsPrimary = !IDisplayCluster::IsAvailable()
		|| IDisplayCluster::Get().GetClusterMgr()->IsPrimary();

	const EQTitleCardMode Mode = bIsPrimary ? WallMode : FloorMode;
	if (Mode == EQTitleCardMode::Hidden) return;

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		TitleWidget = CreateWidget<UQTitleCard>(PC, UQTitleCard::StaticClass());
		if (TitleWidget)
		{
			TitleWidget->Title           = Title;
			TitleWidget->Subtitle        = Subtitle;
			TitleWidget->bShowBackground = (Mode == EQTitleCardMode::Full || Mode == EQTitleCardMode::BackgroundOnly);
			TitleWidget->bShowText       = (Mode == EQTitleCardMode::Full || Mode == EQTitleCardMode::TextOnly);
			TitleWidget->BackgroundColor = BackgroundColor;
			TitleWidget->TitleColor      = TitleColor;
			TitleWidget->SubtitleColor   = SubtitleColor;
			TitleWidget->StartColor      = StartColor;
			TitleWidget->AddToViewport(99);
		}
	}
}
