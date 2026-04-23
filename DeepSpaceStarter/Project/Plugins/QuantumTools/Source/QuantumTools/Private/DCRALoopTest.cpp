#include "DCRALoopTest.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"

ADCRALoopTest::ADCRALoopTest()
{
    PrimaryActorTick.bCanEverTick = true;

    SphereMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereMesh"));
    RootComponent = SphereMesh;
    SphereMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (MeshAsset.Succeeded())
    {
        SphereMesh->SetStaticMesh(MeshAsset.Object);
        SphereMesh->SetRelativeScale3D(FVector(0.5f));
    }
}

void ADCRALoopTest::BeginPlay()
{
    Super::BeginPlay();

    if (!bActive)
    {
        SetActorTickEnabled(false);
        return;
    }

    // Only the primary node drives movement.
    // Secondary nodes receive DCRA position via nDisplay replication.
    // In PIE / standalone there is no cluster manager — always act as primary.
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

    // Placed position in the level = center of oscillation
    Origin = GetActorLocation();

    DCRA = UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass());

    if (DCRA)
    {
        UE_LOG(LogTemp, Log, TEXT("[DCRALoopTest] DCRA found at %s | Sphere origin: %s"),
            *DCRA->GetActorLocation().ToString(), *Origin.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DCRALoopTest] No DisplayClusterRootActor found — sphere moves, DCRA sync disabled"));
    }
}

void ADCRALoopTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    Time += DeltaTime;

    const float Angle  = (CycleDuration > 0.f) ? (Time / CycleDuration) * TWO_PI : 0.f;
    const FVector NewLocation = Origin + Axis.GetSafeNormal() * Amplitude * FMath::Sin(Angle);

    // Move the sphere — always visible as ground truth that the actor is running
    SetActorLocation(NewLocation);

    // Sync DCRA to the same position
    if (DCRA)
    {
        DCRA->SetActorLocation(NewLocation);
    }
}
