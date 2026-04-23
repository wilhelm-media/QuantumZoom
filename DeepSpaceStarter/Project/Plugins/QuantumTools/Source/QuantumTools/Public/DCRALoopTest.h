#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCRALoopTest.generated.h"

/**
 * DCRA Loop Test Actor
 *
 * Drop this into any level. On BeginPlay it finds the DisplayClusterRootActor
 * and drives it on a sine loop so you can confirm DCRA-based navigation works
 * in the nDisplay cluster before wiring up real controller input.
 *
 * Primary node only — secondary nodes receive the updated position via
 * nDisplay's built-in replication. Works in PIE/standalone too.
 *
 * Remove or set bActive=false before shipping.
 */
UCLASS()
class QUANTUMTOOLS_API ADCRALoopTest : public AActor
{
    GENERATED_BODY()

public:
    ADCRALoopTest();

    /** Enable/disable without removing from level */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    bool bActive = true;

    /** Peak displacement in cm (default 300cm = 3m) */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    float Amplitude = 300.f;

    /** Seconds per full back-and-forth cycle */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    float CycleDuration = 6.f;

    /** Direction of movement in world space — gets normalized at runtime */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    FVector Axis = FVector(1.f, 0.f, 0.f);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    AActor* DCRA = nullptr;
    FVector OriginLocation = FVector::ZeroVector;
    float Time = 0.f;
    bool bIsPrimary = false;
};
