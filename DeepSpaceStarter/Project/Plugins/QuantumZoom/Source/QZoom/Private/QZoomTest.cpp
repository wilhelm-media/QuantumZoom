#include "QZoomTest.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "LevelSequence.h"
#include "CineCameraActor.h"
#include "GameFramework/PlayerController.h"

AQZoomTest::AQZoomTest()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AQZoomTest::BeginPlay()
{
    Super::BeginPlay();

    if (IDisplayCluster::IsAvailable())
    {
        bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
    }
    else
    {
        bIsPrimary = true;
    }

    if (!bIsPrimary)
    {
        SetActorTickEnabled(false);
        return;
    }

    DCRA = UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass());
    if (!DCRA)
    {
        UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] No DisplayClusterRootActor found - disabled"));
        SetActorTickEnabled(false);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] DCRA found: %s"), *DCRA->GetName());

    // Capture home transform from DCRA if not set manually
    if (ZoomHomeTransform.Equals(FTransform::Identity))
    {
        ZoomHomeTransform = DCRA->GetActorTransform();
    }
}

void AQZoomTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bTransitioning)
    {
        TickTransition(DeltaTime);
    }
    else if (!bInCinematicMode)
    {
        HandleZoom(DeltaTime);
    }

    HandleDPadInput();
}

void AQZoomTest::HandleZoom(float DeltaTime)
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;

    const bool bZoomIn  = PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder);
    const bool bZoomOut = PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder);

    if (!bZoomIn && !bZoomOut) return;

    const float Dir = bZoomIn ? 1.f : -1.f;
    DCRA->SetActorLocation(DCRA->GetActorLocation() + ZoomAxis.GetSafeNormal() * ZoomSpeed * DeltaTime * Dir);
}

void AQZoomTest::HandleDPadInput()
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;

    const bool bLeft  = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left);
    const bool bRight = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right);
    const bool bUp    = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up);
    const bool bDown  = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down);

    if (bLeft && !bDPadLeftPrev && !bTransitioning)
    {
        if (IsValid(CameraA) && SequenceA.IsValid())
            StartTransition(CameraA, SequenceA);
        else
            UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] CameraA or SequenceA not assigned"));
    }

    if (bRight && !bDPadRightPrev && !bTransitioning)
    {
        if (IsValid(CameraB) && SequenceB.IsValid())
            StartTransition(CameraB, SequenceB);
        else
            UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] CameraB or SequenceB not assigned"));
    }

    if (bUp && !bDPadUpPrev && !bTransitioning)
    {
        StartReturnToZoom();
    }

    if (bDown && !bDPadDownPrev && !bTransitioning)
    {
        ResetToHome();
    }

    bDPadLeftPrev  = bLeft;
    bDPadRightPrev = bRight;
    bDPadUpPrev    = bUp;
    bDPadDownPrev  = bDown;
}

void AQZoomTest::StartTransition(ACineCameraActor* TargetCamera, TSoftObjectPtr<ULevelSequence> Sequence)
{
    if (!IsValid(TargetCamera) || !DCRA) return;

    PendingCamera        = TargetCamera;
    PendingSequence      = Sequence;
    TransitionStart      = DCRA->GetActorTransform();
    TransitionEnd        = TargetCamera->GetActorTransform();
    TransitionAlpha      = 0.f;
    bTransitioning       = true;
    bIsReturnTransition  = false;

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Transition -> %s"), *TargetCamera->GetName());
}

void AQZoomTest::StartReturnToZoom()
{
    if (!DCRA) return;

    StopActiveSequence();
    DCRA->DetachFromActor(FDetachmentTransformRules::KeepWorldTransformRules);

    TransitionStart      = DCRA->GetActorTransform();
    TransitionEnd        = ZoomHomeTransform;
    TransitionAlpha      = 0.f;
    bTransitioning       = true;
    bIsReturnTransition  = true;
    bInCinematicMode     = false;

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Returning to ZoomCam"));
}

void AQZoomTest::ResetToHome()
{
    if (!DCRA) return;

    StopActiveSequence();
    DCRA->DetachFromActor(FDetachmentTransformRules::KeepWorldTransformRules);
    DCRA->SetActorTransform(ZoomHomeTransform);
    bInCinematicMode = false;
    bTransitioning   = false;

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Reset to home"));
}

void AQZoomTest::StopActiveSequence()
{
    if (IsValid(SequencePlayer) && SequencePlayer->IsPlaying())
    {
        SequencePlayer->Stop();
    }
}

void AQZoomTest::TickTransition(float DeltaTime)
{
    TransitionAlpha = FMath::Clamp(TransitionAlpha + DeltaTime / TransitionDuration, 0.f, 1.f);

    const float T   = FMath::InterpEaseInOut(0.f, 1.f, TransitionAlpha, TransitionExponent);
    const FVector  Loc = FMath::Lerp(TransitionStart.GetLocation(), TransitionEnd.GetLocation(), T);
    const FQuat    Rot = FQuat::Slerp(TransitionStart.GetRotation(), TransitionEnd.GetRotation(), T);

    DCRA->SetActorLocationAndRotation(Loc, Rot);

    if (TransitionAlpha >= 1.f)
    {
        CompleteTransition();
    }
}

void AQZoomTest::CompleteTransition()
{
    bTransitioning = false;

    if (bIsReturnTransition)
    {
        bIsReturnTransition = false;
        UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Back in ZoomCam mode"));
        return;
    }

    // Cinematic transition complete — attach DCRA to camera
    if (IsValid(PendingCamera))
    {
        DCRA->AttachToComponent(
            PendingCamera->GetRootComponent(),
            FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, false)
        );
        bInCinematicMode = true;
        UE_LOG(LogTemp, Log, TEXT("[QZoomTest] DCRA attached to %s"), *PendingCamera->GetName());
    }

    // Play sequence
    ULevelSequence* Seq = PendingSequence.LoadSynchronous();
    if (IsValid(Seq))
    {
        FMovieSceneSequencePlaybackSettings Settings;
        Settings.bAutoPlay = true;
        ALevelSequenceActor* OutActor = nullptr;
        SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(), Seq, Settings, OutActor);
        SequenceActor  = OutActor;
        if (SequencePlayer)
        {
            SequencePlayer->Play();
            UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Sequence '%s' playing"), *Seq->GetName());
        }
    }

    PendingCamera = nullptr;
}
