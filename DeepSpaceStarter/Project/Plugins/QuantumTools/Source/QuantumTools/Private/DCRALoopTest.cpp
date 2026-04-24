#include "DCRALoopTest.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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
}

void ADCRALoopTest::BeginPlay()
{
    Super::BeginPlay();

    // --- Build sphere at runtime (more reliable than constructor helpers in nDisplay) ---
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Mesh)
    {
        SphereMesh->SetStaticMesh(Mesh);
        UE_LOG(LogTemp, Log, TEXT("[DCRALoopTest] Sphere mesh loaded OK"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DCRALoopTest] Sphere mesh load FAILED"));
    }

    UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (BaseMat)
    {
        UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, this);
        DynMat->SetVectorParameterValue(TEXT("Color"), SphereColor);
        SphereMesh->SetMaterial(0, DynMat);
    }

    const float Scale = FMath::Max(SphereSize, 1.f) / 100.f;
    SphereMesh->SetRelativeScale3D(FVector(Scale));
    SphereMesh->SetVisibility(bShowSphere, true);

    // --- Cluster role ---
    if (IDisplayCluster::IsAvailable())
    {
        bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
    }
    else
    {
        bIsPrimary = true;
    }

    if (!bActive || !bIsPrimary)
    {
        SetActorTickEnabled(false);
        return;
    }

    // OscillationCenter = explicit world position if set, otherwise actor's placed location
    Origin = OscillationCenter.IsZero() ? GetActorLocation() : OscillationCenter;

    // Move actor to origin so sphere starts at the right place
    SetActorLocation(Origin);

    // --- Find DCRA ---
    DCRA = UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass());

    if (DCRA)
    {
        UE_LOG(LogTemp, Log, TEXT("[DCRALoopTest] DCRA found at %s | Oscillation center: %s"),
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
