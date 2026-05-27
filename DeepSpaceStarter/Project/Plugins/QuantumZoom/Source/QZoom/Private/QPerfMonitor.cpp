#include "QPerfMonitor.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Components/WidgetComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformMemory.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "AefPharusSubsystem.h"
#include "AefPharusInstance.h"
#include "RHI.h"
#include "RenderCore.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "QNiagaraController.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

// Thread-time globals from RenderCore (in CPU cycles — convert with FPlatformTime::ToMilliseconds)
extern RENDERCORE_API uint32 GGameThreadTime;
extern RENDERCORE_API uint32 GRenderThreadTime;
extern RENDERCORE_API uint32 GRHIThreadTime;

bool AQPerfMonitor::bIsOverlayCapturingInput = false;
const FString AQPerfMonitor::HeartbeatEventName = TEXT("QPerf.Heartbeat");

AQPerfMonitor::AQPerfMonitor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AQPerfMonitor::BeginPlay()
{
	Super::BeginPlay();

	// Reset session state — ignore any stale level-saved values
	bExportEnabled  = false;
	bPausePharus    = true;
	MenuPage        = 0;
	MenuSelection   = 0;
	bOverlayVisible = false;

	// Find Niagara controller (optional — overlay still works without it)
	for (TActorIterator<AQNiagaraController> It(GetWorld()); It; ++It)
	{
		NiagaraController = *It;
		break;
	}

	if (IDisplayCluster::IsAvailable())
		bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
	else
		bIsPrimary = true;

	DCRA = UGameplayStatics::GetActorOfClass(GetWorld(), ADisplayClusterRootActor::StaticClass());

	GcDelegateHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(
		this, &AQPerfMonitor::OnPreGarbageCollect);

	// Register Pharus cluster event listener on ALL nodes so stop/start broadcasts reach everyone
	if (IDisplayCluster::IsAvailable())
	{
		PharusClusterEventDelegate = FOnClusterEventJsonListener::CreateUObject(
			this, &AQPerfMonitor::OnPharusClusterEvent);
		IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(PharusClusterEventDelegate);

		// Heartbeat for cluster-sync health: timestamp every tick, measure receive lag
		HeartbeatClusterEventDelegate = FOnClusterEventJsonListener::CreateUObject(
			this, &AQPerfMonitor::OnHeartbeatClusterEvent);
		IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(HeartbeatClusterEventDelegate);
	}

	// Wall-clock baseline for truthful per-tick measurement
	LastTickRealTime = FPlatformTime::Seconds();

	// Auto-stop Pharus 3s after start so pool actors initialise first, then cluster stops cleanly
	if (bIsPrimary)
	{
		GetWorldTimerManager().SetTimer(AutoStopPharusTimer, [this]()
		{
			BroadcastPharusToggle(false);
			UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Auto-stopped Pharus on startup"));
		}, 3.f, false);
	}

	if (!bIsPrimary)
	{
		// Floor node: screen-space overlay — positions from RawPosition (0-1), DCRA-independent
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			FloorOverlay = CreateWidget<UQFloorOverlay>(PC, UQFloorOverlay::StaticClass());
			if (FloorOverlay)
				FloorOverlay->AddToViewport(0);
		}
		SetActorTickEnabled(false);
		return;
	}

	// Widget — Wall node only, screen-space via viewport so it remains visible
	// during cinematic sequences regardless of camera direction/FOV.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PerfWidget = CreateWidget<UQPerfWidget>(PC, UQPerfWidget::StaticClass());
		if (PerfWidget)
		{
			PerfWidget->AddToViewport(100);
			PerfWidget->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	// Diagnostic rotating cube — hidden by default, toggled via Tracking page row 4
	TestCubeMesh = NewObject<UStaticMeshComponent>(this, TEXT("TestCube"));
	TestCubeMesh->SetMobility(EComponentMobility::Movable);
	TestCubeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UStaticMesh* CubeAsset = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		TestCubeMesh->SetStaticMesh(CubeAsset);
	TestCubeMesh->AttachToComponent(GetRootComponent(),
		FAttachmentTransformRules::KeepRelativeTransform);
	TestCubeMesh->RegisterComponent();
	TestCubeMesh->SetRelativeScale3D(FVector(TestCubeScale));
	if (DCRA)
	{
		const FVector WorldOff = DCRA->GetActorRotation().RotateVector(TestCubeOffset);
		TestCubeMesh->SetWorldLocation(DCRA->GetActorLocation() + WorldOff);
	}
	TestCubeMesh->SetVisibility(false);

	UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Ready. Toggle: %s  Confirm: %s"),
		*ToggleKey.GetFName().ToString(), *ConfirmKey.GetFName().ToString());
}

void AQPerfMonitor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IDisplayCluster::IsAvailable())
	{
		IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(PharusClusterEventDelegate);
		IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(HeartbeatClusterEventDelegate);
	}
	bIsOverlayCapturingInput = false;
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(GcDelegateHandle);
	if (bTracking) StopAndExport();
	Super::EndPlay(EndPlayReason);
}

void AQPerfMonitor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Publish capture state BEFORE QZoomTest may tick and read it
	bIsOverlayCapturingInput = bOverlayVisible;

	HandleInput();

	// (Widget is now viewport-attached; no per-frame world positioning needed)

	// Rotate diagnostic cube
	if (bTestCubeActive && TestCubeMesh)
	{
		TestCubeRotation += DeltaTime * 60.f;
		TestCubeMesh->SetWorldRotation(FRotator(TestCubeRotation * 0.5f, TestCubeRotation, 0.f));
	}

	// Distance readout DCRA -> sound source — computed every frame, displayed in widget page 1
	FString SoundDistStr;
	if (IsValid(DCRA) && !SoundActorName.IsEmpty())
	{
		if (!IsValid(CachedSoundActor))
		{
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			{
				AActor* A = *It;
				if (!A) continue;
				const bool bNameMatch = A->GetName() == SoundActorName;
#if WITH_EDITOR
				const bool bLabelMatch = A->GetActorLabel() == SoundActorName;
#else
				const bool bLabelMatch = false;
#endif
				if (bNameMatch || bLabelMatch)
				{
					CachedSoundActor = A;
					break;
				}
			}
		}
		if (IsValid(CachedSoundActor))
		{
			const float Dist = FVector::Dist(DCRA->GetActorLocation(), CachedSoundActor->GetActorLocation());
			SoundDistStr = FString::Printf(TEXT("DCRA -> %s  :  %.0f cm"), *SoundActorName, Dist);
		}
		else
		{
			SoundDistStr = FString::Printf(TEXT("DCRA -> %s  :  not found"), *SoundActorName);
		}
	}

	// Wall-clock tick delta — truthful even when bUseFixedFrameRate clamps DeltaTime
	const double NowReal = FPlatformTime::Seconds();
	const float TickMs   = LastTickRealTime > 0.0
		? static_cast<float>((NowReal - LastTickRealTime) * 1000.0)
		: 0.f;
	LastTickRealTime = NowReal;

	// Broadcast cluster-sync heartbeat (receive handler updates LastClusterLagMs)
	BroadcastHeartbeat();

	// Smooth FPS/frametime for display
	const float InstFps = DeltaTime > 0.f ? (1.f / DeltaTime) : DisplayFps;
	DisplayFps  = DisplayFps  * 0.9f + InstFps              * 0.1f;
	DisplayFtMs = DisplayFtMs * 0.9f + (DeltaTime * 1000.f) * 0.1f;

	const float GpuMs  = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
	const float GameMs = static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime));
	const float DrawMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	const float RhiMs  = static_cast<float>(FPlatformTime::ToMilliseconds(GRHIThreadTime));
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	const float RamMb = static_cast<float>(MemStats.UsedPhysical) / (1024.f * 1024.f);

#if WITH_EDITOR
	const int32 ShaderJobs = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
	const int32 ShaderJobs = 0;
#endif

	// Niagara: count active components and total emitters in this world
	int32 NiagaraActive   = 0;
	int32 NiagaraEmitters = 0;
	{
		UWorld* MyWorld = GetWorld();
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			UNiagaraComponent* NC = *It;
			if (!NC || NC->GetWorld() != MyWorld || !NC->IsActive()) continue;
			NiagaraActive++;
			if (UNiagaraSystem* Sys = NC->GetAsset())
				NiagaraEmitters += Sys->GetEmitterHandles().Num();
		}
	}

	// Stutter detection: wall-clock-based. DeltaTime is unreliable with fixed frame rate,
	// so trigger on whichever signal actually reveals a stall:
	//   - real Tick wall-clock delta > 50 ms
	//   - game-thread time > 50 ms
	//   - GPU frame time > 50 ms
	const float FrameMs       = DeltaTime * 1000.f;
	const bool  bStutterFrame = (TickMs > 50.f) || (GameMs > 50.f) || (GpuMs > 50.f);
	const float NowSec        = static_cast<float>(GetWorld()->GetTimeSeconds());

	if (bStutterFrame)
		RecentStutterTimes.Add(NowSec);

	// Prune entries older than 5s
	const float WindowStart = NowSec - 5.f;
	while (RecentStutterTimes.Num() > 0 && RecentStutterTimes[0] < WindowStart)
		RecentStutterTimes.RemoveAt(0, 1, EAllowShrinking::No);

	StuttersInWindow = RecentStutterTimes.Num();

	// Average interval between stutters in the window (confirms periodicity if ~1.0s)
	if (RecentStutterTimes.Num() >= 2)
	{
		float Sum = 0.f;
		for (int32 i = 1; i < RecentStutterTimes.Num(); ++i)
			Sum += RecentStutterTimes[i] - RecentStutterTimes[i - 1];
		LastStutterIntervalAvg = Sum / (RecentStutterTimes.Num() - 1);
	}
	else
	{
		LastStutterIntervalAvg = 0.f;
	}

	bPharusRunning = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>())
			bPharusRunning = Pharus->IsPharusSystemRunning();
	}

	// Bind delegates on (re)start; clear count when Pharus is not running
	if (bPharusRunning && !bPharusDelegatesBound)
		TryBindPharusDelegates();
	else if (!bPharusRunning && bPharusDelegatesBound)
	{
		LiveTrackIDs.Empty();
		bPharusDelegatesBound = false;
	}

	const int32 ActivePharusTracks = LiveTrackIDs.Num();

	if (bTracking)
	{
		TrackingTime += DeltaTime;

		FPerfSample S;
		S.Time            = TrackingTime;
		S.Fps             = InstFps;
		S.FtMs            = FrameMs;
		S.TickMs          = TickMs;
		S.GameMs          = GameMs;
		S.DrawMs          = DrawMs;
		S.RhiMs           = RhiMs;
		S.GpuMs           = GpuMs;
		S.RamMb           = RamMb;
		S.bGcEvent        = bGcThisFrame;
		S.GcInterval      = LastGcInterval;
		S.ShaderJobs      = ShaderJobs;
		S.NiagaraActive   = NiagaraActive;
		S.NiagaraEmitters = NiagaraEmitters;
		S.bStutter        = bStutterFrame;
		S.ClusterLagMs    = LastClusterLagMs;
		Samples.Add(S);

		if (TrackingTime >= TrackingDuration)
			StopAndExport();
	}

	bGcThisFrame = false;

	if (IsValid(PerfWidget) && bOverlayVisible)
	{
		const float Countdown = bTracking ? FMath::Max(0.f, TrackingDuration - TrackingTime) : 0.f;
		const FString ResolvedExportPath = ExportPath.IsEmpty() ? FPaths::ProjectSavedDir() : ExportPath;
		FQNiagaraDisplay NiaDisp;
		if (IsValid(NiagaraController))
		{
			NiaDisp.bAvailable           = true;
			NiaDisp.bActive              = NiagaraController->bActive;
			NiaDisp.VectorFieldIntensity = NiagaraController->VectorFieldIntensity;
			NiaDisp.SpawnRate            = NiagaraController->SpawnRate;
			NiaDisp.Lifetime             = NiagaraController->Lifetime;
			NiaDisp.SpriteSize           = NiagaraController->SpriteSize;
			NiaDisp.EmitterRadius        = NiagaraController->EmitterRadius;
		}

		PerfWidget->UpdateStats(
			DisplayFps, DisplayFtMs,
			GameMs, DrawMs, RhiMs, GpuMs,
			TickMs, LastClusterLagMs,
			RamMb,
			TotalGcEvents, LastGcInterval, ShaderJobs,
			NiagaraActive, NiagaraEmitters,
			StuttersInWindow, LastStutterIntervalAvg, bStutterFrame,
			bPharusRunning, ActivePharusTracks,
			bTracking, Countdown, LastExportMsg,
			MenuPage, MenuSelection, bExportEnabled, bPausePharus, bTestCubeActive, ResolvedExportPath,
			NiaDisp, SoundDistStr,
			bRestartPromptActive, RestartPromptTimeout - RestartPromptElapsed);
	}
}

void AQPerfMonitor::HandleInput()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	const bool bToggleDown  = PC->IsInputKeyDown(ToggleKey);     // B
	const bool bRestartDown = PC->IsInputKeyDown(RestartKey);    // A

	// ── Restart prompt: A starts it, B confirms within timeout ────────────────
	if (!bRestartPromptActive)
	{
		if (bRestartDown && !bRestartKeyPrev)
		{
			bRestartPromptActive = true;
			RestartPromptElapsed = 0.f;
			// Force-show widget so prompt is visible regardless of overlay state
			bOverlayVisible = true;
			if (PerfWidget)
				PerfWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	else
	{
		RestartPromptElapsed += GetWorld()->GetDeltaSeconds();

		if (bToggleDown && !bTogglePrev)
		{
			// CONFIRM — soft audio reset (full RestartLevel freezes in nDisplay cluster mode)
			UE_LOG(LogTemp, Warning, TEXT("[QPerfMonitor] Restart confirmed — resetting audio"));
			bRestartPromptActive = false;
			bRestartKeyPrev      = bRestartDown;
			bTogglePrev          = bToggleDown;

			// Clear the listener override; QZoomTest's per-tick push will reapply next frame.
			PC->ClearAudioListenerOverride();

			// Stop + restart every active AudioComponent in this world — forces a clean
			// spatializer state, fixes the "DCRA went through SOUND_01" stuck mute.
			int32 RestartedCount = 0;
			for (TObjectIterator<UAudioComponent> It; It; ++It)
			{
				UAudioComponent* AC = *It;
				if (AC && AC->GetWorld() == GetWorld() && AC->IsActive())
				{
					AC->Stop();
					AC->Play();
					RestartedCount++;
				}
			}
			UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Restarted %d AudioComponents"), RestartedCount);

			return;
		}

		if (RestartPromptElapsed >= RestartPromptTimeout)
		{
			bRestartPromptActive = false;
			UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Restart prompt timed out"));
		}

		// While prompt is active, swallow B/A so they don't double-toggle overlay
		bRestartKeyPrev = bRestartDown;
		bTogglePrev     = bToggleDown;
		return;
	}
	bRestartKeyPrev = bRestartDown;

	// ── Toggle overlay (B normal behavior) ────────────────────────────────────
	if (bToggleDown && !bTogglePrev)
	{
		bOverlayVisible = !bOverlayVisible;
		if (PerfWidget)
		{
			PerfWidget->SetVisibility(bOverlayVisible
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Hidden);
		}
		LastExportMsg.Empty();
	}
	bTogglePrev = bToggleDown;

	// ── Toggle UE 'stat unitGraph' live graph (Game/Draw/GPU/RHI thread times) ──
	const bool bStatGraphDown = PC->IsInputKeyDown(StatGraphKey);
	if (bStatGraphDown && !bStatGraphPrev)
	{
		bStatGraphActive = !bStatGraphActive;
		// Switchboard launches with -NoScreenMessages / DisableAllScreenMessages, which suppresses
		// AddOnScreenDebugMessage. Force-enable while Y is active so the distance readout shows.
		if (GEngine) GEngine->bEnableOnScreenDebugMessages = bStatGraphActive;
		PC->ConsoleCommand(bStatGraphActive ? TEXT("stat unitGraph") : TEXT("stat none"));
	}
	bStatGraphPrev = bStatGraphDown;

	// ── Menu navigation (only when overlay is open) ───────────────────────────
	if (!bOverlayVisible) return;

	const bool bNavUp    = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up);
	const bool bNavDown  = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down);
	const bool bNavLeft  = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left);
	const bool bNavRight = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right);
	const bool bConfirm  = PC->IsInputKeyDown(ConfirmKey);
	const bool bLB       = PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder);
	const bool bRB       = PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder);

	// Page switching with LB/RB (overlay-only)
	if (bLB && !bLBPrev) { MenuPage = 0; MenuSelection = 0; }
	if (bRB && !bRBPrev) { MenuPage = 1; MenuSelection = 0; }

	const int32 MaxSelOnPage = (MenuPage == 0) ? 4 : 5;
	if (bNavUp && !bNavUpPrev)
		MenuSelection = FMath::Max(0, MenuSelection - 1);
	if (bNavDown && !bNavDownPrev)
		MenuSelection = FMath::Min(MaxSelOnPage, MenuSelection + 1);

	if (MenuPage == 0)
	{
		if (bConfirm && !bConfirmPrev)
		{
			if      (MenuSelection == 0) bExportEnabled = !bExportEnabled;
			else if (MenuSelection == 1) BroadcastPharusToggle(!bPharusRunning);
			else if (MenuSelection == 2) bPausePharus = !bPausePharus;
			else if (MenuSelection == 3 && !bTracking) StartTracking();
			else if (MenuSelection == 4)
			{
				bTestCubeActive = !bTestCubeActive;
				if (TestCubeMesh) TestCubeMesh->SetVisibility(bTestCubeActive);
			}
		}
	}
	else // MenuPage == 1 — Niagara
	{
		if (IsValid(NiagaraController))
		{
			// Active toggle on row 0 via ConfirmKey
			if (bConfirm && !bConfirmPrev && MenuSelection == 0)
			{
				NiagaraController->bActive = !NiagaraController->bActive;
				NiagaraController->BroadcastBool(TEXT("Active"), NiagaraController->bActive);
			}

			// D-Pad Left/Right adjusts float values on rows 1..5
			const bool bDec = bNavLeft  && !bNavLeftPrev;
			const bool bInc = bNavRight && !bNavRightPrev;
			if (bDec || bInc)
			{
				const float Sign = bInc ? 1.f : -1.f;
				switch (MenuSelection)
				{
				case 1: // VectorFieldIntensity
					NiagaraController->VectorFieldIntensity = FMath::Clamp(NiagaraController->VectorFieldIntensity + Sign * 50.f, 0.f, 2000.f);
					NiagaraController->BroadcastFloat(TEXT("VectorFieldIntensity"), NiagaraController->VectorFieldIntensity);
					break;
				case 2: // SpawnRate
					NiagaraController->SpawnRate = FMath::Clamp(NiagaraController->SpawnRate + Sign * 500.f, 0.f, 20000.f);
					NiagaraController->BroadcastFloat(TEXT("SpawnRate"), NiagaraController->SpawnRate);
					break;
				case 3: // Lifetime
					NiagaraController->Lifetime = FMath::Clamp(NiagaraController->Lifetime + Sign * 0.5f, 0.5f, 30.f);
					NiagaraController->BroadcastFloat(TEXT("Lifetime"), NiagaraController->Lifetime);
					break;
				case 4: // SpriteSize
					NiagaraController->SpriteSize = FMath::Clamp(NiagaraController->SpriteSize + Sign * 0.2f, 0.05f, 10.f);
					NiagaraController->BroadcastFloat(TEXT("SpriteSize"), NiagaraController->SpriteSize);
					break;
				case 5: // EmitterRadius
					NiagaraController->EmitterRadius = FMath::Clamp(NiagaraController->EmitterRadius + Sign * 100.f, 50.f, 5000.f);
					NiagaraController->BroadcastFloat(TEXT("EmitterRadius"), NiagaraController->EmitterRadius);
					break;
				}
			}
		}
	}

	bNavUpPrev    = bNavUp;
	bNavDownPrev  = bNavDown;
	bNavLeftPrev  = bNavLeft;
	bNavRightPrev = bNavRight;
	bConfirmPrev  = bConfirm;
	bLBPrev       = bLB;
	bRBPrev       = bRB;
}

void AQPerfMonitor::StartTracking()
{
	Samples.Reset();
	Samples.Reserve(FMath::CeilToInt(TrackingDuration * 65.f));
	TrackingTime      = 0.f;
	TotalGcEvents     = 0;
	LastGcTime        = -1.f;
	LastGcInterval    = 0.f;
	bPharusWasStopped = false;
	LastExportMsg.Empty();
	bTracking = true;

	// Start UE's built-in CSV profiler in parallel — writes wide engine-stat dump
	// (WaitForRHIThread, WaitForGPU, Niagara/*, etc.) to Saved/Profiling/CSV/
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->ConsoleCommand(TEXT("csvProfile start"));
	}

	if (bPausePharus)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>())
			{
				if (Pharus->IsPharusSystemRunning())
				{
					Pharus->StopPharusSystem();
					bPharusWasStopped = true;
					UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Pharus stopped for clean measurement"));
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Tracking started — %.0fs / Export: %s / Pharus: %s"),
		TrackingDuration,
		bExportEnabled ? TEXT("ON") : TEXT("OFF"),
		bPausePharus   ? TEXT("PAUSED") : TEXT("running"));
}

void AQPerfMonitor::OnPreGarbageCollect()
{
	bGcThisFrame = true;
	TotalGcEvents++;

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (LastGcTime >= 0.f)
		LastGcInterval = Now - LastGcTime;
	LastGcTime = Now;
}

void AQPerfMonitor::BroadcastPharusToggle(bool bStart)
{
	if (!IDisplayCluster::IsAvailable())
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>())
			{
				if (bStart) Pharus->StartPharusSystem();
				else        Pharus->StopPharusSystem();
			}
		}
		return;
	}

	FDisplayClusterClusterEventJson Event;
	Event.Name     = TEXT("QZoom.PharusToggle");
	Event.Type     = TEXT("Pharus");
	Event.Category = TEXT("QZoom");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("Action"), bStart ? TEXT("Start") : TEXT("Stop"));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQPerfMonitor::OnPharusClusterEvent(const FDisplayClusterClusterEventJson& Event)
{
	if (Event.Name != TEXT("QZoom.PharusToggle")) return;

	const FString* Action = Event.Parameters.Find(TEXT("Action"));
	if (!Action) return;

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>())
		{
			if (*Action == TEXT("Start"))
			{
				Pharus->StartPharusSystem();
				// Delegates will rebind on next Tick once IsPharusSystemRunning() returns true
			}
			else
			{
				Pharus->StopPharusSystem();
				LiveTrackIDs.Empty();
				bPharusDelegatesBound = false;
			}
		}
	}
}

void AQPerfMonitor::TryBindPharusDelegates()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>();
	if (!Pharus || !Pharus->IsPharusSystemRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[QPerfMonitor] TryBind: Pharus not running"));
		return;
	}

	TArray<FName> Names = Pharus->GetAllInstanceNames();
	UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] TryBind: %d instance(s) found"), Names.Num());
	if (Names.Num() == 0) return;

	int32 BoundCount = 0;
	for (const FName& Name : Names)
	{
		UAefPharusInstance* Inst = Pharus->GetTrackerInstance(Name);
		if (Inst)
		{
			Inst->OnTrackUpdated.AddDynamic(this, &AQPerfMonitor::OnQPMTrackUpdated);
			Inst->OnTrackLost.AddDynamic(this, &AQPerfMonitor::OnQPMTrackLost);
			BoundCount++;
			UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] TryBind: bound instance '%s'"), *Name.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[QPerfMonitor] TryBind: instance '%s' returned null"), *Name.ToString());
		}
	}

	// Only mark as bound if at least one instance succeeded — keep retrying otherwise
	if (BoundCount > 0)
	{
		bPharusDelegatesBound = true;
		UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Pharus delegates bound (%d/%d)"), BoundCount, Names.Num());
	}
}

void AQPerfMonitor::OnQPMTrackUpdated(int32 TrackID, const FAefPharusTrackData& TrackData)
{
	LiveTrackIDs.Add(TrackID);
	UE_LOG(LogTemp, Verbose, TEXT("[QPerfMonitor] TrackUpdated ID=%d  total=%d"), TrackID, LiveTrackIDs.Num());
}

void AQPerfMonitor::OnQPMTrackLost(int32 TrackID)
{
	LiveTrackIDs.Remove(TrackID);
	UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] TrackLost    ID=%d  total=%d"), TrackID, LiveTrackIDs.Num());
}

void AQPerfMonitor::StopAndExport()
{
	bTracking = false;

	if (bPharusWasStopped)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>())
				Pharus->StartPharusSystem();
		}
		bPharusWasStopped = false;
		UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Pharus restarted"));
	}

	// If export is disabled, just log summary and return
	if (!bExportEnabled)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[QPerfMonitor] Session ended — %d samples, %d GC events (Export OFF, nothing saved)"),
			Samples.Num(), TotalGcEvents);
		Samples.Empty();
		return;
	}

	// Summary stats
	float FpsSum = 0.f, FpsMin = 1e9f, FtMax = 0.f;
	float GameMax = 0.f, DrawMax = 0.f, RhiMax = 0.f, GpuMax = 0.f;
	float TickMax = 0.f, ClusterLagMax = 0.f;
	float GcIntSum = 0.f; int32 GcIntCount = 0;
	int32 StutterFrames = 0;
	int32 NiagaraActiveMax = 0;
	const float StutterThreshMs = (1000.f / 60.f) * 1.5f; // >25ms at 60fps target

	// Stutter timestamps + intervals (the killer diagnostic for ~1s periodicity)
	TArray<float> StutterTimestamps;
	TArray<float> StutterIntervals;
	float LastStutterT = -1.f;

	for (const FPerfSample& S : Samples)
	{
		FpsSum += S.Fps;
		FpsMin  = FMath::Min(FpsMin, S.Fps);
		FtMax   = FMath::Max(FtMax,  S.FtMs);
		GameMax = FMath::Max(GameMax, S.GameMs);
		DrawMax = FMath::Max(DrawMax, S.DrawMs);
		RhiMax  = FMath::Max(RhiMax,  S.RhiMs);
		GpuMax  = FMath::Max(GpuMax,  S.GpuMs);
		TickMax       = FMath::Max(TickMax,       S.TickMs);
		ClusterLagMax = FMath::Max(ClusterLagMax, S.ClusterLagMs);
		NiagaraActiveMax = FMath::Max(NiagaraActiveMax, S.NiagaraActive);
		if (S.GcInterval > 0.f) { GcIntSum += S.GcInterval; GcIntCount++; }
		// Use the per-frame wall-clock-based bStutter flag (set in Tick) — not ft_ms which is clamped
		if (S.bStutter)
		{
			StutterFrames++;
			StutterTimestamps.Add(S.Time);
			if (LastStutterT >= 0.f)
				StutterIntervals.Add(S.Time - LastStutterT);
			LastStutterT = S.Time;
		}
	}

	float StutterIntervalAvg = 0.f, StutterIntervalStdDev = 0.f;
	if (StutterIntervals.Num() > 0)
	{
		float Sum = 0.f;
		for (float I : StutterIntervals) Sum += I;
		StutterIntervalAvg = Sum / StutterIntervals.Num();
		float VarSum = 0.f;
		for (float I : StutterIntervals)
			VarSum += FMath::Square(I - StutterIntervalAvg);
		StutterIntervalStdDev = FMath::Sqrt(VarSum / StutterIntervals.Num());
	}

	const int32 N        = Samples.Num();
	const float FpsAvg   = N > 0 ? FpsSum / N : 0.f;
	const float GcIntAvg = GcIntCount > 0 ? GcIntSum / GcIntCount : 0.f;

	// Build JSON
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	{
		TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
		Meta->SetStringField(TEXT("date"),             FDateTime::Now().ToString(TEXT("%Y-%m-%dT%H:%M:%S")));
		Meta->SetNumberField(TEXT("duration_s"),       TrackingDuration);
		Meta->SetBoolField  (TEXT("pharus_active"),    !bPausePharus);
		Meta->SetNumberField(TEXT("fixed_frame_rate"), 60.0);
		Root->SetObjectField(TEXT("meta"), Meta);
	}

	{
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("fps_avg"),           FMath::RoundToFloat(FpsAvg   * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("fps_min"),           FMath::RoundToFloat(FpsMin   * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("frame_time_max_ms"), FMath::RoundToFloat(FtMax    * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("game_ms_max"),       FMath::RoundToFloat(GameMax  * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("draw_ms_max"),       FMath::RoundToFloat(DrawMax  * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("rhi_ms_max"),        FMath::RoundToFloat(RhiMax   * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("gpu_ms_max"),        FMath::RoundToFloat(GpuMax   * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("tick_ms_max"),        FMath::RoundToFloat(TickMax       * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("cluster_lag_ms_max"), FMath::RoundToFloat(ClusterLagMax * 10.f) / 10.f);
		Summary->SetNumberField(TEXT("gc_event_count"),    TotalGcEvents);
		Summary->SetNumberField(TEXT("gc_interval_avg_s"), FMath::RoundToFloat(GcIntAvg * 100.f) / 100.f);
		Summary->SetNumberField(TEXT("stutter_frames"),    StutterFrames);
		Summary->SetNumberField(TEXT("stutter_interval_avg_s"),    FMath::RoundToFloat(StutterIntervalAvg    * 1000.f) / 1000.f);
		Summary->SetNumberField(TEXT("stutter_interval_stddev_s"), FMath::RoundToFloat(StutterIntervalStdDev * 1000.f) / 1000.f);
		Summary->SetNumberField(TEXT("niagara_active_max"),        NiagaraActiveMax);

		// Raw timestamps + intervals — if all intervals cluster near a single value, periodicity confirmed
		TArray<TSharedPtr<FJsonValue>> TsJson;
		for (float T : StutterTimestamps)
			TsJson.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(T * 1000.f) / 1000.f));
		Summary->SetArrayField(TEXT("stutter_timestamps_s"), TsJson);

		TArray<TSharedPtr<FJsonValue>> IntJson;
		for (float I : StutterIntervals)
			IntJson.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(I * 1000.f) / 1000.f));
		Summary->SetArrayField(TEXT("stutter_intervals_s"), IntJson);
		Root->SetObjectField(TEXT("summary"), Summary);
	}

	{
		TArray<TSharedPtr<FJsonValue>> SamplesJson;
		SamplesJson.Reserve(N);
		for (const FPerfSample& S : Samples)
		{
			TSharedRef<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetNumberField(TEXT("t"),       FMath::RoundToFloat(S.Time       * 1000.f) / 1000.f);
			SObj->SetNumberField(TEXT("fps"),     FMath::RoundToFloat(S.Fps        *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("ft_ms"),   FMath::RoundToFloat(S.FtMs       *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("tick_ms"), FMath::RoundToFloat(S.TickMs     *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("game_ms"), FMath::RoundToFloat(S.GameMs     *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("draw_ms"), FMath::RoundToFloat(S.DrawMs     *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("rhi_ms"),  FMath::RoundToFloat(S.RhiMs      *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("gpu_ms"),  FMath::RoundToFloat(S.GpuMs      *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("ram_mb"),    FMath::RoundToFloat(S.RamMb));
			SObj->SetBoolField  (TEXT("gc"),        S.bGcEvent);
			SObj->SetNumberField(TEXT("gc_int"),    FMath::RoundToFloat(S.GcInterval * 1000.f) / 1000.f);
			SObj->SetNumberField(TEXT("shaders"),   S.ShaderJobs);
			SObj->SetNumberField(TEXT("nia_act"),   S.NiagaraActive);
			SObj->SetNumberField(TEXT("nia_emit"),  S.NiagaraEmitters);
			SObj->SetBoolField  (TEXT("stutter"),   S.bStutter);
			SObj->SetNumberField(TEXT("cluster_lag_ms"), FMath::RoundToFloat(S.ClusterLagMs * 10.f) / 10.f);
			SamplesJson.Add(MakeShared<FJsonValueObject>(SObj));
		}
		Root->SetArrayField(TEXT("samples"), SamplesJson);
	}

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(Root, Writer);

	const FString Filename = FString::Printf(TEXT("QUANTUM_ZOOM_PerformanceLog_%s.json"),
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	const FString BasePath = ExportPath.IsEmpty() ? FPaths::ProjectSavedDir() : ExportPath;
	IFileManager::Get().MakeDirectory(*BasePath, true); // create path recursively if needed
	const FString FullPath = FPaths::Combine(BasePath, Filename);

	if (FFileHelper::SaveStringToFile(JsonString, *FullPath))
	{
		LastExportMsg = FString::Printf(TEXT("EXPORT OK  ->  %s"), *Filename);
		UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] %s  (%d samples, %d GC, %d stutter frames)"),
			*LastExportMsg, N, TotalGcEvents, StutterFrames);
	}
	else
	{
		LastExportMsg = FString::Printf(TEXT("EXPORT FAILED  ->  %s"), *FullPath);
		UE_LOG(LogTemp, Warning, TEXT("[QPerfMonitor] Export failed: %s"), *FullPath);
	}

	// Stop the CSV profiler and schedule copy of the latest .csv next to the .json
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->ConsoleCommand(TEXT("csvProfile stop"));
		PendingCsvBaseName = Filename.LeftChop(5); // strip ".json"
		GetWorldTimerManager().SetTimer(CsvCopyTimer, this,
			&AQPerfMonitor::CopyLatestCsvProfile, 1.5f, false);
	}

	Samples.Empty();
}

void AQPerfMonitor::CopyLatestCsvProfile()
{
	const FString CsvDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling/CSV"));

	IFileManager& FM = IFileManager::Get();
	TArray<FString> Files;
	FM.FindFiles(Files, *(CsvDir / TEXT("*.csv")), true, false);
	if (Files.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QPerfMonitor] No CSV profile files found in %s"), *CsvDir);
		return;
	}

	// Find newest file by modification time
	FString LatestFile;
	FDateTime LatestTime = FDateTime::MinValue();
	for (const FString& F : Files)
	{
		const FString FullPath = FPaths::Combine(CsvDir, F);
		const FDateTime ModTime = FM.GetTimeStamp(*FullPath);
		if (ModTime > LatestTime)
		{
			LatestTime = ModTime;
			LatestFile = FullPath;
		}
	}
	if (LatestFile.IsEmpty()) return;

	const FString BasePath = ExportPath.IsEmpty() ? FPaths::ProjectSavedDir() : ExportPath;
	const FString DestPath = FPaths::Combine(BasePath, PendingCsvBaseName + TEXT(".csv"));

	if (FM.Copy(*DestPath, *LatestFile) == COPY_OK)
	{
		UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] CSV profile copied: %s"), *DestPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[QPerfMonitor] Failed to copy CSV: %s -> %s"),
			*LatestFile, *DestPath);
	}
}

void AQPerfMonitor::BroadcastHeartbeat()
{
	if (!IDisplayCluster::IsAvailable())
	{
		// Single-process / PIE: cluster lag is meaningless, leave at last value
		return;
	}
	FDisplayClusterClusterEventJson Event;
	Event.Name     = HeartbeatEventName;
	Event.Type     = TEXT("Heartbeat");
	Event.Category = TEXT("QPerf");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("ts"), FString::Printf(TEXT("%.6f"), FPlatformTime::Seconds()));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQPerfMonitor::OnHeartbeatClusterEvent(const FDisplayClusterClusterEventJson& Event)
{
	if (Event.Name != HeartbeatEventName) return;
	const double SentTime = FCString::Atod(*Event.Parameters.FindRef(TEXT("ts")));
	const double NowTime  = FPlatformTime::Seconds();
	LastClusterLagMs = static_cast<float>((NowTime - SentTime) * 1000.0);
}
