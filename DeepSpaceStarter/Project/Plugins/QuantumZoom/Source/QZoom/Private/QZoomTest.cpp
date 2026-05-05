#include "QZoomTest.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "LevelSequence.h"
#include "CineCameraActor.h"
#include "GameFramework/PlayerController.h"

const FString AQZoomTest::ClusterEventName = TEXT("QZoom.DCRATransform");

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
        // ALL nodes listen for DCRA transform events
        IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(this);
    }
    else
    {
        bIsPrimary = true;
    }

    // Secondary nodes don't need to tick — they receive via cluster events
    if (!bIsPrimary)
    {
        SetActorTickEnabled(false);
    }

    DCRA = UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass());
    if (!DCRA)
    {
        UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] No DisplayClusterRootActor found"));
        SetActorTickEnabled(false);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] DCRA: %s | Primary: %s"),
        *DCRA->GetName(), bIsPrimary ? TEXT("YES") : TEXT("NO"));

    // Bind AudioListener to DCRA so spatialized sound follows the camera
    if (bIsPrimary)
    {
        APlayerController* PC = GetWorld()->GetFirstPlayerController();
        if (PC)
        {
            PC->SetAudioListenerOverride(
                DCRA->GetRootComponent(),
                FVector::ZeroVector,
                FRotator::ZeroRotator
            );
            UE_LOG(LogTemp, Log, TEXT("[QZoomTest] AudioListener bound to DCRA"));
        }
    }

    if (ZoomHomeTransform.Equals(FTransform::Identity))
    {
        ZoomHomeTransform = DCRA->GetActorTransform();
    }
}

void AQZoomTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (IDisplayCluster::IsAvailable())
    {
        IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(this);
    }
    Super::EndPlay(EndPlayReason);
}

void AQZoomTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bTransitioning)
        TickTransition(DeltaTime);
    else if (!bInCinematicMode)
        HandleZoom(DeltaTime);

    HandleDPadInput();
}

// ─── Cluster Event ────────────────────────────────────────────────────────────

void AQZoomTest::BroadcastDCRATransform(const FVector& Loc, const FQuat& Rot)
{
    if (!IDisplayCluster::IsAvailable())
    {
        ApplyDCRATransform(Loc, Rot);
        return;
    }

    FDisplayClusterClusterEventJson Event;
    Event.Name     = ClusterEventName;
    Event.Type     = TEXT("Transform");
    Event.Category = TEXT("QZoom");
    Event.bShouldDiscardOnRepeat = false;

    Event.Parameters.Add(TEXT("X"),  FString::SanitizeFloat(Loc.X));
    Event.Parameters.Add(TEXT("Y"),  FString::SanitizeFloat(Loc.Y));
    Event.Parameters.Add(TEXT("Z"),  FString::SanitizeFloat(Loc.Z));
    Event.Parameters.Add(TEXT("QX"), FString::SanitizeFloat(Rot.X));
    Event.Parameters.Add(TEXT("QY"), FString::SanitizeFloat(Rot.Y));
    Event.Parameters.Add(TEXT("QZ"), FString::SanitizeFloat(Rot.Z));
    Event.Parameters.Add(TEXT("QW"), FString::SanitizeFloat(Rot.W));

    // false = broadcast to all nodes including this one
    IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQZoomTest::OnClusterEventJson(const FDisplayClusterClusterEventJson& Event)
{
    if (Event.Name != ClusterEventName || !DCRA) return;

    const float X  = FCString::Atof(*Event.Parameters.FindRef(TEXT("X")));
    const float Y  = FCString::Atof(*Event.Parameters.FindRef(TEXT("Y")));
    const float Z  = FCString::Atof(*Event.Parameters.FindRef(TEXT("Z")));
    const float QX = FCString::Atof(*Event.Parameters.FindRef(TEXT("QX")));
    const float QY = FCString::Atof(*Event.Parameters.FindRef(TEXT("QY")));
    const float QZ = FCString::Atof(*Event.Parameters.FindRef(TEXT("QZ")));
    const float QW = FCString::Atof(*Event.Parameters.FindRef(TEXT("QW")));

    ApplyDCRATransform(FVector(X, Y, Z), FQuat(QX, QY, QZ, QW));
}

void AQZoomTest::ApplyDCRATransform(const FVector& Loc, const FQuat& Rot)
{
    if (DCRA) DCRA->SetActorLocationAndRotation(Loc, Rot);
}

// ─── Zoom ─────────────────────────────────────────────────────────────────────

void AQZoomTest::HandleZoom(float DeltaTime)
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;

    const bool bZoomIn  = PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder);
    const bool bZoomOut = PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder);
    if (!bZoomIn && !bZoomOut) return;

    const float Dir = bZoomIn ? 1.f : -1.f;
    const FVector NewLoc = DCRA->GetActorLocation() + ZoomAxis.GetSafeNormal() * ZoomSpeed * DeltaTime * Dir;
    BroadcastDCRATransform(NewLoc, DCRA->GetActorQuat());
}

// ─── D-Pad ────────────────────────────────────────────────────────────────────

void AQZoomTest::HandleDPadInput()
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;

    const bool bLeft  = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left);
    const bool bRight = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right);
    const bool bUp    = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up);
    const bool bDown  = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down);

    if (bLeft  && !bDPadLeftPrev  && !bTransitioning) { IsValid(CameraA) && SequenceA.IsValid() ? StartTransition(CameraA, SequenceA) : UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] CameraA/SequenceA not set")); }
    if (bRight && !bDPadRightPrev && !bTransitioning) { IsValid(CameraB) && SequenceB.IsValid() ? StartTransition(CameraB, SequenceB) : UE_LOG(LogTemp, Warning, TEXT("[QZoomTest] CameraB/SequenceB not set")); }
    if (bUp    && !bDPadUpPrev    && !bTransitioning) { StartReturnToZoom(); }
    if (bDown  && !bDPadDownPrev  && !bTransitioning) { ResetToHome(); }

    bDPadLeftPrev  = bLeft;
    bDPadRightPrev = bRight;
    bDPadUpPrev    = bUp;
    bDPadDownPrev  = bDown;
}

// ─── Transitions ──────────────────────────────────────────────────────────────

void AQZoomTest::StartTransition(ACineCameraActor* TargetCamera, TSoftObjectPtr<ULevelSequence> Sequence)
{
    if (!IsValid(TargetCamera) || !DCRA) return;

    PendingCamera       = TargetCamera;
    PendingSequence     = Sequence;
    TransitionStart     = DCRA->GetActorTransform();
    TransitionEnd       = TargetCamera->GetActorTransform();
    TransitionAlpha     = 0.f;
    bTransitioning      = true;
    bIsReturnTransition = false;

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Transition -> %s"), *TargetCamera->GetName());
}

void AQZoomTest::StartReturnToZoom()
{
    if (!DCRA) return;

    StopActiveSequence();
    DCRA->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    TransitionStart     = DCRA->GetActorTransform();
    TransitionEnd       = ZoomHomeTransform;
    TransitionAlpha     = 0.f;
    bTransitioning      = true;
    bIsReturnTransition = true;
    bInCinematicMode    = false;

    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Returning to ZoomCam"));
}

void AQZoomTest::ResetToHome()
{
    if (!DCRA) return;

    StopActiveSequence();
    DCRA->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    bInCinematicMode = false;
    bTransitioning   = false;

    BroadcastDCRATransform(ZoomHomeTransform.GetLocation(), ZoomHomeTransform.GetRotation());
    UE_LOG(LogTemp, Log, TEXT("[QZoomTest] Reset to home"));
}

void AQZoomTest::StopActiveSequence()
{
    if (IsValid(SequencePlayer) && SequencePlayer->IsPlaying())
        SequencePlayer->Stop();
}

void AQZoomTest::TickTransition(float DeltaTime)
{
    TransitionAlpha = FMath::Clamp(TransitionAlpha + DeltaTime / TransitionDuration, 0.f, 1.f);

    const float T   = FMath::InterpEaseInOut(0.f, 1.f, TransitionAlpha, TransitionExponent);
    const FVector L = FMath::Lerp(TransitionStart.GetLocation(), TransitionEnd.GetLocation(), T);
    const FQuat   R = FQuat::Slerp(TransitionStart.GetRotation(), TransitionEnd.GetRotation(), T);

    BroadcastDCRATransform(L, R);

    if (TransitionAlpha >= 1.f) CompleteTransition();
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

    if (IsValid(PendingCamera))
    {
        DCRA->AttachToComponent(
            PendingCamera->GetRootComponent(),
            FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, false)
        );
        bInCinematicMode = true;
        UE_LOG(LogTemp, Log, TEXT("[QZoomTest] DCRA attached to %s"), *PendingCamera->GetName());
    }

    ULevelSequence* Seq = PendingSequence.LoadSynchronous();
    if (IsValid(Seq))
    {
        FMovieSceneSequencePlaybackSettings Settings;
        Settings.bAutoPlay = true;
        ALevelSequenceActor* OutActor = nullptr;
        SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(), Seq, Settings, OutActor);
        SequenceActor  = OutActor;
        if (SequencePlayer) SequencePlayer->Play();
    }

    PendingCamera = nullptr;
}
