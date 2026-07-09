#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "QZoomRailPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class UCurveFloat;
class UTextRenderComponent;

/**
 * AQZoomRailPawn
 *
 * The player view for the QuantumZOOM scale descent.
 *   Right Trigger = zoom in   (ZoomProgress ->1)
 *   Left  Trigger = zoom out  (ZoomProgress ->0)
 *   Right Stick   = look around (yaw/pitch offset while sliding the rail)
 *
 * ZoomProgress (0..1) LERPs the camera along a linear rail (RailStart->RailEnd, or the
 * Waypoints piecewise-linear path). It is polled ONLY on the primary node, then broadcast
 * via an nDisplay cluster event; every node applies the same ZoomProgress + look, so Wall,
 * Floor and both eyes render an identical transform.
 *
 * nDisplay wiring: attach your DisplayCluster Root Actor (DCRA) to this pawn (or drive the
 * DCRA transform from it) exactly as QZoomTest does, so the cluster view follows the rail.
 */
UCLASS()
class QZOOM_API AQZoomRailPawn : public APawn
{
	GENERATED_BODY()

public:
	AQZoomRailPawn();

	/** Rail endpoints. ZoomProgress lerps the camera between these (linear zoom). */
	UPROPERTY(EditAnywhere, Category="QZoomRail")
	FVector RailStart = FVector(-3500.f, 0.f, 120.f);

	UPROPERTY(EditAnywhere, Category="QZoomRail")
	FVector RailEnd = FVector(45000.f, 0.f, 120.f);

	/** Optional curved rail. If it has >= 2 points it overrides RailStart/RailEnd (piecewise-linear). */
	UPROPERTY(EditAnywhere, Category="QZoomRail")
	TArray<FVector> Waypoints;

	/** 0 = rail start, 1 = rail end. Driven by triggers on the primary node; cluster-synced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="QZoomRail", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ZoomProgress = 0.f;

	// ── Cinematic zoom ────────────────────────────────────────────────────────
	// Speed varies along the rail: slow (dwell) at each station, fast through the empty gaps,
	// with eased start/stop. The trigger sets INTENT (direction + push); the profile shapes SPEED.

	/** Base rail-fraction/sec at full trigger, before the speed profile is applied. */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Cinematic", meta=(ClampMin="0.005", ClampMax="2.0"))
	float BaseZoomRate = 0.06f;

	/** Optional authored speed curve: ZoomProgress(0..1) -> speed multiplier. Overrides the analytic
	 *  dwell below when set — draw dips at the stations, peaks in the gaps for full cinematic control. */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Cinematic")
	TObjectPtr<UCurveFloat> SpeedProfile;

	/** Fallback dwell (used only when SpeedProfile is empty): number of stations to slow at. */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Cinematic", meta=(ClampMin="2", ClampMax="12"))
	int32 StationCount = 6;

	/** Fallback dwell: speed multiplier AT a station (1 = no slow-down, 0.25 = crawl for the hero beat). */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Cinematic", meta=(ClampMin="0.05", ClampMax="1.0"))
	float StationDwell = 0.25f;

	/** Fallback dwell: half-width (rail fraction) of the slow zone around each station. */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Cinematic", meta=(ClampMin="0.01", ClampMax="0.3"))
	float DwellWidth = 0.06f;

	/** Ease of start/stop: higher = snappier, lower = more cinematic glide in/out of the zoom. */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Cinematic", meta=(ClampMin="0.5", ClampMax="20.0"))
	float SpeedDamping = 3.5f;

	/** Look-around speed at full stick (deg/sec). */
	UPROPERTY(EditAnywhere, Category="QZoomRail")
	float LookRate = 90.f;

	/** Pitch clamp for look-around (deg). */
	UPROPERTY(EditAnywhere, Category="QZoomRail", meta=(ClampMin="0.0", ClampMax="89.0"))
	float MaxPitch = 75.f;

	/** Re-center the look toward rail-forward when the stick is released. */
	UPROPERTY(EditAnywhere, Category="QZoomRail")
	bool bRecenterLook = false;

	/** Optional: also drive this actor's transform (your nDisplay DCRA) so the cluster view follows the
	 *  rail WITHOUT re-parenting. Leave empty if the DCRA is already a child of this pawn. */
	UPROPERTY(EditAnywhere, Category="QZoomRail")
	TSoftObjectPtr<AActor> DisplayClusterRoot;

	// ── Constant scale/zoom readout (HUD) ─────────────────────────────────────
	/** Physical scale (metres) at each station, rail order. Log-interpolated for the live readout. */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Readout")
	TArray<float> ScaleMeters = {0.09f, 3e-6f, 1e-8f, 2e-10f, 1e-10f, 8e-15f};

	/** Camera-relative offset (fwd, right, up) of the readout panel — a real 3D element, stereo-safe. */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Readout")
	FVector ReadoutOffset = FVector(600.f, 0.f, -230.f);

	/** Readout text height (world units at ReadoutOffset depth). */
	UPROPERTY(EditAnywhere, Category="QZoomRail|Readout")
	float ReadoutSize = 26.f;

	/** Read live by AZoomStreamer for load/unload decisions. */
	float GetZoomProgress() const { return ZoomProgress; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float Dt) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent>     Root = nullptr;
	UPROPERTY() TObjectPtr<UCameraComponent>    Camera = nullptr;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Readout = nullptr;   // constant scale/zoom panel

	float LookYaw = 0.f;
	float LookPitch = 0.f;
	float ZoomVel = 0.f;   // current damped signed zoom speed (cinematic ease)

	TWeakObjectPtr<AActor> ResolvedDCRA;

	bool bIsPrimary = false;
	bool bInCluster = false;   // true only in a live nDisplay cluster session (not PIE/editor)
	FOnClusterEventJsonListener ClusterListener;
	static const FString EventName;

	void   PollGamepad(float Dt);   // primary only
	void   Broadcast();             // primary -> all nodes
	void   OnClusterEvent(const FDisplayClusterClusterEventJson& E);
	void   ApplyState();            // set transform from ZoomProgress + look (every node)
	FVector EvalRail(float T) const;
	float  SpeedProfileAt(float T) const;   // cinematic speed multiplier at rail position T
	float  CurrentScaleMeters() const;      // log-interpolated physical scale at ZoomProgress
	FString FormatScale(float Metres) const;
	void   UpdateReadout();                 // refresh the constant scale/zoom panel
};
