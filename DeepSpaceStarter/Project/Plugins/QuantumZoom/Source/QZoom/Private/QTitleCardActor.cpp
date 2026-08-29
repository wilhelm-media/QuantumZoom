#include "QTitleCardActor.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "QZoomStagePawn.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"

AQTitleCardActor::AQTitleCardActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AQTitleCardActor::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!TitleWidget || !CardComp) return;
	CardElapsed += Dt;
	// the ACTOR drives the fade — a component-hosted widget never receives NativeTick
	if (TitleWidget->ApplyPhase(CardElapsed))
	{
		CardComp->SetVisibility(false);
		SetActorTickEnabled(false);      // done for the session
	}
}

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

			// WORLD SPACE, not viewport: a screen-space widget is painted identically into both
			// eyes (zero parallax) and can never be stereo. The plane hangs CardDistance in
			// front of the show camera — which never moves in this show, so one placement at
			// BeginPlay is exact for the whole session.
			FVector CamLoc = FVector::ZeroVector;
			FRotator CamRot = FRotator::ZeroRotator;
			for (TActorIterator<AQZoomStagePawn> It(GetWorld()); It; ++It)
			{
				if (UCameraComponent* Cam = It->FindComponentByClass<UCameraComponent>())
				{
					CamLoc = Cam->GetComponentLocation();
					CamRot = Cam->GetComponentRotation();
				}
				break;
			}
			CardComp = NewObject<UWidgetComponent>(this, TEXT("CardComp"));
			CardComp->RegisterComponent();
			CardComp->SetWidgetSpace(EWidgetSpace::World);
			CardComp->SetDrawSize(FVector2D(7680.f, 4320.f));
			CardComp->SetBlendMode(EWidgetBlendMode::Transparent);
			CardComp->SetTwoSided(true);
			CardComp->SetWidget(TitleWidget);
			const FVector Fwd = CamRot.Vector();
			CardComp->SetWorldLocation(CamLoc + Fwd * CardDistance);
			// widget front faces its +X; turn it back toward the camera
			CardComp->SetWorldRotation((-Fwd).Rotation());
			const float S = CardHeight / 4320.f;
			CardComp->SetWorldScale3D(FVector(S));
			CardComp->SetTranslucentSortPriority(100);   // above the scene, like the old Z-order 99
			TitleWidget->bExternallyDriven = true;        // this actor's Tick owns the clock
		}
	}
}
