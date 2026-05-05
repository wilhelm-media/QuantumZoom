#include "DCRALoopTest.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Cluster/DisplayClusterClusterEvent.h"

const FString ADCRALoopTest::ClusterEventName = TEXT("DCRALoop.Transform");

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

    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Mesh) SphereMesh->SetStaticMesh(Mesh);

    UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (BaseMat)
    {
        UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, this);
        DynMat->SetVectorParameterValue(TEXT("Color"), SphereColor);
        SphereMesh->SetMaterial(0, DynMat);
    }

    SphereMesh->SetRelativeScale3D(FVector(FMath::Max(SphereSize, 1.f) / 100.f));
    SphereMesh->SetVisibility(bShowSphere, true);

    if (IDisplayCluster::IsAvailable())
    {
        bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
        IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(this);
    }
    else
    {
        bIsPrimary = true;
    }

    // Secondary nodes only listen — no tick needed
    if (!bActive || !bIsPrimary)
    {
        SetActorTickEnabled(false);
    }

    if (!bIsPrimary) return;

    Origin = OscillationCenter.IsZero() ? GetActorLocation() : OscillationCenter;
    SetActorLocation(Origin);

    DCRA = UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass());
    UE_LOG(LogTemp, Log, TEXT("[DCRALoopTest] DCRA: %s | Primary: YES"),
        DCRA ? *DCRA->GetActorLocation().ToString() : TEXT("NOT FOUND"));
}

void ADCRALoopTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (IDisplayCluster::IsAvailable())
        IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(this);
    Super::EndPlay(EndPlayReason);
}

void ADCRALoopTest::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    Time += DeltaTime;
    const float Angle = (CycleDuration > 0.f) ? (Time / CycleDuration) * TWO_PI : 0.f;
    const FVector NewLoc = Origin + Axis.GetSafeNormal() * Amplitude * FMath::Sin(Angle);

    SetActorLocation(NewLoc);

    if (!DCRA) return;

    if (IDisplayCluster::IsAvailable())
    {
        FDisplayClusterClusterEventJson Event;
        Event.Name     = ClusterEventName;
        Event.Type     = TEXT("Transform");
        Event.Category = TEXT("DCRALoop");
        Event.bShouldDiscardOnRepeat = false;
        Event.Parameters.Add(TEXT("X"), FString::SanitizeFloat(NewLoc.X));
        Event.Parameters.Add(TEXT("Y"), FString::SanitizeFloat(NewLoc.Y));
        Event.Parameters.Add(TEXT("Z"), FString::SanitizeFloat(NewLoc.Z));
        IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
    }
    else
    {
        DCRA->SetActorLocation(NewLoc);
    }
}

void ADCRALoopTest::OnClusterEventJson(const FDisplayClusterClusterEventJson& Event)
{
    if (Event.Name != ClusterEventName || !DCRA) return;

    const float X = FCString::Atof(*Event.Parameters.FindRef(TEXT("X")));
    const float Y = FCString::Atof(*Event.Parameters.FindRef(TEXT("Y")));
    const float Z = FCString::Atof(*Event.Parameters.FindRef(TEXT("Z")));
    DCRA->SetActorLocation(FVector(X, Y, Z));
}
