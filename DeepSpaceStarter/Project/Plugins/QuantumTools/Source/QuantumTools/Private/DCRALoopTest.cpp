#include "DCRALoopTest.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"

ADCRALoopTest::ADCRALoopTest()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ADCRALoopTest::BeginPlay()
{
    Super::BeginPlay();

    if (!bActive)
    {
        SetActorTickEnabled(false);
        return;
    }

    // In a live nDisplay cluster, only the primary node runs game logic.
    // Secondary nodes get DCRA position updates via nDisplay replication.
    // In PIE / standalone there is no cluster — treat as primary.
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
        OriginLocation = DCRA->GetActorLocation();
        UE_LOG(LogTemp, Log, TEXT("[DCRALoopTest] DCRA found at %s — loop active"), *OriginLocation.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DCRALoopTest] No DisplayClusterRootActor in level — nothing to drive"));
        SetActorTickEnabled(false);
    }
}

void ADCRALoopTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    Time += DeltaTime;

    const float Angle = (CycleDuration > 0.f) ? (Time / CycleDuration) * TWO_PI : 0.f;
    const FVector Offset = Axis.GetSafeNormal() * Amplitude * FMath::Sin(Angle);

    DCRA->SetActorLocation(OriginLocation + Offset);
}
