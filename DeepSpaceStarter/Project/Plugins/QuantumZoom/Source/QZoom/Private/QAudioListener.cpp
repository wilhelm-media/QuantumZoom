#include "QAudioListener.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"

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

	// Diagnostic 2D test sound (primary only) — fired ~1s in so the audio device is settled.
	if (bPlayTestSound && TestSound)
	{
		FTimerHandle DummyHandle;
		GetWorld()->GetTimerManager().SetTimer(DummyHandle, this, &AQAudioListener::PlayTestSound, 1.0f, false);
		UE_LOG(LogTemp, Warning, TEXT("[QAudioListener] Test sound scheduled (1s): %s"), *GetNameSafe(TestSound));
	}
}

void AQAudioListener::PlayTestSound()
{
	if (!TestSound) return;
	// 2D = non-spatialized, full volume, ignores listener position + attenuation.
	UGameplayStatics::PlaySound2D(GetWorld(), TestSound, TestVolume);
	UE_LOG(LogTemp, Warning, TEXT("[QAudioListener] >>> TEST SOUND played 2D: %s (vol %.2f) — if you hear this, audio OUTPUT works <<<"),
		*GetNameSafe(TestSound), TestVolume);
}

void AQAudioListener::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		PC->ClearAudioListenerOverride();

	Super::EndPlay(EndPlayReason);
}
