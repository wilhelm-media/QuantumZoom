#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
 * D-Pad Up        = Smooth return to ZoomCam (detach from cinematic, lerp to home)
 * D-Pad Down      = Instant reset to home position
 */
UCLASS()
class QZOOM_API AQZoomTest : public AActor
{
    GENERATED_BODY()

public:
    AQZoomTest();

    // --- Zoom ---

    /** World axis to zoom along */
    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    FVector ZoomAxis = FVector(1.f, 0.f, 0.f);

    /** Movement speed in cm/s while shoulder button is held */
    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    float ZoomSpeed = 200.f;

    /**
     * Home transform the DCRA returns to on D-Pad Up/Down.
     * If left at default (0,0,0) it is captured from the DCRA at BeginPlay.
     */
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

    /** Lerp duration in seconds (used for both cinematic and return transitions) */
    UPROPERTY(EditAnywhere, Category="QZoom|Transition", meta=(ClampMin="0.1"))
    float TransitionDuration = 1.5f;

    /** Easing exponent: 1=linear, 2=quadratic, 3=cubic */
    UPROPERTY(EditAnywhere, Category="QZoom|Transition", meta=(ClampMin="1.0", ClampMax="5.0"))
    float TransitionExponent = 2.f;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    void HandleZoom(float DeltaTime);
    void HandleDPadInput();
    void StartTransition(ACineCameraActor* TargetCamera, TSoftObjectPtr<ULevelSequence> Sequence);
    void StartReturnToZoom();
    void ResetToHome();
    void StopActiveSequence();
    void TickTransition(float DeltaTime);
    void CompleteTransition();

    AActor* DCRA = nullptr;
    bool bIsPrimary = false;
    bool bInCinematicMode = false;

    bool bDPadLeftPrev  = false;
    bool bDPadRightPrev = false;
    bool bDPadUpPrev    = false;
    bool bDPadDownPrev  = false;

    bool bTransitioning     = false;
    bool bIsReturnTransition = false;
    float TransitionAlpha   = 0.f;
    FTransform TransitionStart;
    FTransform TransitionEnd;

    ACineCameraActor* PendingCamera = nullptr;
    TSoftObjectPtr<ULevelSequence> PendingSequence;

    UPROPERTY()
    TObjectPtr<ULevelSequencePlayer> SequencePlayer = nullptr;

    UPROPERTY()
    TObjectPtr<ALevelSequenceActor> SequenceActor = nullptr;
};
