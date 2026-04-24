#include "DCRALoopTest.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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
    SphereMesh->SetCastShadow(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshAsset(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (MeshAsset.Succeeded())
    {
        SphereMesh->SetStaticMesh(MeshAsset.Object);
    }

    static ConstructorHelpers::FObjectFinder<UMaterial> MatAsset(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (MatAsset.Succeeded())
    {
        SphereMesh->SetMaterial(0, MatAsset.Object);
    }
}

void ADCRALoopTest::BeginPlay()
{
    Super::BeginPlay();

    // Apply sphere visibility and size from properties
    SphereMesh->SetVisibility(bShowSphere);
    const float Scale = SphereSize / 100.f; // mesh default = 100cm diameter
    SphereMesh->SetRelativeScale3D(FVector(Scale));

    // Create dynamic material and apply color
    UMaterialInterface* BaseMat = SphereMesh->GetMaterial(0);
    if (BaseMat)
    {
        SphereMat = UMaterialInstanceDynamic::Create(BaseMat, this);
        SphereMesh->SetMaterial(0, SphereMat);
        SphereMat->SetVectorParameterValue(TEXT("Color"), SphereColor);
    }

    if (!bActive)
    {
        SetActorTickEnabled(false);
        return;
    }

    // Primary node drives movement — secondary nodes receive DCRA position
    // via nDisplay replication. PIE / standalone = always primary.
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

    // Placed position = center of oscillation
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

    const float Angle = (CycleDuration > 0.f) ? (Time / CycleDuration) * TWO_PI : 0.f;
    const FVector NewLocation = Origin + Axis.GetSafeNormal() * Amplitude * FMath::Sin(Angle);

    SetActorLocation(NewLocation);

    if (DCRA)
    {
        DCRA->SetActorLocation(NewLocation);
    }
}
