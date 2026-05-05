#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cluster/IDisplayClusterClusterEventListener.h"
#include "QZoomTest.generated.h"

class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class ACineCameraActor;

/**
 * QZoomTest
 *
 * R1 (hold)       = Zoom In
 * L1 (hold)       = Zoom Out
 * D-Pad Left      = Transition to SequenceA + CameraA
 * D-Pad Right     = Transition to SequenceB + CameraB
 * D-Pad Up        = Smooth return to ZoomCam
 * D-Pad Down      = Instant reset to home
 *
 * DCRA position is broadcast via nDisplay Cluster Events so all
 * nodes (Wall + Floor) stay in sync.
 */
UCLASS()
class QZOOM_API AQZoomTest : public AActor, public IDisplayClusterClusterEventListener
{
    GENERATED_BODY()

public:
    AQZoomTest();

    // --- Zoom ---

    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    FVector ZoomAxis = FVector(1.f, 0.f, 0.f);

    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    float ZoomSpeed = 200.f;

    /** Free movement speed in cm/s (Left Trigger held + sticks) */
    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    float FreeMoveSpeed = 400.f;

    /** Yaw rotation speed in degrees/s (Left Trigger held + Right Stick X) */
    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    float FreeRotateSpeed = 60.f;

    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    FTransform ZoomHomeTransform;

    // --- Sequences ---

    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TSoftObjectPtr<ULevelSequence> SequenceA;

    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TObjectPtr<ACineCameraActor> CameraA;

    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TSoftObjectPtr<ULevelSequence> SequenceB;

    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TObjectPtr<ACineCameraActor> CameraB;

    // --- Transition ---

    UPROPERTY(EditAnywhere, Category="QZoom|Transition", meta=(ClampMin="0.1"))
    float TransitionDuration = 1.5f;

    UPROPERTY(EditAnywhere, Category="QZoom|Transition", meta=(ClampMin="1.0", ClampMax="5.0"))
    float TransitionExponent = 2.f;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    // IDisplayClusterClusterEventListener — fires on ALL nodes
    virtual void OnClusterEventJson(const FDisplayClusterClusterEventJson& Event) override;

private:
    void HandleZoom(float DeltaTime);
    void HandleFreeMovement(float DeltaTime);
    void HandleDPadInput();
    void StartTransition(ACineCameraActor* TargetCamera, TSoftObjectPtr<ULevelSequence> Sequence);
    void StartReturnToZoom();
    void ResetToHome();
    void StopActiveSequence();
    void TickTransition(float DeltaTime);
    void CompleteTransition();

    /** Broadcast new DCRA transform to all cluster nodes */
    void BroadcastDCRATransform(const FVector& Loc, const FQuat& Rot);
    /** Apply transform to local DCRA — called on all nodes via cluster event */
    void ApplyDCRATransform(const FVector& Loc, const FQuat& Rot);

    static const FString ClusterEventName;

    AActor* DCRA = nullptr;
    bool bIsPrimary = false;
    bool bInCinematicMode = false;

    bool bDPadLeftPrev  = false;
    bool bDPadRightPrev = false;
    bool bDPadUpPrev    = false;
    bool bDPadDownPrev  = false;

    bool bTransitioning      = false;
    bool bIsReturnTransition = false;
    float TransitionAlpha    = 0.f;
    FTransform TransitionStart;
    FTransform TransitionEnd;

    ACineCameraActor* PendingCamera = nullptr;
    TSoftObjectPtr<ULevelSequence> PendingSequence;

    UPROPERTY()
    TObjectPtr<ULevelSequencePlayer> SequencePlayer = nullptr;

    UPROPERTY()
    TObjectPtr<ALevelSequenceActor> SequenceActor = nullptr;
};
