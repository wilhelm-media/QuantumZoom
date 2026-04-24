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
    if (DCRA)
    {
        UE_LOG(LogTemp, Log, TEXT("[QZoomTest] DCRA found: %s"), *DCRA->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] No DisplayClusterRootActor found - zoom disabled"));
        SetActorTickEnabled(false);
    }
}

void AQZoomTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!DCRA) return;

    if (bTransitioning)
    {
        TickTransition(DeltaTime);
    }
    else
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
    const FVector Delta = ZoomAxis.GetSafeNormal() * ZoomSpeed * DeltaTime * Dir;
    DCRA->SetActorLocation(DCRA->GetActorLocation() + Delta);
}

void AQZoomTest::HandleDPadInput()
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;

    const bool bLeft  = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left);
    const bool bRight = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right);

    if (bLeft && !bDPadLeftPrev)
    {
        if (IsValid(CameraA) && SequenceA.IsValid())
        {
            StartTransition(CameraA, SequenceA);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] CameraA or SequenceA not assigned"));
        }
    }

    if (bRight && !bDPadRightPrev)
    {
        if (IsValid(CameraB) && SequenceB.IsValid())
        {
            StartTransition(CameraB, SequenceB);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] CameraB or SequenceB not assigned"));
        }
    }

    bDPadLeftPrev  = bLeft;
    bDPadRightPrev = bRight;
}

void AQZoomTest::StartTransition(ACineCameraActor* TargetCamera, TSoftObjectPtr<ULevelSequence> Sequence)
{
    if (!IsValid(TargetCamera) || !DCRA) return;

    PendingCamera   = TargetCamera;
    PendingSequence = Sequence;

    TransitionStart = DCRA->GetActorTransform();
    TransitionEnd   = TargetCamera->GetActorTransform();
    TransitionAlpha = 0.f;
    bTransitioning  = true;

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Transition started -> %s"), *TargetCamera->GetName());
}

void AQZoomTest::TickTransition(float DeltaTime)
{
    TransitionAlpha = FMath::Clamp(TransitionAlpha + DeltaTime / TransitionDuration, 0.f, 1.f);

    const float T = FMath::InterpEaseInOut(0.f, 1.f, TransitionAlpha, TransitionExponent);

    const FVector Loc = FMath::Lerp(TransitionStart.GetLocation(), TransitionEnd.GetLocation(), T);
    const FQuat   Rot = FQuat::Slerp(TransitionStart.GetRotation(), TransitionEnd.GetRotation(), T);

    DCRA->SetActorLocationAndRotation(Loc, Rot);

    if (TransitionAlpha >= 1.f)
    {
        CompleteTransition();
    }
}

void AQZoomTest::CompleteTransition()
{
    bTransitioning = false;

    if (IsValid(PendingCamera) && IsValid(DCRA))
    {
        DCRA->AttachToComponent(
            PendingCamera->GetRootComponent(),
            FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, false)
        );
        UE_LOG(LogTemp, Log, TEXT("[QZoomTest] DCRA attached to %s"), *PendingCamera->GetName());
    }

    ULevelSequence* Seq = PendingSequence.LoadSynchronous();
    if (IsValid(Seq))
    {
        FMovieSceneSequencePlaybackSettings Settings;
        Settings.bAutoPlay = true;
        ALevelSequenceActor* OutActor = nullptr;
        SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(), Seq, Settings, OutActor);
        SequenceActor = OutActor;
        if (SequencePlayer)
        {
            SequencePlayer->Play();
            UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Sequence '%s' playing"), *Seq->GetName());
        }
    }

    PendingCamera = nullptr;
}
