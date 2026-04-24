#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DCRALoopTest.generated.h"

UCLASS()
class QUANTUMTOOLS_API ADCRALoopTest : public AActor
{
    GENERATED_BODY()

public:
    ADCRALoopTest();

    // --- Origin ---

    /** Center of oscillation in world space (cm). Overrides the actor's placed position. */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    FVector OscillationCenter = FVector::ZeroVector;

    // --- Loop ---

    /** Disable without removing from level */
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

    // --- Sphere ---

    /** Show or hide the debug sphere */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop|Sphere")
    bool bShowSphere = true;

    /** Sphere diameter in cm */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop|Sphere", meta = (ClampMin = "1.0"))
    float SphereSize = 100.f;

    /** Sphere color */
    UPROPERTY(EditAnywhere, Category = "DCRA Loop|Sphere")
    FLinearColor SphereColor = FLinearColor(1.f, 0.3f, 0.f, 1.f);

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
