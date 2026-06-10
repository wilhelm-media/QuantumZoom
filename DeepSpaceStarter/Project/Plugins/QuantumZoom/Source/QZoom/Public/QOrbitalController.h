#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "QOrbitalController.generated.h"

class UNiagaraComponent;

/**
 * QOrbitalController
 *
 * Drives a Niagara System designed for CH4 (Molecular Structure / Quantum Switches).
 * The target Niagara System must expose the following User Parameters (case-sensitive):
 *
 *   User.Hybridization    (float)
 *   User.CloudFuzziness   (float)
 *   User.LayerGradient    (float)
 *   User.SwitchProgress   (float)
 *   User.SpawnDensity     (float)
 *   User.CoreColor        (LinearColor)
 *   User.EdgeColor        (LinearColor)
 *
 * See ASSETS/CH4_MOLECULAR/setup_orbital_v01.md for Niagara wiring.
 *
 * Each parameter change is broadcast via nDisplay cluster events so Wall and Floor
 * stay in sync.
 */
UCLASS()
class QZOOM_API AQOrbitalController : public AActor
{
	GENERATED_BODY()

public:
	AQOrbitalController();

	/** Actor that owns the orbital Niagara system (e.g. NS_Orbital in the level) */
	UPROPERTY(EditAnywhere, Category="QOrbital")
	TSoftObjectPtr<AActor> TargetActor;

	UPROPERTY(EditAnywhere, Category="QOrbital")
	bool bActive = true;

	// ───────────────────────────────────────────────────────────────────────
	// Geometry — what kind of orbital shape, how solid vs. fuzzy
	// ───────────────────────────────────────────────────────────────────────

	/** 0 = spherical (s), 1 = linear sp (180°), 2 = trigonal sp² (120°), 3 = tetrahedral sp³ (109.5°). Continuous so you can morph between */
	UPROPERTY(EditAnywhere, Category="QOrbital|Geometry", meta=(ClampMin="0.0", ClampMax="3.0"))
	float Hybridization = 3.f;

	/** 0 = solid spheres, 1 = pure point clouds. Markos's "point clouds × spheres" balance */
	UPROPERTY(EditAnywhere, Category="QOrbital|Geometry", meta=(ClampMin="0.0", ClampMax="1.0"))
	float CloudFuzziness = 0.7f;

	/** Overall orbital extent in cm. Drives color-falloff distance AND layer-rotation scaling. */
	UPROPERTY(EditAnywhere, Category="QOrbital|Geometry", meta=(ClampMin="50.0", ClampMax="2000.0"))
	float MaxRadius = 250.f;

	// ───────────────────────────────────────────────────────────────────────
	// Motion — Markos's explicit layering directive
	// ───────────────────────────────────────────────────────────────────────

	/** Strength of outer-faster-than-inner gradient. 0 = uniform, 2 = clear layering, 200 = extreme curl. */
	UPROPERTY(EditAnywhere, Category="QOrbital|Motion", meta=(ClampMin="0.0", ClampMax="200.0"))
	float LayerGradient = 2.f;

	// ───────────────────────────────────────────────────────────────────────
	// Density
	// ───────────────────────────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, Category="QOrbital|Density", meta=(ClampMin="0.0", ClampMax="50000.0"))
	float SpawnDensity = 8000.f;

	// ───────────────────────────────────────────────────────────────────────
	// Switch — the methionine oxidation morph (0=before, 1=after)
	// ───────────────────────────────────────────────────────────────────────

	/** 0 = before oxidation, 1 = after. Drives orbital re-distribution toward partner bond */
	UPROPERTY(EditAnywhere, Category="QOrbital|Switch", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SwitchProgress = 0.f;

	// ───────────────────────────────────────────────────────────────────────
	// Color anchors (Markos's max-3-colors rule)
	// ───────────────────────────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, Category="QOrbital|Color")
	FLinearColor CoreColor = FLinearColor(1.f, 0.4f, 0.05f, 1.f);

	UPROPERTY(EditAnywhere, Category="QOrbital|Color")
	FLinearColor EdgeColor = FLinearColor(0.3f, 0.75f, 1.f, 0.3f);

	/** Inner threshold (fraction of MaxRadius). Below this distance: pure CoreColor. */
	UPROPERTY(EditAnywhere, Category="QOrbital|Color", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ColorCoreEdge = 0.4f;

	/** Outer threshold (fraction of MaxRadius). Above this distance: pure EdgeColor. Must be > ColorCoreEdge. */
	UPROPERTY(EditAnywhere, Category="QOrbital|Color", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ColorEdgeEdge = 0.9f;

	// ───────────────────────────────────────────────────────────────────────
	// API
	// ───────────────────────────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category="QOrbital")
	void ApplyAll();

	/** Deferred direct-apply called 0.2s after BeginPlay — runs on every node, no cluster-event reliance. */
	UFUNCTION()
	void ApplyAllDirect();

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
