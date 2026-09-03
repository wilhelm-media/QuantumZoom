#include "QTitleCardActor.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	if (!TitleWidget) return;
	CardElapsed += Dt;
	// the ACTOR drives the fade — a component-hosted widget never receives NativeTick
	const bool bDone = TitleWidget->ApplyPhase(CardElapsed);
	// The sphere is the background now, so it needs the phase colour every frame. The widget
	// publishes it whether or not it paints one itself.
	if (BgMID) BgMID->SetVectorParameterValue(TEXT("Color"), TitleWidget->CurrentBackground);
	if (bDone)
	{
		if (CardComp) CardComp->SetVisibility(false);
		if (BgComp)   BgComp->SetVisibility(false);
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

	const bool bWantBg   = (Mode == EQTitleCardMode::Full || Mode == EQTitleCardMode::BackgroundOnly);
	const bool bWantText = (Mode == EQTitleCardMode::Full || Mode == EQTitleCardMode::TextOnly);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		TitleWidget = CreateWidget<UQTitleCard>(PC, UQTitleCard::StaticClass());
		if (TitleWidget)
		{
			TitleWidget->Title           = Title;
			TitleWidget->Subtitle        = Subtitle;
			// The widget never paints the background any more — the sphere does, in every
			// direction. Left on, the plane would still carry the rectangle that gets cut off.
			TitleWidget->bShowBackground = false;
			TitleWidget->bShowText       = bWantText;
			TitleWidget->BackgroundColor = BackgroundColor;
			TitleWidget->TitleColor      = TitleColor;
			TitleWidget->SubtitleColor   = SubtitleColor;
			TitleWidget->StartColor      = StartColor;
			// set BEFORE the component takes the widget — RebuildWidget reads these once
			TitleWidget->TitleFontSize    = TitleFontSize;
			TitleWidget->SubtitleFontSize = SubtitleFontSize;
			TitleWidget->SubtitleGap      = SubtitleGap;

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
			// ── THE SURROUND. Built first so it sorts behind the text. ──────────────────
			if (bWantBg)
			{
				UStaticMesh* Sphere = LoadObject<UStaticMesh>(
					nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
				UMaterialInterface* BgMat = BackgroundMaterial
					? BackgroundMaterial.Get()
					: LoadObject<UMaterialInterface>(nullptr,
						TEXT("/Game/QuantumZoom/ASSETS/materials/M_QZ_CardBG.M_QZ_CardBG"));
				if (Sphere && BgMat)
				{
					BgComp = NewObject<UStaticMeshComponent>(this, TEXT("CardBG"));
					BgComp->RegisterComponent();
					BgComp->SetStaticMesh(Sphere);
					BgComp->SetWorldLocation(CamLoc);
					// the engine sphere is radius 50
					BgComp->SetWorldScale3D(FVector(FMath::Max(BackgroundRadius, CardDistance * 1.5f) / 50.f));
					BgComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					BgComp->SetCastShadow(false);
					BgComp->bReceivesDecals = false;
					BgMID = BgComp->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BgMat);
					// Below the text plane (100) and above the scene's own translucency, so the
					// lab's particles cannot draw through the card.
					BgComp->SetTranslucentSortPriority(90);
					if (BgMID) BgMID->SetVectorParameterValue(TEXT("Color"), StartColor);
				}
				else
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[QTitleCard] surround sphere not built: mesh=%d material=%d"),
						Sphere ? 1 : 0, BgMat ? 1 : 0);
				}
			}

			if (bWantText)
			{
				CardComp = NewObject<UWidgetComponent>(this, TEXT("CardComp"));
				CardComp->RegisterComponent();
				CardComp->SetWidgetSpace(EWidgetSpace::World);
				CardComp->SetDrawSize(FVector2D(7680.f, 4320.f));
				CardComp->SetBlendMode(EWidgetBlendMode::Transparent);
				CardComp->SetTwoSided(true);
				CardComp->SetWidget(TitleWidget);
				const FVector Fwd   = CamRot.Vector();
				const FVector Right = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y);
				const FVector Up    = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Z);
				CardComp->SetWorldLocation(CamLoc + Fwd * CardDistance
				                           + Right * CardOffset.X + Up * CardOffset.Y);
				// widget front faces its +X; turn it back toward the camera
				CardComp->SetWorldRotation((-Fwd).Rotation());
				const float S = CardHeight / 4320.f;
				CardComp->SetWorldScale3D(FVector(S));
				CardComp->SetTranslucentSortPriority(100);   // above the scene, like the old Z-order 99
			}
			TitleWidget->bExternallyDriven = true;        // this actor's Tick owns the clock
		}
	}
}
