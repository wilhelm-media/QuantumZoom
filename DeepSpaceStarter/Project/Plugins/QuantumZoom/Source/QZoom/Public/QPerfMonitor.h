#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "QPerfWidget.h"
#include "QFloorOverlay.h"
#include "AefPharusTypes.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "QPerfMonitor.generated.h"

class UWidgetComponent;

struct FPerfSample
{
	float Time       = 0.f;
	float Fps        = 0.f;
	float FtMs       = 0.f;
	float GpuMs      = 0.f;
	float RamMb      = 0.f;
	bool  bGcEvent   = false;
	float GcInterval = 0.f;
	int32 ShaderJobs = 0;
};

UCLASS()
class QZOOM_API AQPerfMonitor : public AActor
{
	GENERATED_BODY()

public:
	AQPerfMonitor();

	/** Set to true by Tick when the overlay is open — QZoomTest checks this to block D-Pad */
	static bool bIsOverlayCapturingInput;

	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FVector OverlayOffset = FVector(400.f, 0.f, 50.f);

	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FVector2D OverlaySize = FVector2D(160.f, 120.f);

	/** Gamepad_Special_Right (Start/Menu) — toggle overlay */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FKey ToggleKey = EKeys::Gamepad_Special_Right;

	/** Confirm / select menu item (also starts tracking when START TRACKING is selected) */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FKey ConfirmKey = EKeys::Gamepad_Special_Left;

	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay", meta=(ClampMin="10.0"))
	float TrackingDuration = 300.f;

	/** Export destination — leave empty to use Project/Saved/ as fallback */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Export")
	FString ExportPath;

	/** Pause Pharus UDP tracking during measurement — toggled via widget UI (D-Pad + face button) */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Pharus")
	bool bPausePharus = true;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	void HandleInput();
	void StartTracking();
	void StopAndExport();
	void OnPreGarbageCollect();
	void BroadcastPharusToggle(bool bStart);
	void OnPharusClusterEvent(const FDisplayClusterClusterEventJson& Event);
	void TryBindPharusDelegates();

	UFUNCTION()
	void OnQPMTrackUpdated(int32 TrackID, const FAefPharusTrackData& TrackData);

	UFUNCTION()
	void OnQPMTrackLost(int32 TrackID);

	AActor* DCRA    = nullptr;
	bool bIsPrimary = false;

	UPROPERTY()
	TObjectPtr<UWidgetComponent> WidgetComp = nullptr;

	UPROPERTY()
	TObjectPtr<UQPerfWidget> PerfWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UQFloorOverlay> FloorOverlay = nullptr;

	// Overlay state
	bool bOverlayVisible = false;
	bool bTogglePrev     = false;

	// Menu navigation — when overlay is open, D-Pad navigates, ConfirmKey confirms
	int32 MenuSelection  = 0;   // 0=EXPORT, 1=PHARUS ON/OFF, 2=TRACK PHARUS, 3=START TRACKING
	bool  bPharusRunning = false;
	bool bNavUpPrev      = false;
	bool bNavDownPrev    = false;
	bool bConfirmPrev    = false;

	// Export toggle (default OFF — user must enable in widget)
	bool bExportEnabled  = false;

	// Delegate-based live track counter (replaces GetActiveTrackIDs polling)
	TSet<int32> LiveTrackIDs;
	bool bPharusDelegatesBound = false;

	// Tracking state
	bool  bTracking         = false;
	float TrackingTime      = 0.f;
	float LastGcTime        = -1.f;
	float LastGcInterval    = 0.f;
	int32 TotalGcEvents     = 0;
	bool  bGcThisFrame      = false;
	bool  bPharusWasStopped = false;

	// Display smoothing
	float DisplayFps  = 60.f;
	float DisplayFtMs = 16.7f;

	FString LastExportMsg;
	TArray<FPerfSample> Samples;
	FDelegateHandle GcDelegateHandle;
	FOnClusterEventJsonListener PharusClusterEventDelegate;
	FTimerHandle AutoStopPharusTimer;
};
