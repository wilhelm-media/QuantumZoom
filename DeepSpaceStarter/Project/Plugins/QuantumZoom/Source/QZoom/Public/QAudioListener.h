#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QAudioListener.generated.h"

/**
 * Place once in the level. On the primary (wall) node it attaches the audio
 * listener to the DisplayClusterRootActor so spatial audio tracks the DCRA
 * camera in both PIE and nDisplay cluster mode.
 */
UCLASS()
class QZOOM_API AQAudioListener : public AActor
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
