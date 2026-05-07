#include "QAudioListener.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "GameFramework/PlayerController.h"

void AQAudioListener::BeginPlay()
{
	Super::BeginPlay();

	bool bIsPrimary = true;
	if (IDisplayCluster::IsAvailable())
		bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();

	if (!bIsPrimary)
	{
		SetActorTickEnabled(false);
		return;
	}

	AActor* DCRA = UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass());
	APlayerController* PC = GetWorld()->GetFirstPlayerController();

	if (DCRA && PC)
	{
		PC->SetAudioListenerOverride(DCRA->GetRootComponent(), FVector::ZeroVector, FRotator::ZeroRotator);
		UE_LOG(LogTemp, Log, TEXT("[QAudioListener] Listener attached to DCRA"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[QAudioListener] DCRA or PlayerController not found — listener not overridden"));
	}
}

void AQAudioListener::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		PC->ClearAudioListenerOverride();

	Super::EndPlay(EndPlayReason);
}
