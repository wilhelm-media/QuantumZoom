#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "QNiagaraController.generated.h"

class UNiagaraComponent;

UCLASS()
class QZOOM_API AQNiagaraController : public AActor
{
	GENERATED_BODY()

public:
	AQNiagaraController();

	/** The actor that owns the Niagara system to control (e.g. TestVectorField in your level) */
	UPROPERTY(EditAnywhere, Category="QNiagara")
	TSoftObjectPtr<AActor> TargetActor;

	UPROPERTY(EditAnywhere, Category="QNiagara")
	bool bActive = true;

	UPROPERTY(EditAnywhere, Category="QNiagara|Params", meta=(ClampMin="0.0", ClampMax="2000.0"))
	float VectorFieldIntensity = 400.f;

	UPROPERTY(EditAnywhere, Category="QNiagara|Params", meta=(ClampMin="0.0", ClampMax="20000.0"))
	float SpawnRate = 5000.f;

	UPROPERTY(EditAnywhere, Category="QNiagara|Params", meta=(ClampMin="0.5", ClampMax="30.0"))
	float Lifetime = 5.f;

	UPROPERTY(EditAnywhere, Category="QNiagara|Params", meta=(ClampMin="0.05", ClampMax="10.0"))
	float SpriteSize = 3.f;

	UPROPERTY(EditAnywhere, Category="QNiagara|Params")
	FLinearColor ParticleColor = FLinearColor(0.f, 0.85f, 0.85f, 1.f);

	UPROPERTY(EditAnywhere, Category="QNiagara|Params", meta=(ClampMin="50.0", ClampMax="5000.0"))
	float EmitterRadius = 150.f;

	/** Broadcast all current values to every cluster node */
	UFUNCTION(BlueprintCallable, Category="QNiagara")
	void ApplyAll();

	/** Cluster-sync setters — call from external code (e.g. QPerfMonitor widget) */
	void BroadcastFloat(const FString& ParamName, float Value);
	void BroadcastColor(const FString& ParamName, const FLinearColor& Value);
	void BroadcastBool (const FString& ParamName, bool Value);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> NiagaraComp = nullptr;

	bool bIsPrimary = false;
	FOnClusterEventJsonListener ClusterEventDelegate;
	static const FString ClusterEventName;

	UNiagaraComponent* ResolveNiagaraComponent();
	void OnClusterEventJson(const FDisplayClusterClusterEventJson& Event);

	void ApplyFloat (const FString& ParamName, float Value);
	void ApplyColor (const FString& ParamName, const FLinearColor& Value);
	void ApplyActive(bool bNewActive);
};
