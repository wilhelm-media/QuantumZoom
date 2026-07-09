#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QZoomStreamer.generated.h"

class AQZoomStagePawn;

/**
 * AZoomStreamer
 *
 * Predictive load/unload of the scale sublevels, keyed to the rail pawn's ZoomProgress.
 * Keeps only ~2 blocks resident (current + LoadAhead[+LoadBehind]) and pre-loads the next
 * block once you cross PredictiveLead into the current one, so the seam is already resident
 * before you arrive. Because ZoomProgress is cluster-synced on the pawn, every node makes the
 * SAME load/unload decisions — no Wall/Floor desync.
 *
 * Setup: extract each S0..S5 Outliner folder into its own streaming sublevel of the persistent
 * level, then list them in StageLevels IN RAIL ORDER (index 0 = rail start).
 */
UCLASS()
class QZOOM_API AZoomStreamer : public AActor
{
	GENERATED_BODY()

public:
	AZoomStreamer();

	/** Sublevel (short) names in rail order, e.g. L_QZ_S0_Lab ... L_QZ_S5_Quarks. */
	UPROPERTY(EditAnywhere, Category="QZoomStream")
	TArray<FName> StageLevels;

	/** Blocks kept loaded ahead of the current one. */
	UPROPERTY(EditAnywhere, Category="QZoomStream", meta=(ClampMin="0", ClampMax="4"))
	int32 LoadAhead = 1;

	/** Blocks kept loaded behind the current one. */
	UPROPERTY(EditAnywhere, Category="QZoomStream", meta=(ClampMin="0", ClampMax="4"))
	int32 LoadBehind = 0;

	/** Begin lead-loading the next block once we are this far (0..1) into the current one. */
	UPROPERTY(EditAnywhere, Category="QZoomStream", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PredictiveLead = 0.6f;

	/** Optional explicit pawn; auto-found in the world if left empty. */
	UPROPERTY(EditAnywhere, Category="QZoomStream")
	TSoftObjectPtr<AQZoomStagePawn> StagePawn;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;

private:
	TWeakObjectPtr<AQZoomStagePawn> Pawn;
	TArray<int32> LoadedState;   // per StageLevels index: -1 unknown, 0 unloaded, 1 loaded

	AQZoomStagePawn* ResolvePawn();
	void UpdateStreaming();
	void SetStageLoaded(int32 Index, bool bLoad);
};
