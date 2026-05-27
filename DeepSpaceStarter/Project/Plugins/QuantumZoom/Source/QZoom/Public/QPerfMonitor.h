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
class AQNiagaraController;

struct FPerfSample
{
	float Time            = 0.f;
	float Fps             = 0.f;
	float FtMs            = 0.f;
	float TickMs          = 0.f;   // REAL wall-clock tick delta (FPlatformTime), not capped by fixed frame rate
	float GameMs          = 0.f;
	float DrawMs          = 0.f;
	float RhiMs           = 0.f;
	float GpuMs           = 0.f;
	float RamMb           = 0.f;
	bool  bGcEvent        = false;
	float GcInterval      = 0.f;
	int32 ShaderJobs      = 0;
	int32 NiagaraActive   = 0;
	int32 NiagaraEmitters = 0;
	bool  bStutter        = false;
	float ClusterLagMs    = 0.f;   // last cluster-event heartbeat round-trip
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

	/** B button — toggle overlay */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FKey ToggleKey = EKeys::Gamepad_FaceButton_Right;

	/** X button — confirm / select menu item (also starts tracking when START TRACKING is selected) */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FKey ConfirmKey = EKeys::Gamepad_FaceButton_Left;

	/** Y button — toggle UE built-in 'stat unitGraph' live graph */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FKey StatGraphKey = EKeys::Gamepad_FaceButton_Top;

	/** A button — start "fresh restart" prompt; confirmed by pressing B within timeout */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay")
	FKey RestartKey = EKeys::Gamepad_FaceButton_Bottom;

	/** Seconds the restart prompt stays open before auto-canceling */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay", meta=(ClampMin="1.0", ClampMax="30.0"))
	float RestartPromptTimeout = 5.f;

	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Overlay", meta=(ClampMin="10.0"))
	float TrackingDuration = 300.f;

	/** Export destination — leave empty to use Project/Saved/ as fallback */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Export")
	FString ExportPath;

	/** Pause Pharus UDP tracking during measurement — toggled via widget UI (D-Pad + face button) */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Pharus")
	bool bPausePharus = true;

	/** Position offset from DCRA where the diagnostic rotating cube spawns */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|TestCube")
	FVector TestCubeOffset = FVector(500.f, 0.f, 0.f);

	/** Uniform scale of the diagnostic cube */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|TestCube")
	float TestCubeScale = 2.f;

	/** Actor label/name of the ambient sound used for distance readout when stat graph is active */
	UPROPERTY(EditAnywhere, Category="QPerfMonitor|Audio")
	FString SoundActorName = TEXT("SOUND_01");

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
	bool bStatGraphPrev   = false;
	bool bStatGraphActive = false;

	// Restart prompt state
	bool  bRestartPromptActive = false;
	float RestartPromptElapsed = 0.f;
	bool  bRestartKeyPrev      = false;

	// Menu navigation — when overlay is open, D-Pad navigates, ConfirmKey confirms.
	// MenuPage 0 (Tracking): 0=EXPORT, 1=PHARUS ON/OFF, 2=TRACK PHARUS, 3=START TRACKING
	// MenuPage 1 (Niagara):  0=ACTIVE, 1=Intensity, 2=SpawnRate, 3=Lifetime, 4=SpriteSize, 5=EmitterRadius
	int32 MenuPage       = 0;
	int32 MenuSelection  = 0;
	bool  bPharusRunning = false;
	bool bNavUpPrev      = false;
	bool bNavDownPrev    = false;
	bool bNavLeftPrev    = false;
	bool bNavRightPrev   = false;
	bool bConfirmPrev    = false;
	bool bLBPrev         = false;
	bool bRBPrev         = false;

	UPROPERTY()
	TObjectPtr<AQNiagaraController> NiagaraController = nullptr;

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> TestCubeMesh = nullptr;
	bool  bTestCubeActive  = false;
	float TestCubeRotation = 0.f;

	UPROPERTY()
	TObjectPtr<AActor> CachedSoundActor = nullptr;

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

	// Rolling 5-second window of stutter timestamps (frames where FtMs > 25)
	TArray<float> RecentStutterTimes;
	float LastStutterIntervalAvg = 0.f;
	int32 StuttersInWindow        = 0;

	FString LastExportMsg;
	TArray<FPerfSample> Samples;
	FDelegateHandle GcDelegateHandle;
	FOnClusterEventJsonListener PharusClusterEventDelegate;
	FTimerHandle AutoStopPharusTimer;

	// Wall-clock tick measurement (truthful even with bUseFixedFrameRate)
	double LastTickRealTime = 0.0;

	// Cluster-event heartbeat
	FOnClusterEventJsonListener HeartbeatClusterEventDelegate;
	static const FString HeartbeatEventName;
	float LastClusterLagMs = 0.f;

	void BroadcastHeartbeat();
	void OnHeartbeatClusterEvent(const FDisplayClusterClusterEventJson& Event);

	// CSV-Profiler auto-capture
	FTimerHandle CsvCopyTimer;
	FString PendingCsvBaseName;
	void CopyLatestCsvProfile();
};
