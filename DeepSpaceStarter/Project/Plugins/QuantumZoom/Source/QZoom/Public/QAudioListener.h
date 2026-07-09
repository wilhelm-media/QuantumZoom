#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QAudioListener.generated.h"

class USoundBase;

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

	// ── DIAGNOSTIC: simple 2D test sound ──────────────────────────────────────
	// If enabled, plays TestSound as a NON-spatialized (2D) sound on the primary
	// node ~1s after BeginPlay. Bypasses attenuation + listener distance entirely,
	// so it isolates the question: does audio output work at all on this node?
	//   hear it  -> output OK; the real soundscape's spatialization/placement is the issue
	//   silent   -> deeper problem (output device / routing / muted on the node)

	/** Turn the diagnostic test sound on. Leave OFF for production. */
	UPROPERTY(EditAnywhere, Category="QAudioListener|Test")
	bool bPlayTestSound = false;

	/** Sound to play 2D as the test (e.g. the Endocellular soundscape, or any SoundWave/Cue). */
	UPROPERTY(EditAnywhere, Category="QAudioListener|Test", meta=(EditCondition="bPlayTestSound"))
	TObjectPtr<USoundBase> TestSound = nullptr;

	/** Volume multiplier for the test sound (boosted so it's unmistakable). */
	UPROPERTY(EditAnywhere, Category="QAudioListener|Test", meta=(EditCondition="bPlayTestSound", ClampMin="0.0", ClampMax="4.0"))
	float TestVolume = 1.5f;

private:
	void PlayTestSound();
};
