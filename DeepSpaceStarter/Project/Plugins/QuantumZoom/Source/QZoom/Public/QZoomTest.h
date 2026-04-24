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
 * Drop into level. Assign sequences + cameras in Details panel.
 *
 * R1 (hold)       = Zoom In  — moves DCRA along +ZoomAxis
 * L1 (hold)       = Zoom Out — moves DCRA along -ZoomAxis
 * D-Pad Left      = Transition to SequenceA + CameraA
 * D-Pad Right     = Transition to SequenceB + CameraB
 *
 * Transition: DCRA lerps to target camera → attaches → sequence plays.
 * DCRA found by tag "NDisplayRoot" (set this tag on your DCRA in the level).
 */
UCLASS()
class QZOOM_API AQZoomTest : public AActor
{
    GENERATED_BODY()

public:
    AQZoomTest();

    // --- Zoom ---

    /** World axis to zoom along — normalized at runtime */
    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    FVector ZoomAxis = FVector(1.f, 0.f, 0.f);

    /** Movement speed in cm/s while shoulder button is held */
    UPROPERTY(EditAnywhere, Category="QZoom|Zoom")
    float ZoomSpeed = 200.f;

    // --- Sequences ---

    /** Level Sequence for D-Pad Left */
    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TSoftObjectPtr<ULevelSequence> SequenceA;

    /** Camera actor for Sequence A — DCRA attaches to this on transition */
    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TObjectPtr<ACineCameraActor> CameraA;

    /** Level Sequence for D-Pad Right */
    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TSoftObjectPtr<ULevelSequence> SequenceB;

    /** Camera actor for Sequence B */
    UPROPERTY(EditAnywhere, Category="QZoom|Sequences")
    TObjectPtr<ACineCameraActor> CameraB;

    // --- Transition ---

    /** Seconds for DCRA to lerp from current position to target camera */
    UPROPERTY(EditAnywhere, Category="QZoom|Transition", meta=(ClampMin="0.1"))
    float TransitionDuration = 1.5f;

    /** Easing exponent: 1=linear, 2=quadratic, 3=cubic */
    UPROPERTY(EditAnywhere, Category="QZoom|Transition", meta=(ClampMin="1.0", ClampMax="5.0"))
    float TransitionExponent = 2.f;

    /** Actor tag used to find the DisplayClusterRootActor in the level */
    UPROPERTY(EditAnywhere, Category="QZoom|Transition")
    FName DCRATag = "NDisplayRoot";

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    void HandleZoom(float DeltaTime);
    void HandleDPadInput();
    void StartTransition(ACineCameraActor* TargetCamera, TSoftObjectPtr<ULevelSequence> Sequence);
    void TickTransition(float DeltaTime);
    void CompleteTransition();

    AActor* DCRA = nullptr;
    bool bIsPrimary = false;

    bool bDPadLeftPrev  = false;
    bool bDPadRightPrev = false;

    bool bTransitioning = false;
    float TransitionAlpha = 0.f;
    FTransform TransitionStart;
    FTransform TransitionEnd;

    ACineCameraActor* PendingCamera = nullptr;
    TSoftObjectPtr<ULevelSequence> PendingSequence;

    UPROPERTY()
    TObjectPtr<ULevelSequencePlayer> SequencePlayer = nullptr;

    UPROPERTY()
    TObjectPtr<ALevelSequenceActor> SequenceActor = nullptr;
};
