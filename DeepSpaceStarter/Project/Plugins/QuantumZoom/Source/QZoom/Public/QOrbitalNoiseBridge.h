#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QOrbitalNoiseBridge.generated.h"

class UMaterialParameterCollection;

/** Mirrors the MPC's 'orbital_noise' scalar into every Niagara User.OrbitalNoise, every frame,
 *  in EDITOR and game alike. Exists because the sequencer can only key material collections,
 *  the particles can only read user parameters, and the pawn (which bridges the two in the
 *  show) lives in the mockup — while the authoring happens in the station level, where nothing
 *  else ticks. Drop one of these next to the fillers and the curve gets live feedback. */
UCLASS()
class QZOOM_API AQOrbitalNoiseBridge : public AActor
{
	GENERATED_BODY()

public:
	AQOrbitalNoiseBridge();
	virtual void Tick(float Dt) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	UPROPERTY(EditAnywhere, Category="Bridge")
	TObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY(EditAnywhere, Category="Bridge")
	FName CollectionParameter = TEXT("orbital_noise");

	UPROPERTY(EditAnywhere, Category="Bridge")
	FName NiagaraParameter = TEXT("OrbitalNoise");
};
