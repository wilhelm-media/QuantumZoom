#include "QPerfMonitor.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Components/WidgetComponent.h"
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
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

bool AQPerfMonitor::bIsOverlayCapturingInput = false;

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
	MenuSelection   = 0;
	bOverlayVisible = false;

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
	}

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

	// Widget — Wall node only
	WidgetComp = NewObject<UWidgetComponent>(this, TEXT("PerfOverlay"));
	WidgetComp->SetWidgetClass(UQPerfWidget::StaticClass());
	WidgetComp->SetWidgetSpace(EWidgetSpace::World);
	WidgetComp->SetDrawSize(FVector2D(800.f, 600.f));
	WidgetComp->RegisterComponent();
	WidgetComp->AttachToComponent(GetRootComponent(),
		FAttachmentTransformRules::KeepRelativeTransform);

	const float ScaleX = OverlaySize.X / 800.f;
	const float ScaleZ = OverlaySize.Y / 600.f;
	WidgetComp->SetRelativeScale3D(FVector(ScaleX, 1.f, ScaleZ));
	WidgetComp->InitWidget();
	WidgetComp->SetHiddenInGame(true);

	PerfWidget = Cast<UQPerfWidget>(WidgetComp->GetUserWidgetObject());

	UE_LOG(LogTemp, Log, TEXT("[QPerfMonitor] Ready. Toggle: %s  Confirm: %s"),
		*ToggleKey.GetFName().ToString(), *ConfirmKey.GetFName().ToString());
}

void AQPerfMonitor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IDisplayCluster::IsAvailable())
		IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(PharusClusterEventDelegate);
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

	// Track DCRA position
	if (DCRA && WidgetComp)
	{
		const FVector WorldOffset = DCRA->GetActorRotation().RotateVector(OverlayOffset);
		WidgetComp->SetWorldLocation(DCRA->GetActorLocation() + WorldOffset);
		WidgetComp->SetWorldRotation(DCRA->GetActorRotation() + FRotator(0.f, 180.f, 0.f));
	}

	// Smooth FPS/frametime for display
	const float InstFps = DeltaTime > 0.f ? (1.f / DeltaTime) : DisplayFps;
	DisplayFps  = DisplayFps  * 0.9f + InstFps              * 0.1f;
	DisplayFtMs = DisplayFtMs * 0.9f + (DeltaTime * 1000.f) * 0.1f;

	const float GpuMs = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	const float RamMb = static_cast<float>(MemStats.UsedPhysical) / (1024.f * 1024.f);

#if WITH_EDITOR
	const int32 ShaderJobs = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
	const int32 ShaderJobs = 0;
#endif

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
		S.Time       = TrackingTime;
		S.Fps        = InstFps;
		S.FtMs       = DeltaTime * 1000.f;
		S.GpuMs      = GpuMs;
		S.RamMb      = RamMb;
		S.bGcEvent   = bGcThisFrame;
		S.GcInterval = LastGcInterval;
		S.ShaderJobs = ShaderJobs;
		Samples.Add(S);

		if (TrackingTime >= TrackingDuration)
			StopAndExport();
	}

	bGcThisFrame = false;

	if (PerfWidget && bOverlayVisible)
	{
		const float Countdown = bTracking ? FMath::Max(0.f, TrackingDuration - TrackingTime) : 0.f;
		const FString ResolvedExportPath = ExportPath.IsEmpty() ? FPaths::ProjectSavedDir() : ExportPath;
		PerfWidget->UpdateStats(
			DisplayFps, DisplayFtMs, GpuMs, RamMb,
			TotalGcEvents, LastGcInterval, ShaderJobs,
			bPharusRunning, ActivePharusTracks,
			bTracking, Countdown, LastExportMsg,
			MenuSelection, bExportEnabled, bPausePharus, ResolvedExportPath);
	}
}

void AQPerfMonitor::HandleInput()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	// ── Toggle overlay ────────────────────────────────────────────────────────
	const bool bToggleDown = PC->IsInputKeyDown(ToggleKey);
	if (bToggleDown && !bTogglePrev)
	{
		bOverlayVisible = !bOverlayVisible;
		if (WidgetComp) WidgetComp->SetHiddenInGame(!bOverlayVisible);
		LastExportMsg.Empty();
	}
	bTogglePrev = bToggleDown;

	// ── Menu navigation (only when overlay is open) ───────────────────────────
	if (!bOverlayVisible) return;

	const bool bNavUp   = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up);
	const bool bNavDown = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down);
	const bool bConfirm = PC->IsInputKeyDown(ConfirmKey);

	if (bNavUp && !bNavUpPrev)
		MenuSelection = FMath::Max(0, MenuSelection - 1);

	if (bNavDown && !bNavDownPrev)
		MenuSelection = FMath::Min(3, MenuSelection + 1);  // 0=EXPORT, 1=PHARUS ON/OFF, 2=TRACK PHARUS, 3=START TRACKING

	if (bConfirm && !bConfirmPrev)
	{
		if (MenuSelection == 0)
		{
			bExportEnabled = !bExportEnabled;
		}
		else if (MenuSelection == 1)
		{
			BroadcastPharusToggle(!bPharusRunning);
		}
		else if (MenuSelection == 2)
		{
			bPausePharus = !bPausePharus;
		}
		else if (MenuSelection == 3 && !bTracking)
		{
			StartTracking();
		}
	}

	bNavUpPrev   = bNavUp;
	bNavDownPrev = bNavDown;
	bConfirmPrev = bConfirm;
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
	float GcIntSum = 0.f; int32 GcIntCount = 0;
	int32 StutterFrames = 0;
	const float StutterThreshMs = (1000.f / 60.f) * 1.5f; // >25ms at 60fps target

	for (const FPerfSample& S : Samples)
	{
		FpsSum += S.Fps;
		FpsMin  = FMath::Min(FpsMin, S.Fps);
		FtMax   = FMath::Max(FtMax,  S.FtMs);
		if (S.GcInterval > 0.f) { GcIntSum += S.GcInterval; GcIntCount++; }
		if (S.FtMs > StutterThreshMs) StutterFrames++;
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
		Summary->SetNumberField(TEXT("gc_event_count"),    TotalGcEvents);
		Summary->SetNumberField(TEXT("gc_interval_avg_s"), FMath::RoundToFloat(GcIntAvg * 100.f) / 100.f);
		Summary->SetNumberField(TEXT("stutter_frames"),    StutterFrames);
		Root->SetObjectField(TEXT("summary"), Summary);
	}

	{
		TArray<TSharedPtr<FJsonValue>> SamplesJson;
		SamplesJson.Reserve(N);
		for (const FPerfSample& S : Samples)
		{
			TSharedRef<FJsonObject> SObj = MakeShared<FJsonObject>();
			SObj->SetNumberField(TEXT("t"),      FMath::RoundToFloat(S.Time       * 1000.f) / 1000.f);
			SObj->SetNumberField(TEXT("fps"),    FMath::RoundToFloat(S.Fps        *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("ft_ms"),  FMath::RoundToFloat(S.FtMs       *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("gpu_ms"), FMath::RoundToFloat(S.GpuMs      *   10.f) /   10.f);
			SObj->SetNumberField(TEXT("ram_mb"), FMath::RoundToFloat(S.RamMb));
			SObj->SetBoolField  (TEXT("gc"),     S.bGcEvent);
			SObj->SetNumberField(TEXT("gc_int"), FMath::RoundToFloat(S.GcInterval * 1000.f) / 1000.f);
			SObj->SetNumberField(TEXT("shaders"),S.ShaderJobs);
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

	Samples.Empty();
}
