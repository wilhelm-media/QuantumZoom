#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "DCRALoopTest.generated.h"

UCLASS()
class QUANTUMTOOLS_API ADCRALoopTest : public AActor
{
    GENERATED_BODY()

public:
    ADCRALoopTest();

    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    FVector OscillationCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    bool bActive = true;

    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    float Amplitude = 300.f;

    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    float CycleDuration = 6.f;

    UPROPERTY(EditAnywhere, Category = "DCRA Loop")
    FVector Axis = FVector(1.f, 0.f, 0.f);

    UPROPERTY(EditAnywhere, Category = "DCRA Loop|Sphere")
    bool bShowSphere = true;

    UPROPERTY(EditAnywhere, Category = "DCRA Loop|Sphere", meta = (ClampMin = "1.0"))
    float SphereSize = 100.f;

    UPROPERTY(EditAnywhere, Category = "DCRA Loop|Sphere")
    FLinearColor SphereColor = FLinearColor(1.f, 0.3f, 0.f, 1.f);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    void OnClusterEventJson(const FDisplayClusterClusterEventJson& Event);

private:
    UPROPERTY()
    UStaticMeshComponent* SphereMesh = nullptr;

    AActor* DCRA = nullptr;
    FVector Origin = FVector::ZeroVector;
    float Time = 0.f;
    bool bIsPrimary = false;

    static const FString ClusterEventName;

    FOnClusterEventJsonListener ClusterEventDelegate;
};
