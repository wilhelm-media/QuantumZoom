#include "QZoomStagePawn.h"
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
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/LightComponent.h"
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

	InitFillers();          // scientific space fillers (F cycles off/motes/grid/structures)
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
	ApplyStations();    // every node (secondaries got ZoomProgress/orbit via the event)

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
	if (bRB && !bRBPrev) { StyleLightStep = FMath::Min(StyleLightStep + 1, 5); ApplyStyleLight(); }
	if (bLB && !bLBPrev) { StyleLightStep = FMath::Max(StyleLightStep - 1, 0); ApplyStyleLight(); }
	bLBPrev = bLB; bRBPrev = bRB;

	// A / B (face bottom/right) = filler DENSITY (moved off the shoulders). 5 levels, sparse -> EXTREME.
	const bool bA = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom), bB = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Right);
	if (bB && !bBPrev) { FillerDensity = FMath::Min(FillerDensity + 1, 4); InitFillers(); }
	if (bA && !bAPrev) { FillerDensity = FMath::Max(FillerDensity - 1, 0); InitFillers(); }
	bAPrev = bA; bBPrev = bB;

	// X (face-button left) = cycle the NirA representation: 0 high-res FBX / 1 low-res FBX / 2 procedural
	const bool bX = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);
	if (bX && !bXPrev) { NiraVersion = (NiraVersion + 1) % 3; }
	bXPrev = bX;

	// Y (face-button top) = CLEAN MODE: hide the whole HUD for clean photography plates.
	const bool bY = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top);
	if (bY && !bYPrev) { bCleanMode = !bCleanMode; SetCleanMode(bCleanMode); }
	bYPrev = bY;
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
	ApplyStations();
}

float AQZoomStagePawn::StageCentre(int32 N) const
{
	// Stages live in [ZoomLeadIn, 1] so ZoomProgress 0 sits BEFORE stage 0 — you start further out and S0
	// blooms in as you begin. ZoomLeadIn 0 restores the original centres (N/(StationCount-1)).
	const float f = (StationCount > 1) ? (float)N / (float)(StationCount - 1) : 0.f;
	return ZoomLeadIn + f * (1.f - ZoomLeadIn);
}

float AQZoomStagePawn::StationScale(int32 N) const
{
	return FMath::Exp((ZoomProgress - StageCentre(N)) * ZoomK);
}

void AQZoomStagePawn::ApplyStations()
{
	UWorld* W = GetWorld();
	if (!W) return;
	const FRotator Orbit(OrbitPitch, OrbitYaw, 0.f);
	const float LnMin = FMath::Loge(FMath::Max(MinVisScale, 1e-30f));   // MaxVis is now per-station (LnMaxS below)
	LevelFade.Reset();   // rebuild the per-sublevel light-fade map this frame
	if (WarmupLeft < 0) WarmupLeft = ShaderWarmupFrames;   // lazy-init the shader warm-up counter

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
		const float LnMaxS = FMath::Loge(FMath::Max(StMaxVis, MinVisScale * 2.f));

		const float S    = StationScale(N);
		const float LogS = FMath::Loge(FMath::Max(S, 1e-30f));
		// soft cross-fade: 1 inside the band, ramping to 0 over StationFadeWidth at each edge (prev/next dissolve).
		const float Up = FMath::Clamp((LogS - LnMin) / FMath::Max(StationFadeWidth, 1e-3f), 0.f, 1.f);
		const float Dn = FMath::Clamp((LnMaxS - LogS) / FMath::Max(StationFadeWidth, 1e-3f), 0.f, 1.f);
		float Fade = Up * Dn;
		Fade = Fade * Fade * (3.f - 2.f * Fade);   // smoothstep
		bool bVis = (Fade > 0.002f);
		if (Ver >= 0 && Ver != NiraVersion) bVis = false;   // inactive version -> hidden

		if (ULevel* Lvl = A->GetLevel()) { float& lf = LevelFade.FindOrAdd(Lvl); lf = FMath::Max(lf, bVis ? Fade : 0.f); }

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
			A->SetActorTransform(FTransform(Orbit, Anchor, FVector(1e-3f)));   // subpixel dot at centre
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
		const int32 Num = PC->GetNumMaterials();
		for (int32 m = 0; m < Num; ++m)
		{
			UMaterialInstanceDynamic* DMI = Cast<UMaterialInstanceDynamic>(PC->GetMaterial(m));
			if (!DMI) DMI = PC->CreateDynamicMaterialInstance(m);
			if (DMI) DMI->SetScalarParameterValue(TEXT("StationFade"), Fade);
		}
	}
}

void AQZoomStagePawn::UpdateLights()
{
	UWorld* W = GetWorld();
	if (!W || LevelFade.Num() == 0) return;
	// Fade every light that lives in a station sublevel by that station's current visibility, so lights ramp
	// in/out with the scene instead of popping on when the sublevel streams in.
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		const float* fp = LevelFade.Find(A->GetLevel());
		if (!fp) continue;
		TArray<ULightComponent*> Lights;
		A->GetComponents<ULightComponent>(Lights);
		for (ULightComponent* LC : Lights)
		{
			if (!LC) continue;
			const TWeakObjectPtr<ULightComponent> Key(LC);
			float* bp = LightBaseIntensity.Find(Key);
			if (!bp) { bp = &LightBaseIntensity.Add(Key, LC->Intensity); }   // capture authored intensity once
			LC->SetIntensity(*bp * (*fp));
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

void AQZoomStagePawn::ApplyStyleLight()
{
	ULightComponent* LC = GetStyleLight();
	if (!LC) return;
	static const float Lux[6] = { 0.f, 2000.f, 5000.f, 10000.f, 25000.f, 50000.f }; // off .. full (current)
	LC->SetIntensity(Lux[FMath::Clamp(StyleLightStep, 0, 5)]);
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

float AQZoomStagePawn::CurrentScaleMeters() const
{
	const int32 N = ScaleMeters.Num();
	if (N == 0) return 0.f;
	if (N == 1) return ScaleMeters[0];
	// remap past the lead-in so the scale readout tracks the shifted stage centres (lead-in reads as S0 scale)
	const float Zn = FMath::Clamp((ZoomProgress - ZoomLeadIn) / FMath::Max(1.f - ZoomLeadIn, 1e-3f), 0.f, 1.f);
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
	Readout->SetText(FText::FromString(FString::Printf(
		TEXT("OBSERVER   %s\nSPEED      %s /s\nZOOM       %s\nDEPTH      %.0f%%\nPRESET     %s\nFILLERS    %s  [%s]"),
		*FormatScale(ObserverSize), *FormatRate(ObserverSpeed), *FormatZoom(Power), ZoomProgress * 100.f,
		PPNames[FMath::Clamp(PPPreset, 0, 9)], FillNames[FMath::Clamp(FillerMode, 0, 3)],
		DensNames[FMath::Clamp(FillerDensity, 0, 4)])));

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
	UMaterialInterface* Emis = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_Filler.M_Filler"));
	if (!Emis) Emis = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_Accent.M_Accent"));  // fallback
	UMaterialInterface* Dim  = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_UIPanel.M_UIPanel"));
	FRandomStream R(20260705);   // FIXED seed → identical scatter on every cluster node (no wall/floor desync)

	auto MakeISM = [&](UStaticMesh* M, UMaterialInterface* Mat) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);   // CRITICAL: runtime-added instances only render on a Movable ISM
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		C->SetWorldLocation(Anchor);                   // pivot AT the subject -> swirl/scale happen around it
		C->SetCastShadow(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	// dynamic material instances so we can drive FillerFade (opacity) per frame
	if (Motes) FillerMotesMID  = Motes->CreateDynamicMaterialInstance(0);
	if (Worm)  FillerStructMID = Worm ->CreateDynamicMaterialInstance(0);
	SetFillerMode(FillerMode);   // re-apply visibility (this runs on density rebuilds too)
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
	const float fs = FMath::Clamp(FMath::Exp((ZoomProgress - c2) * 2.4f), 0.5f, 12.0f);
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
	if (FillerMotesMID)  FillerMotesMID ->SetScalarParameterValue(TEXT("FillerFade"), presence);
	if (FillerStructMID) FillerStructMID->SetScalarParameterValue(TEXT("FillerFade"), presence);
}
