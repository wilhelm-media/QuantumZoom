#include "QZoomStagePawn.h"
#include "HAL/IConsoleManager.h"   // R3 Nanite-diagnostic cvar toggle (r.Nanite.ProxyRenderMode)
#include "RHIDefinitions.h"         // ERHIFeatureLevel — the HUD reports the node's feature level (SM5 vs SM6)
#include "SceneInterface.h"         // FSceneInterface::GetFeatureLevel()
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Font.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Engine/PostProcessVolume.h"
#include "Kismet/GameplayStatics.h"
#include "DisplayClusterRootActor.h"   // free-look drives the DCRA in a cluster (nDisplay frustum source)
#include "Camera/PlayerCameraManager.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "NiagaraComponent.h"

const FString AQZoomStagePawn::EventName = TEXT("QZoomStage.State");
static const FName TAG_STATION(TEXT("QZStation"));

AQZoomStagePawn::AQZoomStagePawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);

	// Constant scale/zoom panel — parented to the (fixed) camera, so it sits at a stable stereo depth.
	Readout = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Readout"));
	Readout->SetupAttachment(Camera);
	Readout->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));   // face back toward the camera
	Readout->SetHorizontalAlignment(EHTA_Left);               // anchor the panel from its top-left
	Readout->SetVerticalAlignment(EVRTA_TextTop);
	Readout->SetTextRenderColor(FColor(190, 225, 255));

	// Fixed per-stage detail block — camera-parented (so it never moves with the scaling geometry).
	auto MakeText = [this](const TCHAR* Name) -> UTextRenderComponent*
	{
		UTextRenderComponent* T = CreateDefaultSubobject<UTextRenderComponent>(Name);
		T->SetupAttachment(Camera);
		T->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
		T->SetHorizontalAlignment(EHTA_Left);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		return T;
	};
	DetailTitle = MakeText(TEXT("DetailTitle"));
	DetailSub   = MakeText(TEXT("DetailSub"));
	DetailScale = MakeText(TEXT("DetailScale"));
	DetailProv  = MakeText(TEXT("DetailProv"));

	Streaks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Streaks"));
	Streaks->SetupAttachment(Root);
	Streaks->SetCastShadow(false);
	Streaks->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Info-background panels — a SEPARATE depth layer behind the text (camera-parented), so the
	// text parallax-pops in front of them in stereo. Depth separation = BackgroundDepthOffset.
	auto MakeBG = [this](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Camera);
		C->SetCastShadow(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return C;
	};
	ReadoutBG = MakeBG(TEXT("ReadoutBG"));
	DetailBG  = MakeBG(TEXT("DetailBG"));
	// Editorial hairline rules + progress bar — thin emissive geometry (same camera-parented factory).
	ReadoutBar     = MakeBG(TEXT("ReadoutBar"));
	ReadoutBarFill = MakeBG(TEXT("ReadoutBarFill"));
	DetailRule     = MakeBG(TEXT("DetailRule"));

	// Default per-stage info (ASCII for font safety; editable in the Details panel).
	// ASCII fallback (CDO). The placed pawn's authoritative pretty copy (Å/µm/en-dash) is set unicode-safe
	// by config_editorial.py; keeping the source pure-ASCII avoids any /utf-8 / C4819 dependency at compile.
	StageTitle      = { TEXT("LAB"), TEXT("FUNGAL CELL"), TEXT("NirA PROTEIN"), TEXT("MET169"), TEXT("ELECTRON DENSITY"), TEXT("NUCLEUS - QUARKS") };
	StageSub        = { TEXT("Aspergillus nidulans culture"), TEXT("hyphal cell / nucleus"), TEXT("AlphaFold P28348"), TEXT("sulfur atom - the redox switch"), TEXT("S-O bonding orbital (DFT)"), TEXT("S-32: quarks + gluons") };
	StageScaleLabel = { TEXT("~9 cm"), TEXT("~2-5 um"), TEXT("~10 nm"), TEXT("~0.2 nm"), TEXT("~1 A"), TEXT("~8 fm") };
	StageProv       = { TEXT("visible light"), TEXT("light microscopy"), TEXT("predicted structure"), TEXT("ball-and-stick model"), TEXT("first-principles B3LYP"), TEXT("QCD illustration - not imaged") };

	// PIE only takes possession (see BeginPlay). In the cluster the nDisplay DCRA is the view and this
	// pawn must NOT possess — it just ticks, drives ZoomProgress, and broadcasts.
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AQZoomStagePawn::BeginPlay()
{
	Super::BeginPlay();

	bInCluster = IDisplayCluster::IsAvailable()
	          && IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster;
	if (bInCluster)
	{
		bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
		ClusterListener = FOnClusterEventJsonListener::CreateUObject(this, &AQZoomStagePawn::OnClusterEvent);
		IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(ClusterListener);
	}
	else
	{
		bIsPrimary = true;   // PIE / editor / standalone
	}
	UE_LOG(LogTemp, Log, TEXT("[QZoomStagePawn] cluster=%d primary=%d"), bInCluster, bIsPrimary);

	// SVT STREAMING BANDWIDTH (the mip-pop fix, see UpdateOxidation): the default 512 MiB/s throttle serves
	// LOWER MIPS when exceeded — our ping-pong's fast sweep (2 volumes x ~187 fps peak x ~1.4 MB/frame)
	// blows through it, so the sulfur switch flashed blurry near every turnaround. Deep Space nodes have
	// NVMe + big GPUs; give the SVT streamer real headroom. Runs on EVERY node (BeginPlay is per-process).
	if (IConsoleVariable* CvBW = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SparseVolumeTexture.Streaming.BandwidthLimit")))
		CvBW->Set(FMath::Max(SVTBandwidthMiB, 512), ECVF_SetByConsole);

	// PIE only: become the view. In a cluster the nDisplay DCRA renders — do NOT possess (the pawn just
	// ticks + broadcasts). Input polling works either way via GetFirstPlayerController on the primary.
	if (!bInCluster)
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			if (PC->GetPawn() != this) PC->Possess(this);

	// The pawn KEEPS its placed orientation (matching the static DCRA, +X) so the camera-attached UI stays
	// aligned with the cluster view. The zoom CENTRE is set purely by Anchor: the stations sit there and the
	// streak field is aimed there each frame (UpdateStreaks). Move Anchor (esp. Z=up, Y=right) to move it.

	if (Readout)
	{
		Readout->SetRelativeLocation(ReadoutOffset);
		Readout->SetWorldSize(ReadoutSize);
	}

	// Always-on-top text material so station geometry can never occlude or shade the info layer.
	if (!ReadoutMaterial)
		ReadoutMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_ReadoutText.M_ReadoutText"));
	if (ReadoutMaterial)
		for (UTextRenderComponent* T : { Readout, DetailTitle, DetailSub, DetailScale, DetailProv })
			if (T) T->SetTextMaterial(ReadoutMaterial);
	if (Readout) Readout->SetTextRenderColor(TextColor.ToFColor(true));   // constant readout uses the interface colour

	// HUD ON TOP, no environment reaction (Michael): the text was being occluded by 3D objects and picking up
	// lights/shadows. The material is already Unlit + DisableDepthTest, but the COMPONENTS also need:
	//  - a very high translucency sort priority so they draw AFTER all scene translucency (never behind a mesh)
	//  - no shadow casting / receiving (a TextRender shouldn't, but be explicit)
	//  - render in the main pass only (so nothing depth-writes them into occlusion)
	// Excluding them from the PP presets: they are Unlit + depth-test-off, so lighting/shadows already miss
	// them; the remaining grade (tonemap/color) is applied to the whole frame — the honest way to exempt HUD
	// from that is a separate post-tonemap pass, which TextRender can't do alone. Flagged below.
	for (UTextRenderComponent* T : { Readout, DetailTitle, DetailSub, DetailScale, DetailProv })
	{
		if (!T) continue;
		T->SetTranslucentSortPriority(1000);   // draw last, on top of scene translucency
		T->SetCastShadow(false);
		T->bAffectDynamicIndirectLighting = false;
		T->bAffectDistanceFieldLighting = false;
		T->SetReceivesDecals(false);
	}

	// Fixed detail block (camera-relative) — EDITORIAL: RIGHT-aligned column, anchored from the TOP so it sits
	// at the SAME height as the readout. Title (optional serif) → hairline rule → sub → scale → provenance.
	for (UTextRenderComponent* T : { DetailTitle, DetailSub, DetailScale, DetailProv })
		if (T) { T->SetHorizontalAlignment(EHTA_Right); T->SetVerticalAlignment(EVRTA_TextTop); }
	if (!TitleFont)
		TitleFont = LoadObject<UFont>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_fonts/F_Cormorant.F_Cormorant"));
	if (TitleFont && DetailTitle) DetailTitle->SetFont(TitleFont);   // guarded: null → keep the legible default sans
	if (DetailTitle) { DetailTitle->SetRelativeLocation(DetailOffset);                                    DetailTitle->SetWorldSize(DetailSize * 1.7f); }
	if (DetailSub)   { DetailSub  ->SetRelativeLocation(DetailOffset + FVector(0, 0, -DetailSize * 2.6f)); DetailSub  ->SetWorldSize(DetailSize); }
	if (DetailScale) { DetailScale->SetRelativeLocation(DetailOffset + FVector(0, 0, -DetailSize * 3.8f)); DetailScale->SetWorldSize(DetailSize * 0.95f); }
	if (DetailProv)  { DetailProv ->SetRelativeLocation(DetailOffset + FVector(0, 0, -DetailSize * 4.9f)); DetailProv ->SetWorldSize(DetailSize * 0.8f); }

	// Info-background panels: a vertical quad behind each text block, pushed BackgroundDepthOffset deeper
	// (so the text pops toward the viewer in stereo). Centred behind the top-left-anchored text.
	UStaticMesh*        Plane    = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* PanelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_UIPanel.M_UIPanel"));
	auto SetupBG = [&](UStaticMeshComponent* BG, TObjectPtr<UMaterialInstanceDynamic>& MID, float TextForward, const FVector2D& CenterYZ, const FVector2D& Size)
	{
		if (!BG) return;
		if (Plane) BG->SetStaticMesh(Plane);
		if (PanelMat)
		{
			MID = UMaterialInstanceDynamic::Create(PanelMat, this);
			if (MID) { MID->SetVectorParameterValue(TEXT("Color"), BackgroundColor); BG->SetMaterial(0, MID); }
		}
		BG->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));   // stand the plane up, facing the camera (two-sided)
		BG->SetRelativeLocation(FVector(TextForward + BackgroundDepthOffset, CenterYZ.X, CenterYZ.Y));   // panel centre, pushed deeper
		BG->SetRelativeScale3D(FVector(Size.Y / 100.f, Size.X / 100.f, 1.f));   // BasicShapes/Plane is 100uu (H->X, W->Y)
		BG->SetVisibility(bShowBackground);
	};
	SetupBG(ReadoutBG, ReadoutBGMID, ReadoutOffset.X, ReadoutBGCenter, ReadoutBGSize);
	SetupBG(DetailBG,  DetailBGMID,  DetailOffset.X,  DetailBGCenter,  DetailBGSize);

	// Editorial accent geometry: readout progress track + fill, and the detail-title hairline rule.
	// Thin emissive cubes (two-sided, always-on-top via M_Accent). Colour = single brand accent.
	UStaticMesh*        Cube   = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* AccMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_Accent.M_Accent"));
	if (!AccMat) AccMat = ReadoutMaterial;   // fallback: reuse the always-on-top text material (rules read white)
	const float Th = FMath::Max(RuleThickness, 0.25f) / 100.f;
	// progress track: below the four readout lines, left end at the readout's left column
	ReadoutBarFwd  = ReadoutOffset.X;
	ReadoutBarLeft = ReadoutOffset.Y;
	ReadoutBarUp   = ReadoutOffset.Z - ReadoutSize * 7.0f;
	SetupAccentMesh(ReadoutBar, ReadoutBarMID, Cube, AccMat, AccentColor, TrackDim);
	if (ReadoutBar)
	{
		ReadoutBar->SetRelativeLocation(FVector(ReadoutBarFwd, ReadoutBarLeft + ReadoutRuleWidth * 0.5f, ReadoutBarUp));
		ReadoutBar->SetRelativeScale3D(FVector(0.02f, ReadoutRuleWidth / 100.f, Th));
	}
	SetupAccentMesh(ReadoutBarFill, ReadoutBarFillMID, Cube, AccMat, AccentColor, 1.0f);   // width driven per-frame
	// detail-title underline: right-aligned to the detail column's right edge, just under the title
	SetupAccentMesh(DetailRule, DetailRuleMID, Cube, AccMat, AccentColor, 1.0f);
	if (DetailRule)
	{
		const float RuleUp = DetailOffset.Z - DetailSize * 1.9f;
		DetailRule->SetRelativeLocation(FVector(DetailOffset.X, DetailOffset.Y - DetailRuleWidth * 0.5f, RuleUp));
		DetailRule->SetRelativeScale3D(FVector(0.02f, DetailRuleWidth / 100.f, Th));
	}

	InitStreaks();
	// Stage soundscape: emit ONLY on the primary node (or PIE) so the cluster doesn't echo N× — the
	// non-primary nodes still tick + drive ZoomProgress, they just make no sound.
	if (!bInCluster || bIsPrimary)
		InitStageAudio();

	// Cinematic post-processing: an UNBOUND master volume (affects the DCRA render on every node). Reuse one
	// already in the level if present, else spawn one. Cycled through 4 presets with Up/Down arrows.
	if (GetWorld())
		PPVolume = GetWorld()->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity);
	if (PPVolume) { PPVolume->bUnbound = true; PPVolume->Priority = 1000.f; PPVolume->BlendWeight = 1.f; PPVolume->bEnabled = true; }
	ApplyPPPreset(PPPreset);
	UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] PPVolume %s"), PPVolume ? TEXT("spawned") : TEXT("FAILED — presets have no volume"));

	// ── STARTUP TIMING ──────────────────────────────────────────────────────────────────────────────────
	// I have guessed at this stall four times (warm-up, fillers, DDC, DMIs) and been wrong each time.
	// Stop guessing: TIME each phase and let the log name the cost.
	{
		const double T0 = FPlatformTime::Seconds();
		InitFillers();          // scientific space fillers (F cycles off/motes/grid/structures)
		const double T1 = FPlatformTime::Seconds();
		BuildMaterialCache();   // every DMI up front, so the per-frame path only SETS parameters
		const double T2 = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] BeginPlay: InitFillers %.1f ms | BuildMaterialCache %.1f ms"),
			(T1 - T0) * 1000.0, (T2 - T1) * 1000.0);
	}
	SetFillerMode(FillerMode);
	PrevZoom = ZoomProgress;

	ApplyStations();
	UpdateReadout();
	UpdateInfoLayer();
}

void AQZoomStagePawn::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bInCluster)
		IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(ClusterListener);
	Super::EndPlay(Reason);
}

void AQZoomStagePawn::Tick(float Dt)
{
	Super::Tick(Dt);
	if (bIsPrimary)
	{
		PollInput(Dt);
		Broadcast();
	}
	// ── HITCH DETECTOR ──────────────────────────────────────────────────────────────────────────────────
	// Michael reports a heavy stall at the start that clears once the lab settles. Rather than theorise a
	// fifth time, measure: log any frame slower than HitchMs, with the pawn's own phase timings, and say
	// whether the time is INSIDE this Tick or outside it (= renderer/streaming/PSO, i.e. not the pawn).
	const double TickT0 = FPlatformTime::Seconds();
	static double LastFrameEnd = 0.0;
	const double GapMs = (LastFrameEnd > 0.0) ? (TickT0 - LastFrameEnd) * 1000.0 : 0.0;

	const double B0 = FPlatformTime::Seconds();
	BuildMaterialCache();   // no-op once every station's DMIs are cached (cheap count check while streaming)
	const double B1 = FPlatformTime::Seconds();
	ApplyStations();    // every node (secondaries got ZoomProgress/orbit via the event)
	const double B2 = FPlatformTime::Seconds();

	// Cluster-consistent visual velocity from the synced ZoomProgress delta (valid on every node).
	const float VisVel = (Dt > 1e-5f) ? (ZoomProgress - PrevZoom) / Dt : 0.f;
	PrevZoom = ZoomProgress;

	// Observer model: the observer shrinks with the scale to keep the hero wall-sized. Holding a fixed
	// real-world pace (ObserverRefSpeed) as it shrinks makes the RELATIVE speed blow up with depth —
	// that is the cue you asked for; the streak build-up then falls out of the physics.
	const float M  = CurrentScaleMeters();
	const float S0 = (ScaleMeters.Num() > 0) ? ScaleMeters[0] : 1.f;
	ObserverSize  = ObserverStartSize * (M / FMath::Max(S0, 1e-30f));                     // shrinks from human scale
	const float ZoomAct = FMath::Clamp(FMath::Abs(VisVel) / FMath::Max(BaseZoomRate, 1e-4f), 0.f, 1.f);
	ObserverSpeed = (ObserverRefSpeed / FMath::Max(ObserverSize, 1e-30f)) * ZoomAct;      // observer-sizes / sec

	UpdateReadout();
	UpdateInfoLayer();

	// Streaks fade in as the relative speed climbs (log scale) — no streaks up top, building deep.
	float Intensity = 0.f;
	if (ObserverSpeed > 1.f)
	{
		const float Lo = FMath::Loge(FMath::Max(StreakSpeedLo, 1.f));
		const float Hi = FMath::Loge(FMath::Max(StreakSpeedHi, StreakSpeedLo * 10.f));
		float t = FMath::Clamp((FMath::Loge(ObserverSpeed) - Lo) / (Hi - Lo), 0.f, 1.f);
		Intensity = t * t * (3.f - 2.f * t);
	}
	// Time-smooth: gentle GLIDE in (StreakFade), snappier retract out (StreakFadeOut) so a stop reads crisp.
	const float FadeRate = (Intensity < StreakIntensitySmoothed) ? StreakFadeOut : StreakFade;
	StreakIntensitySmoothed = FMath::FInterpTo(StreakIntensitySmoothed, Intensity, Dt, FadeRate);
	UpdateStreaks(Dt, VisVel, StreakIntensitySmoothed);
	UpdateAudio();
	UpdateFillers(Dt);   // molecular fillers: inherit orbit + self-swirl + zoom-scale
	UpdateLights();      // fade station-sublevel lights by their station's visibility (populated in ApplyStations)
	ApplyStyleLight();   // every frame so the intensity EASES toward its ladder target (not just on press)
	UpdateOxidation(Dt); // S3: ping-pong the sulfur-switch SVT frame (dwell -> snap -> dwell)
	UpdateGuides();      // authoring aid: fixed front-facing marker + zoom-centre reticle at the Anchor
	ApplyNiraShells();   // hide SM_S2_NirA's enclosing ribbon/VOLUME so they don't obscure MET169
	ApplyPalette();      // squeeze the hue spread toward the amber/blue poles (Back/Start, R3 = reset)
	UpdateCH4Cycle(Dt);  // CH4: FmoB docks + oxidises Met169, reductase strips it back. Loops.
	UpdateRefParticles();// log-spaced self-similar mote field: the scale reference

	// ── HITCH REPORT ────────────────────────────────────────────────────────────────────────────────────
	const double TickT1 = FPlatformTime::Seconds();
	const double TickMs  = (TickT1 - TickT0) * 1000.0;
	const double FrameMs = GapMs + TickMs;
	LastFrameEnd = TickT1;
	if (bLogHitches && FrameMs > HitchMs && HitchesLogged < 400)
	{
		++HitchesLogged;
		// GapMs = everything OUTSIDE this Tick (render thread, GPU, streaming, PSO compiles, other actors).
		// TickMs = this pawn. If Gap >> Tick, the stall is NOT my code — and that is the answer we need.
		UE_LOG(LogTemp, Warning,
			TEXT("[QZoomHitch] frame %.1f ms = pawn %.2f (cache %.2f, stations %.2f) + OUTSIDE %.1f  | zoom %.0f%% warm %d"),
			FrameMs, TickMs, (B1 - B0) * 1000.0, (B2 - B1) * 1000.0, GapMs, ZoomProgress * 100.f, WarmupLeft);
	}
}

void AQZoomStagePawn::PollInput(float Dt)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	// Zoom: RT in / LT out (+ W/S keyboard fallback). Constant progress rate → exponential visual
	// scale = constant orders-of-magnitude per second, the natural Powers-of-Ten cadence.
	float RT = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
	float LT = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
	if (PC->IsInputKeyDown(EKeys::W)) RT = FMath::Max(RT, 1.f);   // Up/Down freed for PP presets
	if (PC->IsInputKeyDown(EKeys::S)) LT = FMath::Max(LT, 1.f);
	const float Target = (RT - LT) * BaseZoomRate;
	ZoomVel = FMath::FInterpTo(ZoomVel, Target, Dt, SpeedDamping);
	ZoomProgress = FMath::Clamp(ZoomProgress + ZoomVel * Dt, 0.f, 1.f);

	// Look-around ORBITS the content (camera stays fixed → DCRA stays fixed).
	const float DZ = 0.15f;
	float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	float RY = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	RX = (FMath::Abs(RX) > DZ) ? RX : 0.f;
	RY = (FMath::Abs(RY) > DZ) ? RY : 0.f;
	OrbitYaw   += RX * OrbitRate * Dt;
	OrbitPitch += RY * OrbitRate * Dt;   // completely free — no pitch clamp (MaxPitch now unused)

	// LEFT STICK = FREE LOOK: turn the camera in place, position stays on the orbit path. After FreeLookHold
	// seconds idle, ease back to centre over FreeLookReturn seconds. Any input cancels the return instantly.
	float LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	float LY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	LX = (FMath::Abs(LX) > DZ) ? LX : 0.f;
	LY = (FMath::Abs(LY) > DZ) ? LY : 0.f;
	if (LX != 0.f || LY != 0.f)
	{
		// active input: drive the offset, reset the idle clock, cancel any in-progress return
		LookYaw   = FMath::Clamp(LookYaw   + LX * FreeLookRate * Dt, -FreeLookMaxYaw,   FreeLookMaxYaw);
		LookPitch = FMath::Clamp(LookPitch + LY * FreeLookRate * Dt, -FreeLookMaxPitch, FreeLookMaxPitch);   // up=up
		LookIdle = 0.f;
		LookRetT = -1.f;
	}
	else
	{
		LookIdle += Dt;
		if (LookRetT < 0.f && LookIdle >= FreeLookHold)   // idle long enough: begin the ease-back
		{
			LookRetT = 0.f; LookRetFromYaw = LookYaw; LookRetFromPitch = LookPitch;
		}
		if (LookRetT >= 0.f)
		{
			LookRetT += Dt;
			const float u = FMath::Clamp(LookRetT / FMath::Max(FreeLookReturn, 1e-3f), 0.f, 1.f);
			const float e = u * u * u * (u * (u * 6.f - 15.f) + 10.f);   // smootherstep
			LookYaw   = FMath::Lerp(LookRetFromYaw,   0.f, e);
			LookPitch = FMath::Lerp(LookRetFromPitch, 0.f, e);
			if (u >= 1.f) { LookYaw = 0.f; LookPitch = 0.f; LookRetT = -1.f; }
		}
	}
	ApplyFreeLook(LookYaw, LookPitch);

	// D-Pad Up/Down = PP preset (neutral + P1..P4 = 5 states); D-Pad Left/Right = space-filler cycle. Rising-edge.
	const bool bUp = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up),   bDn = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down);
	const bool bLf = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left), bRt = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right);
	if (bUp && !bUpPrev)    { PPPreset   = (PPPreset   + 1) % 10; ApplyPPPreset(PPPreset); }
	if (bDn && !bDownPrev)  { PPPreset   = (PPPreset   + 9) % 10; ApplyPPPreset(PPPreset); }
	if (bRt && !bRightPrev) { FillerMode = (FillerMode + 1) % 4; SetFillerMode(FillerMode); }
	if (bLf && !bLeftPrev)  { FillerMode = (FillerMode + 3) % 4; SetFillerMode(FillerMode); }
	bUpPrev = bUp; bDownPrev = bDn; bLeftPrev = bLf; bRightPrev = bRt;

	// LB / RB = STYLE LIGHT intensity (the freed S4 key light). 5 steps: off / dim / base / bright / blown.
	const bool bLB = PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder), bRB = PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder);
	// clamp to the LADDER's length, not a hard-coded 5 — otherwise the extra high steps are unreachable
	if (bRB && !bRBPrev) { StyleLightStep = FMath::Min(StyleLightStep + 1, FMath::Max(StyleLightLadder.Num() - 1, 0)); ApplyStyleLight(); }
	if (bLB && !bLBPrev) { StyleLightStep = FMath::Max(StyleLightStep - 1, 0); ApplyStyleLight(); }
	bLBPrev = bLB; bRBPrev = bRB;

	// A / B (face bottom/right) = filler DENSITY (moved off the shoulders). 5 levels, sparse -> EXTREME.
	const bool bA = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom), bB = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Right);
	if (bB && !bBPrev) { FillerDensity = FMath::Min(FillerDensity + 1, 4); InitFillers(); }
	if (bA && !bAPrev) { FillerDensity = FMath::Max(FillerDensity - 1, 0); InitFillers(); }
	bAPrev = bA; bBPrev = bB;

	// X (face-button left) = cycle the NirA representation: 0 high-res FBX / 1 low-res FBX / 2 procedural
	const bool bX = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);
	if (bX && !bXPrev) { NiraVersion = (NiraVersion + 1) % FMath::Max(NiraVersionCount, 1); }
	bXPrev = bX;

	// Y (face-button top) = CLEAN MODE: hide the whole HUD for clean photography plates.
	const bool bY = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top);
	if (bY && !bYPrev) { bCleanMode = !bCleanMode; SetCleanMode(bCleanMode); }
	bYPrev = bY;

	// F2 toggles the authoring guides (front-facing marker + centre reticle). Keyboard-only ON PURPOSE: it is
	// a setup aid, not show content — a controller button could be hit by a visitor mid-installation.
	const bool bG = PC->IsInputKeyDown(EKeys::F2);
	if (bG && !bGuidePrev) { bShowFrontIndicator = !bShowFrontIndicator; }
	bGuidePrev = bG;

	// PALETTE squeeze — Back/Start step the hue window, R3 resets to the authored palette.
	// (Back/Start/R3 were the only free inputs: LB/RB=light, A/B=filler density, X=NirA version,
	//  Y=clean mode, L3=oxidation freeze, D-pad=PP preset + filler mode, triggers=zoom, R-stick=orbit.)
	const bool bPm = PC->IsInputKeyDown(EKeys::Gamepad_Special_Left)  || PC->IsInputKeyDown(EKeys::LeftBracket);
	const bool bPp = PC->IsInputKeyDown(EKeys::Gamepad_Special_Right) || PC->IsInputKeyDown(EKeys::RightBracket);
	if (bPm && !bPalMinusPrev) PaletteWidth = FMath::Clamp(PaletteWidth - PaletteStep, 0.f, 1.f);  // tighter
	if (bPp && !bPalPlusPrev)  PaletteWidth = FMath::Clamp(PaletteWidth + PaletteStep, 0.f, 1.f);  // wider
	bPalMinusPrev = bPm; bPalPlusPrev = bPp;

	// R3 (right-stick click) = NANITE DIAGNOSTIC cycle, for pinning the low-poly-hull-on-the-wall issue
	// WITHOUT the console (hard to reach in the cluster). Cycles r.Nanite.ProxyRenderMode:
	//   0 = normal (whatever the cluster does now)
	//   2 = FORCE fallback (should look WORSE — if it matches the bug, the cluster was already doing this)
	//   3 = FORCE full Nanite (should look BEST — if this fixes it, we bake it into the packaged config)
	// The current mode is shown in the HUD (NaniteDiag) so it's readable on the wall.
	const bool bR3 = PC->IsInputKeyDown(EKeys::Gamepad_RightThumbstick);
	if (bR3 && !bNaniteDiagPrev)
	{
		// The PRIMARY steps the counter here; Broadcast() sends 'nan' and every node applies the cvar in
		// OnClusterEvent so wall + floor stay in lock-step. The primary must ALSO apply it locally right now:
		// it just bumped NaniteDiagStep, so when its OWN broadcast loops back the (NewNan != NaniteDiagStep)
		// guard is already false and the cvar would never actually get set on THIS node — which is exactly
		// why R3 did nothing on the single-node local switchboard. Apply here (idempotent); OnClusterEvent
		// converges the secondaries.
		NaniteDiagStep = (NaniteDiagStep + 1) % 3;
		ApplyNaniteDiag();
	}
	bNaniteDiagPrev = bR3;

	// L3 (left-stick click) = freeze/resume the S3 oxidation ping-pong (hold a hero state for a photo).
	const bool bL3 = PC->IsInputKeyDown(EKeys::Gamepad_LeftThumbstick);
	if (bL3 && !bL3Prev) { bOxFrozen = !bOxFrozen; }
	bL3Prev = bL3;
}

void AQZoomStagePawn::Broadcast()
{
	if (!bInCluster) return;
	FDisplayClusterClusterEventJson Event;
	Event.Name     = EventName;
	Event.Type     = TEXT("State");
	Event.Category = TEXT("QZoomStage");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("z"),     FString::SanitizeFloat(ZoomProgress));
	Event.Parameters.Add(TEXT("yaw"),   FString::SanitizeFloat(OrbitYaw));
	Event.Parameters.Add(TEXT("pitch"), FString::SanitizeFloat(OrbitPitch));
	Event.Parameters.Add(TEXT("pp"),    FString::FromInt(PPPreset));
	Event.Parameters.Add(TEXT("fill"),  FString::FromInt(FillerMode));
	Event.Parameters.Add(TEXT("dens"),  FString::FromInt(FillerDensity));
	Event.Parameters.Add(TEXT("swirl"), FString::SanitizeFloat(FillerSwirl));
	Event.Parameters.Add(TEXT("nver"),  FString::FromInt(NiraVersion));
	Event.Parameters.Add(TEXT("slit"),  FString::FromInt(StyleLightStep));
	Event.Parameters.Add(TEXT("clean"), FString::FromInt(bCleanMode ? 1 : 0));
	Event.Parameters.Add(TEXT("oxt"),   FString::SanitizeFloat(OxTime));
	Event.Parameters.Add(TEXT("oxf"),   FString::FromInt(bOxFrozen ? 1 : 0));
	Event.Parameters.Add(TEXT("pal"),   FString::SanitizeFloat(PaletteWidth));   // palette squeeze: all walls agree
	Event.Parameters.Add(TEXT("nan"),   FString::FromInt(NaniteDiagStep));       // Nanite diagnostic mode: every node
	Event.Parameters.Add(TEXT("lyaw"),  FString::SanitizeFloat(LookYaw));        // free-look offset: the FLOOR node needs it
	Event.Parameters.Add(TEXT("lpit"),  FString::SanitizeFloat(LookPitch));      // (floor is a separate node, no PollInput)
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQZoomStagePawn::OnClusterEvent(const FDisplayClusterClusterEventJson& E)
{
	if (E.Name != EventName) return;
	ZoomProgress = FCString::Atof(*E.Parameters.FindRef(TEXT("z")));
	OrbitYaw     = FCString::Atof(*E.Parameters.FindRef(TEXT("yaw")));
	OrbitPitch   = FCString::Atof(*E.Parameters.FindRef(TEXT("pitch")));
	const int32 NewPP = FCString::Atoi(*E.Parameters.FindRef(TEXT("pp")));
	if (NewPP != PPPreset)     { PPPreset = NewPP;     ApplyPPPreset(PPPreset); }
	const int32 NewFill = FCString::Atoi(*E.Parameters.FindRef(TEXT("fill")));
	if (NewFill != FillerMode) { FillerMode = NewFill; SetFillerMode(FillerMode); }
	const int32 NewDens = FCString::Atoi(*E.Parameters.FindRef(TEXT("dens")));
	if (NewDens != FillerDensity) { FillerDensity = NewDens; InitFillers(); }
	FillerSwirl = FCString::Atof(*E.Parameters.FindRef(TEXT("swirl")));   // shared phase from primary
	NiraVersion = FCString::Atoi(*E.Parameters.FindRef(TEXT("nver")));
	const int32 NewSL = FCString::Atoi(*E.Parameters.FindRef(TEXT("slit")));
	if (NewSL != StyleLightStep) { StyleLightStep = NewSL; ApplyStyleLight(); }
	const bool NewClean = (FCString::Atoi(*E.Parameters.FindRef(TEXT("clean"))) != 0);
	if (NewClean != bCleanMode) { bCleanMode = NewClean; SetCleanMode(bCleanMode); }
	OxTime    = FCString::Atof(*E.Parameters.FindRef(TEXT("oxt")));
	bOxFrozen = (FCString::Atoi(*E.Parameters.FindRef(TEXT("oxf"))) != 0);
	if (const FString* Pal = E.Parameters.Find(TEXT("pal"))) PaletteWidth = FCString::Atof(**Pal);   // ApplyPalette picks it up
	// NANITE diagnostic: apply the cvar on every node (this receive runs on primary + all secondaries), so the
	// floor node changes with the wall. No equality guard — the primary already bumped its own NaniteDiagStep in
	// PollInput, so a guard would make its self-loopback a no-op; ApplyNaniteDiag is idempotent, just re-set it.
	if (const FString* Nan = E.Parameters.Find(TEXT("nan")))
	{
		NaniteDiagStep = FCString::Atoi(**Nan);
		ApplyNaniteDiag();
	}
	// FREE-LOOK on every node. In a real cluster the FLOOR is a SEPARATE node running its own copy of this pawn;
	// PollInput()/ApplyFreeLook() only run on the primary, so the floor never turned. Carry the look offset in the
	// event and drive ApplyFreeLook here so every node's DCRA/viewpoints rotate identically.
	if (const FString* Ly = E.Parameters.Find(TEXT("lyaw")))  LookYaw   = FCString::Atof(**Ly);
	if (const FString* Lp = E.Parameters.Find(TEXT("lpit")))  LookPitch = FCString::Atof(**Lp);
	if (!bIsPrimary) ApplyFreeLook(LookYaw, LookPitch);   // primary already applied it in PollInput this frame
	ApplyStations();
}

float AQZoomStagePawn::StageCentre(int32 N) const
{
	// Stages live in [ZoomLeadIn, 1] so ZoomProgress 0 sits BEFORE stage 0 — you start further out and S0
	// blooms in as you begin. ZoomLeadIn 0 restores the original centres (N/(StationCount-1)).
	float f = (StationCount > 1) ? (float)N / (float)(StationCount - 1) : 0.f;

	// LOG SPACING: place the station where its SCALE says, not where its index says. By index every leg gets
	// an equal slice of ZoomProgress regardless of how far it travels, so with this ladder the 4.48-decade
	// S0->S1 and the 0.30-decade S3->S4 take the same time — a 15x swing in apparent zoom rate. Weighting by
	// log(scale) makes equal progress mean equal decades, which is a constant perceived rate.
	const int32 Last = ScaleMeters.Num() - 1;
	if (bLogSpacedStations && Last >= 1 && ScaleMeters.IsValidIndex(N))
	{
		const float L0 = FMath::Loge(FMath::Max(ScaleMeters[0],    1e-30f));
		const float LE = FMath::Loge(FMath::Max(ScaleMeters[Last], 1e-30f));
		const float LN = FMath::Loge(FMath::Max(ScaleMeters[N],    1e-30f));
		const float Span = L0 - LE;                       // positive: scales shrink along the ladder
		if (FMath::Abs(Span) > 1e-6f) f = (L0 - LN) / Span;
	}
	return ZoomLeadIn + f * (1.f - ZoomLeadIn);
}

float AQZoomStagePawn::StationK(int32 N) const
{
	// Per-station steepness. Priority: enabled Handover.Timing > StationZoomK > global ZoomK. So the simplified
	// Handover set (item 4) wins where it's on, and everything falls back cleanly where it isn't.
	float K = ZoomK;
	if (Handover.IsValidIndex(N) && Handover[N].bEnabled)      K = Handover[N].Timing;
	else if (StationZoomK.IsValidIndex(N) && StationZoomK[N] > 0.f) K = StationZoomK[N];
	return FMath::Max(K * ZoomIntensity, 0.01f);
}

float AQZoomStagePawn::StationScale(int32 N) const
{
	return FMath::Exp((ZoomProgress - StageCentre(N)) * StationK(N));
}

float AQZoomStagePawn::LocalK() const
{
	// The K "at this depth", for things that follow the dive as a whole rather than belonging to one station
	// (the fillers). Blend between the two stations we sit between, so K changes smoothly across a leg instead
	// of snapping at the midpoint — a hard switch would visibly jolt the filler growth mid-zoom.
	if (StationCount < 2) return StationK(0);
	const float c0 = StageCentre(0), cN = StageCentre(StationCount - 1);
	const float t = FMath::Clamp((ZoomProgress - c0) / FMath::Max(cN - c0, 1e-3f), 0.f, 1.f) * (StationCount - 1);
	const int32 i = FMath::Clamp((int32)t, 0, StationCount - 2);
	return FMath::Lerp(StationK(i), StationK(i + 1), FMath::Clamp(t - (float)i, 0.f, 1.f));
}

void AQZoomStagePawn::UpdateRefParticles()
{
	if (!bRefParticles)
	{
		if (RefISM) RefISM->SetVisibility(false);
		return;
	}

	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Sphere) return;

	const int32 N = FMath::Max(RefParticleCount, 0);

	if (!RefISM)
	{
		RefISM = NewObject<UInstancedStaticMeshComponent>(this, TEXT("RefParticles"));
		RefISM->SetupAttachment(RootComponent);
		RefISM->RegisterComponent();
		RefISM->SetStaticMesh(Sphere);
		RefISM->SetMobility(EComponentMobility::Movable);
		RefISM->SetCastShadow(false);                    // 700 shadow casters for dust is not a trade
		RefISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (UMaterialInterface* Base = Sphere->GetMaterial(0))
			RefMID = RefISM->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Base);
	}
	if (RefMID)
	{
		RefMID->SetVectorParameterValue(TEXT("Color"), RefParticleColor * RefParticleBrightness);
		RefMID->SetVectorParameterValue(TEXT("BaseColor"), RefParticleColor);
		RefMID->SetScalarParameterValue(TEXT("Emissive"), RefParticleBrightness);
	}
	RefISM->SetVisibility(true);

	// Directions and phase offsets are generated ONCE and reused. Regenerating per frame would make
	// the field boil instead of stream, and re-randomising every tick is the classic way to turn a
	// scale cue back into noise.
	if (RefDirs.Num() != N)
	{
		RefDirs.SetNum(N);
		RefOffsets.SetNum(N);
		RefSizeJitter.SetNum(N);
		FRandomStream R(20260730);
		const float Clear = FMath::Clamp(RefParticleClearAxis, 0.f, 0.9f);
		for (int32 i = 0; i < N; ++i)
		{
			FVector D;
			int32 Guard = 0;
			do
			{
				D = R.GetUnitVector();
				++Guard;
			}
			// keep a cone clear of the view axis so motes never sit on the hero
			while (Guard < 32 && FMath::Abs(D.X) > 1.f - Clear);
			RefDirs[i] = D;
			RefOffsets[i] = R.GetFraction();
			RefSizeJitter[i] = 0.55f + R.GetFraction() * 0.9f;
		}
		RefISM->ClearInstances();
		for (int32 i = 0; i < N; ++i) RefISM->AddInstance(FTransform::Identity);
	}
	if (RefISM->GetInstanceCount() != N) return;

	const float LogSpan = FMath::Loge(FMath::Max(RefParticleMaxUU / FMath::Max(RefParticleMinUU, 1.f), 1.0001f));
	const FRotator Orbit(OrbitPitch, OrbitYaw, 0.f);

	for (int32 i = 0; i < N; ++i)
	{
		// frac walks 0->1 with the zoom and wraps; radius is LOG-spaced across the shell, which is
		// what holds apparent density constant in a logarithmic descent.
		const float Frac = FMath::Frac(RefOffsets[i] + ZoomProgress * RefParticleCycles);
		const float Radius = RefParticleMinUU * FMath::Exp(Frac * LogSpan);

		// size proportional to radius -> constant ANGULAR size -> the field reads self-similar
		const float Size = Radius * RefParticleSizeFrac * RefSizeJitter[i];

		// fade in at the far end, out at the near end, so recycling is never visible
		const float FadeIn  = FMath::Clamp(Frac / 0.12f, 0.f, 1.f);
		const float FadeOut = FMath::Clamp((1.f - Frac) / 0.16f, 0.f, 1.f);
		float A = FMath::Min(FadeIn, FadeOut);
		A = A * A * (3.f - 2.f * A);

		const FVector P = Anchor + Orbit.RotateVector(RefDirs[i] * Radius);
		FTransform T(FQuat::Identity, P, FVector(FMath::Max(Size * A, 0.01f) / 50.f));
		RefISM->UpdateInstanceTransform(i, T, true, (i == N - 1), false);
	}
}


/** Piecewise-linear evaluation of a phase->position path, with wrap-around at 1. */
static FVector CH4EvalPath(float Phase, const TArray<TPair<float, FVector>>& Keys)
{
	if (Keys.Num() == 0) return FVector::ZeroVector;
	if (Phase <= Keys[0].Key) return Keys[0].Value;
	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		if (Phase <= Keys[i].Key)
		{
			const float Span = FMath::Max(Keys[i].Key - Keys[i - 1].Key, KINDA_SMALL_NUMBER);
			float t = (Phase - Keys[i - 1].Key) / Span;
			t = t * t * (3.f - 2.f * t);                     // smoothstep: no hard corners
			return FMath::Lerp(Keys[i - 1].Value, Keys[i].Value, t);
		}
	}
	return Keys.Last().Value;
}


void AQZoomStagePawn::UpdateCH4Cycle(float Dt)
{
	if (!bCH4Cycle) return;
	UWorld* W = GetWorld();
	if (!W) return;

	// Advance ONLY while the reaction's station is actually on screen. That is what makes it
	// zoom-driven rather than clock-driven: walk away and the reaction holds where it was.
	const float S = StationScale(CH4Station);
	const bool bStationUp = (S > MinVisScale && S < MaxVisScale);
	if (bStationUp)
	{
		CH4Phase = FMath::Fmod(CH4Phase + Dt / FMath::Max(CH4CycleSeconds, 0.01f), 1.f);
	}

	// Approach lines. The oxidase comes straight down the Met169 axis; the reductase arrives on a
	// different azimuth about that axis so the two arrivals do not read as one object returning.
	const FVector FmobDir = CH4FmobDock.GetSafeNormal();
	const FVector MsraDir = CH4MsraDock.GetSafeNormal();
	const FVector FmobFar = FmobDir * CH4ApproachUU;
	const FVector MsraFar =
		FQuat(FmobDir, FMath::DegreesToRadians(CH4MsraApproachDeg)).RotateVector(MsraDir)
		* CH4ApproachUU;

	// Phases are the CH4 beats divided by the 90 s cycle, so the pre-vis timing carries over.
	//   oxidant_in 26 -> .289   dock 36 -> .400   dark 56 -> .622
	//   deactivated 66 -> .733  rescue_in 72 -> .800   donate 80 -> .889   recovered 88 -> .978
	TArray<TPair<float, FVector>> FmobKeys;
	FmobKeys.Add({0.000f, FmobFar});
	FmobKeys.Add({0.200f, FmobFar});
	FmobKeys.Add({0.289f, FMath::Lerp(FmobFar, CH4FmobDock, 0.55f)});
	FmobKeys.Add({0.400f, CH4FmobDock});
	FmobKeys.Add({0.733f, CH4FmobDock});
	FmobKeys.Add({0.800f, FMath::Lerp(FmobFar, CH4FmobDock, 0.45f)});
	FmobKeys.Add({1.000f, FmobFar});

	TArray<TPair<float, FVector>> MsraKeys;
	MsraKeys.Add({0.000f, MsraFar});
	MsraKeys.Add({0.622f, MsraFar});
	MsraKeys.Add({0.800f, FMath::Lerp(MsraFar, CH4MsraDock, 0.55f)});
	MsraKeys.Add({0.889f, CH4MsraDock});
	MsraKeys.Add({0.978f, CH4MsraDock});
	MsraKeys.Add({1.000f, FMath::Lerp(MsraFar, CH4MsraDock, 0.35f)});

	// Presence windows, so each enzyme fades in on approach and out on departure instead of
	// popping. Multiplied by the station's own fade, which is what already reads well.
	auto Presence = [](float P, float In, float Out)
	{
		const float Ramp = 0.06f;                            // fraction of the cycle
		if (P < In - Ramp || P > Out + Ramp) return 0.f;
		const float A = FMath::Clamp((P - (In - Ramp)) / Ramp, 0.f, 1.f);
		const float B = FMath::Clamp(((Out + Ramp) - P) / Ramp, 0.f, 1.f);
		float F = FMath::Min(A, B);
		return F * F * (3.f - 2.f * F);
	};

	const float StationFadeNow = bStationUp ? 1.f : 0.f;

	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		const bool bFmob = A->Tags.Contains(FName(TEXT("QZCH4Fmob")));
		const bool bMsra = A->Tags.Contains(FName(TEXT("QZCH4Msra")));
		if (!bFmob && !bMsra) continue;

		const FVector Local = bFmob ? CH4EvalPath(CH4Phase, FmobKeys)
		                            : CH4EvalPath(CH4Phase, MsraKeys);
		const float Vis = StationFadeNow * (bFmob ? Presence(CH4Phase, 0.20f, 0.80f)
		                                          : Presence(CH4Phase, 0.622f, 1.00f));

		// Relative to the station pivot, so the pawn's exp() scale carries them automatically.
		A->SetActorRelativeLocation(Local);
		A->SetActorHiddenInGame(Vis <= 0.002f);
		if (Vis > 0.002f) SetStationFade(A, Vis);
	}
}


void AQZoomStagePawn::ApplyStations()
{
	UWorld* W = GetWorld();
	if (!W) return;
	const FRotator Orbit(OrbitPitch, OrbitYaw, 0.f);
	LevelFade.Reset();   // rebuild the per-sublevel light-fade map this frame  (MinVis + MaxVis are per-station now)
	if (WarmupLeft < 0) WarmupLeft = ShaderWarmupFrames;   // lazy-init the shader warm-up counter

	// Optional re-arm on the way back to the top. OFF by default (WarmupRearmBelow = 0): re-priming re-pays the
	// full cost every return to 0%, which is a recurring hitch rather than a one-time startup cost.
	if (WarmupRearmBelow > 0.f)
	{
		if (ZoomProgress > WarmupRearmBelow) { bWasAboveRearm = true; }
		else if (bWasAboveRearm) { bWasAboveRearm = false; WarmupLeft = ShaderWarmupFrames; }
	}

	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!A->Tags.Contains(TAG_STATION)) continue;

		// second tag = station index; optional "QZVer<k>" tag marks a swappable version (NirA high/low/proc);
		// optional "QZMaxVis<f>" tag overrides MaxVisScale for THIS station only, so an early layer (petri S0,
		// cell/nidulans S1) can clear out sooner while NirA/MET169 keep the big global MaxVis (= linger longer).
		int32 N = -1, Ver = -1;
		float StMaxVis = MaxVisScale;
		for (const FName& T : A->Tags)
		{
			const FString Ts = T.ToString();
			if (Ts.StartsWith(TEXT("QZVer")))    { Ver = FCString::Atoi(*Ts.Mid(5)); continue; }
			if (Ts.StartsWith(TEXT("QZMaxVis"))) { StMaxVis = FCString::Atof(*Ts.Mid(8)); continue; }
			if (T != TAG_STATION && N < 0)       { N = FCString::Atoi(*Ts); }
		}
		if (N < 0) continue;

		// PER-STATION handover (item 4): the simplified Handover[] entry, when enabled, overrides the global
		// MinVis / fade-width / MaxVis for THIS station. Otherwise fall back to the globals (and the QZMaxVis
		// tag for Dissolve, preserved for stations already tuned that way).
		float StMinVis   = MinVisScale;
		float StFadeW    = StationFadeWidth;
		if (Handover.IsValidIndex(N) && Handover[N].bEnabled)
		{
			StMinVis = Handover[N].InitialSize;
			StFadeW  = Handover[N].FadeIn;
			StMaxVis = Handover[N].Dissolve;   // the struct's Dissolve beats the QZMaxVis tag
		}
		const float StLnMin = FMath::Loge(FMath::Max(StMinVis, 1e-30f));
		const float LnMaxS  = FMath::Loge(FMath::Max(StMaxVis, StMinVis * 2.f));

		const float S    = StationScale(N);
		const float LogS = FMath::Loge(FMath::Max(S, 1e-30f));
		// soft cross-fade: 1 inside the band, ramping to 0 over the fade width at each edge (prev/next dissolve).
		const float Up = FMath::Clamp((LogS - StLnMin) / FMath::Max(StFadeW, 1e-3f), 0.f, 1.f);
		const float Dn = FMath::Clamp((LnMaxS - LogS) / FMath::Max(StFadeW, 1e-3f), 0.f, 1.f);
		float Fade = Up * Dn;
		Fade = Fade * Fade * (3.f - 2.f * Fade);   // smoothstep
		bool bVis = (Fade > 0.002f);
		if (Ver >= 0 && Ver != NiraVersion) bVis = false;   // inactive version -> hidden

		if (ULevel* Lvl = A->GetLevel())
		{
			float& lf = LevelFade.FindOrAdd(Lvl); lf = FMath::Max(lf, bVis ? Fade : 0.f);
			TrackedLevels.Add(Lvl);   // remember every station sublevel we've ever seen, so UpdateLights can
			                          // fade its lights OUT (to 0) on frames where the station isn't visible.
		}

		TArray<AActor*> Attached;
		A->GetAttachedActors(Attached);

		// SHADER WARM-UP: while WarmupLeft>0, render every NOT-yet-visible station once — dissolved to nothing
		// at a subpixel scale — so its material shaders/PSOs compile now instead of flashing default grey the
		// first time it enters the band. Visible stations render normally.
		const bool bPrime = (!bVis && WarmupLeft > 0);

		A->SetActorHiddenInGame(!(bVis || bPrime));
		for (AActor* Ch : Attached) Ch->SetActorHiddenInGame(!(bVis || bPrime));

		if (bVis)
		{
			A->SetActorTransform(FTransform(Orbit, Anchor, FVector(S)));   // children follow via attachment
			SetStationFade(A, Fade);
			for (AActor* Ch : Attached) SetStationFade(Ch, Fade);
		}
		else if (bPrime)
		{
			// Prime at the Anchor, SUBPIXEL. Two things learned the hard way here:
			//  1. do NOT prime in front of the camera to dodge HZB occlusion — materials without a StationFade
			//     param (quark blockout, the SVT) cannot dissolve to nothing, so an unoccluded prime FLASHES ON
			//     THE LENS at startup, worse than the thing it fixed.
			//  2. do NOT raise this to "a few pixels" to force Nanite residency. That makes every hidden station
			//     draw REAL geometry — 2.5M triangles across the set — for the whole warm-up, which tanked the
			//     frame rate AND made popping worse, not better (Michael, twice). Subpixel is the point: it
			//     compiles the shader and costs nothing.
			A->SetActorTransform(FTransform(Orbit, Anchor, FVector(WarmupPrimeScale)));
			SetStationFade(A, 0.f);                                            // masked dissolve -> invisible
			for (AActor* Ch : Attached) SetStationFade(Ch, 0.f);
		}
	}
	if (WarmupLeft > 0) --WarmupLeft;
}

void AQZoomStagePawn::SetStationFade(AActor* A, float Fade)
{
	if (!A) return;
	// Iterate ALL primitives (static/ISM/procedural meshes AND the HeterogeneousVolume) + Niagara. Any material
	// with a "StationFade" scalar param dissolves; Niagara gets a "StationFade" user float (bind it in the emitter).
	TArray<UPrimitiveComponent*> Prims;
	A->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* PC : Prims)
	{
		if (!PC) continue;
		if (UNiagaraComponent* NC = Cast<UNiagaraComponent>(PC))
		{
			NC->SetNiagaraVariableFloat(FString(TEXT("StationFade")), Fade);   // User.StationFade in the system
			continue;
		}
		// NOTE: no CreateDynamicMaterialInstance here any more. Creating DMIs lazily from this per-frame loop
		// was the startup stall — the warm-up unhides every station on frame 1, so ~144 DMIs were created and
		// PSO-compiled at once. They are built in BuildMaterialCache() at BeginPlay now; this only SETS.
		const int32 Num = PC->GetNumMaterials();
		for (int32 m = 0; m < Num; ++m)
		{
			if (UMaterialInstanceDynamic* DMI = Cast<UMaterialInstanceDynamic>(PC->GetMaterial(m)))
			{
				DMI->SetScalarParameterValue(TEXT("StationFade"), Fade);
			}
		}
	}
}

void AQZoomStagePawn::UpdateLights()
{
	UWorld* W = GetWorld();
	if (!W) return;
	// Fade every light that lives in a station sublevel by that station's current visibility, so lights ramp
	// in/out with the scene instead of popping on when the sublevel streams in.
	//
	// BUGFIX (Michael: "faded in once, won't fade out again — no re-trigger"): the old code did
	// `if (!LevelFade.Find(level)) continue;` — so a level with NO entry this frame (its station fully out of
	// the band, or its LevelFade left at 0) was SKIPPED, freezing its light at last-written intensity forever.
	// Now: a missing/zero entry means fade = 0 (light OFF), which is applied like any other value. So the light
	// ramps back down and can be re-triggered on the next pass.
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		// The style light is pawn-owned (LB/RB drives its absolute intensity ladder). Skip it here or the two
		// writers fight per-frame and the shoulder buttons go erratic.
		if (A->Tags.Contains(FName(TEXT("QZStyleLight")))) continue;

		// TWO ways a light can follow a station, and the TAG is the one to author against.
		//
		// Historically the only route was LEVEL MEMBERSHIP: a light fades with whichever station
		// sublevel it happens to sit in. That works, but it forces every accent light to live in a
		// streamed sublevel, and a light in an UNLOADED sublevel does not exist — which is exactly
		// the pop-in this function's comment warns about.
		//
		// Since the camera never moves and every station is scaled to the same Anchor, one global
		// rig genuinely serves all 13 decades; there is no per-scale lighting need. So an accent
		// light should be able to live in the PERSISTENT level and still fade with its station.
		// "QZLight<N>" does that, level-independently.
		int32 TagStation = -1;
		for (const FName& T : A->Tags)
		{
			const FString Ts = T.ToString();
			if (Ts.StartsWith(TEXT("QZLight")))
			{
				TagStation = FCString::Atoi(*Ts.Mid(7));
				break;
			}
		}

		float Fade = 0.f;
		if (TagStation >= 0)
		{
			// Same band test ApplyStations uses, so a tagged light tracks its station exactly.
			const float Sc = StationScale(TagStation);
			Fade = (Sc > MinVisScale && Sc < MaxVisScale) ? 1.f : 0.f;
		}
		else if (TrackedLevels.Contains(A->GetLevel()))
		{
			const float* fp = LevelFade.Find(A->GetLevel());
			Fade = fp ? *fp : 0.f;   // missing entry -> 0, so the light fades OUT instead of freezing
		}
		else
		{
			continue;    // neither tagged nor in a tracked station level: a global light, leave it alone
		}
		TArray<ULightComponent*> Lights;
		A->GetComponents<ULightComponent>(Lights);
		for (ULightComponent* LC : Lights)
		{
			if (!LC) continue;
			const TWeakObjectPtr<ULightComponent> Key(LC);
			float* bp = LightBaseIntensity.Find(Key);
			if (!bp) { bp = &LightBaseIntensity.Add(Key, LC->Intensity); }   // capture authored intensity once
			// PROLONG the fade (Michael, 3x): ease the applied fade toward the station's target instead of
			// snapping to it. LightFadeSpeed lower = slower ramp. Per-light smoothed value so each light lags.
			float& sm = LightFadeSmoothed.FindOrAdd(Key);
			sm = FMath::FInterpTo(sm, Fade, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f,
			                      FMath::Max(LightFadeSpeed, 0.01f));
			LC->SetIntensity(*bp * sm);
		}
		// Post-process volumes in a station sublevel fade too (Michael: "PP fades as well"): scale their
		// BlendWeight by the station's visibility so the grade ramps in/out with the scene instead of popping.
		if (APostProcessVolume* PPV = Cast<APostProcessVolume>(A))
		{
			const TWeakObjectPtr<AActor> Key(A);
			float* bw = PPBaseWeight.Find(Key);
			if (!bw) { bw = &PPBaseWeight.Add(Key, PPV->BlendWeight); }   // capture authored weight once
			float& pps = PPFadeSmoothed.FindOrAdd(Key);
			pps = FMath::FInterpTo(pps, Fade, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f,
			                       FMath::Max(LightFadeSpeed, 0.01f));
			PPV->BlendWeight = *bw * pps;
		}
	}
}

ULightComponent* AQZoomStagePawn::GetStyleLight()
{
	if (StyleLight.IsValid()) return StyleLight.Get();
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	// find the actor tagged 'QZStyleLight' (the freed S4 key light) — runtime-safe (labels don't exist at runtime)
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		if (!It->Tags.Contains(FName(TEXT("QZStyleLight")))) continue;
		TArray<ULightComponent*> Lights;
		It->GetComponents<ULightComponent>(Lights);
		if (Lights.Num() > 0) { StyleLight = Lights[0]; return Lights[0]; }
	}
	return nullptr;
}

void AQZoomStagePawn::ApplyNaniteDiag()
{
	// r.Nanite.ProxyRenderMode: 0 normal / 2 FORCE fallback / 3 FORCE full Nanite. Idempotent — safe to call
	// every time the step is (re)set, on the primary AND every secondary, so all nodes render the hull the same.
	static const int32 Modes[3] = { 0, 2, 3 };
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite.ProxyRenderMode")))
		CV->Set(Modes[FMath::Clamp(NaniteDiagStep, 0, 2)], ECVF_SetByConsole);
}

void AQZoomStagePawn::ApplyStyleLight()
{
	ULightComponent* LC = GetStyleLight();
	if (!LC) return;

	// The ladder is authored now (StyleLightLadder) instead of a hard-coded array, and it runs past the old
	// 50k ceiling — Michael wants this to ADD fidelity, not cap it. Lifted out of L_QZ_S4_Density into the
	// persistent level, so it no longer lives or dies with S4's streaming.
	const int32 N = StyleLightLadder.Num();
	if (N == 0) return;
	const int32 Step = FMath::Clamp(StyleLightStep, 0, N - 1);
	// EASE toward the target intensity instead of snapping (Michael). StyleLightEased is interpolated in Tick;
	// here we just push the current eased value onto the light. Default step is 0 -> starts dark.
	//
	// ASYMMETRIC EASE (Michael: "fade-IN is still too quick, the fade-out is ok"): FInterpTo moves proportional
	// to the remaining gap, and the ladder is huge + non-linear ({0..300000}), so a RISE from a low step covers
	// a massive absolute range and front-loads it — the light snaps bright, then crawls. The fade-out over the
	// same curve reads fine. So we ease UP with a SEPARATE, slower speed (StyleLightRiseSpeed) and keep the
	// existing speed for holding/falling. Result: the fade-in takes as long as the fade-out you already like.
	const float Target = StyleLightLadder[Step];
	const bool  bRising = Target > StyleLightEased;
	const float Speed   = bRising ? FMath::Max(StyleLightRiseSpeed, 0.01f)
	                              : FMath::Max(StyleLightEaseSpeed, 0.01f);
	StyleLightEased = FMath::FInterpTo(StyleLightEased, Target, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f, Speed);
	LC->SetIntensity(StyleLightEased);

	// Follow the subject. A point light at a FIXED spot lights a fixed volume — but the world rescales around
	// the Anchor, so a static lamp fell out of usefulness at depth (part of why it felt limiting). Riding the
	// Anchor with an offset keeps the key light on the hero at every scale.
	if (AActor* LA = LC->GetOwner())
	{
		const FRotator Orbit(OrbitPitch, OrbitYaw, 0.f);
		LA->SetActorLocation(Anchor + Orbit.RotateVector(StyleLightOffset));
	}
	// Radius scales with the current station so the falloff stays proportional to what is on screen instead
	// of being a fixed bubble the scene outgrows.
	if (UPointLightComponent* PLC = Cast<UPointLightComponent>(LC))
	{
		PLC->SetAttenuationRadius(FMath::Max(StyleLightRadius, 1.f));
		PLC->SetCastShadows(bStyleLightShadows);   // see the header: shadows here can never cache -> huge cost
	}
}

void AQZoomStagePawn::SetCleanMode(bool bOn)
{
	// Clean plates for photography: hide the whole editorial HUD. UpdateReadout/UpdateInfoLayer only push
	// text + colour (never visibility), so a hidden component stays hidden — nothing re-shows it each frame.
	UTextRenderComponent* Texts[] = { Readout, DetailTitle, DetailSub, DetailScale, DetailProv };
	for (UTextRenderComponent* T : Texts) if (T) T->SetVisibility(!bOn);
	UStaticMeshComponent* Rules[] = { ReadoutBar, ReadoutBarFill, DetailRule };
	for (UStaticMeshComponent* M : Rules) if (M) M->SetVisibility(!bOn);
	if (bOn && ReadoutBG) ReadoutBG->SetVisibility(false);   // panel is off by default; only ever hide it
}

UHeterogeneousVolumeComponent* AQZoomStagePawn::GetOxVolume()
{
	if (OxVolume.IsValid()) return OxVolume.Get();
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		if (!It->Tags.Contains(FName(TEXT("QZOxSwitch")))) continue;
		TArray<UHeterogeneousVolumeComponent*> Vols;
		It->GetComponents<UHeterogeneousVolumeComponent>(Vols);
		if (Vols.Num() > 0) { OxVolume = Vols[0]; Vols[0]->SetPlaying(false); return Vols[0]; }   // pawn owns the frame
	}
	return nullptr;
}

void AQZoomStagePawn::UpdateOxidation(float Dt)
{
	// primary advances the ping-pong clock (unless L3-frozen); secondaries receive OxTime via the cluster event
	if ((bIsPrimary || !bInCluster) && !bOxFrozen)
	{
		OxTime += Dt;
		if (OxTime >= OxPeriod) OxTime -= OxPeriod;
	}
	// triangle 0->1->0 over the period, then smootherstep -> dwell at the bonded/loosened ends, SNAP through the
	// bond-forming middle (the "click"). frame 0 = ON (bonded S=O), frame OxFrames-1 = OFF (loosened).
	const float Half = FMath::Max(OxPeriod * 0.5f, 0.01f);
	float Raw = (OxTime < Half) ? (OxTime / Half) : ((OxPeriod - OxTime) / Half);
	Raw = FMath::Clamp(Raw, 0.f, 1.f);
	const float E = Raw * Raw * Raw * (Raw * (Raw * 6.f - 15.f) + 10.f);   // smootherstep = dwell + snap
	const float Frame = E * (float)FMath::Max(OxFrames - 1, 1);

	// FLASH FIX round 3 (Michael's 3rd recording: still a same-state BLUR pop mid-sweep, bandwidth cap didn't
	// cure it): raising BandwidthLimit only lets the streamer TRY harder — it's still ASYNC + NON-BLOCKING, so
	// when our smootherstep requests ~150 frames faster than I/O can install tiles, the frame renders at
	// whatever mip is currently resident = the blur. The real cure is BLOCKING requests during the fast sweep
	// (guarantee full mip in the SAME frame), which is affordable here: 2 small volumes, NVMe. But blocking on
	// EVERY tick of the 3 s dwell is wasteful and can micro-stutter, so gate it on sweep SPEED: block only when
	// the frame is moving fast (the snap), stream async during the dwell. |dFrame/dt| in frames/sec.
	const float FrameVel = FMath::Abs(Frame - OxPrevFrame) / FMath::Max(Dt, 1e-4f);
	OxPrevFrame = Frame;
	const bool bSnapping = FrameVel > OxBlockFrameVel;   // during the click: force full-res

	// Drive EVERY QZOxSwitch, not just the first: there are now two (the S4 deep-dive volume AND the S3
	// MET169-focus copy). GetOxVolume cached only one, so the S3 orbital never animated.
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			if (!It->Tags.Contains(FName(TEXT("QZOxSwitch")))) continue;
			TArray<UHeterogeneousVolumeComponent*> Vols;
			It->GetComponents<UHeterogeneousVolumeComponent>(Vols);
			for (UHeterogeneousVolumeComponent* HV : Vols)
			{
				if (!HV) continue;
				// CLUSTER FIX: bPlaying=false made GetFrameAndIssueStreamingRequest pass bHasValidFrameRate=false,
				// so NO frame streaming request fires — in PIE the frames are already resident (works), but in a
				// cooked/switchboard build only frame 0 ever streams in, so the animation appears frozen.
				// Set playing=true (this makes the streaming request fire) but immediately override Frame every
				// tick, so OUR ping-pong drives it, not the SVT's internal clock.
				//
				// FLASH FIX round 1 (wrap): with bPlaying=true the component ALSO advances Frame by dt*FrameRate in
				// its own Tick AFTER our SetFrame — and with bLooping the overshoot past the last frame Fmod-
				// WRAPPED to ~frame 0 (bonded state flashed at the top dwell). looping OFF -> overshoot CLAMPS
				// to the last frame instead. That killed the wrong-state flash.
				//
				// FLASH FIX round 2 (mip pop, Michael's 2nd recording: still a soft/bloated 1-2-frame flash near the
				// turnarounds — the SAME state, just BLURRY, sharpening a frame later): that is the SVT streamer's
				// bandwidth throttle, not a frame-index bug. r.SparseVolumeTexture.Streaming.BandwidthLimit
				// defaults to 512 MiB/s and the engine documents the behaviour verbatim: "When requests exceed
				// this limit, the system will stream at lower mip levels instead." Our smootherstep SNAPS through
				// ~150 frames in <1 s (~187 fps peak) x ~1.4 MB/frame x TWO QZOxSwitch volumes — over budget, so
				// the fast sweep gets served coarse mips -> the blur pop. The limit is raised in BeginPlay
				// (every node). FrameRate here stays LOW on purpose: the streamer estimates the real rate from
				// the frame indices it sees (GetEstimatedFrameRate), and a big value would make the component's
				// own dt*rate advance fight our downward sweep by several frames per tick.
				//
				// round 3: BLOCKING requests during the snap (see above) — guarantees the requested frame is
				// resident at full mip in this same frame, so no coarse-mip blur pop. Off during the dwell so
				// the steady state streams async (no needless per-frame stall).
				HV->bIssueBlockingRequests = bSnapping ? 1u : 0u;
				HV->SetLooping(false);
				HV->SetFrameRate(1.0f);
				HV->SetPlaying(true);
				HV->SetFrame(Frame);
			}
		}
	}
	UpdateS3Focus();
}

void AQZoomStagePawn::UpdateS3Focus()
{
	// THE FOCUS (Michael): as the dive SETTLES on MET169 (S3), the NirA hull dissolves out and the orbital
	// SVT blooms in — same spot, same scale. It is a shift of ATTENTION, not a zoom. Driven by how close
	// ZoomProgress is to S3's centre: focus = 0 at the station edges, 1 dead-centre on MET169.
	UWorld* W = GetWorld();
	if (!W) return;
	const float c3 = StageCentre(3);
	// how deep INTO the S3 band we are. Width ~ half the station spacing so it eases in over the approach.
	const float HalfBand = (StationCount > 1) ? (0.5f / (float)(StationCount - 1)) * (1.f - ZoomLeadIn) : 0.17f;
	float f = 1.f - FMath::Clamp(FMath::Abs(ZoomProgress - c3) / FMath::Max(HalfBand * S3FocusWidth, 1e-3f), 0.f, 1.f);
	f = f * f * (3.f - 2.f * f);   // smoothstep: gentle bloom, gentle release
	S3Focus = f;

	// Michael: just the orbital SVT at MET169 — no S3 NirA copy (it double-dissolved against the real S2
	// NirA and popped). So the focus only blooms the orbital IN as the dive settles on MET169.
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (A->Tags.Contains(FName(TEXT("QZS3Orbital"))))
		{
			A->SetActorHiddenInGame(f < 0.01f);
			SetStationFade(A, f);
		}
	}
}

void AQZoomStagePawn::BuildMaterialCache()
{
	// Create every DMI ONCE, up front. This is the fix for the startup stall: CreateDynamicMaterialInstance
	// used to happen lazily inside the per-frame fade loop, so the warm-up (which unhides all six stations on
	// frame 1) triggered ~144 DMI creations + PSO compiles simultaneously. Doing it here means the cost is
	// paid once, at BeginPlay, before the first frame is presented — and every later frame just SETS floats.
	UWorld* W = GetWorld();
	if (!W) return;
	// Rebuild while stations are still streaming in: at BeginPlay the sublevels may not be resident yet, so a
	// one-shot build would silently miss them and their DMIs would be created lazily on-screen again — the
	// very stall this exists to prevent. Re-harvest until every tagged station has been seen, then stop.
	int32 NStations = 0;
	for (TActorIterator<AActor> It(W); It; ++It) { if (It->Tags.Contains(TAG_STATION)) ++NStations; }
	if (bMatCacheBuilt && NStations <= MatCacheStations) return;   // nothing new appeared
	MatCacheStations = NStations;
	bMatCacheBuilt = true;
	MatCache.Reset();
	ActorMats.Reset();

	auto Harvest = [this](AActor* A)
	{
		if (!A) return;
		TArray<int32>& Idx = ActorMats.FindOrAdd(A);
		TArray<UPrimitiveComponent*> Prims;
		A->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* PC : Prims)
		{
			if (!PC || Cast<UNiagaraComponent>(PC)) continue;   // Niagara takes a user float, not a DMI
			for (int32 m = 0; m < PC->GetNumMaterials(); ++m)
			{
				UMaterialInstanceDynamic* DMI = Cast<UMaterialInstanceDynamic>(PC->GetMaterial(m));
				if (!DMI) DMI = PC->CreateDynamicMaterialInstance(m);
				if (!DMI) continue;
				FQZMat E;
				E.DMI = DMI;
				float Probe = 0.f;
				E.bHasFade = DMI->GetScalarParameterValue(TEXT("StationFade"), Probe);
				FLinearColor C;
				if      (DMI->GetVectorParameterValue(TEXT("BaseColor"), C)) { E.ColorParam = TEXT("BaseColor"); E.BaseColor = C; }
				else if (DMI->GetVectorParameterValue(TEXT("Color"),     C)) { E.ColorParam = TEXT("Color");     E.BaseColor = C; }
				Idx.Add(MatCache.Add(E));
			}
		}
	};

	// stations + their children …
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		if (!A->Tags.Contains(TAG_STATION)) continue;
		Harvest(A);
		TArray<AActor*> Kids;
		A->GetAttachedActors(Kids);
		for (AActor* Ch : Kids) Harvest(Ch);
	}
	// … plus the pawn's own procedural clouds, so the palette reaches the FILLERS too (Michael: it should
	// affect the fillers and every other scene material, not just the stations).
	Harvest(this);

	int32 NFade = 0, NCol = 0;
	for (const FQZMat& E : MatCache) { if (E.bHasFade) ++NFade; if (E.ColorParam != NAME_None) ++NCol; }
	UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] material cache: %d DMIs (%d fadeable, %d coloured) across %d actors "
		"— built ONCE at BeginPlay instead of lazily during the warm-up"), MatCache.Num(), NFade, NCol, ActorMats.Num());
}

void AQZoomStagePawn::ApplyPalette()
{
	// Re-apply only when the width actually changes.
	if (FMath::IsNearlyEqual(PaletteApplied, PaletteWidth, 1e-4f)) return;
	PaletteApplied = PaletteWidth;
	if (!bMatCacheBuilt) return;   // nothing to tint until the cache exists

	auto Squeeze = [this](const FLinearColor& In) -> FLinearColor
	{
		FLinearColor HSV = In.LinearRGBToHSV();          // H in DEGREES (0..360), S/V 0..1
		const float H = HSV.R, S = HSV.G, V = HSV.B;
		if (S < 0.02f) return In;                        // greys carry no hue — squeezing them does nothing

		// signed shortest distance to each pole, then pick the NEARER one. Squeezing toward a single global
		// centre would collapse the amber/blue structure the palette is built on (golds -> green).
		auto Delta = [](float A, float B) { return FMath::UnwindDegrees(A - B); };
		const float dA = Delta(H, PaletteHueA), dB = Delta(H, PaletteHueB);
		const float Pole = (FMath::Abs(dA) <= FMath::Abs(dB)) ? PaletteHueA : PaletteHueB;
		const float d    = (FMath::Abs(dA) <= FMath::Abs(dB)) ? dA : dB;

		const float NewH = Pole + d * PaletteWidth;                       // width 1 = authored, 0 = on the pole
		const float Tight = 1.f - FMath::Clamp(PaletteWidth, 0.f, 1.f);   // 0 = authored .. 1 = fully squeezed
		const float NewS = FMath::Lerp(S, PaletteSatTarget, Tight * PaletteSatAmount);

		FLinearColor Out = FLinearColor(FMath::UnwindDegrees(NewH) < 0.f ? FMath::UnwindDegrees(NewH) + 360.f
		                                                                 : FMath::UnwindDegrees(NewH),
		                                FMath::Clamp(NewS, 0.f, 1.f), V, HSV.A).HSVToLinearRGB();
		Out.A = In.A;
		return Out;
	};

	// Walk the CACHE, not the world: no iteration, no DMI creation, no GetVectorParameterValue probing.
	// The cache covers the stations, their children AND the pawn's own filler clouds — so the squeeze now
	// reaches the fillers and every other scene material, which is what Michael asked for.
	int32 N = 0;
	for (const FQZMat& E : MatCache)
	{
		if (E.ColorParam == NAME_None) continue;
		UMaterialInstanceDynamic* DMI = E.DMI.Get();
		if (!DMI) continue;
		// always derive from the AUTHORED colour captured at build time — never from the current value, which
		// would compound press-on-press and drift to mud.
		DMI->SetVectorParameterValue(E.ColorParam, Squeeze(E.BaseColor));
		++N;
	}
	UE_LOG(LogTemp, Log, TEXT("[QZoomStage] palette width %.2f -> %d params (poles %.0f / %.0f)"),
		PaletteWidth, N, PaletteHueA, PaletteHueB);
}

void AQZoomStagePawn::ApplyNiraShells()
{
	// SM_S2_NirA carries 'ribbon' + 'mat_volume' slots: surfaces that ENCLOSE the whole protein. MET169 sits
	// at the centre, so on the way into S3 you view the residue from inside them and they obscure it. The
	// high-res variant has no such slots (atoms only) — that asymmetry is why the two versions disagree.
	// Toggled by SLOT so the C/N/O/S atoms are never touched. Applied once, and again whenever the flag flips.
	if (bNiraShellsApplied == bNiraShells) return;
	bNiraShellsApplied = bNiraShells;
	UWorld* W = GetWorld();
	if (!W) return;
	UMaterialInterface* Ribbon = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_Nira_ribbon.MI_Nira_ribbon"));
	UMaterialInterface* Volume = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_Nira_volume.MI_Nira_volume"));
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		TArray<UStaticMeshComponent*> Meshes;
		It->GetComponents<UStaticMeshComponent>(Meshes);
		for (UStaticMeshComponent* MC : Meshes)
		{
			UStaticMesh* SM = MC ? MC->GetStaticMesh() : nullptr;
			if (!SM || !SM->GetName().Equals(TEXT("SM_S2_NirA"))) continue;
			const TArray<FStaticMaterial>& Slots = SM->GetStaticMaterials();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				const FString N = Slots[i].MaterialSlotName.ToString();
				const bool bShell = N.Contains(TEXT("volume")) || N.Equals(TEXT("Material_0"));   // mat_volume + ribbon
				if (!bShell) continue;
				if (bNiraShells) MC->SetMaterial(i, N.Contains(TEXT("volume")) ? Volume : Ribbon);
				else             MC->SetMaterial(i, nullptr);   // null = slot draws nothing
			}
			UE_LOG(LogTemp, Log, TEXT("[QZoomStage] NirA shells %s"), bNiraShells ? TEXT("SHOWN") : TEXT("hidden"));
		}
	}
}

void AQZoomStagePawn::ApplyFreeLook(float Yaw, float Pitch)
{
	// nDisplay builds every viewport's frustum from the DCRA's transform, NOT the pawn's Camera. So in a
	// cluster, rotating the Camera does nothing on the wall — the fix is to rotate the DCRA. We rotate it
	// about its OWN pivot (a head turn in place), preserving its authored base transform.
	if (!DCRA.IsValid())
	{
		if (UWorld* W = GetWorld())
			DCRA = Cast<AActor>(UGameplayStatics::GetActorOfClass(W, ADisplayClusterRootActor::StaticClass()));
		if (DCRA.IsValid())
		{
			DCRABaseRot = DCRA->GetActorRotation();   // capture the authored orientation ONCE
			DCRABaseSet = true;
			// Deep Space has SEPARATE view origins per viewport: VP_Wall reads DefaultViewPoint (the DCRA
			// root), VP_Floor reads a distinct FloorViewpoint component. Rotating the ACTOR only turned the
			// wall — the floor kept its own component's rotation. So we collect EVERY scene component under
			// the DCRA whose name looks like a view point and rotate each, capturing its base once.
			DCRAViewpoints.Reset(); DCRAViewpointBase.Reset();
			TArray<USceneComponent*> Comps;
			DCRA->GetComponents<USceneComponent>(Comps);
			for (USceneComponent* C : Comps)
			{
				if (!C) continue;
				const FString N = C->GetName();
				if (N.Contains(TEXT("ViewPoint")) || N.Contains(TEXT("Viewpoint")) || N.Contains(TEXT("ViewOrigin")))
				{
					DCRAViewpoints.Add(C);
					DCRAViewpointBase.Add(C->GetRelativeRotation());
				}
			}
		}
	}

	const FQuat Offset = FRotator(Pitch, Yaw, 0.f).Quaternion();

	if (DCRA.IsValid() && DCRABaseSet)
	{
		// rotate the actor (drives DefaultViewPoint = the wall) …
		DCRA->SetActorRotation((DCRABaseRot.Quaternion() * Offset).Rotator());
		// … AND every named viewpoint component (drives the floor + any other node) about its own base.
		for (int32 i = 0; i < DCRAViewpoints.Num(); ++i)
		{
			if (USceneComponent* C = DCRAViewpoints[i].Get())
				C->SetRelativeRotation((DCRAViewpointBase[i].Quaternion() * Offset).Rotator());
		}
	}
	if (Camera)
	{
		Camera->SetRelativeRotation(FRotator(Pitch, Yaw, 0.f));   // the PIE viewport
	}
}

void AQZoomStagePawn::UpdateGuides()
{
	if (!bShowFrontIndicator)
	{
		if (GuideISM)       GuideISM->SetVisibility(false);
		if (GuideCentreISM) GuideCentreISM->SetVisibility(false);
		return;
	}

	UStaticMesh* Cube   = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_Accent.M_Accent"));
	if (!Cube || !Sphere) return;

	auto MakeGuide = [&](TObjectPtr<UInstancedStaticMeshComponent>& Slot, TObjectPtr<UMaterialInstanceDynamic>& MidSlot,
	                     UStaticMesh* M, int32 Count)
	{
		if (Slot) return;
		Slot = NewObject<UInstancedStaticMeshComponent>(this);
		Slot->SetMobility(EComponentMobility::Movable);
		Slot->SetupAttachment(RootComponent);
		Slot->RegisterComponent();
		Slot->SetCastShadow(false);
		Slot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Slot->SetStaticMesh(M);
		if (Mat)
		{
			MidSlot = UMaterialInstanceDynamic::Create(Mat, this);
			Slot->SetMaterial(0, MidSlot);
		}
		for (int32 i = 0; i < Count; ++i) Slot->AddInstance(FTransform::Identity);
	};
	MakeGuide(GuideISM,       GuideFrontMID,  Cube,   9);   // 8 shaft segments + 1 head
	MakeGuide(GuideCentreISM, GuideCentreMID, Sphere, 3);   // 3 nested reticle rings at the Anchor
	if (!GuideISM || !GuideCentreISM) return;
	GuideISM->SetVisibility(true);
	GuideCentreISM->SetVisibility(true);

	// ── the FRONT arrow: deliberately NOT orbited ──────────────────────────────────────────────────────
	// Stations get (Orbit, Anchor, Scale) every frame; this does not. It marks the ROOM, so it must stay put
	// while the subject turns under it — that IS the alignment reference. Orbiting it would make it useless.
	const FVector Dir = bAimFrontAtViewpoint ? (RoomViewpoint - Anchor).GetSafeNormal()
	                                         : FrontDirection.GetSafeNormal();
	const float   Seg = FrontIndicatorSize / 9.f;
	const float   Th  = FMath::Max(FrontIndicatorSize * 0.018f, 1.f);   // shaft thickness
	for (int32 i = 0; i < 8; ++i)
	{
		// dashed shaft: pointing FROM the subject TOWARD the audience
		const FVector P = Anchor + Dir * (Seg * (i + 1.4f));
		const FTransform T(Dir.Rotation(), P, FVector(Seg * 0.055f, Th * 0.02f, Th * 0.02f));
		GuideISM->UpdateInstanceTransform(i, T, true, false, false);
	}
	// arrow head — a flattened block at the far end, reading as a wedge toward the room
	{
		const FVector P = Anchor + Dir * (FrontIndicatorSize * 1.02f);
		const FTransform T(Dir.Rotation(), P, FVector(Seg * 0.09f, Th * 0.055f, Th * 0.055f));
		GuideISM->UpdateInstanceTransform(8, T, true, true, false);
	}
	if (GuideFrontMID) GuideFrontMID->SetVectorParameterValue(TEXT("Color"), FrontIndicatorColor);

	// ── the zoom-CENTRE reticle: three nested shells at the Anchor ─────────────────────────────────────
	// Sized off the CURRENT nearest station scale so it stays legible at every depth instead of vanishing
	// or swallowing the frame — the "hard to adjust exactly" part.
	for (int32 i = 0; i < 3; ++i)
	{
		const float R = FrontIndicatorSize * (0.012f + 0.020f * i);
		const FTransform T(FRotator::ZeroRotator, Anchor, FVector(R / 50.f));   // engine Sphere r=50
		GuideCentreISM->UpdateInstanceTransform(i, T, true, (i == 2), false);
	}
	if (GuideCentreMID) GuideCentreMID->SetVectorParameterValue(TEXT("Color"), CentreIndicatorColor * 0.5f);
}

float AQZoomStagePawn::CurrentScaleMeters() const
{
	const int32 N = ScaleMeters.Num();
	if (N == 0) return 0.f;
	if (N == 1) return ScaleMeters[0];
	// remap past the lead-in so the scale readout tracks the shifted stage centres (lead-in reads as S0 scale)
	const float Zn = FMath::Clamp((ZoomProgress - ZoomLeadIn) / FMath::Max(1.f - ZoomLeadIn, 1e-3f), 0.f, 1.f);

	// Must use the SAME spacing as StageCentre(), or the readout disagrees with the descent it is describing.
	// Under log spacing the stations sit at their log positions, so the scale between them is just a straight
	// log-lerp end to end — constant decades per unit of progress, no per-segment remap.
	if (bLogSpacedStations && N >= 2)
	{
		const float L0 = FMath::Loge(FMath::Max(ScaleMeters[0],     1e-30f));
		const float LE = FMath::Loge(FMath::Max(ScaleMeters[N - 1], 1e-30f));
		return FMath::Exp(FMath::Lerp(L0, LE, Zn));
	}

	const float P = Zn * (float)(N - 1);
	const int32 I = FMath::Clamp((int32)P, 0, N - 2);
	const float F = P - (float)I;
	const float A = FMath::Loge(FMath::Max(ScaleMeters[I],     1e-30f));
	const float B = FMath::Loge(FMath::Max(ScaleMeters[I + 1], 1e-30f));
	return FMath::Exp(FMath::Lerp(A, B, F));
}

FString AQZoomStagePawn::FormatScale(float M) const
{
	struct FUnit { float S; const TCHAR* U; };
	static const FUnit Units[] = {
		{1e-15f, TEXT("fm")}, {1e-12f, TEXT("pm")}, {1e-10f, TEXT("A")},
		{1e-9f,  TEXT("nm")}, {1e-6f,  TEXT("um")}, {1e-3f,  TEXT("mm")},
		{1e-2f,  TEXT("cm")}, {1.f,    TEXT("m")} };
	int32 Pick = 0;
	for (int32 i = 0; i < 8; ++i) if (Units[i].S <= M) Pick = i;
	// proper glyphs for the symbol units, built from code points so the SOURCE stays pure-ASCII
	FString U = Units[Pick].U;
	if      (Pick == 2) U = FString::Printf(TEXT("%c"),   (TCHAR)0x00C5);              // Angstrom  Å
	else if (Pick == 4) U = FString::Printf(TEXT("%c%c"), (TCHAR)0x00B5, (TCHAR)'m');  // micron    µm
	return FString::Printf(TEXT("%.2f %s"), M / Units[Pick].S, *U);
}

void AQZoomStagePawn::UpdateReadout()
{
	if (!Readout) return;
	const float M     = CurrentScaleMeters();
	const float Ref   = (ScaleMeters.Num() > 0) ? ScaleMeters[0] : 1.f;
	const float Power = FMath::LogX(10.f, FMath::Max(Ref, 1e-30f) / FMath::Max(M, 1e-30f));
	// Editorial readout + a live PRESET/FILLER indicator so you can see the arrow/F state change.
	static const TCHAR* PPNames[10]  = { TEXT("neutral"), TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4"),
	                                     TEXT("P5"), TEXT("P6"), TEXT("P7"), TEXT("P8"), TEXT("P9") };
	static const TCHAR* FillNames[4] = { TEXT("off"), TEXT("proteins"), TEXT("enzymes"), TEXT("full medium") };
	static const TCHAR* DensNames[5] = { TEXT("sparse"), TEXT("normal"), TEXT("dense"), TEXT("swarm"), TEXT("EXTREME") };
	// PALETTE line only shows once squeezed — no clutter at the authored default.
	const FString PalLine = (PaletteWidth < 0.999f)
		? FString::Printf(TEXT("\nPALETTE    %.0f%%"), PaletteWidth * 100.f) : FString();
	// NANITE diagnostic line only shows when R3 has cycled off 'normal', so it's invisible in normal use.
	// Now it also reports the RUNTIME truth on THIS node so a wall-vs-PIE difference is readable directly:
	// the feature level (Nanite needs SM6 — if the node is SM5 that is the whole bug), whether r.Nanite is on,
	// and the live ProxyRenderMode. If PIE looks right and the wall doesn't, this line says why.
	static const TCHAR* NaniteNames[3] = { TEXT("normal"), TEXT("FORCE fallback"), TEXT("FORCE full") };
	FString NanLine;
	if (NaniteDiagStep != 0)
	{
		int32 NaniteOn = -1, ProxyMode = -1;
		if (IConsoleVariable* CvN = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite")))            NaniteOn  = CvN->GetInt();
		if (IConsoleVariable* CvP = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite.ProxyRenderMode"))) ProxyMode = CvP->GetInt();
		const ERHIFeatureLevel::Type FL = (GetWorld() && GetWorld()->Scene) ? GetWorld()->Scene->GetFeatureLevel() : ERHIFeatureLevel::Num;
		const TCHAR* FLName = (FL == ERHIFeatureLevel::SM6) ? TEXT("SM6")
		                    : (FL == ERHIFeatureLevel::SM5) ? TEXT("SM5(!Nanite)") : TEXT("other");
		NanLine = FString::Printf(TEXT("\nNANITE     %s  |  FL %s  r.Nanite=%d  proxy=%d"),
			NaniteNames[FMath::Clamp(NaniteDiagStep, 0, 2)], FLName, NaniteOn, ProxyMode);
	}
	Readout->SetText(FText::FromString(FString::Printf(
		TEXT("OBSERVER   %s\nSPEED      %s /s\nZOOM       %s\nDEPTH      %.0f%%\nPRESET     %s\nFILLERS    %s  [%s]%s%s"),
		*FormatScale(ObserverSize), *FormatRate(ObserverSpeed), *FormatZoom(Power), ZoomProgress * 100.f,
		PPNames[FMath::Clamp(PPPreset, 0, 9)], FillNames[FMath::Clamp(FillerMode, 0, 3)],
		DensNames[FMath::Clamp(FillerDensity, 0, 4)], *PalLine, *NanLine)));

	// Drive the progress FILL: grows from the fixed left end (ReadoutBarLeft) toward the right.
	if (ReadoutBarFill)
	{
		const float W = FMath::Max(ReadoutRuleWidth * FMath::Clamp(ZoomProgress, 0.f, 1.f), 0.01f);
		ReadoutBarFill->SetRelativeLocation(FVector(ReadoutBarFwd, ReadoutBarLeft + W * 0.5f, ReadoutBarUp));
		ReadoutBarFill->SetRelativeScale3D(FVector(0.02f, W / 100.f, FMath::Max(RuleThickness, 0.25f) / 100.f));
	}
}

FString AQZoomStagePawn::FormatRate(float PerSec) const
{
	// observer-sizes per second, with SI magnitude suffix (k, M, G, T, P, E)
	static const TCHAR* Suffix[] = { TEXT(""), TEXT("k"), TEXT("M"), TEXT("G"), TEXT("T"), TEXT("P"), TEXT("E") };
	float V = FMath::Abs(PerSec);
	int32 i = 0;
	while (V >= 1000.f && i < 6) { V /= 1000.f; ++i; }
	return FString::Printf(TEXT("%.1f %s"), V, Suffix[i]);
}

FString AQZoomStagePawn::Superscript(int32 N)
{
	// code points for superscript 0-9 (pure-ASCII source; Roboto covers this range)
	static const int32 CP[10] = { 0x2070,0x00B9,0x00B2,0x00B3,0x2074,0x2075,0x2076,0x2077,0x2078,0x2079 };
	const bool bNeg = N < 0; N = FMath::Abs(N);
	TArray<int32> Dig;
	if (N == 0) Dig.Add(0);
	while (N > 0) { Dig.Add(N % 10); N /= 10; }
	FString S;
	if (bNeg) S += FString::Printf(TEXT("%c"), (TCHAR)0x207B);   // superscript minus
	for (int32 k = Dig.Num() - 1; k >= 0; --k) S += FString::Printf(TEXT("%c"), (TCHAR)CP[Dig[k]]);
	return S;
}

FString AQZoomStagePawn::FormatZoom(float Power) const
{
	// 10^Power rendered as scientific magnitude "m.m × 10^e" with a superscript exponent (× = U+00D7)
	const int32 e = FMath::FloorToInt(Power);
	const float m = FMath::Pow(10.f, Power - (float)e);   // mantissa 1.0 .. 9.99
	// caret exponent (font-safe) — the TextRender font cache lacks superscript glyphs (they boxed out)
	return FString::Printf(TEXT("%.1f %c 10^%d"), m, (TCHAR)0x00D7, e);
}

void AQZoomStagePawn::SetupAccentMesh(UStaticMeshComponent* M, TObjectPtr<UMaterialInstanceDynamic>& MID,
	UStaticMesh* Cube, UMaterialInterface* Mat, const FLinearColor& Col, float Bright)
{
	if (!M) return;
	if (Cube) M->SetStaticMesh(Cube);
	if (Mat)
	{
		MID = UMaterialInstanceDynamic::Create(Mat, this);
		if (MID)
		{
			// M_Accent exposes "Color"; the always-on-top text material fallback uses "Emissive" — set both.
			MID->SetVectorParameterValue(TEXT("Color"),    Col * Bright);
			MID->SetVectorParameterValue(TEXT("Emissive"), Col * Bright);
			M->SetMaterial(0, MID);
		}
	}
	M->SetVisibility(true);
}

void AQZoomStagePawn::UpdateInfoLayer()
{
	const int32 N = StageTitle.Num();
	if (N == 0) return;

	// nearest stage + a smoothstep fade that peaks at the stage centre and drops to 0 between stages,
	// so each stage's detail text fades IN as you arrive and OUT as you leave — fixed in the view.
	const float Zn     = FMath::Clamp((ZoomProgress - ZoomLeadIn) / FMath::Max(1.f - ZoomLeadIn, 1e-3f), 0.f, 1.f);
	const float P      = Zn * (float)FMath::Max(N - 1, 1);
	const int32 i      = FMath::Clamp(FMath::RoundToInt(P), 0, N - 1);
	const float Centre = StageCentre(i);
	const float D      = FMath::Abs(ZoomProgress - Centre);
	float Fade = 1.f - FMath::Clamp(D / FMath::Max(InfoFadeWidth, 1e-3f), 0.f, 1.f);
	Fade = Fade * Fade * (3.f - 2.f * Fade);

	auto Line = [&](UTextRenderComponent* T, const TArray<FString>& Arr, float Bright)
	{
		if (!T) return;
		T->SetText(FText::FromString(Arr.IsValidIndex(i) ? Arr[i] : FString()));
		// Vertex ALPHA is a DEAD lever on this text material (opacity = glyph coverage only; the component
		// does not carry a fadeable alpha into opacity — verified across three attempts, translucent + opaque
		// bases both). So fade the COLOUR from the defined text colour toward FadeToColor (the scene
		// background). The detail block fades out BETWEEN stations, where the geometry has dissolved and the
		// flat background sits behind it, so the glyph blends into it and vanishes — no black, no pop.
		const float F = FMath::Clamp(Fade, 0.f, 1.f);
		FColor C = FMath::Lerp(FadeToColor, TextColor * Bright, F).ToFColor(true);
		C.A = 255;
		T->SetTextRenderColor(C);
	};
	Line(DetailTitle, StageTitle,      1.00f);
	Line(DetailSub,   StageSub,        0.90f);
	Line(DetailScale, StageScaleLabel, 0.82f);
	Line(DetailProv,  StageProv,       0.72f);

	// Fade the detail background panel with the text — text + panel stay one stereo unit.
	if (DetailBGMID)
		DetailBGMID->SetVectorParameterValue(TEXT("Color"),
			FLinearColor(BackgroundColor.R, BackgroundColor.G, BackgroundColor.B, BackgroundColor.A * Fade));

	// Fade the title hairline rule in lockstep (M_Accent → "Color"; text-material fallback → "Emissive").
	if (DetailRuleMID)
	{
		DetailRuleMID->SetVectorParameterValue(TEXT("Color"),    AccentColor * Fade);
		DetailRuleMID->SetVectorParameterValue(TEXT("Emissive"), AccentColor * Fade);
	}
}

void AQZoomStagePawn::InitStreaks()
{
	if (!Streaks) return;
	if (!StreakMesh)     StreakMesh     = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!StreakMaterial) StreakMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_Streak.M_Streak"));
	if (StreakMesh) Streaks->SetStaticMesh(StreakMesh);
	if (StreakMaterial)
	{
		StreakMID = UMaterialInstanceDynamic::Create(StreakMaterial, this);   // for the brightness fade
		Streaks->SetMaterial(0, StreakMID ? StreakMID.Get() : StreakMaterial.Get());
	}

	Streaks->ClearInstances();
	StarPos.Reset();
	for (int32 k = 0; k < StreakCount; ++k)
	{
		const float R   = FMath::Sqrt(FMath::FRand()) * StreakRadius;   // uniform disc
		const float Ang = FMath::FRand() * 2.f * PI;
		const FVector P(StreakNear + FMath::FRand() * StreakRange, FMath::Cos(Ang) * R, FMath::Sin(Ang) * R);
		StarPos.Add(P);
		Streaks->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(0.3f, 0.05f, 0.05f)));
	}
	Streaks->SetVisibility(false);
}

void AQZoomStagePawn::UpdateStreaks(float Dt, float Vel, float Intensity)
{
	if (!Streaks) return;
	// Aim the streak field at the zoom centre (Anchor) so the warp converges wherever Anchor projects,
	// independent of the camera facing. This is what carries the vanishing point up onto the wall.
	{
		const FVector D = Anchor - GetActorLocation();
		if (!D.IsNearlyZero()) Streaks->SetWorldRotation(D.Rotation());
	}
	if (StreakMID) StreakMID->SetVectorParameterValue(TEXT("Emissive"), StreakColor * Intensity);   // gentle brightness fade
	if (Intensity < 0.004f) { Streaks->SetVisibility(false); return; }   // fully faded -> hide (imperceptible)
	Streaks->SetVisibility(true);

	// Stars flow past with the zoom; the streak is a trail anchored at the star (fixed end) that extends
	// toward the vanishing point. On stop the trail retracts to that ONE fixed end (a hard "drop out of
	// hyperdrive" snap-back), NOT symmetrically into its own midpoint. The anchoring must use a LATCHED
	// flow sign: VisVel is a raw per-frame delta, so it snaps to 0 the instant the trigger is released —
	// sign(VisVel) would then jump to 0 mid-fade and re-centre every still-long streak on its middle
	// (that was the "contract to the mid" artefact). StreakDir holds the last real direction instead.
	if (FMath::Abs(Vel) > 1e-4f) StreakDir = (Vel > 0.f) ? 1.f : -1.f;
	const float Move    = -Vel * StreakSpeedScale * Dt;
	// Length scales all the way to ZERO with intensity — on a stop the streak shrinks to nothing at the
	// object end, so it's already gone before the visibility cutoff fires (no residual stub that pops off).
	const float Len     = FMath::Clamp(Intensity * StreakLenScale, 0.f, StreakLenMax);
	const float Half    = StreakDir * Len * 0.5f;   // fixed end = star (P.X); far end retracts to it as Len fades
	const FVector Sc(Len / 100.f, 0.05f, 0.05f);   // BasicShapes/Cube is 100uu

	const int32 Count = FMath::Min(Streaks->GetInstanceCount(), StarPos.Num());
	for (int32 k = 0; k < Count; ++k)
	{
		FVector& P = StarPos[k];
		P.X += Move;
		if      (P.X < StreakNear)                P.X += StreakRange;   // wrap to keep the field around the camera
		else if (P.X > StreakNear + StreakRange)  P.X -= StreakRange;
		const FTransform T(FRotator::ZeroRotator, FVector(P.X + Half, P.Y, P.Z), Sc);
		Streaks->UpdateInstanceTransform(k, T, /*bWorldSpace*/false, /*bMarkDirty*/false, /*bTeleport*/false);
	}
	Streaks->MarkRenderStateDirty();
}

void AQZoomStagePawn::InitStageAudio()
{
	// One CONTINUOUS looping source per stage track, placed at the Anchor (so it spatialises from the zoom
	// target ahead). Started once and never stopped — only the volume is faded (the SoundWave must have
	// bVirtualizeWhenSilent so a volume-0 track keeps running instead of being culled + failing to restart).
	StageAudio.Reset();
	for (USoundBase* S : StageSounds)
	{
		UAudioComponent* AC = nullptr;
		if (S)
		{
			AC = NewObject<UAudioComponent>(this);
			if (AC)
			{
				AC->bAutoActivate = false;
				AC->RegisterComponent();
				AC->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
				AC->SetWorldLocation(Anchor);
				AC->SetSound(S);
				AC->SetVolumeMultiplier(0.f);
				AC->bOverrideAttenuation = true;
				AC->AttenuationOverrides.bAttenuate   = bSpatialAudio;
				AC->AttenuationOverrides.bSpatialize  = bSpatialAudio;
				AC->AttenuationOverrides.AttenuationShape        = EAttenuationShape::Sphere;
				AC->AttenuationOverrides.AttenuationShapeExtents = FVector(8000.f, 0.f, 0.f);   // big inner radius → no distance cull; volume is driven by us
				AC->AttenuationOverrides.FalloffDistance         = 100000.f;
				AC->Play();
			}
		}
		StageAudio.Add(AC);   // keep index aligned with StageSounds (entry may be null)
	}
	UE_LOG(LogTemp, Log, TEXT("[QZoomStage] stage audio: %d continuous tracks (spatial=%d)"), StageAudio.Num(), (int)bSpatialAudio);
}

void AQZoomStagePawn::UpdateAudio()
{
	const int32 N = StageAudio.Num();
	if (N == 0) return;
	for (int32 i = 0; i < N; ++i)
	{
		if (!StageAudio[i]) continue;
		const float Centre = StageCentre(i);                               // this track's stage centre (incl. lead-in)
		const float D = FMath::Abs(ZoomProgress - Centre);
		float W = 1.f - FMath::Clamp(D / FMath::Max(StageAudioWidth, 1e-3f), 0.f, 1.f);
		W = W * W * (3.f - 2.f * W);                                        // smoothstep
		W = FMath::Max(W, AudioBed);                                        // continuous bed floor
		StageAudio[i]->SetVolumeMultiplier(W * MasterVolume);
	}
}

void AQZoomStagePawn::ApplyPPPreset(int32 P)
{
	if (!PPVolume) return;
	FPostProcessSettings S;   // fresh — only the fields we set are overridden
	const float Focal = FVector::Dist(Camera ? Camera->GetComponentLocation() : GetActorLocation(), Anchor);
	auto Bloom = [&](float i){ S.bOverride_BloomIntensity = true; S.BloomIntensity = i; };
	auto Vig   = [&](float i){ S.bOverride_VignetteIntensity = true; S.VignetteIntensity = i; };
	auto Temp  = [&](float t){ S.bOverride_WhiteTemp = true; S.WhiteTemp = t; };
	auto Sat   = [&](float s){ S.bOverride_ColorSaturation = true; S.ColorSaturation = FVector4(s, s, s, 1.f); };
	auto Con   = [&](float c){ S.bOverride_ColorContrast = true;   S.ColorContrast   = FVector4(c, c, c, 1.f); };
	auto Exp   = [&](float e){ S.bOverride_AutoExposureBias = true; S.AutoExposureBias = e; };
	auto DOF   = [&](float f){ S.bOverride_DepthOfFieldFocalDistance = true; S.DepthOfFieldFocalDistance = Focal;
	                            S.bOverride_DepthOfFieldFstop = true; S.DepthOfFieldFstop = f; };
	auto Tint  = [&](float r, float g, float b){ S.bOverride_SceneColorTint = true; S.SceneColorTint = FLinearColor(r, g, b, 1.f); };
	switch (P)
	{
	case 1: Bloom(1.6f); DOF(2.0f); Temp(7600.f); Sat(0.88f); Con(1.10f); Vig(0.50f); Exp( 0.3f); break;                    // P1 Cinematic — warm, glow, shallow DOF
	case 2: Bloom(1.5f); Tint(1.0f, 0.22f, 0.18f); Temp(3200.f); Sat(1.30f); Con(1.28f); Vig(0.55f); Exp(0.1f); break;      // P2 Red Alert — crimson wash, warm, heavy vignette
	case 3: Bloom(2.6f); DOF(1.4f); Temp(4400.f); Sat(0.70f); Con(1.20f); Vig(0.85f); Exp(-0.7f); break; // P3 Deep Space — dark, heavy bloom+DOF
	case 4: Bloom(1.2f); DOF(1.8f); Temp(6200.f); Sat(1.55f); Con(1.15f); Vig(0.30f); Exp( 0.2f); break; // P4 Vivid — saturated showcase, mild DOF
	case 5: Bloom(0.8f); Tint(0.40f, 0.02f, 0.02f); Temp(2600.f); Sat(0.90f); Con(1.60f); Vig(0.90f); Exp(-1.2f); break; // P5 Infrared — near-black crimson, crushed blacks
	case 6: Bloom(3.0f); Tint(0.14f, 0.55f, 1.00f); Temp(9800.f); Sat(1.10f); Con(1.25f); Vig(0.20f); Exp( 0.5f); break; // P6 Cyanotype — icy blueprint blue, big glow
	case 7: Bloom(4.0f); Sat(2.30f); Con(1.30f); Vig(0.00f); Exp( 1.2f); break;                                        // P7 Overdrive — blown-out neon, everything screaming
	case 8: Bloom(0.0f); Sat(0.00f); Con(1.90f); Vig(0.95f); Exp(-0.2f); break;                                        // P8 Noir — hard black & white, heavy vignette
	case 9: Bloom(2.6f); Tint(1.00f, 0.10f, 0.90f); Temp(5200.f); Sat(2.00f); Con(1.20f); Vig(0.35f); Exp( 0.3f); break; // P9 Psychedelic — magenta/green wash, max saturation
	default: break;  // 0 neutral — no overrides at all, the raw look
	}
	PPVolume->Settings = S;
	UE_LOG(LogTemp, Log, TEXT("[QZoomStage] PP preset %d"), P);
}

void AQZoomStagePawn::InitFillers()
{
	UStaticMesh* Sph  = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	// FILLER MATERIAL: M_FillerGlow — a dedicated Unlit + ADDITIVE + one-sided material so the medium is (a) cheap
	// (no deferred lighting on thousands of instances, no masked-clip, half the fragments vs the two-sided master)
	// and (b) FUSED-looking: a soft Fresnel core makes each sphere a glow that ADDS with its neighbours, so the
	// cloud reads as one continuous mass instead of discrete balls — Michael's "give them a hull". It still exposes
	// BaseColor (palette squeeze) + StationFade (grow-and-fade), so all the existing wiring keeps working.
	// Falls back to the lit master, then M_Filler, if the glow material is missing.
	UMaterialInterface* Emis = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_FillerGlow.M_FillerGlow"));
	if (!Emis) Emis = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_StationMaster.M_StationMaster"));
	if (!Emis) Emis = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_Filler.M_Filler"));
	UMaterialInterface* Dim  = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_UIPanel.M_UIPanel"));
	FRandomStream R(20260705);   // FIXED seed → identical scatter on every cluster node (no wall/floor desync)

	auto MakeISM = [&](UStaticMesh* M, UMaterialInterface* Mat) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);   // CRITICAL: runtime-added instances only render on a Movable ISM
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		C->SetWorldLocation(Anchor);                   // pivot AT the subject -> swirl/scale happen around it
		// CHEAP MEDIUM (Michael: "kill the perf but keep the density"): the fillers glow on their own, so they
		// don't need to cast, receive, or feed ANY lighting. Turning all of it off is most of the win at density.
		C->SetCastShadow(false);
		C->bCastDynamicShadow = false;
		C->bCastStaticShadow = false;
		C->bAffectDynamicIndirectLighting = false;
		C->bAffectDistanceFieldLighting = false;
		C->SetReceivesDecals(false);
		C->bReceivesDecals = false;
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		// LOD-free spheres are cheap per-instance; keep them from being culled-then-repopped every frame by the
		// aggressive scale animation (WorldPositionOffset-free), so no per-frame proxy rebuild.
		if (M)   C->SetStaticMesh(M);
		if (Mat) C->SetMaterial(0, Mat);
		return C;
	};

	(void)Cube; (void)Dim;

	// Rebuild-safe: create the component once, or just clear its instances on a density re-build.
	auto Ensure = [&](TObjectPtr<UInstancedStaticMeshComponent>& Slot, UStaticMesh* M, UMaterialInterface* Mat) -> UInstancedStaticMeshComponent*
	{
		if (!Slot) Slot = MakeISM(M, Mat);
		else       Slot->ClearInstances();
		return Slot.Get();
	};

	// DENSITY table — deliberately EXTREME at the top. LB/RB step FillerDensity; the cloud rebuilds.
	static const float DensMul[5] = { 0.35f, 1.0f, 3.0f, 35.0f, 70.0f };   // sparse / normal / dense / swarm / EXTREME
	const float dens = DensMul[FMath::Clamp(FillerDensity, 0, 4)];

	// Instances are LOCAL (centred on the component, which sits at the Anchor) so a whole cloud spins & scales as a body.
	// A "globule" = a space-filling protein: atoms packed into a filled sphere (denser toward the core). This is exactly
	// how CPK / space-fill models read — shape does the scientific work, so one accent colour is fine (brand teal).
	auto AddGlobule = [&](UInstancedStaticMeshComponent* C, const FVector& c, float rad, int32 n, float atomLo, float atomHi)
	{
		for (int32 j = 0; j < n; ++j)
		{
			const FVector dir = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f)).GetSafeNormal();
			const float   rr  = rad * FMath::Sqrt(R.FRand());     // sqrt bias -> uniformly FILLED sphere, not a shell
			const float   a   = R.FRandRange(atomLo, atomHi);
			C->AddInstance(FTransform(FRotator::ZeroRotator, c + dir * rr, FVector(a)));
		}
	};
	// An "enzyme-worm" = a backbone Ca-trace: overlapping beads walking a smooth, gently-curving random 3D path.
	auto AddWorm = [&](UInstancedStaticMeshComponent* C, const FVector& s, float len, int32 beads, float bead)
	{
		FVector p = s;
		FVector dir = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f)).GetSafeNormal();
		const float step = len / FMath::Max(beads, 1);
		for (int32 j = 0; j < beads; ++j)
		{
			const float t = (float)j / FMath::Max(beads - 1, 1);
			const float taper = 0.75f + 0.25f * FMath::Sin(t * PI);   // slightly fatter in the middle, like a folded chain
			C->AddInstance(FTransform(FRotator::ZeroRotator, p, FVector(bead * taper)));
			const FVector turn = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f)).GetSafeNormal();
			dir = (dir + turn * 0.30f).GetSafeNormal();             // low turn = smooth worm, not a random walk
			p += dir * step;
		}
	};

	// 1) PROTEINS — micro globules everywhere + a few macro globules, filling the medium at two scales.
	UInstancedStaticMeshComponent* Motes = Ensure(FillerMotes, Sph, Emis);
	const int32 NMicro = FMath::RoundToInt(34 * dens);
	const int32 NMacro = FMath::RoundToInt( 6 * dens);
	for (int32 i = 0; i < NMicro; ++i)   // micro proteins (~0.3-0.7 m)
	{
		const FVector c(R.FRandRange(-5500.f, 5500.f), R.FRandRange(-5500.f, 5500.f), R.FRandRange(-5500.f, 5500.f));
		AddGlobule(Motes, c, R.FRandRange(130.f, 320.f), 30, 0.55f, 1.15f);
	}
	for (int32 i = 0; i < NMacro; ++i)    // macro proteins (~1.5-2.5 m, denser)
	{
		const FVector c(R.FRandRange(-6500.f, 6500.f), R.FRandRange(-6500.f, 6500.f), R.FRandRange(-6500.f, 6500.f));
		AddGlobule(Motes, c, R.FRandRange(650.f, 1150.f), 95, 1.0f, 1.9f);
	}
	// 2) ENZYME-WORMS — long swirling backbone chains threading the void.
	UInstancedStaticMeshComponent* Worm = Ensure(FillerStruct, Sph, Emis);
	const int32 NWorm = FMath::RoundToInt(22 * dens);
	for (int32 i = 0; i < NWorm; ++i)
	{
		const FVector s(R.FRandRange(-6000.f, 6000.f), R.FRandRange(-6000.f, 6000.f), R.FRandRange(-6000.f, 6000.f));
		AddWorm(Worm, s, R.FRandRange(800.f, 1600.f), 24, R.FRandRange(0.55f, 0.95f));
	}
	FillerGrid = nullptr;   // grid retired — the medium is now proteins + enzymes
	// DMIs so we can drive the fade per frame — and, now that the medium runs on M_StationMaster, so it has a
	// real authored BaseColor for the palette to squeeze FROM. Two different colours on purpose: the proteins
	// sit on the blue pole and the enzymes on the amber one, so the medium reads as part of the same palette
	// rather than a separate layer, and it responds to the squeeze like everything else.
	if (Motes) FillerMotesMID  = Motes->CreateDynamicMaterialInstance(0);
	if (Worm)  FillerStructMID = Worm ->CreateDynamicMaterialInstance(0);
	if (FillerMotesMID)  FillerMotesMID ->SetVectorParameterValue(TEXT("BaseColor"), FillerColorMotes);
	if (FillerStructMID) FillerStructMID->SetVectorParameterValue(TEXT("BaseColor"), FillerColorStruct);
	SetFillerMode(FillerMode);   // re-apply visibility (this runs on density rebuilds too)

	// A/B re-run InitFillers to rebuild the cloud, which recreates these DMIs — so the material cache and the
	// palette must be re-derived, or the squeeze silently stops applying to the new instances.
	bMatCacheBuilt = false;
	MatCacheStations = -1;
	PaletteApplied = -1.f;
	UE_LOG(LogTemp, Log, TEXT("[QZoomStage] fillers built @dens[%d]x%.2f: proteins=%d atoms, enzymes=%d beads"),
		FillerDensity, dens, Motes ? Motes->GetInstanceCount() : 0, Worm ? Worm->GetInstanceCount() : 0);
}

void AQZoomStagePawn::SetFillerMode(int32 M)
{
	if (FillerMotes)  FillerMotes ->SetVisibility(M == 1 || M == 3);   // proteins
	if (FillerStruct) FillerStruct->SetVisibility(M == 2 || M == 3);   // enzyme-worms
	if (FillerGrid)   FillerGrid  ->SetVisibility(false);             // retired
}

void AQZoomStagePawn::UpdateFillers(float Dt)
{
	if (!FillerMotes && !FillerStruct) return;

	// Advance the shared swirl phase on the PRIMARY only; nodes get it via Broadcast so every wall tumbles in
	// lock-step (no seam desync). Wrapped at 3600 (=10 turns) with 0.1-multiple rates so spins never jump.
	if (bIsPrimary || !bInCluster)
	{
		FillerSwirl += Dt * 14.f;                             // quicker base rate — living, not a shallow pond
		if (FillerSwirl >= 3600.f) FillerSwirl -= 3600.f;
	}
	const float r = FMath::DegreesToRadians(FillerSwirl);

	// Inherit the SAME orbit the stations use (OrbitPitch/OrbitYaw) so the fillers turn WITH the world when you
	// rotate/yaw — they belong to the scene, not the HUD. On top of that, each cloud tumbles on its own.
	const FQuat  Orbit = FRotator(OrbitPitch, OrbitYaw, 0.f).Quaternion();

	// MOLECULAR PRESENCE — the enzyme/protein medium belongs to the NirA neighbourhood, not the petri-dish
	// scale. Ramp it up between the cell (S1) and NirA (S2), hold through MET169 (S3), fade out before the
	// electron-density scale (S4). Outside that band the medium is absent (scale -> 0 + hidden).
	const float c1 = StageCentre(1), c2 = StageCentre(2), c3 = StageCentre(3), c4 = StageCentre(4);
	const float rise = FMath::Clamp((ZoomProgress - (c1 + c2) * 0.5f) / FMath::Max(c2 - (c1 + c2) * 0.5f, 1e-3f), 0.f, 1.f);
	const float fall = FMath::Clamp(((c3 + c4) * 0.5f - ZoomProgress) / FMath::Max((c3 + c4) * 0.5f - c3, 1e-3f), 0.f, 1.f);
	float presence = FMath::Min(rise, fall);
	presence = presence * presence * (3.f - 2.f * presence);   // smoothstep -> molecular-band opacity

	// SIZE grows MONOTONICALLY with the dive (never shrinks — shrinking breaks immersion). As you push past
	// NirA toward MET169 the medium enlarges and passes the camera; it disappears by FADING (FillerFade), not
	// by shrinking. Growth is anchored at the NirA centre so the medium reads "relative to NirA" there.
	//
	// The growth rate is DERIVED FROM THE WORLD'S OWN K (Michael: "the scaling of the fillers needs to match
	// the zoom"). It used to be a hard-coded 2.4 against a world ZoomK of 12 — i.e. the medium grew 5x slower
	// than everything around it and then hit a 12x ceiling, so it visibly lagged and then froze while the
	// scene kept rushing past. Tracking LocalK() keeps the medium on the same curve as the stations, at
	// FillerScaleRatio of their rate (1.0 = exactly the world's rate; < 1 = the medium reads as "further out").
	const float fs = FMath::Clamp(FMath::Exp((ZoomProgress - c2) * LocalK() * FillerScaleRatio),
	                              FillerScaleMin, FillerScaleMax);
	const FVector FScale(fs);
	const bool bBand = presence > 0.005f;

	// PROTEINS — gentle drift with a soft two-frequency wobble (alive, but unhurried).
	if (FillerMotes)
	{
		FillerMotes->SetVisibility(bBand && (FillerMode == 1 || FillerMode == 3));
		const FRotator Self(8.f * FMath::Sin(r * 1.3f), FillerSwirl * 0.8f, 6.f * FMath::Sin(r * 0.7f));
		FillerMotes->SetWorldRotation(Orbit * Self.Quaternion());
		FillerMotes->SetWorldScale3D(FScale);
	}
	// ENZYME-WORMS — quicker, erratic multi-axis tumble. Incommensurate frequencies never line up → it never
	// visibly repeats, reading as churning living physics rather than a smooth carousel.
	if (FillerStruct)
	{
		FillerStruct->SetVisibility(bBand && (FillerMode == 2 || FillerMode == 3));
		const FRotator Self(24.f * FMath::Sin(r * 0.9f) + 13.f * FMath::Sin(r * 2.7f),
		                    -FillerSwirl * 2.0f,
		                    17.f * FMath::Sin(r * 1.9f));
		FillerStruct->SetWorldRotation(Orbit * Self.Quaternion());
		FillerStruct->SetWorldScale3D(FScale);
	}
	// fade via OPACITY (FillerFade) — grow-and-fade, not shrink
	// Write BOTH names: the medium moved from M_Filler (param 'FillerFade') to M_StationMaster (param
	// 'StationFade') so the palette could tint it. Setting a name a material does not expose is a harmless
	// no-op, so this keeps the dissolve working on either master rather than silently breaking it.
	if (FillerMotesMID)  { FillerMotesMID ->SetScalarParameterValue(TEXT("FillerFade"), presence);
	                       FillerMotesMID ->SetScalarParameterValue(TEXT("StationFade"), presence); }
	if (FillerStructMID) { FillerStructMID->SetScalarParameterValue(TEXT("FillerFade"), presence);
	                       FillerStructMID->SetScalarParameterValue(TEXT("StationFade"), presence); }
}
