#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCRALoopTest.generated.h"

/**
 * DCRA Loop Test Actor
 *
 * Place freely in the level — placed position = center of oscillation.
 * A sphere shows the actor moving. The DCRA is synced to the same position
 * each frame so you can tell whether nDisplay picks up DCRA movement at runtime.
 *
 * Two outcomes:
 *   Sphere moves, view shifts   → DCRA-driven navigation works, wire controller the same way
 *   Sphere moves, view static   → nDisplay ignores runtime DCRA moves, navigation must move scene content instead
 *   Nothing moves               → actor not running or DCRA not found, check Output Log
 */
UCLASS()
class QUANTUMTOOLS_API ADCRALoopTest : public AActor
{
    GENERATED_BODY()

public:
    ADCRALoopTest();

    /** Kill switch — disable without removing from level */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    bool bActive = true;

    /** Peak displacement in cm */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    float Amplitude = 300.f;

    /** Seconds per full back-and-forth cycle */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    float CycleDuration = 6.f;

    /** Movement direction in world space — normalized at runtime */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    FVector Axis = FVector(1.f, 0.f, 0.f);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    UPROPERTY()
    UStaticMeshComponent* SphereMesh = nullptr;

    AActor* DCRA = nullptr;
    FVector Origin = FVector::ZeroVector;
    float Time = 0.f;
    bool bIsPrimary = false;
};
