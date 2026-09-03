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
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Engine/PostProcessVolume.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "DisplayClusterRootActor.h"   // free-look drives the DCRA in a cluster (nDisplay frustum source)
#include "Camera/PlayerCameraManager.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"   // idle orbit: last-user-interaction clock
#include "Components/HeterogeneousVolumeComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"                 // the environment rig scaled per stage
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "NiagaraComponent.h"
#include "QHudText.h"
#include "Blueprint/UserWidget.h"

const FString AQZoomStagePawn::EventName = TEXT("QZoomStage.State");
static const FName TAG_STATION(TEXT("QZStation"));
// Retired content: kept in the level, never rendered. Until now this tag was documentation only.
static const FName TAG_RETIRED(TEXT("QZRetired"));
// Authoring guides: visible in the editor so a stage can be aimed, never visible in the show.
static const FName TAG_GUIDE(TEXT("QZGuide"));
// A hand-authored Niagara system that keeps its OWN palette and sizes: the pawn still drives its
// fade, its scale reference, its activation and OrbitalNoise, but leaves ParticleColor and the two
// ParticleScale handles alone. Without this every system in a visible station is amber by frame 1.
static const FName TAG_KEEPSTYLE(TEXT("QZKeepStyle"));
// An actor that should turn WITH the free look. ApplyFreeLook rotates the DCRA, every named
// viewpoint component and the PIE camera — but never the pawn, so anything parented to the pawn
// stays put while the view swings away from it. A headlight has to travel with the look or it
// lights the place you just stopped looking at.
static const FName TAG_FOLLOWLOOK(TEXT("QZFollowLook"));
// Nur diese Actors bekommen NetAmount von der Tiefe gefahren. M_NiraMaster traegt NetAmount
// auch auf den Nukleonen; ohne die Eingrenzung wuerden deren authorierte Werte mitgerissen.
static const FName TAG_NETSOLID(TEXT("QZNetSolid"));

// The nominal span every hero is normalised to: 2 x E_TARGET 1732, the size Petri, Nidulans,
// NirA and MET169 all sit at. It is the one number that turns a station's exp() scale back
// into a real on-screen size, which the comms traffic needs so it stays tied to the object
// instead of drifting off it as the ladder descends.
static constexpr float STATION_SPAN_UU = 3464.f;

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
	// The big frame-rate: its own component because a TextRender has ONE size for all its text —
	// the number must be readable from across the room while the finger is on the mute keys.
	FpsBig = MakeText(TEXT("FpsBig"));
	FpsBig->SetVerticalAlignment(EVRTA_TextBottom);
	FpsBig->SetTextRenderColor(FColor(255, 171, 46));

	DetailIndex = MakeText(TEXT("DetailIndex"));
	DetailTitle = MakeText(TEXT("DetailTitle"));
	DetailSub   = MakeText(TEXT("DetailSub"));
	DetailScale = MakeText(TEXT("DetailScale"));
	DetailProv  = MakeText(TEXT("DetailProv"));

	Streaks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Streaks"));
	Streaks->SetupAttachment(Root);
	Streaks->SetCastShadow(false);
	Streaks->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// The activation/deactivation shell + its light. Both live on the pawn rather than in the
	// MET169 sublevel so they survive that level's streaming and can be driven every frame.
	CH4EnergyISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CH4Energy"));
	CH4EnergyISM->SetupAttachment(Root);
	CH4EnergyISM->SetCastShadow(false);
	CH4EnergyISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CH4EnergyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CH4EnergyLight"));
	CH4EnergyLight->SetupAttachment(Root);
	CH4EnergyLight->SetCastShadows(false);   // repositioned every frame: shadows can never cache
	CH4EnergyLight->SetVisibility(false);

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

	// Default caption rows — ONE PER LADDER STATION, and the ladder has had eight since the
	// restructure. These six were left behind by it: the block was still captioning a six-stage
	// world while the geometry ran on eight, so every caption below the third stage described
	// something that was no longer on screen. SyncLadder now resizes these to StationCount, and
	// these defaults finally name the eight stations that exist.
	// ASCII for font safety; the placed pawn's pretty copy (A/um/en-dash) is written by
	// dev/qz_caption_layout.py — keeping the source pure-ASCII avoids any /utf-8 dependency.
	StageTitle      = { TEXT("LAB"),                  TEXT("NEURAL STRUCTURE"),        TEXT("CELL"),
	                    TEXT("NirA PROTEIN"),         TEXT("MET169"),                  TEXT("NUCLEUS"),
	                    TEXT("NUCLEONS"),             TEXT("QUARKS") };
	StageSub        = { TEXT("fruiting body, forest floor"), TEXT("dendritic network"),          TEXT("hyphal cell and nucleus"),
	                    TEXT("AlphaFold P28348"),             TEXT("sulfur atom - the redox switch"), TEXT("a bright unresolved point"),
	                    TEXT("S-32: 16 protons, 16 neutrons"),TEXT("valence quarks and gluon flux") };
	StageScaleLabel = { TEXT("~9 cm"),   TEXT("~300 um"), TEXT("~3 um"),  TEXT("~10 nm"),
	                    TEXT("~0.2 nm"), TEXT("~1 pm"),   TEXT("~8 fm"),  TEXT("~0.3 fm") };
	StageProv       = { TEXT("visible light"),          TEXT("reconstructed morphology"), TEXT("light microscopy"),
	                    TEXT("predicted structure"),    TEXT("ball-and-stick model"),     TEXT("position marker - not imaged"),
	                    TEXT("nuclear shell model"),    TEXT("QCD illustration - not imaged") };

	// PIE only takes possession (see BeginPlay). In the cluster the nDisplay DCRA is the view and this
	// pawn must NOT possess — it just ticks, drives ZoomProgress, and broadcasts.
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AQZoomStagePawn::BeginPlay()
{
	Super::BeginPlay();

	// Before anything reads the ladder. StageCentre, the audio envelopes, the readout and the filler
	// band all derive from ScaleMeters/StationCount, so those have to agree with the authored rows
	// from the first frame rather than after the first edit.
	SyncLadder();

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

	// The show boots in whatever HUD state is authored (default 0 = clean). Before this, the HUD
	// was always on at start and Y only ever hid it.
	HUDMode = FMath::Clamp(HUDMode, 0, 2);
	bCleanMode = (HUDMode == 0);
	SetCleanMode(bCleanMode);

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

	// Mirror both columns out to the corners from the three Interface numbers. This has to run
	// BEFORE anything reads ReadoutOffset/DetailOffset — the background panels, the progress
	// track and the title rule are all positioned off them further down.
	ReadoutOffset = FVector(UIDepth, -UIMarginRight, UIMarginUp);
	DetailOffset  = FVector(UIDepth,  UIMarginRight, UIMarginUp);
	if (Readout)
	{
		Readout->SetRelativeLocation(ReadoutOffset);
		Readout->SetWorldSize(ReadoutSize);
	}

	// Always-on-top text material so station geometry can never occlude or shade the info layer.
	if (!ReadoutMaterial)
		ReadoutMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_ReadoutText.M_ReadoutText"));
	if (ReadoutMaterial)
	{
		// One shared MID so ApplyPPPreset can drive GradeComp (the anti-grade brightness boost).
		// A material without the parameter just ignores the write — nothing depends on the patch
		// script having run.
		if (!TextMID) TextMID = UMaterialInstanceDynamic::Create(ReadoutMaterial, this);
		for (UTextRenderComponent* T : { Readout, FpsBig, DetailIndex, DetailTitle, DetailSub, DetailScale, DetailProv })
			if (T) T->SetTextMaterial(TextMID ? (UMaterialInterface*)TextMID : ReadoutMaterial.Get());
	}
	if (Readout) Readout->SetTextRenderColor(TextColor.ToFColor(true));   // constant readout uses the interface colour

	// HUD ON TOP, no environment reaction (Michael): the text was being occluded by 3D objects and picking up
	// lights/shadows. The material is already Unlit + DisableDepthTest, but the COMPONENTS also need:
	//  - a very high translucency sort priority so they draw AFTER all scene translucency (never behind a mesh)
	//  - no shadow casting / receiving (a TextRender shouldn't, but be explicit)
	//  - render in the main pass only (so nothing depth-writes them into occlusion)
	// Excluding them from the PP presets: they are Unlit + depth-test-off, so lighting/shadows already miss
	// them; the remaining grade (tonemap/color) is applied to the whole frame — the honest way to exempt HUD
	// from that is a separate post-tonemap pass, which TextRender can't do alone. Flagged below.
	for (UTextRenderComponent* T : { Readout, FpsBig, DetailIndex, DetailTitle, DetailSub, DetailScale, DetailProv })
	{
		if (!T) continue;
		// Mark every HUD glyph for the post-tonemap restore. Writing custom depth is what puts
		// the pixel in the stencil buffer at all; the value is what M_PP_HUD matches against.
		T->SetRenderCustomDepth(bHUDExemptFromPP);
		T->SetCustomDepthStencilValue(HUDStencil);
		T->SetTranslucentSortPriority(1000);   // draw last, on top of scene translucency
		T->SetCastShadow(false);
		T->bAffectDynamicIndirectLighting = false;
		T->bAffectDistanceFieldLighting = false;
		T->SetReceivesDecals(false);
	}

	// The caption is a RIGHT-aligned column anchored from the TOP, so it grows down-and-inward from
	// the top-right corner exactly as the readout grows down-and-inward from the top-left.
	for (UTextRenderComponent* T : { DetailIndex, DetailTitle, DetailSub, DetailScale, DetailProv })
		if (T) { T->SetHorizontalAlignment(EHTA_Right); T->SetVerticalAlignment(EVRTA_TextTop); }
	if (!TitleFont)
		TitleFont = LoadObject<UFont>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_fonts/F_Cormorant.F_Cormorant"));
	if (TitleFont && DetailTitle) DetailTitle->SetFont(TitleFont);   // guarded: null → keep the legible default sans
	LayoutInterface();   // positions + sizes both columns; re-run every frame so the numbers tune live

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
	// the hairline rules and the progress bar are HUD too — same stencil, same exemption
	for (UStaticMeshComponent* M : { ReadoutBar, ReadoutBarFill, DetailRule })
	{
		if (!M) continue;
		M->SetRenderCustomDepth(bHUDExemptFromPP);
		M->SetCustomDepthStencilValue(HUDStencil);
	}
	SetupAccentMesh(ReadoutBar,     ReadoutBarMID,     Cube, AccMat, AccentColor, TrackDim);
	SetupAccentMesh(ReadoutBarFill, ReadoutBarFillMID, Cube, AccMat, AccentColor, 1.0f);   // width driven per-frame
	SetupAccentMesh(DetailRule,     DetailRuleMID,     Cube, AccMat, AccentColor, 1.0f);
	// The rules hang off the two column anchors, so LayoutInterface places them too — one function
	// owns the whole interface geometry and there is no second copy of the arithmetic to drift.
	UILaidDepth = -1.f;   // force it: the accent meshes only exist as of these three lines
	LayoutInterface();

	// ── the energy shell, allocated once ─────────────────────────────────────────────────
	// Directions on a Fibonacci sphere: an even shell with no pole clumping and no seam, which
	// a naive lat/long loop cannot give and which shows up immediately on a shell this sparse.
	if (CH4EnergyISM && CH4EnergyCount > 0)
	{
		UStaticMesh* Shard = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_FillerGlow.M_FillerGlow"));
		if (!Glow) Glow = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_Streak.M_Streak"));
		if (Shard) CH4EnergyISM->SetStaticMesh(Shard);
		if (Glow)
		{
			CH4EnergyMID = UMaterialInstanceDynamic::Create(Glow, this);
			CH4EnergyISM->SetMaterial(0, CH4EnergyMID ? CH4EnergyMID.Get() : Glow);
		}
		CH4EnergyISM->ClearInstances();
		CH4EnergyDir.Reset();     CH4EnergyJitter.Reset();  CH4EnergyPhase.Reset();
		const float Ga = PI * (3.f - FMath::Sqrt(5.f));            // golden angle
		for (int32 i = 0; i < CH4EnergyCount; ++i)
		{
			const float z = 1.f - 2.f * (i + 0.5f) / (float)CH4EnergyCount;
			const float r = FMath::Sqrt(FMath::Max(0.f, 1.f - z * z));
			const float a = Ga * i;
			CH4EnergyDir.Add(FVector(FMath::Cos(a) * r, FMath::Sin(a) * r, z));
			CH4EnergyJitter.Add(FMath::FRand());
			CH4EnergyPhase.Add(FMath::FRand() * 2.f * PI);
			CH4EnergyISM->AddInstance(FTransform(FRotator::ZeroRotator, Anchor, FVector(0.01f)));
		}
		CH4EnergyISM->SetVisibility(false);
		UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] CH4 energy shell: %d shards"), CH4EnergyCount);
	}

	InitStreaks();
	// Stage soundscape: emit ONLY on the primary node (or PIE) so the cluster doesn't echo N× — the
	// non-primary nodes still tick + drive ZoomProgress, they just make no sound.
	if (!bInCluster || bIsPrimary)
		InitStageAudio();

	// ── Cinematic post-processing ────────────────────────────────────────────────────────────
	// PREFER A VOLUME PLACED IN THE LEVEL. The comment here used to say "reuse one already in the
	// level if present, else spawn one" — and the code never looked. It always spawned, and that
	// is why the presets did nothing on the wall: the cluster logs show "PPVolume spawned" and
	// "PP preset 1/2/3" on both nodes, so the path fires, yet preset 2's SceneColorTint
	// (1.0, 0.22, 0.18) — an unmissable red wash — never reached the render.
	//
	// APostProcessVolume is an ABrush. Spawned at runtime it has no brush geometry, and every
	// property that decides whether it participates (bUnbound, Priority, bEnabled) was being set
	// AFTER SpawnActor — i.e. after the actor had already registered itself and been inserted
	// into the world's priority-sorted volume list. A volume placed in the level is built and
	// registered by the editor and has none of those problems.
	if (UWorld* PPW = GetWorld())
	{
		for (TActorIterator<APostProcessVolume> It(PPW); It; ++It)
		{
			if (It->Tags.Contains(FName(TEXT("QZPostProcess")))) { PPVolume = *It; break; }
			if (!PPVolume) PPVolume = *It;   // any placed volume beats a spawned one
		}
		if (!PPVolume)
		{
			// Fallback only. DEFERRED, so the flags are set before the actor registers rather
			// than after — the ordering that made the spawned volume unreliable.
			FTransform T = FTransform::Identity;
			APostProcessVolume* V = PPW->SpawnActorDeferred<APostProcessVolume>(
				APostProcessVolume::StaticClass(), T);
			if (V)
			{
				V->bUnbound = true; V->Priority = 1000.f; V->BlendWeight = 1.f; V->bEnabled = true;
				V->FinishSpawning(T);
				PPVolume = V;
			}
		}
	}
	if (PPVolume) { PPVolume->bUnbound = true; PPVolume->Priority = 1000.f; PPVolume->BlendWeight = 1.f; PPVolume->bEnabled = true; }

	// ── the HUD restore, added as a blendable on the same volume ─────────────────────────
	// On the SAME volume on purpose: its weight rides BlendWeight, so at weight 0 there is no
	// grade AND no restore, and the two can never disagree about how much is applied.
	if (bHUDExemptFromPP && PPVolume)
	{
		if (!HUDPostMaterial)
			HUDPostMaterial = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_PP_HUD.M_PP_HUD"));
		if (HUDPostMaterial)
		{
			HUDPostMID = UMaterialInstanceDynamic::Create(HUDPostMaterial, this);
			if (HUDPostMID)
			{
				HUDPostMID->SetScalarParameterValue(TEXT("HUDStencil"), (float)HUDStencil);
				HUDPostMID->SetVectorParameterValue(TEXT("HUDColor"), TextColor);
				PPVolume->AddOrUpdateBlendable(HUDPostMID, 1.f);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] HUD post-process exemption: material %s, stencil %d"),
			HUDPostMaterial ? TEXT("loaded") : TEXT("MISSING — HUD will be graded"), HUDStencil);
	}
	// THE INTERFACE, in a pass the grade cannot reach. Created here rather than in the
	// constructor: CreateWidget needs a PlayerController, and on a cluster secondary there may
	// not be one — a null widget simply means the 3D fallback keeps working.
	if (bHUDScreenSpace && !HudTextWidget)
	{
		if (APlayerController* HudPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			HudTextWidget = CreateWidget<UQHudText>(HudPC, UQHudText::StaticClass());
			if (HudTextWidget)
			{
				HudTextWidget->AddToViewport(500);   // above everything the show draws
				HudTextWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] screen-space HUD %s"),
			HudTextWidget ? TEXT("created") : TEXT("FAILED (no PlayerController) — 3D text stays"));
	}

	ApplyPPPreset(PPPreset);
	UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] PPVolume %s  (unbound=%d prio=%.0f enabled=%d)"),
		PPVolume ? *PPVolume->GetName() : TEXT("FAILED — presets have no volume"),
		PPVolume ? (int32)PPVolume->bUnbound : -1,
		PPVolume ? PPVolume->Priority : -1.f,
		PPVolume ? (int32)PPVolume->bEnabled : -1);

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
#if WITH_EDITOR
	// EDITOR WORLD: the only job is sequencer-scrub feedback — mirror the MPC's orbital_noise
	// into every Niagara so the curve is authored against live particles. The show logic below
	// must never run outside a game world (ApplyStations would rearrange the open level).
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		if (M169MPC)
		{
			const float ON = UKismetMaterialLibrary::GetScalarParameterValue(
				this, M169MPC, TEXT("orbital_noise"));
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			{
				TArray<UNiagaraComponent*> NCs;
				It->GetComponents<UNiagaraComponent>(NCs);
				for (UNiagaraComponent* NC : NCs)
					NC->SetVariableFloat(OrbitalNoiseParam, ON);
			}
		}
		return;
	}
#endif
	if (bIsPrimary)
	{
		PollInput(Dt);
	}
	// ── HITCH DETECTOR ──────────────────────────────────────────────────────────────────────────────────
	// Michael reports a heavy stall at the start that clears once the lab settles. Rather than theorise a
	// fifth time, measure: log any frame slower than HitchMs, with the pawn's own phase timings, and say
	// whether the time is INSIDE this Tick or outside it (= renderer/streaming/PSO, i.e. not the pawn).
	const double TickT0 = FPlatformTime::Seconds();
	static double LastFrameEnd = 0.0;
	const double GapMs = (LastFrameEnd > 0.0) ? (TickT0 - LastFrameEnd) * 1000.0 : 0.0;

	const double B0 = FPlatformTime::Seconds();
	{
		const int32 PrevStations = MatCacheStations;
		BuildMaterialCache();   // no-op once every station's DMIs are cached (cheap count check while streaming)
		// ON SITE the show STREAMS its stations — a station arriving after the [X] toggle kept its
		// full shaders and the "simple" compare silently measured a mixed frame. New arrivals now
		// join the running diagnosis (the apply is re-entrant and skips what is already swapped).
		if (ShaderLevel != 0 && MatCacheStations > PrevStations) ApplyShaderLevel(ShaderLevel);
	}
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
	UpdateStagePresets();   // the ladder drives the look: per-stage PP preset + style light

	// BROADCAST LAST, NOT FIRST.
	// This used to run at the top of Tick, right after PollInput — so it sent the values the
	// frame STARTED with, before UpdateStagePresets had chosen anything. The cluster event from
	// frame N therefore carried the preset from frame N-1, arrived a frame later, and
	// OnClusterEvent dutifully applied it: the blend set P5 on frame 1 and the stale event put
	// it back to 0 on frame 2. The log shows exactly that —
	//     [ 1] PP preset 5
	//     [ 2] PP preset 0
	// and because PresetBlendApplied was already 5 the seed never fired again. Stuck on 0
	// forever, which is "the preset does not trigger at all". Mine, from moving the seed out of
	// the every-frame force.
	// Sending at the END means the event carries the frame's FINAL state, which is what every
	// other broadcast value (fillers, style light, palette, clean mode) wanted all along.
	if (bIsPrimary)
	{
		Broadcast();
	}

	// Orbit angular rate, measured the same way VisVel is: a per-frame delta over Dt. FindDeltaAngle
	// keeps the yaw wrap at +-180 from registering as a huge spin.
	{
		const float dY = FMath::FindDeltaAngleDegrees(PrevOrbitYaw,   OrbitYaw);
		const float dP = FMath::FindDeltaAngleDegrees(PrevOrbitPitch, OrbitPitch);
		PrevOrbitYaw = OrbitYaw;  PrevOrbitPitch = OrbitPitch;
		const float Inv = (Dt > 1e-5f) ? (1.f / Dt) : 0.f;
		// smoothed a little: raw mouse deltas are spiky, and a spiky rate makes the field stutter
		OrbitYawRate   = FMath::FInterpTo(OrbitYawRate,   dY * Inv, Dt, 12.f);
		OrbitPitchRate = FMath::FInterpTo(OrbitPitchRate, dP * Inv, Dt, 12.f);
	}

	// ORBIT HAS TO BE MEASURED THE SAME WAY THE ZOOM IS.
	// The first version added the orbit as a flat term straight onto Intensity, which threw away the
	// only thing that makes the streaks a depth cue: ObserverSpeed is a RELATIVE speed, in
	// observer-sizes per second, and the observer SHRINKS as you descend. That is why the zoom
	// produces nothing at the lab and a storm at the nucleus. A flat orbit term has no such
	// dependence, so a swing in the lab fired at full strength — the same swing, the same streaks,
	// at every depth.
	//
	// Expressed as an observer speed instead, the orbit goes through the identical log ramp and
	// inherits the depth behaviour for free: at the lab the observer is human-sized and the orbit is
	// slow relative to it; deep down the same degrees per second is an enormous relative speed.
	// StreakOrbitBoost now means "a full-rate orbit counts as this fraction of a full-rate zoom".
	const float OrbRate  = FMath::Sqrt(OrbitYawRate * OrbitYawRate +
	                                   OrbitPitchRate * OrbitPitchRate);
	const float OrbAct   = FMath::Clamp(OrbRate / FMath::Max(StreakOrbitFull, 1.f), 0.f, 1.f);
	const float OrbSpeed = (ObserverRefSpeed / FMath::Max(ObserverSize, 1e-30f))
	                     * OrbAct * StreakOrbitBoost;

	// Streaks fade in as the relative speed climbs (log scale) — no streaks up top, building deep.
	const float EffSpeed = FMath::Max(ObserverSpeed, OrbSpeed);
	float Intensity = 0.f;
	if (EffSpeed > 1.f)
	{
		const float Lo = FMath::Loge(FMath::Max(StreakSpeedLo, 1.f));
		const float Hi = FMath::Loge(FMath::Max(StreakSpeedHi, StreakSpeedLo * 10.f));
		float t = FMath::Clamp((FMath::Loge(EffSpeed) - Lo) / (Hi - Lo), 0.f, 1.f);
		Intensity = t * t * (3.f - 2.f * t);
	}
	// Time-smooth: gentle GLIDE in (StreakFade), snappier retract out (StreakFadeOut) so a stop reads crisp.
	const float FadeRate = (Intensity < StreakIntensitySmoothed) ? StreakFadeOut : StreakFade;
	StreakIntensitySmoothed = FMath::FInterpTo(StreakIntensitySmoothed, Intensity, Dt, FadeRate);
	UpdateStreaks(Dt, VisVel, StreakIntensitySmoothed);
	UpdateAudio();
	UpdateFillers(Dt);   // molecular fillers: inherit orbit + self-swirl + zoom-scale
	UpdateFadeTagged();  // QZFade<N>: environment assets that ride a station's fade without being one
	UpdateLights();      // fade station-sublevel lights by their station's visibility (populated in ApplyStations)
	UpdateGlobalEnv();   // and scale the SHARED rig (key/fill/sky/fog/atmosphere) by what is on screen
	ApplyStyleLight();   // every frame so the intensity EASES toward its ladder target (not just on press)
	UpdateOxidation(Dt); // S3: ping-pong the sulfur-switch SVT frame (dwell -> snap -> dwell)
	UpdateGuides();      // authoring aid: fixed front-facing marker + zoom-centre reticle at the Anchor
	ApplyNiraShells();   // hide SM_S2_NirA's enclosing ribbon/VOLUME so they don't obscure MET169
	ApplyPalette();      // squeeze the hue spread toward the amber/blue poles (Back/Start, R3 = reset)
	UpdateQuarkTriad(Dt);// S7: valence quarks wander, gluon strings are rebuilt to follow them
	UpdateCH4Cycle(Dt);  // CH4: FmoB docks + oxidises Met169, reductase strips it back. Loops.
	UpdateCH4Energy(Dt); // and the activation/deactivation shell that reads off the same beats
	UpdateRefParticles();// log-spaced self-similar mote field: the scale reference
	SampleFPS(Dt);       // feed the readout's current + median frame rate
	UpdateCommsStreams(Dt); // per-station outward data traffic: everything communicates

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

void AQZoomStagePawn::ToggleSim8K()
{
	bSim8K = !bSim8K;
	IConsoleVariable* SP = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	if (!SP) return;
	if (bSim8K)
	{
		FVector2D VP(1920, 1080);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			const FIntPoint P = GEngine->GameViewport->Viewport->GetSizeXY();
			if (P.X > 0 && P.Y > 0) VP = FVector2D(P.X, P.Y);
		}
		// enough percentage that internal pixels reach the wall's 7680x4320
		Sim8KPct = FMath::Clamp(FMath::CeilToInt(
			100.f * FMath::Sqrt(33177600.f / float(VP.X * VP.Y))), 100, 400);
		SP->Set(float(Sim8KPct));
		UE_LOG(LogTemp, Warning,
			TEXT("[QZoomPerf] SIM 8K: ScreenPercentage %d%% on a %.0fx%.0f window ")
			TEXT("(~%.1f MPix internal; the wall's stereo adds ~x1.9 on top)"),
			Sim8KPct, VP.X, VP.Y,
			VP.X * VP.Y * FMath::Square(Sim8KPct / 100.f) / 1e6f);
	}
	else
	{
		SP->Set(100.f);
		Sim8KPct = 0;
		UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] SIM 8K off"));
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
	float Target = (RT - LT) * BaseZoomRate;

	// PACING — the chapter's duration. Multiplies the trigger rate and NOTHING else: no station
	// moves, no band changes width, every depth ever dialled stays where it is. Only the seconds
	// between them change. That is the whole point: the ladder is log-spaced by physics, so a
	// chapter's WIDTH is not authorable, but its DURATION now is.
	Target *= PaceAt(ZoomProgress);

	// DETENTS — the script's [PAUSE] marks made physical. Near an authored depth the descent gets heavy,
	// so the operator can settle on a beat and talk over it instead of fighting the trigger to hold still.
	// Deliberately a damping factor and not a stop: DetentDamping is clamped above zero, so holding the
	// trigger always pulls through. A detent you cannot leave is a trap, not a rest.
	if (ZoomDetents.Num() > 0 && DetentWidth > 1e-4f)
	{
		float Nearest = BIG_NUMBER;
		for (float D : ZoomDetents) Nearest = FMath::Min(Nearest, FMath::Abs(ZoomProgress - D));
		if (Nearest < DetentWidth)
		{
			const float T = Nearest / DetentWidth;                       // 0 at the centre, 1 at the edge
			Target *= FMath::Lerp(FMath::Clamp(DetentDamping, 0.05f, 1.f), 1.f, T * T * (3.f - 2.f * T));
		}
	}

	ZoomVel = FMath::FInterpTo(ZoomVel, Target, Dt, SpeedDamping);
	ZoomProgress = FMath::Clamp(ZoomProgress + ZoomVel * Dt, 0.f, 1.f);

	// Slow automatic orbit on top of the stick. The world turns; the camera and the DCRA do not.
	if (!FMath::IsNearlyZero(OrbitAutoYawDegPerSec))
	{
		OrbitYaw = FMath::Fmod(OrbitYaw + OrbitAutoYawDegPerSec * Dt, 360.f);
	}

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

	// ── IDLE ORBIT ──────────────────────────────────────────────────────────────────────────────────────
	// Slate's interaction clock is the "any input" detector: every key, axis and future binding resets it
	// without this code keeping a list. Belt-and-braces: OR in the analogs parsed above, in case a pad
	// driver does not repeat held-analog events into Slate. Buttons need no such guard — a held button
	// was pressed once, and that press went through Slate.
	{
		double SinceInput = double(IdleOrbitAfterSeconds);   // no Slate (headless/cook) → treat as idle
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication& Slate = FSlateApplication::Get();
			SinceInput = Slate.GetCurrentTime() - Slate.GetLastUserInteractionTime();
		}
		const bool bAnalogHeld = (RT > 0.f || LT > 0.f || RX != 0.f || RY != 0.f || LX != 0.f || LY != 0.f);
		const bool bWantIdle = bIdleOrbit && !bAnalogHeld && SinceInput >= double(IdleOrbitAfterSeconds);
		const float Rate = bWantIdle ?  1.f / FMath::Max(IdleOrbitRampIn,  0.05f)
		                             : -1.f / FMath::Max(IdleOrbitRampOut, 0.05f);
		IdleWeight = FMath::Clamp(IdleWeight + Rate * Dt, 0.f, 1.f);
		if (IdleWeight > 0.f)
		{
			const float e = IdleWeight * IdleWeight * (3.f - 2.f * IdleWeight);   // smoothstep the ramp
			OrbitYaw = FMath::Fmod(OrbitYaw + e * IdleOrbitYawDegPerSec * Dt, 360.f);
			IdlePhase += Dt;
			const float Breath = IdleOrbitPitchDeg
				* FMath::Sin(2.f * PI * IdlePhase / FMath::Max(IdleOrbitPitchPeriod, 1.f));
			// the DELTA, weighted: the breath oscillates around the operator's own pitch (OrbitPitch is
			// free and unclamped), and while easing out the remaining offset is walked back to zero by
			// the shrinking weight instead of being dropped in one visible step.
			OrbitPitch += e * Breath - IdlePitchPrev;
			IdlePitchPrev = e * Breath;
		}
		else
		{
			OrbitPitch -= IdlePitchPrev;   // the frame the weight dies, take the last breath offset with it
			IdlePhase = 0.f;
			IdlePitchPrev = 0.f;
		}
	}

	// ── PERF BISECT: 0 = simulate the wall's pixel load (also R3 inside the menu) ───────────────────
	{
		const bool bZero = PC->IsInputKeyDown(EKeys::Zero);
		static bool bZeroPrev = false;
		if (bZero && !bZeroPrev) ToggleSim8K();
		bZeroPrev = bZero;
	}

	// ── PERF BISECT: 1-9 toggle-mute ladder rows 0-8 ────────────────────────────────────────────────────
	// Keyboard only, on purpose: this is a desk diagnostic, and every gamepad button is already spoken for.
	{
		static const FKey DigitKeys[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		                                   EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		uint16 Bits = 0;
		for (int32 i = 0; i < 9; ++i)
			if (PC->IsInputKeyDown(DigitKeys[i])) Bits |= uint16(1) << i;
		const uint16 Pressed = Bits & ~PrevDigitBits;
		PrevDigitBits = Bits;
		for (int32 i = 0; i < 9; ++i)
			if (Pressed & (uint16(1) << i))
			{
				StationMuteMask ^= (1 << i);
				const FString RowName = Handover.IsValidIndex(i) ? Handover[i].Name.ToString() : TEXT("?");
				UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] row %d (%s) %s  mask=0x%X"),
					i, *RowName, (StationMuteMask & (1 << i)) ? TEXT("MUTED") : TEXT("unmuted"), StationMuteMask);
			}
	}

	// D-Pad Up/Down = PP preset; D-Pad Left/Right = space-filler cycle. Rising-edge.
	// KEYBOARD EQUIVALENTS. These were gamepad-only, so at a desk without a controller the
	// preset switcher simply did not exist — which is what "the post processing switcher is
	// gone" is. P / O step the preset, K / J the filler mode. None of these four collide with
	// the keys already bound here (W, S, F2, [ , ]).
	const bool bUp = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up)    || PC->IsInputKeyDown(EKeys::P);
	const bool bDn = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down)  || PC->IsInputKeyDown(EKeys::O);
	const bool bLf = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Left)  || PC->IsInputKeyDown(EKeys::J);
	const bool bRt = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Right) || PC->IsInputKeyDown(EKeys::K);
	if (HUDMode == 2)
	{
		// MUTE MENU is modal on the D-Pad: Up/Down move the cursor. The preset/filler bindings are
		// deliberately swallowed — cycling a PP preset or rebuilding the fillers mid-bisect would
		// change the very frame time being measured.
		const int32 NRows = FMath::Clamp(StationCount, 1, 9);
		if (bUp && !bUpPrev)   MuteSel = (MuteSel + NRows - 1) % NRows;
		if (bDn && !bDownPrev) MuteSel = (MuteSel + 1) % NRows;
		if (bRt && !bRightPrev)
		{
			bParticlesMuted = !bParticlesMuted;
			UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] particles %s"),
				bParticlesMuted ? TEXT("MUTED") : TEXT("on"));
		}
		if (bLf && !bLeftPrev)
		{
			bAnimMuted = !bAnimMuted;
			UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] animation %s"),
				bAnimMuted ? TEXT("MUTED") : TEXT("on"));
		}
	}
	else
	{
		if (bUp && !bUpPrev)    { PPPreset   = (PPPreset   + 1) % 10; ApplyPPPreset(PPPreset); }
		if (bDn && !bDownPrev)  { PPPreset   = (PPPreset   + 9) % 10; ApplyPPPreset(PPPreset); }
		if (bRt && !bRightPrev) { FillerMode = (FillerMode + 1) % 4; SetFillerMode(FillerMode); }
		if (bLf && !bLeftPrev)  { FillerMode = (FillerMode + 3) % 4; SetFillerMode(FillerMode); }
	}
	bUpPrev = bUp; bDownPrev = bDn; bLeftPrev = bLf; bRightPrev = bRt;

	// LB / RB = STYLE LIGHT intensity (the freed S4 key light). 5 steps: off / dim / base / bright / blown.
	// ── LB: the style light, now a single CYCLE ─────────────────────────────────────────────
	// It used to need both shoulders (RB up, LB down). RB is the reaction trigger now, so LB
	// wraps around the ladder on its own — one button, every step reachable, wraps to dark.
	const bool bLB = PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder) || PC->IsInputKeyDown(EKeys::Q);
	if (bLB && !bLBPrev)
	{
		if (HUDMode == 2)   // in the MUTE MENU LB is the NirA half of the bisect split
		{
			bNirAMuted = !bNirAMuted;
			UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] NirA %s"),
				bNirAMuted ? TEXT("MUTED") : TEXT("on"));
		}
		else
		{
			const int32 N = FMath::Max(StyleLightLadder.Num(), 1);
			StyleLightStep = (StyleLightStep + 1) % N;
			ApplyStyleLight();
		}
	}
	bLBPrev = bLB;

	// ── RB: TAP toggles the reaction, HOLD ramps it to CH4SpeedMax ──────────────────────────
	// The distinction is made on RELEASE, not on press: a press cannot yet be classified, and
	// acting on it would fire the toggle at the start of every hold. CH4HoldToTap is deliberately
	// generous (0.35 s) — a decisive tap on a shoulder button still lands well under it, and
	// "hold" is a gesture nobody performs by accident.
	const bool bRB = PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder) || PC->IsInputKeyDown(EKeys::E);
	if (bRB)
	{
		CH4HoldT += Dt;
	}
	else if (bRBPrev)
	{
		if (CH4HoldT < CH4HoldToTap)
		{
			bCH4Running = !bCH4Running;
			UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] reaction %s"),
				bCH4Running ? TEXT("RUNNING") : TEXT("stopped"));
		}
		CH4HoldT = 0.f;
	}
	bRBPrev = bRB;
	// eased, so letting go coasts back to normal speed instead of stepping
	const float SpeedTarget = (bRB && CH4HoldT >= CH4HoldToTap) ? FMath::Max(CH4SpeedMax, 1.f) : 1.f;
	CH4SpeedNow = FMath::FInterpTo(CH4SpeedNow, SpeedTarget, Dt, 3.5f);

	// A / B (face bottom/right) = filler DENSITY (moved off the shoulders). 5 levels, sparse -> EXTREME.
	// In the MUTE MENU they are the menu's instead: A toggles the selected row, B unmutes everything.
	const bool bA = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom), bB = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Right);
	if (HUDMode == 2)
	{
		if (bA && !bAPrev)
		{
			StationMuteMask ^= (1 << MuteSel);
			UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] row %d %s  mask=0x%X"), MuteSel,
				(StationMuteMask & (1 << MuteSel)) ? TEXT("MUTED") : TEXT("unmuted"), StationMuteMask);
		}
		if (bB && !bBPrev && StationMuteMask != 0)
		{
			StationMuteMask = 0;
			UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] all rows unmuted"));
		}
	}
	else
	{
		if (bB && !bBPrev) { FillerDensity = FMath::Min(FillerDensity + 1, 4); InitFillers(); }
		if (bA && !bAPrev) { FillerDensity = FMath::Max(FillerDensity - 1, 0); InitFillers(); }
	}
	bAPrev = bA; bBPrev = bB;

	// X (face-button left) = cycle the NirA representation: 0 high-res FBX / 1 low-res FBX / 2 procedural
	// In the MUTE MENU X is the shader toggle instead: full shaders <-> the flat stand-in.
	const bool bX = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);
	if (bX && !bXPrev)
	{
		if (HUDMode == 2) { ShaderLevel = (ShaderLevel + 1) % 3; ApplyShaderLevel(ShaderLevel); }
		else              { NiraVersion = (NiraVersion + 1) % FMath::Max(NiraVersionCount, 1); }
	}
	bXPrev = bX;

	// Y (face-button top) = HUD cycle: default(clean) -> editorial HUD -> mute menu -> default.
	// bCleanMode stays as the derived "is anything visible" flag so SetCleanMode and the existing
	// cluster "clean" parameter keep meaning what they always meant.
	const bool bY = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top);
	if (bY && !bYPrev)
	{
		HUDMode = (HUDMode + 1) % 3;
		bCleanMode = (HUDMode == 0);
		SetCleanMode(bCleanMode);
		ApplyPPPreset(PPPreset);   // the menu suspends the grade terms it cannot be read through
		UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] HUD mode %d (%s)"), HUDMode,
			HUDMode == 0 ? TEXT("clean") : HUDMode == 1 ? TEXT("HUD") : TEXT("MUTE MENU"));
	}
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
	if (bR3 && !bNaniteDiagPrev && HUDMode == 2)
	{
		ToggleSim8K();   // in the MUTE MENU R3 is the 8K simulation; Nanite diag stays outside
	}
	else if (bR3 && !bNaniteDiagPrev)
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
	// IN THE MUTE MENU it is the NANITE axis instead — mirroring R3, which is the 8K simulation there.
	// The bisect menu needs Nanite as a switch and not only as the R3 proxy-render DIAGNOSTIC: those
	// two answer different questions. The diagnostic asks "which Nanite path is the cluster taking";
	// this asks "is Nanite the problem at all". Only turning it off answers the second one.
	const bool bL3 = PC->IsInputKeyDown(EKeys::Gamepad_LeftThumbstick);
	if (bL3 && !bL3Prev)
	{
		if (HUDMode == 2) { bNaniteOff = !bNaniteOff; ApplyNaniteOff(); }
		else              { bOxFrozen = !bOxFrozen; }
	}
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
	Event.Parameters.Add(TEXT("mute"),  FString::FromInt(StationMuteMask));      // perf-bisect: every node drops the same rows
	Event.Parameters.Add(TEXT("hudm"),  FString::FromInt(HUDMode));              // Y-cycle state (clean / HUD / mute menu)
	Event.Parameters.Add(TEXT("msel"),  FString::FromInt(MuteSel));              // menu cursor, so the wall highlights the same row
	Event.Parameters.Add(TEXT("simp"),  FString::FromInt(ShaderLevel));            // shader TIER 0/1/2: every node must swap or the compare is meaningless
	Event.Parameters.Add(TEXT("c4p"),   FString::SanitizeFloat(CH4Phase));         // the reaction clock: floor + wall must scrub the same frame
	Event.Parameters.Add(TEXT("c4r"),   FString::FromInt(bCH4Running ? 1 : 0));
	Event.Parameters.Add(TEXT("pmut"),  FString::FromInt(bParticlesMuted ? 1 : 0)); // particle bisect axis: all nodes drop them together
	Event.Parameters.Add(TEXT("amut"),  FString::FromInt(bAnimMuted ? 1 : 0));      // CH4 split: the animation half
	Event.Parameters.Add(TEXT("nmut"),  FString::FromInt(bNirAMuted ? 1 : 0));      // CH4 split: the NirA half
	Event.Parameters.Add(TEXT("noff"),  FString::FromInt(bNaniteOff ? 1 : 0));      // Nanite axis: a per-node cvar,
	                                                                               // so every node must be told
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
	if (const FString* Mu = E.Parameters.Find(TEXT("mute")))  StationMuteMask = FCString::Atoi(**Mu);   // ApplyStations below picks it up
	if (const FString* Ms = E.Parameters.Find(TEXT("msel")))  MuteSel = FCString::Atoi(**Ms);
	if (const FString* Sp = E.Parameters.Find(TEXT("simp")))
	{
		// carries the TIER now, not a flag — 0/1/2. An older node sending 0/1 still lands on
		// HIGH/MID, which is the closest honest reading of a two-state message.
		const int32 NewLevel = FMath::Clamp(FCString::Atoi(**Sp), 0, 2);
		if (NewLevel != ShaderLevel) { ShaderLevel = NewLevel; ApplyShaderLevel(ShaderLevel); }
	}
	// SECONDARIES ONLY — the same rule free-look follows. This receive also fires on the primary
	// (self-loopback), and applying the echo there overwrote the live clock with last frame's
	// value and fought the RB toggle with its own stale echo: the phase froze and the running
	// state flickered, which read as "play/stop changes the look, wall only". The primary OWNS
	// the clock; everyone else copies it.
	if (!bIsPrimary)
	{
		if (const FString* C4 = E.Parameters.Find(TEXT("c4p")))  CH4Phase = FCString::Atof(**C4);
		if (const FString* C4r = E.Parameters.Find(TEXT("c4r"))) bCH4Running = (FCString::Atoi(**C4r) != 0);
	}
	// a toggle STATE, not an integrated clock — the primary's self-echo carries the same value,
	// so applying it everywhere is idempotent (unlike c4p, which oscillated)
	if (const FString* Pm = E.Parameters.Find(TEXT("pmut"))) bParticlesMuted = (FCString::Atoi(**Pm) != 0);
	if (const FString* Am = E.Parameters.Find(TEXT("amut"))) bAnimMuted = (FCString::Atoi(**Am) != 0);
	if (const FString* Nm = E.Parameters.Find(TEXT("nmut"))) bNirAMuted = (FCString::Atoi(**Nm) != 0);
	// r.Nanite is a PER-NODE console variable, so a bisect that only switched the primary would
	// compare a Nanite wall against a fallback floor and prove nothing. Applied unconditionally
	// (idempotent), same reasoning as the diagnostic above.
	if (const FString* No = E.Parameters.Find(TEXT("noff")))
	{
		bNaniteOff = (FCString::Atoi(**No) != 0);
		ApplyNaniteOff();
	}
	if (const FString* Hm = E.Parameters.Find(TEXT("hudm")))
	{
		const int32 NewMode = FCString::Atoi(**Hm);
		if (NewMode != HUDMode)   // visibility only on change — SetCleanMode toggles components
		{
			HUDMode = NewMode;
			bCleanMode = (HUDMode == 0);
			SetCleanMode(bCleanMode);
			// Same suspension on every node, or the wall reads its menu through a vignette the
			// floor no longer has — and a bisect compared across two different grades is noise.
			ApplyPPPreset(PPPreset);
		}
	}
	if (!bIsPrimary) ApplyFreeLook(LookYaw, LookPitch);   // primary already applied it in PollInput this frame
	ApplyStations();
}

void AQZoomStagePawn::SyncLadder()
{
	// The ladder used to be spread over StationCount, ScaleMeters and Handover, all index-aligned by
	// hand. Whichever one you edited, the other two silently disagreed — and because the numbers are
	// plausible on their own, a mismatch shows up as a stage that fades at the wrong depth rather
	// than as an error. Authoring now happens on the Handover rows and the rest is derived.
	if (Handover.Num() == 0) return;                 // nothing authored: leave the legacy arrays alone

	StationCount = Handover.Num();
	if (ScaleMeters.Num() < StationCount)
	{
		const float Last = (ScaleMeters.Num() > 0) ? ScaleMeters.Last() : 1.f;
		while (ScaleMeters.Num() < StationCount) ScaleMeters.Add(Last);
	}
	else if (ScaleMeters.Num() > StationCount)
	{
		ScaleMeters.SetNum(StationCount);
	}
	for (int32 i = 0; i < StationCount; ++i)
	{
		// 0 means "not authored here" so a pawn that was tuned before this existed keeps its scales
		if (Handover[i].ScaleMeters > 0.f) ScaleMeters[i] = Handover[i].ScaleMeters;
	}

	// The four caption arrays are index-aligned to the ladder, and they are the one part of it that
	// nothing else validates: a short array does not error, it just captions the wrong station. The
	// pawn carried six rows against an eight-station ladder for exactly that reason. Resizing them
	// here means a ladder edit can only ever produce a BLANK caption, never a wrong one.
	for (TArray<FString>* A : { &StageTitle, &StageSub, &StageScaleLabel, &StageProv })
		if (A->Num() != StationCount) A->SetNum(StationCount);
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

float AQZoomStagePawn::StationSizeMul(int32 N) const
{
	// Only an ENABLED row carries a size; a station still running on the globals renders at its
	// authored size, which is what every stage did before this knob existed.
	if (Handover.IsValidIndex(N) && Handover[N].bEnabled)
		return FMath::Max(Handover[N].SizeMul, 0.01f);
	return 1.f;
}

float AQZoomStagePawn::StationRenderScale(int32 N) const
{
	return StationScale(N) * StationSizeMul(N);
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

void AQZoomStagePawn::SampleFPS(float Dt)
{
	// Dt IS A CONSTANT AND MUST NOT BE MEASURED. DefaultEngine.ini runs bUseFixedFrameRate=60,
	// and under a fixed frame rate the engine hands every Tick exactly 1/60 s HOWEVER long the
	// frame really took — which is why the counter sat at a rock-steady 60 while the show was
	// visibly struggling. Real elapsed wall time is the only honest sample; Dt is still used
	// below only as the smoothing step, where a nominal step is fine.
	const double Now = FPlatformTime::Seconds();
	if (FpsLastWall <= 0.0) { FpsLastWall = Now; return; }
	const float Real = (float)(Now - FpsLastWall);
	FpsLastWall = Now;
	// >1 s is a stall (level load, PIE spin-up), not a frame — poisoning a 20 s median with one
	// 5 s sample would flatten it for half a minute.
	if (Real <= 0.f || Real > 1.f) return;

	// CURRENT is smoothed over ~0.25 s. Raw 1/dt swings by tens of frames between ticks and is
	// genuinely unreadable on a wall; this still moves instantly enough to catch a hitch.
	const float Inst = 1.f / Real;
	FpsCurrent = (FpsCurrent <= 0.f) ? Inst : FMath::FInterpTo(FpsCurrent, Inst, FMath::Max(Dt, Real), 4.f);

	// Ring sized for the window at a generous frame rate. If the machine runs faster than this
	// the window simply covers less than FpsWindowSeconds rather than misreporting.
	const int32 Want = FMath::Clamp(FMath::CeilToInt(FpsWindowSeconds * 240.f), 256, 8192);
	if (FpsRing.Num() != Want)
	{
		FpsRing.SetNumZeroed(Want);
		FpsHead = 0;
		FpsFilled = 0;
	}
	FpsRing[FpsHead] = Real;
	FpsHead = (FpsHead + 1) % FpsRing.Num();
	FpsFilled = FMath::Min(FpsFilled + 1, FpsRing.Num());

	// Sorting ~1200 samples every frame to draw one number would be its own perf problem, so
	// recompute a few times a second. The window is walked BACKWARDS accumulating real elapsed
	// time, which means it is exactly FpsWindowSeconds of history regardless of frame rate —
	// a fixed sample count would silently be 5 s at 240 fps and 40 s at 30 fps.
	FpsMedianTimer -= Real;
	if (FpsMedianTimer > 0.f) return;
	FpsMedianTimer = 0.25f;

	TArray<float> Win;
	Win.Reserve(FpsFilled);
	float Acc = 0.f;
	for (int32 i = 0; i < FpsFilled; ++i)
	{
		const int32 idx = (FpsHead - 1 - i + FpsRing.Num() * 2) % FpsRing.Num();
		const float d = FpsRing[idx];
		if (d <= 0.f) continue;
		Win.Add(d);
		Acc += d;
		if (Acc >= FpsWindowSeconds) break;
	}
	if (Win.Num() == 0) return;
	Win.Sort();
	// median of frame TIMES, then inverted. 1/x is monotonic, so this equals the median of the
	// frame RATES — and taking it on the times avoids inverting every sample first.
	const int32 M = Win.Num() / 2;
	const float MedDt = (Win.Num() % 2) ? Win[M] : (Win[M - 1] + Win[M]) * 0.5f;
	FpsMedian = (MedDt > 0.f) ? (1.f / MedDt) : 0.f;
}


void AQZoomStagePawn::UpdateCommsStreams(float Dt)
{
	if (!bCommsStreams)
	{
		if (CommsISM) CommsISM->SetVisibility(false);
		return;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) return;

	// One entry per station, authored for FEEL. Same gesture everywhere — packets outward along
	// lanes — differing only in rhythm and density, which is what keeps the ladder coherent.
	if (Comms.Num() < StationCount)
	{
		Comms.SetNum(StationCount);
		auto Set = [this](int32 i, int32 lanes, int32 per, float spd, float len, float thick,
		                  float wander, float commit, float branch, float blinkHz,
		                  float burst, FLinearColor c, float bright)
		{
			if (!Comms.IsValidIndex(i)) return;
			FQZComms& E = Comms[i];
			E.Lanes = lanes; E.PerLane = per; E.Speed = spd; E.LengthUU = len;
			E.Thickness = thick; E.Wander = wander; E.Commit = commit; E.Branch = branch;
			E.BlinkHz = blinkHz; E.Burst = burst; E.Color = c; E.Brightness = bright;
		};
		//   idx lanes per  speed  len  thick wander commit branch blink burst  colour                          bright
		// S0 forest: few slow trails that wander a lot and commit little — scent on the air, barely aimed
		Set(0,  8, 12, 0.045f, 170.f, 0.30f, 0.62f, 0.12f, 0.30f, 1.1f, 0.00f, FLinearColor(0.95f,0.78f,0.42f), 3.0f);
		// S1 mycelium: THE foraging network. Heavy branching, hard commitment — a solved path
		Set(1, 26, 26, 0.115f, 130.f, 0.10f, 0.30f, 0.55f, 0.72f, 2.4f, 0.00f, FLinearColor(0.62f,0.96f,0.72f), 5.0f);
		// S2 conidium: a spore CALLING. Few trails, hard bursts, fast nervous blink
		Set(2, 12, 16, 0.170f, 115.f, 0.20f, 0.48f, 0.26f, 0.35f, 5.5f, 0.62f, FLinearColor(1.00f,0.72f,0.38f), 5.4f);
		// S3 NirA: protein signalling — searches briefly, then commits hard and runs
		Set(3, 18, 22, 0.240f,  95.f, 0.12f, 0.28f, 0.66f, 0.40f, 3.0f, 0.00f, FLinearColor(0.55f,0.82f,1.00f), 5.2f);
		// S4 Met169: the switch. Sparse deliberate pulses, slow heavy blink — one packet matters
		Set(4,  7, 10, 0.300f,  85.f, 0.26f, 0.22f, 0.60f, 0.20f, 1.6f, 0.55f, FLinearColor(1.00f,0.66f,0.20f), 7.0f);
		// S5 density: probability, not messages. Many faint trails wandering with almost no commitment
		Set(5, 32, 30, 0.130f,  55.f, 0.38f, 0.80f, 0.08f, 0.55f, 7.5f, 0.00f, FLinearColor(0.82f,0.74f,1.00f), 3.6f);
		// S6 nucleus: the fastest exchange there is — short, quick, near-strobing
		Set(6, 22, 36, 0.640f,  45.f, 0.16f, 0.34f, 0.70f, 0.45f, 11.0f, 0.00f, FLinearColor(0.72f,0.92f,1.00f), 8.0f);
	}

	// Which station is actually on screen, and how strongly. Traffic belongs to the thing you are
	// looking at, so it hands over exactly as the stations do.
	int32 Best = -1;
	float BestFade = 0.f, BestScale = 1.f;
	for (int32 N = 0; N < StationCount; ++N)
	{
		// Use the value ApplyStations already computed for this station. This loop used to re-derive
		// it from the GLOBAL MinVis/MaxVis/fade widths, ignoring every per-station handover — so
		// once stations 0, 1 and 2 were given handovers it could find no station in band at all and
		// reported S-1 with fade 0.00 (visible on the readout at DEPTH 65%, standing inside MET169).
		// Anything keyed off "which station is dominant" was wrong from that point on.
		float F = StationFadeCache.IsValidIndex(N) ? StationFadeCache[N] : 0.f;
		const float S = StationScale(N);
		if (F > BestFade) { BestFade = F; Best = N; BestScale = S; }
	}
	DiagStation = Best;   // reused by the readout diagnostic
	DiagFade = BestFade;
	if (Best < 0 || !Comms.IsValidIndex(Best) || BestFade <= 0.002f)
	{
		if (CommsISM) CommsISM->SetVisibility(false);
		return;
	}
	const FQZComms& C = Comms[Best];
	const int32 Total = FMath::Max(C.Lanes, 0) * FMath::Max(C.PerLane, 0);
	if (Total <= 0)
	{
		if (CommsISM) CommsISM->SetVisibility(false);
		return;
	}

	if (!CommsISM)
	{
		CommsISM = NewObject<UInstancedStaticMeshComponent>(this, TEXT("CommsStreams"));
		CommsISM->SetupAttachment(RootComponent);
		CommsISM->RegisterComponent();
		CommsISM->SetStaticMesh(Cube);
		CommsISM->SetMobility(EComponentMobility::Movable);
		CommsISM->SetCastShadow(false);
		CommsISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (UMaterialInterface* Base = Cube->GetMaterial(0))
			CommsMID = CommsISM->CreateAndSetMaterialInstanceDynamicFromMaterial(0, Base);
	}
	CommsISM->SetVisibility(true);
	if (CommsMID)
	{
		const FLinearColor Lit = C.Color * C.Brightness * BestFade;
		CommsMID->SetVectorParameterValue(TEXT("Color"), Lit);
		CommsMID->SetVectorParameterValue(TEXT("BaseColor"), C.Color);
		CommsMID->SetScalarParameterValue(TEXT("Emissive"), C.Brightness * BestFade);
	}

	// ---- FORAGING TRAILS, not spokes --------------------------------------------------------
	// An Ameisenstrasse does not radiate, it SEARCHES: it casts about, corrects, commits, and
	// splits. So each trail is a random walk that is pulled outward while being allowed to drift
	// sideways, and a fraction of trails BRANCH off an existing one partway along instead of
	// starting at the centre. Straight lanes with a wobble read as engineered; this reads as
	// explored, which is the "not yet understood inner workings" the piece is after.
	//
	// Generated ONCE from a fixed seed. A path re-rolled per frame is not a path, it is noise —
	// and the whole idea depends on these routes persisting so they can be followed.
	const int32 MaxLanes = 64;
	if (CommsTrail.Num() != MaxLanes * CommsSteps || CommsTrailLanes != C.Lanes)
	{
		CommsTrailLanes = C.Lanes;
		CommsTrail.SetNum(MaxLanes * CommsSteps);
		CommsDirs.SetNum(MaxLanes);
		CommsOffsets.SetNum(MaxLanes);
		FRandomStream R(90210);

		for (int32 l = 0; l < MaxLanes; ++l)
		{
			CommsDirs[l] = R.GetUnitVector();
			CommsOffsets[l] = R.GetFraction();

			// branch: start partway along an EARLIER trail rather than at the centre
			const bool bBranch = (l > 2) && (R.GetFraction() < C.Branch);
			const int32 Parent = bBranch ? R.RandRange(0, l - 1) : -1;
			const int32 Split = bBranch ? R.RandRange(3, CommsSteps / 2) : 0;

			FVector Pos = FVector::ZeroVector;
			FVector Head = CommsDirs[l];
			if (Parent >= 0)
			{
				Pos = CommsTrail[Parent * CommsSteps + Split];
				// leave the parent at an angle, so the split is legible as a split
				Head = (Pos.GetSafeNormal() + R.GetUnitVector() * 0.9f).GetSafeNormal();
			}

			for (int32 s = 0; s < CommsSteps; ++s)
			{
				if (Parent >= 0 && s < Split)
				{
					// share the parent's path up to the split point
					CommsTrail[l * CommsSteps + s] = CommsTrail[Parent * CommsSteps + s];
					continue;
				}
				CommsTrail[l * CommsSteps + s] = Pos;

				// wander sideways, then COMMIT back toward straight-out. The tension between the
				// two is the searching read: pure wander drifts and never arrives, pure commit
				// marches in a line.
				const FVector Radial = Pos.IsNearlyZero() ? CommsDirs[l] : Pos.GetSafeNormal();
				const FVector Jitter = R.GetUnitVector() * C.Wander;
				Head = (Head + Jitter + Radial * C.Commit * 1.6f).GetSafeNormal();
				Pos += Head * (1.f / CommsSteps);
			}
		}
		// normalise every trail so its far end sits at unit radius, whatever route it took
		for (int32 l = 0; l < MaxLanes; ++l)
		{
			const float End = FMath::Max(CommsTrail[l * CommsSteps + CommsSteps - 1].Size(), KINDA_SMALL_NUMBER);
			for (int32 s = 0; s < CommsSteps; ++s) CommsTrail[l * CommsSteps + s] /= End;
		}
	}
	if (CommsISM->GetInstanceCount() != Total)
	{
		CommsISM->ClearInstances();
		for (int32 i = 0; i < Total; ++i) CommsISM->AddInstance(FTransform::Identity);
	}

	CommsClock += Dt;

	// Traffic is sized to the STATION, not to the world, so it stays on the object at every scale.
	const float Span = STATION_SPAN_UU * BestScale;
	const float Rin = Span * CommsInnerFrac;
	const float Rout = Span * CommsOuterFrac;
	const FRotator Orbit(OrbitPitch, OrbitYaw, 0.f);

	// sample a trail at t in 0..1, interpolating between its stored points
	auto TrailAt = [this](int32 lane, float t) -> FVector
	{
		const float u = FMath::Clamp(t, 0.f, 1.f) * (CommsSteps - 1);
		const int32 i = FMath::Clamp((int32)u, 0, CommsSteps - 2);
		return FMath::Lerp(CommsTrail[lane * CommsSteps + i],
		                   CommsTrail[lane * CommsSteps + i + 1], u - (float)i);
	};

	int32 idx = 0;
	for (int32 l = 0; l < C.Lanes && idx < Total; ++l)
	{
		const int32 Lane = l % MaxLanes;
		for (int32 p = 0; p < C.PerLane && idx < Total; ++p, ++idx)
		{
			float f = FMath::Frac(CommsOffsets[Lane]
			                      + (float)p / FMath::Max(C.PerLane, 1)
			                      + CommsClock * C.Speed);

			// BURST: squeeze a trail's traffic into part of the cycle, so packets leave in clumps
			// with silence between — a station that CALLS rather than streams.
			float Gate = 1.f;
			if (C.Burst > 0.f)
			{
				const float Win = 1.f - C.Burst;
				if (f > Win) { Gate = 0.f; }
				else         { f = f / FMath::Max(Win, 1e-3f); }
			}

			// follow the meandering route rather than a straight radius
			const FVector Unit = TrailAt(Lane, f);
			const FVector Local = Unit * FMath::Lerp(Rin, Rout, 1.f) * 1.f
			                    + Unit.GetSafeNormal() * 0.f;
			const FVector Pos = Unit * (Rin + (Rout - Rin) * Unit.Size());

			// BLINK — each packet winks on its own phase, so the trail reads as signalling and
			// not as a conveyor belt. Driven through SCALE because one shared MID cannot carry a
			// per-instance brightness without custom data, and a packet scaled to nothing is off.
			const float Ph = (CommsClock * C.BlinkHz + (float)p * 0.37f + (float)Lane * 0.11f);
			const float Blink = 1.f - C.BlinkDepth * (0.5f + 0.5f * FMath::Sin(Ph * 2.f * PI));

			// fade in on departure, out on arrival — nothing appears or vanishes mid-flight
			const float Edge = FMath::Min(FMath::Clamp(f / 0.14f, 0.f, 1.f),
			                              FMath::Clamp((1.f - f) / 0.22f, 0.f, 1.f));
			// packets grow as they travel out, so the trail has a direction at a glance
			const float Grow = 1.f + C.OutwardGain * f;
			const float A = Edge * Gate * Blink;

			const float Len = C.LengthUU * BestScale * Grow * FMath::Max(A, 0.f);
			const float Thk = Len * C.Thickness;

			// aligned to the LOCAL trail direction, so a dash follows the bend of its own path
			const FVector Ahead = TrailAt(Lane, FMath::Min(f + 0.03f, 1.f));
			FVector Fwd = (Ahead - Unit);
			if (Fwd.IsNearlyZero()) Fwd = Unit.GetSafeNormal();
			const FQuat Rot = FRotationMatrix::MakeFromX(Fwd.GetSafeNormal()).ToQuat();
			const FTransform T(Orbit.Quaternion() * Rot,
			                   Anchor + Orbit.RotateVector(Pos),
			                   FVector(FMath::Max(Len, 0.01f), FMath::Max(Thk, 0.01f),
			                           FMath::Max(Thk, 0.01f)) / 100.f);  // BasicShapes/Cube is 100uu
			CommsISM->UpdateInstanceTransform(idx, T, true, (idx == Total - 1), false);
		}
	}
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
		// NOT Sphere->GetMaterial(0). That is the ENGINE DEFAULT on /Engine/BasicShapes, and it
		// has no Color, no BaseColor and no Emissive — so every parameter written just below
		// went nowhere and the field rendered as plain grey spheres. Those are the "random
		// untextured spheres": nothing in any level, created here at runtime.
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_FillerGlow.M_FillerGlow"));
		if (!Base) Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_StationMaster.M_StationMaster"));
		if (!Base) Base = Sphere->GetMaterial(0);   // last resort: grey, but at least drawn
		if (Base)
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


void AQZoomStagePawn::UpdateCH4Energy(float Dt)
{
	if (!CH4EnergyISM) return;
	const float Fade = StationFadeCache.IsValidIndex(CH4Station) ? StationFadeCache[CH4Station] : 0.f;
	if (!bCH4Energy || CH4EnergyCount <= 0 || Fade <= 0.002f)
	{
		CH4EnergyISM->SetVisibility(false);
		if (CH4EnergyLight) CH4EnergyLight->SetVisibility(false);
		return;
	}
	CH4EnergyClock += Dt;

	// ── the state, read straight off the two beats ───────────────────────────────────────
	// Active means the oxygen is NOT on the residue: before it docks, and after it is collected.
	// The transitions are what the whole layer is built around, so they are found first and
	// everything else is expressed against them.
	auto Wrap = [](float A) { A = FMath::Fmod(A + 1.5f, 1.f) - 0.5f; return A; };   // -0.5..0.5
	const float dDock = Wrap(CH4Phase - CH4DockPhase);
	const float dRel  = Wrap(CH4Phase - CH4ReleasePhase);
	const float W     = FMath::Max(CH4EnergyBurstWidth, 0.005f);

	// Deactivated between dock and release. Smoothstepped over the burst width so the shell
	// changes state across the shock rather than on the frame the beat passes.
	auto Step = [](float x) { const float t = FMath::Clamp(x, 0.f, 1.f); return t * t * (3.f - 2.f * t); };
	const float AfterDock = Step((dDock + W) / (2.f * W));
	const float AfterRel  = Step((dRel  + W) / (2.f * W));
	const float Deact     = FMath::Clamp(AfterDock - AfterRel, 0.f, 1.f);
	const float Act       = 1.f - Deact;

	// Two shocks, opposite in sign: the dock IMPLODES, the release EXPLODES and overshoots.
	// Gaussian rather than a triangle so the crack has a soft shoulder and no corner.
	const float ShockIn  = FMath::Exp(-(dDock * dDock) / (W * W));
	const float ShockOut = FMath::Exp(-(dRel  * dRel)  / (W * W));
	const float Shock    = (ShockOut * 1.0f) - (ShockIn * 0.75f);   // release is the louder event

	// ── where the shell sits in the world ────────────────────────────────────────────────
	// It belongs to the residue, so it rides the station's own scale: the shell is a fraction
	// of the station span and grows with it, exactly as the geometry does.
	const float S    = StationRenderScale(CH4Station);
	const float Span = 3464.f * S;
	const float Rest = Span * FMath::Max(CH4EnergyRadius, 0.02f);
	const float R    = Rest * (0.55f + 0.45f * Act + CH4EnergyBurst * Shock);
	const FRotator Orbit(OrbitPitch, OrbitYaw, 0.f);
	const float Swirl = CH4EnergyClock * CH4EnergySwirl * 60.f;   // deg

	const int32 N = FMath::Min(CH4EnergyCount, CH4EnergyDir.Num());
	if (CH4EnergyISM->GetInstanceCount() != N) return;   // InitCH4Energy has not caught up yet
	CH4EnergyISM->SetVisibility(true);

	TArray<FTransform> Xf;
	Xf.SetNum(N);
	// Shard length follows the RADIAL SPEED, so the shell streaks outward on the burst and sits
	// as short dashes at rest. That is the difference between "energy moving" and "dots".
	const float Len = Span * CH4EnergyThickness * (1.f + 9.f * FMath::Abs(Shock));
	const float Thk = Span * CH4EnergyThickness * 0.35f;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector D0 = CH4EnergyDir[i];
		// per-shard swirl about the world Z of the station, plus a shimmer in radius
		const FVector D = FRotator(0.f, Swirl * (0.6f + 0.8f * CH4EnergyJitter[i]), 0.f).RotateVector(D0);
		const float Shim = 1.f + 0.10f * FMath::Sin(CH4EnergyClock * 2.3f + CH4EnergyPhase[i]);
		const float Ri = R * (0.82f + 0.36f * CH4EnergyJitter[i]) * Shim;
		const FVector P = Anchor + Orbit.RotateVector(D * Ri);
		// point the shard along its own radius
		Xf[i] = FTransform(FRotationMatrix::MakeFromX(Orbit.RotateVector(D)).Rotator(), P,
		                   FVector(Len / 100.f, Thk / 100.f, Thk / 100.f));
	}
	CH4EnergyISM->BatchUpdateInstancesTransforms(0, Xf, true, true, false);

	if (CH4EnergyMID)
	{
		const FLinearColor C = FMath::Lerp(CH4EnergyCold, CH4EnergyHot, Act);
		const float B = CH4EnergyBrightness * Fade * (0.22f + 0.78f * Act + 2.2f * FMath::Abs(Shock));
		CH4EnergyMID->SetVectorParameterValue(TEXT("BaseColor"), C * B);
		CH4EnergyMID->SetVectorParameterValue(TEXT("Color"),     C * B);
		CH4EnergyMID->SetVectorParameterValue(TEXT("Emissive"),  C * B);
		CH4EnergyMID->SetScalarParameterValue(TEXT("StationFade"), 1.f);
	}

	// The same envelope on a real light, so the turnover lands on the surrounding atoms rather
	// than floating in front of them as an overlay.
	if (CH4EnergyLight)
	{
		const bool bOn = CH4EnergyLightIntensity > 1.f;
		CH4EnergyLight->SetVisibility(bOn);
		if (bOn)
		{
			CH4EnergyLight->SetWorldLocation(Anchor);
			CH4EnergyLight->SetAttenuationRadius(FMath::Max(R * 3.5f, 50.f));
			CH4EnergyLight->SetLightColor(FMath::Lerp(CH4EnergyCold, CH4EnergyHot, Act));
			CH4EnergyLight->SetIntensity(CH4EnergyLightIntensity * Fade
				* (0.15f + 0.55f * Act + 2.6f * FMath::Abs(Shock)));
		}
	}
}

void AQZoomStagePawn::ApplyShaderLevel(int32 Level)
{
	UWorld* W = GetWorld();
	if (!W) return;

	if (Level <= 0)
	{
		// restore, in the exact order things were saved
		for (int32 c = 0; c < SimpleSavedComps.Num(); ++c)
		{
			UPrimitiveComponent* PC = SimpleSavedComps[c].Get();
			if (!PC) continue;   // its level streamed out — nothing to restore, nothing leaked
			const int32 Start = SimpleSavedStart[c];
			const int32 End = (c + 1 < SimpleSavedStart.Num()) ? SimpleSavedStart[c + 1] : SimpleSavedMats.Num();
			for (int32 i = Start; i < End; ++i)
				PC->SetMaterial(i - Start, SimpleSavedMats[i]);
		}
		for (TWeakObjectPtr<UPrimitiveComponent>& V : SimpleHiddenVols)
			if (UPrimitiveComponent* PC = V.Get()) PC->SetVisibility(true);
		SimpleSavedComps.Reset(); SimpleSavedStart.Reset(); SimpleSavedMats.Reset();
		SimpleSavedIndex.Reset(); SimpleHiddenVols.Reset();
		UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] shaders restored (HIGH)"));
		return;
	}

	if (Level >= 2 && !SimpleShaderMaterial)
		SimpleShaderMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_QZ_SimpleShader.M_QZ_SimpleShader"));
	if (Level >= 2 && !SimpleShaderMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] M_QZ_SimpleShader missing — run dev/qz_simple_shader_material.py"));
		ShaderLevel = 0;
		return;
	}

	// The MID lookup, built once per call. Keyed on the ORIGINAL material, so it survives the
	// component already wearing something else — the originals are what we record and restore.
	TMap<UMaterialInterface*, UMaterialInterface*> Low;
	if (Level == 1)
		for (const FQZLowShader& S : LowShaders)
			if (S.From && S.To) Low.Add(S.From, S.To);

	// NO reset here: the restore path clears the arrays, so they are empty on a fresh engage —
	// and a RE-ENTRANT call (a station streamed in, or a tier change) must keep the originals it
	// still owes the components it already recorded.
	int32 NSwap = 0, NVol = 0, NKept = 0;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		// station heroes + everything attached to them — the same population ApplyStations drives
		bool bStation = It->Tags.Contains(TAG_STATION);
		if (!bStation)
		{
			AActor* P = It->GetAttachParentActor();
			while (P && !bStation) { bStation = P->Tags.Contains(TAG_STATION); P = P->GetAttachParentActor(); }
		}
		if (!bStation) continue;
		TArray<UPrimitiveComponent*> Prims;
		It->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* PC : Prims)
		{
			if (PC->IsA<UHeterogeneousVolumeComponent>())
			{
				// no simple version of a ray-marcher exists — hiding it IS its simple version.
				// LOW only: at MID the scene must still contain what it contains, or the tier
				// compares two different pictures instead of two shader costs.
				if (Level >= 2 && PC->IsVisible())
				{ PC->SetVisibility(false); SimpleHiddenVols.Add(PC); ++NVol; }
				continue;
			}
			if (!PC->IsA<UMeshComponent>()) continue;
			const int32 N = PC->GetNumMaterials();
			if (N == 0) continue;

			// RECORD ONCE. The originals belong to the first time this component is seen; every
			// later pass reads them back out. Recording what a component currently wears would
			// save a stand-in as the "original" and turn restore into a no-op.
			int32* Found = SimpleSavedIndex.Find(PC);
			int32 Start;
			if (Found)
			{
				Start = SimpleSavedStart[*Found];
			}
			else
			{
				SimpleSavedIndex.Add(PC, SimpleSavedComps.Num());
				SimpleSavedComps.Add(PC);
				Start = SimpleSavedMats.Num();
				SimpleSavedStart.Add(Start);
				for (int32 i = 0; i < N; ++i) SimpleSavedMats.Add(PC->GetMaterial(i));
			}

			for (int32 i = 0; i < N && (Start + i) < SimpleSavedMats.Num(); ++i)
			{
				UMaterialInterface* Orig = SimpleSavedMats[Start + i];
				UMaterialInterface* Want = SimpleShaderMaterial;
				if (Level == 1)
				{
					// WALK THE PARENT CHAIN. What sits in a slot at runtime is almost never the
					// asset the table was built from: BuildMaterialCache replaces every slot with
					// a DYNAMIC instance so the station fade has something to write to. A direct
					// lookup therefore missed every single entry — the table was correct and the
					// tier still did nothing. The authored material is that DMI's parent (or its
					// parent's parent), so climb until an entry matches.
					UMaterialInterface** Sub = nullptr;
					for (UMaterialInterface* Key = Orig; Key && !Sub; )
					{
						Sub = Low.Find(Key);
						if (Sub) break;
						UMaterialInstance* MI = Cast<UMaterialInstance>(Key);
						Key = MI ? MI->Parent : nullptr;
					}
					// A material with no light version stays as it is. Substituting the flat
					// stand-in instead would quietly turn MID into LOW for that slot and the
					// measurement would mean nothing.
					Want = Sub ? *Sub : Orig;
					if (!Sub) ++NKept;
				}
				if (PC->GetMaterial(i) != Want) { PC->SetMaterial(i, Want); ++NSwap; }
			}
		}
	}
	static const TCHAR* Names[3] = { TEXT("HIGH"), TEXT("MID"), TEXT("LOW") };
	UE_LOG(LogTemp, Warning,
		TEXT("[QZoomPerf] SHADERS %s: %d slot(s) swapped, %d kept (no light version), %d volume(s) hidden"),
		Names[FMath::Clamp(Level, 0, 2)], NSwap, NKept, NVol);
}

void AQZoomStagePawn::UpdateQuarkTriad(float Dt)
{
	if (!bQuarkMotion) return;
	UWorld* W = GetWorld();
	if (!W) return;

	// ZOOM-DRIVEN, NOT CLOCK-DRIVEN — the same rule the rest of this file follows. The clock only
	// advances while the triad's own station is on screen, so the motion is not already halfway
	// through some cycle by the time anyone arrives, and it holds where it was if you back out.
	// StationFadeCache is the value ApplyStations actually used this frame; re-deriving visibility
	// from the global MinVis/MaxVis is the mistake already fixed twice in this file.
	const float Fade = StationFadeCache.IsValidIndex(QuarkStation)
	                 ? StationFadeCache[QuarkStation] : 0.f;
	if (Fade <= 0.002f) return;
	QuarkClock += Dt * QuarkSpeed;

	// ── collect by tag ───────────────────────────────────────────────────────────────────
	// By TAG rather than by name, so the triad can be re-authored, duplicated or renamed in the
	// level without touching this file — the same contract the stations and the CH4 pair use.
	AActor* Q[3] = { nullptr, nullptr, nullptr };
	AActor* G[3] = { nullptr, nullptr, nullptr };     // 0: 0-1, 1: 1-2, 2: 2-0
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		for (const FName& T : A->Tags)
		{
			const FString Ts = T.ToString();
			if (Ts.StartsWith(TEXT("QZQuark")))
			{
				const int32 i = FCString::Atoi(*Ts.Mid(7));
				if (i >= 0 && i < 3) Q[i] = A;
			}
			else if (Ts.StartsWith(TEXT("QZGluon")))
			{
				const int32 i = FCString::Atoi(*Ts.Mid(7));
				if (i >= 0 && i < 3) G[i] = A;
			}
		}
	}
	if (!Q[0] || !Q[1] || !Q[2]) return;

	// ── rest positions, captured once ────────────────────────────────────────────────────
	// Captured BEFORE anything is written, and only once — read them back later and they would
	// be the wandering positions, so the wander would compound on itself and walk away.
	if (QuarkHome.Num() != 3)
	{
		QuarkHome.SetNum(3);
		for (int32 i = 0; i < 3; ++i) QuarkHome[i] = Q[i]->GetRootComponent()->GetRelativeLocation();
	}

	// ── each quark on its own three frequencies ──────────────────────────────────────────
	// The frequencies are deliberately incommensurable — no two are a simple ratio — so the three
	// paths never come back into phase and the triad never repeats. Equal or harmonic frequencies
	// would give a shape that visibly loops, and a looping quark is a machine, not a particle.
	static const float FX[3] = { 0.73f, 1.11f, 0.47f };
	static const float FY[3] = { 1.31f, 0.61f, 0.97f };
	static const float FZ[3] = { 0.53f, 0.89f, 1.19f };
	FVector Pos[3];
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector Home = QuarkHome[i];
		const float R = FMath::Max(Home.Size(), 1.f) * QuarkWander;
		const float P = (float)i * 2.09439510239f;      // 120 degrees apart, so they start spread
		const FVector Off(
			R * FMath::Sin(QuarkClock * FX[i] + P),
			R * FMath::Sin(QuarkClock * FY[i] + P * 1.7f),
			R * FMath::Sin(QuarkClock * FZ[i] + P * 2.3f));
		Pos[i] = Home + Off;
		Q[i]->GetRootComponent()->SetRelativeLocation(Pos[i]);
	}

	// ── the Y-junction: three strings meet at a shared centre, not at each other ─────────
	// This is the physics, not a styling choice: in a baryon the three flux tubes join at a
	// central STRING JUNCTION — they do not run quark to quark. The junction is pure
	// computation, nothing renders it: the centroid of the live positions plus its own small
	// wander on incommensurable frequencies, so the meeting point breathes instead of sitting
	// nailed to the mathematical average. Three cords aiming at one moving point ARE the node.
	const FVector HomeC = (QuarkHome[0] + QuarkHome[1] + QuarkHome[2]) / 3.f;
	FVector J = (Pos[0] + Pos[1] + Pos[2]) / 3.f;
	{
		const float RJ = FMath::Max((QuarkHome[0] - HomeC).Size(), 1.f) * GluonJunctionWander;
		J += FVector(RJ * FMath::Sin(QuarkClock * 1.41f),
		             RJ * FMath::Sin(QuarkClock * 0.83f + 2.1f),
		             RJ * FMath::Sin(QuarkClock * 1.07f + 4.2f));
	}

	// String e runs quark e -> junction. Orientation matters to the material: +Z (t=1) is the
	// junction end, where M_QZ_QuarkFlux banks its glow, and t=0 tucks into the quark sphere.
	for (int32 e = 0; e < 3; ++e)
	{
		AActor* Beam = G[e];
		if (!Beam) continue;
		const FVector A0 = Pos[e];
		const FVector B0 = J;
		const FVector D = B0 - A0;
		const float Len = D.Size();
		if (Len < 1.f) continue;

		// The beam mesh is the engine cylinder: 100 uu long, centred, running down its local Z.
		// So aim Z along the line and scale Z by length/100 — the mesh itself never changes.
		USceneComponent* RC = Beam->GetRootComponent();
		RC->SetRelativeLocation((A0 + B0) * 0.5f);
		RC->SetRelativeRotation(FRotationMatrix::MakeFromZ(D).Rotator());
		RC->SetRelativeScale3D(FVector(GluonThickness, GluonThickness, Len / 100.f));

		// TENSION: how far this string is stretched beyond its rest length. Fed to the material,
		// where it drives brightness. A stretched gluon string does not thin out and fade the way
		// a stretched spring or an electric field would — it stores more energy the longer it
		// gets. Showing that as "brighter and angrier when pulled" is the one visual claim here
		// that is actually the physics rather than a flourish.
		// Rest length is quark-home to junction-home — a third shorter than the old edge rest,
		// so tension reads correctly against the Y geometry, not the retired triangle's.
		const float Rest = FMath::Max((QuarkHome[e] - HomeC).Size(), 1.f);
		const float Stretch = FMath::Clamp(Len / Rest, 0.25f, 3.f);
		TArray<UPrimitiveComponent*> Prims;
		Beam->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* PC : Prims)
		{
			if (!PC) continue;
			for (int32 m = 0; m < PC->GetNumMaterials(); ++m)
			{
				UMaterialInstanceDynamic* DMI = Cast<UMaterialInstanceDynamic>(PC->GetMaterial(m));
				// BuildMaterialCache already made the DMIs at BeginPlay; creating one here would
				// mean a PSO compile mid-show. If it is not a DMI yet, skip the frame rather than
				// stall — the next frame will have it.
				if (!DMI) continue;
				DMI->SetScalarParameterValue(TEXT("Tension"),
					1.f + (Stretch - 1.f) * GluonTension);
			}
		}
	}
}

void AQZoomStagePawn::UpdateCH4Cycle(float Dt)
{
	if (!bCH4Cycle) return;
	UWorld* W = GetWorld();
	if (!W) return;

	// Advance ONLY while the reaction's station is actually on screen. That is what makes it
	// zoom-driven rather than clock-driven: walk away and the reaction holds where it was.
	// THE GATE MUST BE THE STATION'S ACTUAL VISIBILITY, NOT A RE-DERIVATION.
	// This tested StationScale against the GLOBAL MinVisScale/MaxVisScale (0.2 .. 35), which is
	// exactly the mistake already fixed for the lights: it ignores every per-stage handover value
	// the ladder now carries. With MET169 authored at Timing 42.9 those globals put the reaction's
	// window at 56%-68% of the zoom bar, while the stage itself is visible from 51% — so standing
	// at 53%, watching MET169 fade in, the clock was still frozen and nothing moved no matter how
	// long you waited. StationFadeCache is the value ApplyStations actually used this frame.
	const float CH4Fade = StationFadeCache.IsValidIndex(CH4Station)
	                    ? StationFadeCache[CH4Station] : 0.f;
	// THE OPERATOR DRIVES IT NOW, NOT THE DWELL. Advancing while the station happened to be on
	// screen meant the reaction was always mid-cycle by the time anyone looked at it, and it
	// could not be replayed on cue. RB starts and stops it; holding RB ramps the speed. The
	// station fade still governs VISIBILITY — the molecules belong to that stage — but no longer
	// governs the clock.
	// AUTO-START: the reaction fires itself the first time the dive crosses CH4AutoStartAt.
	// Latched with 2% hysteresis so retreating and diving again replays it; RB stays the
	// manual override. Primary only — bCH4Running rides the cluster event to the others.
	if (CH4AutoStartAt > 0.01f && (bIsPrimary || !bInCluster))
	{
		if (ZoomProgress >= CH4AutoStartAt && !bCH4AutoLatch)
		{
			bCH4AutoLatch = true;
			if (!bCH4Running)
			{
				bCH4Running = true;
				CH4Phase = 0.f;   // clean intro every arrival
				UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] reaction AUTO-START at %.0f%%"),
					ZoomProgress * 100.f);
			}
		}
		else if (ZoomProgress < CH4AutoStartAt - 0.02f)
		{
			bCH4AutoLatch = false;
		}
	}

	const bool bStationUp = bCH4Running;
	// THE CLOCK ADVANCES ON THE PRIMARY ONLY. CH4Phase rides the cluster event (c4p) to every
	// other node — the floor is a separate node where RB is never pressed, so advancing locally
	// left its phase at 0 forever: sequence pinned to frame 0, visitors eroded away by the intro
	// fade — "the animation is not visible on the floor". Secondaries take the synced value.
	if (bCH4Running && (bIsPrimary || !bInCluster))
	{
		const float Step = Dt / FMath::Max(CH4CycleSeconds, 0.01f) * CH4SpeedNow;
		CH4Phase = FMath::Fmod(CH4Phase + Step, 1.f);   // loops for as long as it is left running
		CH4Cycles += Step;
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
	const float Ramp = FMath::Max(CH4PresenceRamp, 0.01f);   // fraction of the cycle
	auto Presence = [Ramp](float P, float In, float Out)
	{
		if (P < In - Ramp || P > Out + Ramp) return 0.f;
		const float A = FMath::Clamp((P - (In - Ramp)) / Ramp, 0.f, 1.f);
		const float B = FMath::Clamp(((Out + Ramp) - P) / Ramp, 0.f, 1.f);
		float F = FMath::Min(A, B);
		return F * F * (3.f - 2.f * F);
	};

	// SMOOTH, not binary. This used to be (bStationUp ? 1 : 0), so the instant the station left
	// its band the partner's visibility snapped 1 -> 0 and the molecule vanished in one frame.
	// Presence already ramps within the cycle; what was missing was the station's own ramp.
	// StationFadeCache is the smoothstepped value ApplyStations computed this frame.
	const float StationFadeNow = StationFadeCache.IsValidIndex(CH4Station)
		? StationFadeCache[CH4Station]
		: (bStationUp ? 1.f : 0.f);

	// SEQUENCER. Scrubbed, never played: the sequence has no clock of its own, it is a curve
	// indexed by CH4Phase. So it inherits everything the code path had — frozen when the station
	// is off screen, accelerating with dwell — while the shape of the motion becomes editable on
	// a timeline. Tagged rather than referenced so the level can be re-authored without a rebuild.
	if (bCH4UseSequencer)
	{
		for (TActorIterator<ALevelSequenceActor> It2(W); It2; ++It2)
		{
			ALevelSequenceActor* SA = *It2;
			if (!SA || !SA->Tags.Contains(FName(TEXT("QZCH4Sequence")))) continue;
			if (ULevelSequencePlayer* Pl = SA->GetSequencePlayer())
			{
				// A player that has never been started does not evaluate a Scrub — its bindings
				// are only resolved on the first Play. One Play/Pause pair on first contact
				// initialises them (auto-play stays off; the scrub owns the clock afterwards).
				// Without this the RB toggle advanced CH4Phase and nothing on screen moved.
				if (!bCH4SeqPrimed)
				{
					Pl->Play();
					Pl->Pause();
					bCH4SeqPrimed = true;
					UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] CH4 sequence primed for scrubbing"));
				}
				const float T = CH4Phase * FMath::Max(CH4SequenceSeconds, 0.01f);
				Pl->SetPlaybackPosition(
					FMovieSceneSequencePlaybackParams(T, EUpdatePositionMethod::Scrub));
			}
		}

		// INTRO FADE for the visiting enzymes: SeqFade rides the first CH4IntroFadeSeconds of
		// sequence time, written to the SAME DMIs the material cache owns (per component, so the
		// shared atom masters fade only on the tagged actors). At phase 0 the visitors are fully
		// eroded away — the animation begins with the methionine alone.
		const float SeqT = CH4Phase * FMath::Max(CH4SequenceSeconds, 0.01f);
		const float IntroFade = FMath::Clamp(SeqT / FMath::Max(CH4IntroFadeSeconds, 0.1f), 0.f, 1.f);
		for (auto& Pair : ActorMats)
		{
			AActor* A = Pair.Key.Get();
			if (!A || !A->Tags.Contains(FName(TEXT("QZCH4FadeIn")))) continue;
			for (int32 Idx : Pair.Value)
				if (MatCache.IsValidIndex(Idx))
					if (UMaterialInstanceDynamic* DMI = MatCache[Idx].DMI.Get())
						DMI->SetScalarParameterValue(TEXT("SeqFade"), IntroFade);

			// The fade must reach the whole SUBTREE: the oxygen carries a particle system as an
			// attached child. Niagara gets the value as a user float (bind it in the emitter for
			// a real fade) — and a hard activation gate besides, so "invisible at the start" holds
			// even for a system that ignores the parameter. Runs after the station loop's own
			// activate/deactivate in the tick, so this gate wins while the intro is at zero.
			TArray<AActor*> Subtree;
			A->GetAttachedActors(Subtree, true, /*recursive=*/true);
			Subtree.Add(A);
			for (AActor* S : Subtree)
			{
				TArray<UNiagaraComponent*> NCs;
				S->GetComponents<UNiagaraComponent>(NCs);
				for (UNiagaraComponent* NC : NCs)
				{
					NC->SetVariableFloat(FName(TEXT("SeqFade")), IntroFade);
					// gate composes with the STATION's own visibility — never activate something
					// the station loop just hid because the viewer is elsewhere on the bar
					const bool bOn = IntroFade > 0.002f && CH4Fade > 0.002f && !bParticlesMuted;
					if (!bOn && NC->IsActive())      NC->DeactivateImmediate();
					else if (bOn && !NC->IsActive()) NC->Activate(true);
				}
			}
		}
	}

	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		const bool bFmob = A->Tags.Contains(FName(TEXT("QZCH4Fmob")));
		const bool bMsra = A->Tags.Contains(FName(TEXT("QZCH4Msra")));
		if (!bFmob && !bMsra) continue;

		// A STOPPED CYCLE MUST NOT WRITE. ApplyStations has already given these actors the
		// ladder's StationFade this frame; the line below would overwrite it with
		// StationFade * Presence(phase), and Presence at phase 0 is exactly 0. So a stopped
		// cycle force-hid both enzymes every frame, whatever the ladder and the materials said.
		// Two writers of one parameter, and the second one always won.
		if (bCH4HoldWhenStopped && !bCH4Running) continue;

		const FVector Local = bFmob ? CH4EvalPath(CH4Phase, FmobKeys)
		                            : CH4EvalPath(CH4Phase, MsraKeys);
		const float Vis = StationFadeNow * (bFmob ? Presence(CH4Phase, CH4FmobIn, CH4FmobOut)
		                                          : Presence(CH4Phase, CH4MsraIn, CH4MsraOut));

		// Relative to the station pivot, so the pawn's exp() scale carries them automatically.
		// When Sequencer owns the motion the pawn must not also write it, or the two fight and
		// the last writer of the frame wins — which reads as jitter, not as a conflict.
		if (!bCH4UseSequencer) A->SetActorRelativeLocation(Local);
		// HIDE THE CHILDREN TOO. SetActorHiddenInGame does not propagate to attached actors, and
		// the partner stopped being a single mesh the moment it became a molecule: the tagged
		// actor is now a bare pivot with eight orbital meshes under it. Hiding the pivot alone
		// left all eight on screen for the rest of the descent — a molecule floating through the
		// nucleus. Same reason the fade has to reach them: their materials are StationFade-masked.
		const bool bHide = (Vis <= 0.002f);
		A->SetActorHiddenInGame(bHide);
		TArray<AActor*> Kids;
		A->GetAttachedActors(Kids, true, /*recursive=*/true);
		for (AActor* Ch : Kids) Ch->SetActorHiddenInGame(bHide);
		if (!bHide)
		{
			SetStationFade(A, Vis);
			for (AActor* Ch : Kids) SetStationFade(Ch, Vis);
		}
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

		// QZRetired WAS ONLY EVER A CONVENTION — nothing in this file implemented it. This loop keys
		// on QZStation, so an actor carrying only QZRetired was never scaled AND never hidden: it sat
		// at its authored transform, full size, ignoring the zoom entirely. That is what "structures
		// visible at the beginning that do not react to the zoom" is. Retiring four stations turned
		// four more of them into that, which is how it finally became visible.
		if (A->Tags.Contains(TAG_RETIRED))
		{
			if (!A->IsHidden())
			{
				A->SetActorHiddenInGame(true);
				// SetActorHiddenInGame does NOT propagate to attached actors, so the children of a
				// retired root would otherwise stay on screen without their parent.
				TArray<AActor*> Kids;
				A->GetAttachedActors(Kids, true, /*recursive=*/true);
				for (AActor* Ch : Kids) Ch->SetActorHiddenInGame(true);
			}
			continue;
		}
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
		// A retired stage keeps its slot in the ladder — and therefore keeps every actor tag below it
		// pointing at the right row — but never renders. This is how a stage is dropped from the show
		// without deleting content or renumbering anything.
		// Perf-bisect mute (keys 1-9) rides the exact same rail as an authored retirement, so a muted
		// station's cost leaves the frame the same way a retired one's does — nothing half-hidden.
		if ((N < 32 && (StationMuteMask & (1 << N))) ||
		    (Handover.IsValidIndex(N) && !Handover[N].bActive))
		{
			StationFadeCache.SetNumZeroed(FMath::Max(StationCount, N + 1));
			StationFadeCache[N] = 0.f;
			A->SetActorHiddenInGame(true);
			TArray<AActor*> Retired;
			A->GetAttachedActors(Retired, true, /*recursive=*/true);
			for (AActor* Ch : Retired) Ch->SetActorHiddenInGame(true);
			continue;
		}

		float StMinVis   = MinVisScale;
		float StFadeW    = StationFadeWidth;
		float StFadeOutW = (StationFadeOutWidth > 0.f) ? StationFadeOutWidth : StationFadeWidth;
		if (Handover.IsValidIndex(N) && Handover[N].bEnabled)
		{
			StMinVis = Handover[N].InitialSize;
			StFadeW  = Handover[N].FadeIn;
			StMaxVis = Handover[N].Dissolve;   // the struct's Dissolve beats the QZMaxVis tag
			// FadeOut 0 means "same as FadeIn", so stations tuned before the split keep their behaviour.
			StFadeOutW = (Handover[N].FadeOut > 0.f) ? Handover[N].FadeOut : StFadeW;
		}
		const float StLnMin = FMath::Loge(FMath::Max(StMinVis, 1e-30f));
		const float LnMaxS  = FMath::Loge(FMath::Max(StMaxVis, StMinVis * 2.f));

		const float S    = StationScale(N);
		const float LogS = FMath::Loge(FMath::Max(S, 1e-30f));
		// soft cross-fade: 1 inside the band, ramping to 0 over the fade width at each edge (prev/next dissolve).
		const float Up = FMath::Clamp((LogS - StLnMin) / FMath::Max(StFadeW,    1e-3f), 0.f, 1.f);
		const float Dn = FMath::Clamp((LnMaxS - LogS) / FMath::Max(StFadeOutW, 1e-3f), 0.f, 1.f);
		float Fade = Up * Dn;
		Fade = Fade * Fade * (3.f - 2.f * Fade);   // smoothstep
		bool bVis = (Fade > 0.002f);
		if (Ver >= 0 && Ver != NiraVersion) bVis = false;   // inactive version -> hidden

		// Publish it. UpdateLights used to re-derive a station's visibility with its own binary test
		// (Sc > MinVisScale && Sc < MaxVisScale), which ignored the handover overrides entirely — so a
		// tagged light popped on at a different depth than the geometry it was lighting. Same number now.
		StationFadeCache.SetNumZeroed(FMath::Max(StationCount, N + 1));
		StationFadeCache[N] = bVis ? Fade : 0.f;

		if (ULevel* Lvl = A->GetLevel())
		{
			float& lf = LevelFade.FindOrAdd(Lvl); lf = FMath::Max(lf, bVis ? Fade : 0.f);
			LevelStationIdx.FindOrAdd(Lvl) = N;   // so a light in this sublevel can use station N's SCALE
			TrackedLevels.Add(Lvl);   // remember every station sublevel we've ever seen, so UpdateLights can
			                          // fade its lights OUT (to 0) on frames where the station isn't visible.
		}

		// RECURSIVE. GetAttachedActors defaults to ONE LEVEL DEEP, and that default was quietly
		// deciding which materials can dissolve. A station used to be a mesh with a few children;
		// the molecules are three deep — QZStation_S3_Met169 -> QZ_OxyNirA -> QZ_OxyNirA_O_pos —
		// so every orbital lobe sat one level below the last level anyone looked at. It was never
		// hidden, never handed a StationFade, and never had a DMI created for it in
		// BuildMaterialCache. No amount of work on the orbital MATERIAL could have made it
		// dissolve: the number never arrived. Same default, same bug, in all five walks.
		TArray<AActor*> Attached;
		A->GetAttachedActors(Attached, true, /*bRecursivelyIncludeAttachedActors=*/true);

		// SHADER WARM-UP: while WarmupLeft>0, render every NOT-yet-visible station once — dissolved to nothing
		// at a subpixel scale — so its material shaders/PSOs compile now instead of flashing default grey the
		// first time it enters the band. Visible stations render normally.
		const bool bPrime = (!bVis && WarmupLeft > 0);

		A->SetActorHiddenInGame(!(bVis || bPrime));
		for (AActor* Ch : Attached)
		{
			// AUTHORING GUIDES NEVER RENDER IN THE SHOW. A guide has to be attached to the
			// station to sit on the zoom centre and travel with it — but attachment is exactly
			// what puts it in this loop, so being hidden in the level was undone every frame the
			// station was visible. The tag is checked here rather than relying on a saved flag,
			// because the saved flag is the thing this loop overwrites.
			if (Ch->Tags.Contains(TAG_GUIDE)) { Ch->SetActorHiddenInGame(true); continue; }
			Ch->SetActorHiddenInGame(!(bVis || bPrime));
		}

		if (bVis)
		{
			// RENDER scale, not the ladder scale. S decides when this station is on screen; RS
			// decides how big it looks while it is. They are the same number until a row asks for
			// a size, and separating them is what lets the back half of the dive be halved without
			// any of its handovers moving.
			const float RS = S * StationSizeMul(N);
			// ENTRY SQUASH. The one place a station's scale can stop being isotropic — a round
			// volume arriving inside a flat cap must otherwise poke through it. Inert unless a row
			// authors a window, and GateScale below deliberately keeps the UNIFORM number: the
			// camera-dissolve bubble measures distance, which has no axes.
			FVector ScaleVec(RS);
			FVector Place = Anchor;
			if (Handover.IsValidIndex(N) && Handover[N].EntryScaleTo > Handover[N].EntryScaleFrom)
			{
				const FQZHandover& H = Handover[N];
				float t = FMath::Clamp((ZoomProgress - H.EntryScaleFrom)
				                       / FMath::Max(H.EntryScaleTo - H.EntryScaleFrom, 1e-4f), 0.f, 1.f);
				t = t * t * (3.f - 2.f * t);
				ScaleVec *= FMath::Lerp(H.EntryScale, FVector::OneVector, t);
				// The nudge rides the SAME curve and dies with it, so the station still ends up
				// exactly on the anchor — the displacement belongs to the arrival, not to the
				// station's place in the world.
				Place += Orbit.RotateVector(H.EntryOffset * (1.f - t));
			}
			A->SetActorTransform(FTransform(Orbit, Place, ScaleVec));   // children follow via attachment
			// The camera-dissolve bubble has to grow with the world. PixelDepth is in world units
			// and the station's world size is 3464*S, so a FIXED 120 uu bubble is 3.5% of the
			// object at S=1 and 0.3% at S=11 — which is why the mushroom never opened: standing
			// inside a cap scaled 11x, every surface around you is thousands of units away, well
			// outside a fixed bubble, so nothing is "near" and nothing dissolves. Scaling it by S
			// keeps "near" meaning the same fraction of whatever you are inside.
			// The bubble measures WORLD units against geometry, so it has to follow the drawn
			// size. Left on S it would be twice too wide on a station rendered at half scale.
			GateScale = RS;
			// FREE LOOK AND ORBIT. Taken from the pawn's own Camera COMPONENT, not from
			// GetPlayerViewPoint. The player controller hands back the CONTROL rotation, and the
			// right stick moves that even though the camera itself never turns — free look orbits
			// the world instead. So the cone drifted away from the content while the picture stayed
			// put, and the dissolve quietly stopped a few seconds after anyone touched the stick.
			//
			// The camera component is the view that is actually rendered: unchanged by orbit (so the
			// opening stays centred while the content spins through it) and correct if something
			// really does turn the camera. Both behaviours come out of the same vector.
			const FVector CamLoc = Camera ? Camera->GetComponentLocation() : GetActorLocation();
			TunnelAxis = FVector::ZeroVector;
			if (bTunnelFollowsView && Camera)
			{
				TunnelAxis = Camera->GetForwardVector();
			}
			if (TunnelAxis.IsNearlyZero()) TunnelAxis = (Anchor - CamLoc).GetSafeNormal();
			if (TunnelAxis.IsNearlyZero()) TunnelAxis = GetActorForwardVector();
			const float GateMul = (Handover.IsValidIndex(N) && Handover[N].bEnabled)
			                    ? Handover[N].NearDissolve : 1.f;
			SetStationFade(A, Fade, GateMul);
			for (AActor* Ch : Attached) SetStationFade(Ch, Fade, GateMul);
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
		else
		{
			// THE OFF-TRANSITION WAS DEAD CODE. SetStationFade — and with it the Niagara
			// deactivation gate — only ran in the bVis branch, so a station leaving the band
			// took its particle systems with it VISUALLY but never stopped their simulation.
			// The quark sea (unbounded lifetimes) then grew, hidden, from the first visit to
			// the end of the session: the permanent slide to 30 fps that survived zooming out.
			// Not visible -> not simulating. No exceptions (Michael's words).
			auto KillFx = [](AActor* Act)
			{
				TArray<UNiagaraComponent*> NCs;
				Act->GetComponents<UNiagaraComponent>(NCs);
				for (UNiagaraComponent* NC : NCs)
					if (NC->IsActive()) NC->DeactivateImmediate();
			};
			KillFx(A);
			for (AActor* Ch : Attached) KillFx(Ch);
		}
	}
	if (WarmupLeft > 0) --WarmupLeft;

	// ── PERF BISECT: the CH4 station split in two, muted independently ──────────────────────────
	// Runs AFTER the main loop (which re-shows everything each frame), so this pass wins. The
	// ANIMATION half is the subtree under the QZBisectAnim-tagged wrapper; the NIRA half is the
	// rest of that station's row. Niagara is deactivated as well — a hidden system still costs
	// its simulation, and the whole point here is to make the cost leave the frame.
	if (bAnimMuted || bNirAMuted)
	{
		TSet<AActor*> Anim;
		for (TActorIterator<AActor> It2(W); It2; ++It2)
		{
			if (!It2->Tags.Contains(FName(TEXT("QZBisectAnim")))) continue;
			Anim.Add(*It2);
			TArray<AActor*> Sub;
			It2->GetAttachedActors(Sub, true, /*recursive=*/true);
			for (AActor* S : Sub) Anim.Add(S);
		}
		const FName RowName(*FString::FromInt(CH4Station));
		for (TActorIterator<AActor> It2(W); It2; ++It2)
		{
			AActor* A2 = *It2;
			const bool bInAnim = Anim.Contains(A2);
			bool bMute = bAnimMuted && bInAnim;
			if (!bMute && bNirAMuted && !bInAnim)
			{
				AActor* P = A2;   // NirA = anything whose ancestry reaches the CH4 station root
				while (P)
				{
					if (P->Tags.Contains(TAG_STATION) && P->Tags.Contains(RowName)) { bMute = true; break; }
					P = P->GetAttachParentActor();
				}
			}
			if (!bMute) continue;
			A2->SetActorHiddenInGame(true);
			TArray<UNiagaraComponent*> NCs;
			A2->GetComponents<UNiagaraComponent>(NCs);
			for (UNiagaraComponent* NC : NCs)
				if (NC->IsActive()) NC->DeactivateImmediate();
		}
	}
}

void AQZoomStagePawn::SetStationFade(AActor* A, float Fade, float GateMul)
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
			// PERF BISECT: the particle axis. Enforced here, in the one loop that owns Niagara
			// activation, so nothing else can switch a muted system back on.
			if (bParticlesMuted)
			{
				if (NC->IsActive()) NC->DeactivateImmediate();
				continue;
			}
			// User.StationFade — bind it to alpha in the system and the emitter dissolves with
			// the stage. SetVariableFloat (FName) rather than SetNiagaraVariableFloat (FString):
			// the FString overload is deprecated and was the one C4996 in this build.
			NC->SetVariableFloat(FName(TEXT("StationFade")), Fade);

			// User.StationScale — the OTHER half of "scaleable". Component scale only reaches
			// particles whose emitters are in Local Space; a world-space emitter keeps its
			// authored size while the world around it grows by orders of magnitude. Publishing
			// the station's current scale lets the system multiply sizes, velocities and spawn
			// rates by it directly, which works either way. Harmless if the system does not
			// expose the parameter — the write simply finds nothing.
			NC->SetVariableFloat(FName(TEXT("StationScale")), FMath::Max(GateScale, 1e-4f));

			// User.ParticleScale / User.ParticleGlowScale — TWO handles, not one. Four of the
			// five emitters are the particles themselves and read correctly at 1; the fifth is a
			// background glow that has to be ~40x larger before it is visible at all. A single
			// shared size cannot serve both: at the value that makes the glow read, the particles
			// are enormous. Published separately so each keeps its own tuning.
			//
			// OPT-OUT. These three are the pawn's SHOW STYLE, not its plumbing: they exist so one
			// set of shoulder buttons can re-tune every system at once and so nothing dies under
			// P5. A system authored for one station with its own palette does not want them —
			// writing them is how a hand-built look silently becomes amber. The tag QZKeepStyle on
			// the actor keeps StationFade, StationScale, the activation gate and OrbitalNoise (the
			// plumbing, which every system needs) and skips only the three style writes.
			const bool bKeepStyle = A->Tags.Contains(TAG_KEEPSTYLE);
			const float PScaleMul = bParticleScaleTracksZoom ? FMath::Max(GateScale, 1e-4f) : 1.f;
			if (!bKeepStyle)
			{
				NC->SetVariableFloat(FName(TEXT("ParticleScale")),     ParticleScale     * PScaleMul);
				NC->SetVariableFloat(FName(TEXT("ParticleGlowScale")), ParticleGlowScale * PScaleMul);

				// User.ParticleColor. A colour, not a brightness, because the problem was never
				// brightness: P5 multiplies green by 0.02, so no amount of gain rescues a green
				// particle — it is being removed by the grade rather than dimmed by it.
				NC->SetVariableLinearColor(FName(TEXT("ParticleColor")), ParticleColor);
			}

			// ── COLOUR CHARGE ───────────────────────────────────────────────────────────
			// One wheel, the three quarks a third of a turn apart — so the three colours are
			// always all present, which is the only part of QCD this is actually quoting.
			// The phase rides FillerSwirl because that clock is already broadcast to every node;
			// a second synchronised clock would be a second thing to get out of step.
			// Written outside the QZKeepStyle opt-out: these systems were built FOR this.
			if (QuarkColorWheel.Num() >= 2)
			{
				int32 QIdx = -1;
				for (AActor* P = A; P && QIdx < 0; P = P->GetAttachParentActor())
				{
					for (const FName& T : P->Tags)
					{
						const FString Ts = T.ToString();
						if (Ts.StartsWith(TEXT("QZQuark")))
						{
							QIdx = FCString::Atoi(*Ts.Mid(7));
							break;
						}
					}
				}
				if (QIdx >= 0)
				{
					static const FName ColNames[3] = { FName(TEXT("Color_1")),
					                                   FName(TEXT("Color_2")),
					                                   FName(TEXT("Color_3")) };
					const int32 W   = QuarkColorWheel.Num();
					const float Per = FMath::Max(QuarkColorPeriodDeg, 1.f);
					const float Ph  = FMath::Fmod(FillerSwirl, Per) / Per;
					for (int32 k = 0; k < 3; ++k)
					{
						const float t  = (Ph + (float)(QIdx + k) / 3.f) * (float)W;
						const int32 i0 = ((int32)FMath::FloorToInt(t) % W + W) % W;
						const int32 i1 = (i0 + 1) % W;
						NC->SetVariableLinearColor(ColNames[k],
							FMath::Lerp(QuarkColorWheel[i0], QuarkColorWheel[i1], FMath::Frac(t)));
					}
				}
			}

			// User.OrbitalNoise mirrors the MPC's 'orbital_noise' scalar, so the SEQUENCER curve
			// (which can only key material collections) reaches the particle systems too — the
			// movement calms down when the story deactivates a molecule. Systems without the
			// parameter ignore the write.
			if (M169MPC)
				NC->SetVariableFloat(OrbitalNoiseParam,
					UKismetMaterialLibrary::GetScalarParameterValue(this, M169MPC, TEXT("orbital_noise")));

			// ACTIVATE IT. The census said active=0, visible=1 — the component was reached and
			// written to, and simply was not playing. Nothing in the pawn ever activated a
			// Niagara system: it relies on bAutoActivate at BeginPlay, which fires while the
			// station is still hidden in the warm-up. A burst or a finite system plays out into
			// a hidden frame, completes, and never runs again; that is an emitter that exists,
			// is visible, receives its parameters, and shows nothing.
			// Tie it to the fade instead, which also stops it simulating while off screen.
			const bool bWantOn = Fade > 0.002f;
			if (bWantOn && !NC->IsActive())       NC->Activate(true);   // true = reset, so it restarts cleanly
			// IMMEDIATE, not graceful: Deactivate() lets live particles finish, and a system with
			// long or infinite lifetimes (the quark sea) never finishes — it kept simulating,
			// invisible, for the rest of the session. That was the permanent frame-rate drop
			// after the quark station had been visited once.
			else if (!bWantOn && NC->IsActive())  NC->DeactivateImmediate();
			// One line per Niagara component, the first time each is written. "No particles"
			// has three causes that look identical — never reached, reached with fade 0, or
			// reached and the system ignores the parameter — and only the first two are mine.
			static TSet<FString> Announced;
			const FString Key = NC->GetPathName();
			if (!Announced.Contains(Key))
			{
				Announced.Add(Key);
				// GetActorLabel is EDITOR ONLY. This line compiled for QuantumZoomEditor and broke
				// the packaged QuantumZoom target with "GetActorLabel ist kein Member von AActor"
				// — the first cook after months of editor-only builds is where that surfaces.
				// QPerfMonitor.cpp already guards its own use the same way; this one did not.
#if WITH_EDITOR
				const FString ActorId = A->GetActorLabel();
#else
				const FString ActorId = A->GetName();
#endif
				UE_LOG(LogTemp, Warning,
					TEXT("[QZoomStage] niagara '%s' on '%s': StationFade=%.3f StationScale=%.2f "
					     "ParticleScale=%.2f GlowScale=%.2f Color=(%.2f,%.2f,%.2f) "
					     "active=%d visible=%d"),
					*NC->GetName(), *ActorId, Fade, GateScale,
					ParticleScale * PScaleMul, ParticleGlowScale * PScaleMul,
					ParticleColor.R, ParticleColor.G, ParticleColor.B,
					(int32)NC->IsActive(), (int32)NC->IsVisible());
			}
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

				// StationDissolve — for the HARD-SURFACE masters (M_NiraMaster's animated net,
				// M_StationMaster, M_CPK, M_Nidulans). Those masks were never built around
				// StationFade at all, which is why the organic NirA dissolved and the hard-surface
				// one never did (Michael's observation, and it was the whole diagnosis). Rather
				// than rewire their mask logic — which destroyed M_NiraMaster's net once — their
				// existing mask is post-multiplied by this gate. At 1 nothing changes at all; at 0
				// the mask goes to zero and the material clips away.
				//
				// Deliberately NOT smoothed the same way: multiplying a mask by a fade gives a
				// hard-edged wipe rather than an eroding one. Curved so the surface holds until
				// the fade is genuinely low instead of thinning out through the whole band.
				// THE GAIN DECIDES WHEN THE UNIFORM WIPE TAKES OVER FROM THE EROSION.
				// The masters are MASKED, so their clip threshold (0.333) is a hard step: the
				// moment this multiplier pushes the mask under it, the WHOLE surface crosses at
				// once. At the old 1.6 that happened at a fade of 0.21 — the erosion was still
				// mid-sentence and the wipe cut it off. Higher gain holds the gate open longer,
				// so the noise erosion stays the visible mechanism and the wipe only closes on
				// something already nearly gone: 4.0 moves the crossing to a fade of 0.083.
				DMI->SetScalarParameterValue(TEXT("StationDissolve"),
				                             FMath::Clamp(Fade * FMath::Max(StationDissolveGain, 0.1f),
				                                          0.f, 1.f));

				// Bubble sized against the station currently being drawn.
				const float G = FMath::Max(GateScale, 1e-4f);
				// GateMul is the per-stage NearDissolve. The bubble already scales with the station,
				// so a swollen hero meets an equally swollen hole and dissolves from a great
				// distance; this is the knob that lets one stage keep a tight hole and be entered.
				const float GM = FMath::Max(GateMul, 0.01f);
				DMI->SetScalarParameterValue(TEXT("CamFadeStart"), CamGateStartUU * G * GM);
				DMI->SetScalarParameterValue(TEXT("CamFadeRange"), FMath::Max(CamGateRangeUU * G * GM, 1.f));

				// The cone, in world space so wall and floor share one opening.
				DMI->SetVectorParameterValue(TEXT("TunnelAxis"),
					FLinearColor(TunnelAxis.X, TunnelAxis.Y, TunnelAxis.Z, 0.f));
				DMI->SetScalarParameterValue(TEXT("TunnelInnerCos"),
					FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(TunnelInnerDeg, 1.f, 89.f))));
				DMI->SetScalarParameterValue(TEXT("TunnelOuterCos"),
					FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(TunnelOuterDeg, 2.f, 180.f))));

				// Das NirA-Netz schliesst sich ueber ein authoriertes Tiefenfenster. Bewusst an
				// ZoomProgress und NICHT an Fade: Fade ist die Blende der Station und faellt an
				// beiden Enden ab, das Netz soll aber monoton dichter werden, waehrend man
				// hindurchfliegt. Der Tag grenzt es auf das Netz ein — dieselbe Zeile wuerde
				// sonst NetAmount auf den Nukleonen ueberschreiben, die denselben Master teilen.
				if (bNetSolidRamp && A->Tags.Contains(TAG_NETSOLID))
				{
					const float t = FMath::Clamp((ZoomProgress - NetSolidFrom)
					                             / FMath::Max(NetSolidTo - NetSolidFrom, 1e-4f),
					                             0.f, 1.f);
					DMI->SetScalarParameterValue(TEXT("NetAmount"),
						FMath::Lerp(NetAmountOpen, NetAmountSolid, t * t * (3.f - 2.f * t)));
				}
			}
		}
	}
}

void AQZoomStagePawn::UpdateFadeTagged()
{
	UWorld* W = GetWorld();
	if (!W) return;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		int32 N = -1;
		for (const FName& T : A->Tags)
		{
			const FString Ts = T.ToString();
			if (Ts.StartsWith(TEXT("QZFade"))) { N = FCString::Atoi(*Ts.Mid(6)); break; }
		}
		if (N < 0) continue;

		// The value ApplyStations already computed. Nothing is moved or scaled — an environment
		// asset keeps whatever placement it was authored with and only its visibility rides the
		// station. Attaching it to the pivot instead would drag it through the exp() scale, which
		// is right for a station's own geometry and wrong for a backdrop.
		const float F = StationFadeCache.IsValidIndex(N) ? StationFadeCache[N] : 0.f;
		const bool bVis = (F > 0.002f);
		A->SetActorHiddenInGame(!bVis);
		for (AActor* Ch : TArray<AActor*>()) { (void)Ch; }
		if (bVis) SetStationFade(A, F);
	}
}

void AQZoomStagePawn::UpdateGlobalEnv()
{
	UWorld* W = GetWorld();
	if (!W) return;

	// Weight each stage's wish by how present it currently is, so the environment cross-fades
	// through a handover instead of stepping when the dominant stage changes.
	float Acc = 0.f, Wt = 0.f, LogAcc = 0.f;
	for (int32 i = 0; i < StationFadeCache.Num(); ++i)
	{
		const float f = StationFadeCache[i];
		if (f <= 0.f) continue;
		const float g = (Handover.IsValidIndex(i) && Handover[i].bEnabled)
		              ? Handover[i].GlobalLight : 1.f;
		Acc += f * g;
		Wt  += f;
		// Accumulate the SCALE in log space, weighted by fade — see below.
		LogAcc += f * FMath::Loge(FMath::Max(StationScale(i), 1e-6f));
	}
	const float Target = (Wt > 1e-4f) ? (Acc / Wt) : 1.f;
	const float Dt = GetWorld()->GetDeltaSeconds();
	EnvMul = FMath::FInterpTo(EnvMul, Target, Dt, 4.f);

	// FOG DEPTH, HELD CONSTANT ACROSS THE SCALE CHANGE — AND CROSS-FADED, NOT SWITCHED.
	// The first version of this took the DOMINANT station's scale via an argmax. That argmax
	// flips in a single frame at every handover, and the scales either side of a handover
	// differ by a large factor, so the fog density stepped — a visible jump in the colour of
	// the whole frame at each stage change. That is the quick colour shift between lab and
	// cell, and it was mine.
	//
	// Weighting log(scale) by fade and taking the mean makes the transition continuous by
	// construction: through a handover the mean slides between the two stations instead of
	// jumping. Log space is the right average here because the scales are exponential — a
	// linear mean of 1 and 80 is 40, which is nowhere near either.
	float FogScale = 1.f;
	if (bFogTracksScale && Wt > 1e-4f)
		FogScale = FMath::Clamp(FMath::Exp(-LogAcc / Wt),
		                        FMath::Min(FogScaleMin, 1.f), FMath::Max(FogScaleMax, 1.f));
	// and one more pass of smoothing, so even a fast trigger cannot step it
	FogScaleSmoothed = (FogScaleSmoothed <= 0.f)
		? FogScale : FMath::FInterpTo(FogScaleSmoothed, FogScale, Dt, 3.f);
	FogScale = FogScaleSmoothed;

	for (TActorIterator<AActor> It(W); It; ++It)
	{
		AActor* A = *It;
		const bool bGlobalTagged = A->Tags.Contains(FName(TEXT("QZGlobalLight")));

		TArray<ULightComponent*> Lights;
		A->GetComponents<ULightComponent>(Lights);
		for (ULightComponent* LC : Lights)
		{
			// only the SHARED rig: the tagged directionals and any sky light. Station lights are
			// UpdateLights' business and must not be touched here, or they would be scaled twice.
			if (!LC || !(bGlobalTagged || LC->IsA<USkyLightComponent>())) continue;
			// Capture the AUTHORED intensity once. Reading it back each frame after writing a
			// scaled value would compound, and the rig would fade to nothing over a few seconds.
			float* Base = EnvBaseIntensity.Find(LC);
			if (!Base) Base = &EnvBaseIntensity.Add(LC, LC->Intensity);
			LC->SetIntensity(*Base * EnvMul);
		}

		TArray<UExponentialHeightFogComponent*> Fogs;
		A->GetComponents<UExponentialHeightFogComponent>(Fogs);
		for (UExponentialHeightFogComponent* FC : Fogs)
		{
			if (!FC) continue;
			float* Base = EnvBaseFog.Find(FC);
			if (!Base) Base = &EnvBaseFog.Add(FC, FC->FogDensity);
			FC->SetFogDensity(*Base * EnvMul * FogScale);
		}

		// The atmosphere has no single intensity worth driving, so it is simply switched off once
		// the environment has faded far enough that it cannot be seen anyway.
		TArray<USkyAtmosphereComponent*> Atmos;
		A->GetComponents<USkyAtmosphereComponent>(Atmos);
		for (USkyAtmosphereComponent* SA : Atmos)
		{
			if (SA) SA->SetVisibility(EnvMul > 0.05f);
		}
	}
}

void AQZoomStagePawn::UpdateLights()
{
	UWorld* W = GetWorld();
	if (!W) return;
	LightDbgCount = 0; LightDbgMax = 0.f; LightDbgTop.Reset();
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

		// GLOBAL RIG — never faded by anything. The key/fill live in the PERSISTENT level, and the
		// persistent level also holds stations 1, 2 and 3. So "fade lights by the level they sit in"
		// silently made the whole rig follow the MYCELIUM: the scene stayed pitch black until station 1
		// appeared, and pushing that onset to ZP 0.24 to stop the mycelium poking through the mushroom
		// turned it into a quarter of the descent in the dark. Level membership is a fine rule for an
		// accent light inside a station sublevel and a wrong one for a rig that serves all 13 decades.
		if (A->Tags.Contains(FName(TEXT("QZGlobalLight")))) continue;

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
		// THIRD ROUTE: an OWN DEPTH WINDOW, for a light that belongs to a moment rather than to a
		// station. Tags QZLightOn<pct> / QZLightOff<pct>, e.g. QZLightOn5 + QZLightOff13 = on from
		// 5% to 13% of the dive. The pawn's own headlamp is the case this exists for: it is not in
		// any station sublevel and must not follow one — it belongs to "you are inside the
		// mushroom", which is a stretch of the zoom bar and nothing else.
		// Read BEFORE QZLight<N> and excluded from it, or Atoi("On5") would silently return 0 and
		// bind the light to station 0.
		float WinOn = -1.f, WinOff = -1.f;
		int32 TagStation = -1;
		for (const FName& T : A->Tags)
		{
			const FString Ts = T.ToString();
			if (Ts.StartsWith(TEXT("QZLightOff")))     { WinOff = FCString::Atof(*Ts.Mid(10)) * 0.01f; }
			else if (Ts.StartsWith(TEXT("QZLightOn"))) { WinOn  = FCString::Atof(*Ts.Mid(9))  * 0.01f; }
			else if (Ts.StartsWith(TEXT("QZLight")) && TagStation < 0)
			{
				const FString Rest = Ts.Mid(7);
				if (!Rest.IsEmpty() && Rest.IsNumeric()) TagStation = FCString::Atoi(*Rest);
			}
		}

		float Fade = 0.f;
		int32 ScaleStation = TagStation;      // which station's SCALE this light should ride
		if (WinOn >= 0.f || WinOff >= 0.f)
		{
			// The window governs WHEN. Whether the light also grows with the world is a separate
			// question, and it is answered by whether the actor ALSO carries a QZLight<N> tag.
			//
			// Without one it stays a lamp on the rig: authored intensity, authored reach, fixed
			// in world units. With one it borrows that station's scale for reach and intensity
			// while keeping the window's timing — which is what a HEADLAMP needs. At 13% the lab
			// renders at roughly ten times its authored size and at 17% at twenty; a fixed 6000
			// units of reach then covers a tenth of the mushroom, and only the nearest part of it
			// is lit. The light did not fail, it simply stayed the size it was told to be.
			//
			// Position is untouched either way: an attached light rides its parent, and the
			// reposition below is skipped for attached actors.
			const float A0 = (WinOn  >= 0.f) ? WinOn  : 0.f;
			const float B0 = (WinOff >= 0.f) ? WinOff : 1.f;
			const float Soft = FMath::Max(LightWindowSoft, 1e-3f);
			const float g = FMath::Clamp((ZoomProgress - A0) / Soft, 0.f, 1.f)
			              * FMath::Clamp((B0 - ZoomProgress) / Soft, 0.f, 1.f);
			Fade = g * g * (3.f - 2.f * g);
			ScaleStation = TagStation;   // -1 when no QZLight<N> tag: the old, unscaled behaviour
		}
		else if (TagStation >= 0)
		{
			// The value ApplyStations actually used this frame — not a re-derivation. The old binary test
			// here read the GLOBAL MinVis/MaxVis and so ignored every per-station handover override.
			Fade = StationFadeCache.IsValidIndex(TagStation) ? StationFadeCache[TagStation] : 0.f;
		}
		else if (TrackedLevels.Contains(A->GetLevel()))
		{
			const float* fp = LevelFade.Find(A->GetLevel());
			Fade = fp ? *fp : 0.f;   // missing entry -> 0, so the light fades OUT instead of freezing
			if (const int32* sp = LevelStationIdx.Find(A->GetLevel())) ScaleStation = *sp;
		}
		else
		{
			continue;    // neither tagged nor in a tracked station level: a global light, leave it alone
		}

		// ── THE ONSET GATE ──────────────────────────────────────────────────────────
		// Geometry and lighting want different clocks. A station has to dissolve in early and
		// from a distance; its lamps belong to the moment you are INSIDE it. Tied to one fade,
		// the cell's key light arrived at 14% with no geometry near it to catch — a lamp
		// switching on in the void. This gate holds a station's lights back to an authored depth
		// and swells them in from there. Smoothstepped: the cure must not be a second pop.
		// Multiplies the LIGHT fade only; the scene's own dissolve is untouched.
		if (Handover.IsValidIndex(ScaleStation) && Handover[ScaleStation].LightOnsetAt > 0.f)
		{
			const FQZHandover& H = Handover[ScaleStation];
			const float g = FMath::Clamp((ZoomProgress - H.LightOnsetAt)
			                             / FMath::Max(H.LightOnsetWidth, 1e-3f), 0.f, 1.f);
			Fade *= g * g * (3.f - 2.f * g);
		}

		// ── THE HOLD ────────────────────────────────────────────────────────────────
		// Applied AFTER the onset, because the two govern opposite ends: the onset decides when a
		// station's lights may arrive, this decides that they need not leave with it. A rig can go
		// on lighting what comes next long after its own geometry has handed over — the light
		// belongs to the space, the mesh only to the chapter.
		if (Handover.IsValidIndex(ScaleStation))
		{
			const FQZHandover& H = Handover[ScaleStation];
			if (H.LightHoldFrom > 0.f && ZoomProgress >= H.LightHoldFrom)
				Fade = FMath::Max(Fade, H.LightHoldLevel);
		}

		TArray<ULightComponent*> Lights;
		A->GetComponents<ULightComponent>(Lights);
		for (ULightComponent* LC : Lights)
		{
			if (!LC) continue;
			const TWeakObjectPtr<ULightComponent> Key(LC);
			float* bp = LightBaseIntensity.Find(Key);
			if (!bp) { bp = &LightBaseIntensity.Add(Key, LC->Intensity); }   // capture authored intensity once

			// ONE BY ONE. Each light waits a little further into the station's fade before it starts,
			// in first-seen order, then rises over the knee. The knee is why a scene is lit WHILE it
			// dissolves in rather than once it is already solid; the stagger is why it arrives as a
			// sequence. Both are per light, so the last one is still climbing as the first sits at full.
			int32* op = LightOrder.Find(Key);
			if (!op) { op = &LightOrder.Add(Key, LightOrderNext++); }
			const float Lit = FMath::Clamp((Fade - LightStaggerStep * (float)(*op))
			                               / FMath::Max(LightFadeKnee, 0.01f), 0.f, 1.f);
			// PROLONG the fade (Michael, 3x): ease the applied fade toward the station's target instead of
			// snapping to it. LightFadeSpeed lower = slower ramp. Per-light smoothed value so each light lags.
			// A station may slow its own fade-IN further via Handover.LightRiseMul; the fade-OUT
			// deliberately ignores it (a slow rise reads as atmosphere, a slow exit reads as a bug).
			float Spd = FMath::Max(LightFadeSpeed, 0.01f);
			if (Lit > 0.f && Handover.IsValidIndex(ScaleStation))
				Spd *= FMath::Max(Handover[ScaleStation].LightRiseMul, 0.05f);
			float& sm = LightFadeSmoothed.FindOrAdd(Key);
			if (Lit < sm) Spd = FMath::Max(LightFadeSpeed, 0.01f);   // falling: shared speed
			sm = FMath::FInterpTo(sm, Lit, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f, Spd);
			++LightDbgCount;
			if (sm > LightDbgMax)
			{
				LightDbgMax = sm;
#if WITH_EDITOR
				LightDbgTop = A->GetActorLabel();
#else
				LightDbgTop = A->GetName();
#endif
			}
			// intensity is written below, once the station's scale is known — see the
			// inverse-square note there. Writing it here as well would be overwritten anyway.

			// ── RIDE THE STATION ────────────────────────────────────────────────────────
			// Intensity alone was the whole of this function, and it is not enough. The pawn
			// scales the world about the Anchor; a light does not follow, so it kept its
			// authored position and its authored attenuation radius while the hero grew past
			// it by three orders of magnitude. The result is a fixed sphere of lit space that
			// the geometry sweeps through — which from the chair looks like lighting that only
			// switches on from certain angles, because only from certain angles is the lit part
			// of the mesh facing you. Nothing about it was actually view-dependent.
			//
			// Position is stored RELATIVE TO THE ANCHOR and re-applied through the same
			// (Orbit, Anchor, Scale) the stations get, so a light keeps its art-directed
			// placement on the subject at every scale. Attenuation scales with it, because a
			// radius in world units means nothing once the subject is 1500x bigger.
			float IntensityMul = 1.f;
			if (ScaleStation >= 0)
			{
				const float LS = FMath::Max(StationRenderScale(ScaleStation), 1e-6f);
				const FQuat  LOrbit = FRotator(OrbitPitch, OrbitYaw, 0.f).Quaternion();

				// ATTACHED LIGHTS ALREADY RIDE SOMETHING. A light parented to an actor follows
				// that actor, and writing a world location here would overwrite whatever moved
				// it — the Sequencer's keys included. So only free-standing lights are placed
				// from their anchor offset; a light bolted to a molecule stays bolted to it.
				if (A->GetAttachParentActor() == nullptr)
				{
					FVector* off = LightBaseOffset.Find(Key);
					if (!off) off = &LightBaseOffset.Add(Key, LC->GetComponentLocation() - Anchor);
					LC->SetWorldLocation(Anchor + LOrbit.RotateVector(*off * LS));
				}

				if (UPointLightComponent* PLC = Cast<UPointLightComponent>(LC))
				{
					float* rad = LightBaseRadius.Find(Key);
					if (!rad) rad = &LightBaseRadius.Add(Key, PLC->AttenuationRadius);
					PLC->SetAttenuationRadius(FMath::Max(*rad * LS, 1.f));
				}

				// INVERSE SQUARE. Moving the light out by S and widening its reach by S is only
				// two thirds of riding the station: a point light falls off with the square of
				// distance, so at S the same surface receives 1/S^2 of the light it did at scale
				// 1. Without this term the lab you art-directed goes quietly darker the further
				// you descend, and no setting appears to have changed — which is the least
				// debuggable kind of drift.
				IntensityMul = FMath::Min(FMath::Pow(LS, LightScalePower),
				                          FMath::Max(LightScaleMaxMul, 1.f));
			}
			LC->SetIntensity(*bp * sm * IntensityMul);
		}
		// Post-process volumes in a station sublevel fade too (Michael: "PP fades as well"): scale their
		// BlendWeight by the station's visibility so the grade ramps in/out with the scene instead of popping.
		if (APostProcessVolume* PPV = Cast<APostProcessVolume>(A))
		{
			const TWeakObjectPtr<AActor> Key(A);
			float* bw = PPBaseWeight.Find(Key);
			if (!bw) { bw = &PPBaseWeight.Add(Key, PPV->BlendWeight); }   // capture authored weight once
			// Same knee as the lights, but never staggered: a grade is one thing, not a sequence.
			const float PPLit = FMath::Clamp(Fade / FMath::Max(LightFadeKnee, 0.01f), 0.f, 1.f);
			float& pps = PPFadeSmoothed.FindOrAdd(Key);
			pps = FMath::FInterpTo(pps, PPLit, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f,
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

float AQZoomStagePawn::PaceAt(float P) const
{
	// Empty curve = no pacing. The inert default, so adding this to a tuned show changes nothing
	// until someone authors a point.
	const int32 N = ZoomPace.Num();
	if (N == 0) return 1.f;
	if (N == 1) return FMath::Max(ZoomPace[0].Speed, ZoomPaceFloor);

	// Before the first point and after the last, the curve holds its end value rather than
	// falling off — a pacing curve that only covers the middle of the dive should leave the ends
	// at their authored speed, not at zero.
	if (P <= ZoomPace[0].Depth)       return FMath::Max(ZoomPace[0].Speed, ZoomPaceFloor);
	if (P >= ZoomPace[N - 1].Depth)   return FMath::Max(ZoomPace[N - 1].Speed, ZoomPaceFloor);

	for (int32 i = 0; i < N - 1; ++i)
	{
		const FQZPacePoint& A = ZoomPace[i];
		const FQZPacePoint& B = ZoomPace[i + 1];
		if (P >= A.Depth && P <= B.Depth)
		{
			const float d = B.Depth - A.Depth;
			// Coincident or unsorted points would divide by ~0; fall back to the left value
			// instead of producing an infinity that would freeze the trigger.
			if (d <= 1e-5f) return FMath::Max(A.Speed, ZoomPaceFloor);
			const float t = (P - A.Depth) / d;
			return FMath::Max(FMath::Lerp(A.Speed, B.Speed, t), ZoomPaceFloor);
		}
	}
	return 1.f;
}

void AQZoomStagePawn::ApplyNaniteOff()
{
	// THE HARD SWITCH, not the diagnostic. r.Nanite 0 takes the Nanite path out of the renderer
	// entirely; every Nanite mesh then draws through its FALLBACK mesh, i.e. ordinary triangles.
	// That is what makes it a valid bisect axis: the scene still renders, so the two frame rates
	// are comparable, and the only variable is whether Nanite ran.
	//
	// Which is also why the fallback has to be CHEAP. A Nanite mesh whose LOD0 is a million
	// triangles renders a million triangles the moment this is switched off — the axis would then
	// measure the decimation, not Nanite.
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite")))
		CV->Set(bNaniteOff ? 0 : 1, ECVF_SetByConsole);
	UE_LOG(LogTemp, Warning, TEXT("[QZoomPerf] Nanite %s"), bNaniteOff ? TEXT("OFF") : TEXT("on"));
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
	float Target = StyleLightLadder[Step];
	// OFF below the gate depth. The step itself is left alone, so LB still cycles and the
	// operator's choice is waiting when the threshold is crossed.
	// RAMP, not switch. Below ZoomOn the light is off; from there it swells to full at ZoomFull.
	// A switch could only choose between arriving in an empty scene (the pop at 14%) and leaving
	// the lab-to-cell stretch unlit (neither station owns a lamp). Growing with the dive is
	// neither. Set ZoomFull <= ZoomOn and the old switch is back.
	if (bStyleLightZoomGate)
	{
		const float A = StyleLightZoomOn;
		const float B = StyleLightZoomFull;
		if (B <= A)
		{
			if (ZoomProgress < A) Target = 0.f;
		}
		else
		{
			float g = FMath::Clamp((ZoomProgress - A) / (B - A), 0.f, 1.f);
			Target *= g * g * (3.f - 2.f * g);
		}
	}
	const bool  bRising = Target > StyleLightEased;
	const float Speed   = bRising ? FMath::Max(StyleLightRiseSpeed, 0.01f)
	                              : FMath::Max(StyleLightEaseSpeed, 0.01f);
	StyleLightEased = FMath::FInterpTo(StyleLightEased, Target, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f, Speed);
	// FInterpTo ASYMPTOTES — it never actually arrives. Cycling back to step 0 therefore left a
	// small residual intensity forever, and on a light with this attenuation radius riding the
	// Anchor that is still visible: "the mushroom is still affected by the light". Snap the last
	// stretch to zero and take the component out of the render entirely, so off means off.
	if (Target <= 0.f && StyleLightEased < 1.f) StyleLightEased = 0.f;
	LC->SetIntensity(StyleLightEased);
	LC->SetVisibility(StyleLightEased > 0.01f);

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
	UTextRenderComponent* Texts[] = { Readout, FpsBig, DetailIndex, DetailTitle, DetailSub, DetailScale, DetailProv };
	for (UTextRenderComponent* T : Texts) if (T) T->SetVisibility(!bOn);
	UStaticMeshComponent* Rules[] = { ReadoutBar, ReadoutBarFill, DetailRule };
	for (UStaticMeshComponent* M : Rules) if (M) M->SetVisibility(!bOn && bShowHUDLines);
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
		// RECURSIVE — see the note in ApplyStations. One level deep meant the orbital lobes never
		// got a DMI at all, so SetStationFade had nothing to write to even once it reached them.
		TArray<AActor*> Kids;
		A->GetAttachedActors(Kids, true, /*recursive=*/true);
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

	// ── ACTORS THAT TRAVEL WITH THE LOOK ────────────────────────────────────────
	// Both POSITION and rotation, and position is the half that matters for a point light: a
	// lamp above the camera lights the same sphere no matter where the camera faces, so only
	// carrying its offset around the view axis makes it a headlight rather than a lamppost.
	// The base offset is captured ONCE, against the camera — re-reading it each frame would
	// compound the rotation and the actor would spiral away.
	// Runs on every node: the primary calls this from PollInput, the secondaries from
	// OnClusterEvent, so wall and floor carry the same offset.
	if (UWorld* W2 = GetWorld())
	{
		const FVector Pivot = Camera ? Camera->GetComponentLocation() : GetActorLocation();
		for (TActorIterator<AActor> It(W2); It; ++It)
		{
			AActor* A = *It;
			if (!A->Tags.Contains(TAG_FOLLOWLOOK)) continue;
			const TWeakObjectPtr<AActor> Key(A);
			FVector* Off = LookFollowBaseOff.Find(Key);
			if (!Off)
			{
				Off = &LookFollowBaseOff.Add(Key, A->GetActorLocation() - Pivot);
				LookFollowBaseRot.Add(Key, A->GetActorRotation());
			}
			A->SetActorLocation(Pivot + Offset.RotateVector(*Off));
			if (const FRotator* R = LookFollowBaseRot.Find(Key))
				A->SetActorRotation((R->Quaternion() * Offset).Rotator());
		}
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

	// The BIG frame-rate: menu always shows it; the editorial HUD shows it with bShowFPS; clean
	// mode hides it via SetCleanMode's text array like everything else. Visibility is set here
	// (every frame) only for the two live modes, so the clean-mode hide is never overridden.
	if (FpsBig)
	{
		const bool bWant = !bCleanMode && (HUDMode == 2 || (HUDMode == 1 && bShowFPS));
		if (FpsBig->IsVisible() != bWant) FpsBig->SetVisibility(bWant);
		if (bWant)
			FpsBig->SetText(FText::FromString(FString::Printf(TEXT("%.0f | %.0f"),
				FpsCurrent, FpsMedian)));
	}

	// ── MUTE MENU (HUDMode 2): the readout becomes the bisect panel. FPS is forced on here —
	// the menu exists to watch frame time against the station list, so hiding it behind bShowFPS
	// would defeat the mode. Early return: the editorial text below has no business overwriting this.
	if (HUDMode == 2)
	{
		FString Menu = FString::Printf(
			TEXT("PERF BISECT                    [Y] weiter\nFPS        %.0f  |  median %.0f\nDEPTH      %.0f%%   TEMPO %.2fx\n"),
			FpsCurrent, FpsMedian, ZoomProgress * 100.f, PaceAt(ZoomProgress));
		const int32 NRows = FMath::Clamp(StationCount, 1, 9);
		for (int32 i = 0; i < NRows; ++i)
		{
			const bool bMuted = (StationMuteMask & (1 << i)) != 0;
			const bool bRet   = Handover.IsValidIndex(i) && !Handover[i].bActive;
			// THE FADE, printed. Five rounds of "it is dark here" were answered by re-deriving
			// band values on paper; every one of them could have been settled by reading the
			// number the frame actually used. StationFadeCache IS that number.
			const float F = StationFadeCache.IsValidIndex(i) ? StationFadeCache[i] : 0.f;
			Menu += FString::Printf(TEXT("\n%s [%d] %-14s %-8s %.2f"),
				(i == MuteSel) ? TEXT(">") : TEXT("  "), i + 1,
				Handover.IsValidIndex(i) ? *Handover[i].Name.ToString() : TEXT("?"),
				bRet ? TEXT("retired") : bMuted ? TEXT("MUTED") : TEXT("on"), F);
		}
		Menu += FString::Printf(
			TEXT("\n\n[A] mute   [B] alle an   [X] shader: %s   [>] partikel: %s")
			TEXT("\n[<] anim: %s   [LB] nira: %s   [R3] 8K-SIM: %s")
			TEXT("\n[L3] NANITE: %s   [DPad hoch/runter] waehlen")
			TEXT("\nLICHTER   %d aktiv, hellstes %.2f  (%s)   ENV %.2f"),
			(ShaderLevel == 0) ? TEXT("HOCH") : (ShaderLevel == 1) ? TEXT("MITTEL") : TEXT("NIEDRIG"),
			bParticlesMuted ? TEXT("AUS") : TEXT("an"),
			bAnimMuted ? TEXT("AUS") : TEXT("an"),
			bNirAMuted ? TEXT("AUS") : TEXT("an"),
			bSim8K ? *FString::Printf(TEXT("%d%% (+Stereo ~x1.9)"), Sim8KPct) : TEXT("aus"),
			bNaniteOff ? TEXT("AUS (Fallback-Mesh)") : TEXT("an"),
			LightDbgCount, LightDbgMax, *LightDbgTop, EnvMul);
		PushHudText(Menu);
		return;
	}
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
	// FPS: current and the median over FpsWindowSeconds. Median is the honest answer to
	// "what does the show actually run at" — one hitch cannot drag it, unlike a mean.
	FString FpsLine;
	if (bShowFPS)
		FpsLine = FString::Printf(TEXT("\nFPS        %.0f  |  median %.0f  (%.0fs)"),
			FpsCurrent, FpsMedian, FpsWindowSeconds);
	// Does StationFade have anywhere to LAND? SetStationFade only writes to materials that are
	// already dynamic instances, so a station with 0 fadeable DMIs jumps straight from opaque to
	// hidden however smooth the curve is. This line distinguishes the two cases on sight.
	FString FadeLine;
	if (bShowFadeDiag)
	{
		// READ THE VALUE BACK OUT. Counting fadeable DMIs only proves the parameter exists; it
		// says nothing about whether SetStationFade's write actually lands. The conidium has now
		// been cleared by inspection on every other axis — instance overrides, master wiring,
		// blend mode, clip value, parameter values, Nanite config — so the one remaining question
		// is whether the material is receiving the number at all. Min/max of what the DMIs report
		// answers it: if the band stays pinned at 1.00 while the station's fade is mid-dissolve,
		// the write is not reaching them; if it tracks, the fault is downstream in the shader.
		int32 NFade = 0;
		float LoFade = 9.f, HiFade = -9.f;
		for (const FQZMat& E : MatCache)
		{
			if (!E.bHasFade) continue;
			++NFade;
			float V = 0.f;
			UMaterialInstanceDynamic* D = E.DMI.Get();   // TWeakObjectPtr has no operator bool
			if (D && D->GetScalarParameterValue(TEXT("StationFade"), V))
			{
				LoFade = FMath::Min(LoFade, V);
				HiFade = FMath::Max(HiFade, V);
			}
		}
		// CH4 state on the HUD. Whether the reaction clock is running is otherwise invisible —
		// a frozen phase and a slow phase look identical from the chair, which cost a whole
		// round of "I waited and nothing happened".
		const float CH4GateFade = StationFadeCache.IsValidIndex(CH4Station)
		                        ? StationFadeCache[CH4Station] : 0.f;
		FadeLine = FString::Printf(
			TEXT("\nFADE       S%d %.2f  |  DMI %d (%d fadeable)  |  readback %.2f-%.2f")
			TEXT("\nCH4        S%d fade %.2f  |  phase %.2f %s  |  cycles %.1f"),
			DiagStation, DiagFade, MatCache.Num(), NFade,
			(LoFade > 8.f ? -1.f : LoFade), (HiFade < -8.f ? -1.f : HiFade),
			CH4Station, CH4GateFade, CH4Phase,
			(bCH4Running ? TEXT("RUNNING") : TEXT("stopped  [RB]")), CH4SpeedNow);
	}
	// PERF-BISECT line: only while something is muted, so normal shows stay clean. Lists every muted
	// row by name — "which key do I press to bring it back" must be readable off the wall.
	FString MuteLine;
	if (StationMuteMask != 0)
	{
		MuteLine = TEXT("\nMUTED     ");
		for (int32 i = 0; i < 9; ++i)
			if (StationMuteMask & (1 << i))
				MuteLine += FString::Printf(TEXT(" [%d]%s"), i + 1,
					Handover.IsValidIndex(i) ? *Handover[i].Name.ToString() : TEXT("?"));
	}
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
	PushHudText(FString::Printf(
		TEXT("OBSERVER   %s\nSPEED      %s /s\nZOOM       %s\nDEPTH      %.0f%%\nPRESET     %s @ %.0f%%\nFILLERS    %s  [%s]"
		     "\nAUDIO      %d/%d live  |  peak %.2f  |  master %.2f%s%s%s%s%s"),
		*FormatScale(ObserverSize), *FormatRate(ObserverSpeed), *FormatZoom(Power), ZoomProgress * 100.f,
		PPNames[FMath::Clamp(PPPreset, 0, 9)],
		// THE BLEND WEIGHT. "The preset is not active" and "the preset is active at weight 0"
		// look identical on screen and need completely different fixes.
		(PPVolume ? PPVolume->BlendWeight * 100.f : -1.f),
		FillNames[FMath::Clamp(FillerMode, 0, 3)],
		DensNames[FMath::Clamp(FillerDensity, 0, 4)],
		// AUDIO — computed every frame since the audio layer was written and never once shown.
		// The comment on AudioLive/AudioPeak claims it is "published to the readout"; it was not.
		// "The music is gone" has three causes that look identical from the chair: no tracks
		// spawned, tracks spawned but not playing, or playing and faded to nothing. These four
		// numbers separate all three at a glance.
		AudioLive, StageAudio.Num(), AudioPeak, MasterVolume,
		*PalLine, *NanLine, *FpsLine, *FadeLine, *MuteLine));

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
	// This unconditional show ran AFTER the BeginPlay clean-mode apply and quietly re-enabled the
	// hairlines every session — the bShowHUDLines gate in SetCleanMode never stood a chance. The
	// only callers are the three line meshes, so the gate belongs here too.
	M->SetVisibility(bShowHUDLines);
}

void AQZoomStagePawn::LayoutInterface()
{
	// Idempotent: the whole point is that this can sit in Tick so the three numbers can be dragged
	// live in the Details panel during PIE. Without a live path they would only apply at BeginPlay,
	// and the corner has to be judged against the actual wall frustum — which is the cluster's, not
	// this camera's, so it cannot be computed here and has to be found by eye.
	if (FMath::IsNearlyEqual(UILaidDepth, UIDepth) &&
	    FMath::IsNearlyEqual(UILaidRight, UIMarginRight) &&
	    FMath::IsNearlyEqual(UILaidUp,    UIMarginUp)) return;
	UILaidDepth = UIDepth; UILaidRight = UIMarginRight; UILaidUp = UIMarginUp;

	// Mirrored about the view centre, same depth, same top line. The two columns are one object.
	ReadoutOffset = FVector(UIDepth, -UIMarginRight, UIMarginUp);
	DetailOffset  = FVector(UIDepth,  UIMarginRight, UIMarginUp);

	if (Readout) { Readout->SetRelativeLocation(ReadoutOffset); Readout->SetWorldSize(ReadoutSize); }
	if (FpsBig)
	{
		// above the readout column, left-anchored to the same margin
		FpsBig->SetRelativeLocation(ReadoutOffset + FVector(0.f, 0.f, ReadoutSize * 2.0f));
		FpsBig->SetWorldSize(ReadoutSize * 3.0f);
	}

	// ── the caption's vertical rhythm ────────────────────────────────────────────────────────
	// Every line is a multiple of DetailSize, so DetailSize alone scales the block. The old ladder
	// ran 1.7 / 1.0 / 0.95 / 0.8 — four sizes within a factor of two, which on an 8K wall reads as
	// one grey slab rather than as a caption. The spread is wider now and the SCALE line is the
	// second-largest thing in the block: it is the one line that changes continuously as you
	// descend, and it is what a science audience looks for.
	const float D = FMath::Max(DetailSize, 1.f);
	float Up = 0.f;
	auto Place = [&](UTextRenderComponent* T, float Size, float Gap)
	{
		if (!T) return;
		Up -= Gap * D;
		T->SetRelativeLocation(DetailOffset + FVector(0.f, 0.f, Up));
		T->SetWorldSize(D * Size);
	};
	if (DetailIndex)
	{
		DetailIndex->SetRelativeLocation(DetailOffset);
		DetailIndex->SetWorldSize(D * 0.55f);
	}
	if (bShowStationIndex) Up -= 1.05f * D;          // clear the index line
	Place(DetailTitle, 1.75f, 0.00f);
	const float RuleUp = DetailOffset.Z + Up - D * 2.15f;
	Place(DetailSub,   0.95f, 2.55f);
	Place(DetailScale, 1.15f, 1.30f);
	Place(DetailProv,  0.60f, 1.60f);

	// ── the two hairline rules ───────────────────────────────────────────────────────────────
	const float Th = FMath::Max(RuleThickness, 0.25f) / 100.f;
	ReadoutBarFwd  = ReadoutOffset.X;
	ReadoutBarLeft = ReadoutOffset.Y;
	ReadoutBarUp   = ReadoutOffset.Z - ReadoutSize * 7.0f;
	if (ReadoutBar)
	{
		ReadoutBar->SetRelativeLocation(FVector(ReadoutBarFwd, ReadoutBarLeft + ReadoutRuleWidth * 0.5f, ReadoutBarUp));
		ReadoutBar->SetRelativeScale3D(FVector(0.02f, ReadoutRuleWidth / 100.f, Th));
	}
	if (DetailRule)
	{
		DetailRule->SetRelativeLocation(FVector(DetailOffset.X, DetailOffset.Y - DetailRuleWidth * 0.5f, RuleUp));
		DetailRule->SetRelativeScale3D(FVector(0.02f, DetailRuleWidth / 100.f, Th));
	}
	// The background panels ride the columns too, or they stay behind where the text used to be.
	if (ReadoutBG) ReadoutBG->SetRelativeLocation(FVector(ReadoutOffset.X + BackgroundDepthOffset, ReadoutBGCenter.X, ReadoutBGCenter.Y));
	if (DetailBG)  DetailBG ->SetRelativeLocation(FVector(DetailOffset.X  + BackgroundDepthOffset, DetailBGCenter.X,  DetailBGCenter.Y));
}

void AQZoomStagePawn::UpdateStagePresets()
{
	// ── the blend: every node, every frame, no broadcast ─────────────────────────────────
	// Runs BEFORE the primary-only gate on purpose. BlendWeight is derived from ZoomProgress,
	// which is already synced, so each node arrives at the same number on its own — and unlike
	// the per-stage path below it cannot fail silently on a secondary.
	if (bPresetBlend && PPVolume)
	{
		// SEED THE PRESET ONCE, THEN LEAVE IT ALONE.
		// This used to force PPPreset back to PresetBlendPreset EVERY FRAME, which meant D-Pad
		// Up/Down changed the preset and the next frame put it straight back — the preset
		// switcher looked broken because this was fighting it, sixty times a second. Mine.
		// Now the target is pushed only when PresetBlendPreset itself changes (including the
		// first frame), so the blend chooses the STARTING look and the operator can still cycle
		// away from it. The weight keeps ramping whichever preset is selected, which is the
		// more useful behaviour anyway: it blends in whatever you have chosen.
		const int32 Want = FMath::Clamp(PresetBlendPreset, 0, 9);
		if (Want != PresetBlendApplied)
		{
			PresetBlendApplied = Want;
			PPPreset = Want;
			ApplyPPPreset(PPPreset);
		}
		const float A = PresetBlendStart, B = PresetBlendEnd;
		float t = (B - A > 1e-4f) ? (ZoomProgress - A) / (B - A) : (ZoomProgress >= B ? 1.f : 0.f);
		t = FMath::Clamp(t, 0.f, 1.f);
		t = t * t * (3.f - 2.f * t);          // smoothstep: no corner at either end
		PPVolume->BlendWeight = t;
	}

	// PRIMARY ONLY. PPPreset and StyleLightStep are already in Broadcast(), so the other nodes
	// receive them through the cluster event. Letting every node decide for itself would give
	// the same value two sources of truth, and they would disagree for a frame at each
	// handover — on a wall that is a visible flicker between two grades.
	if (!bAutoStagePreset || !bIsPrimary || Handover.Num() == 0) return;

	// Same dominance test as the caption, for the same reason: this must agree with what is on
	// screen, and StationFadeCache is the value ApplyStations actually used this frame.
	int32 Best = -1;
	float BestF = 0.f, BestD = 0.f;
	for (int32 N = 0; N < StationCount; ++N)
	{
		const float F = StationFadeCache.IsValidIndex(N) ? StationFadeCache[N] : 0.f;
		if (F <= 0.002f) continue;
		const float Dc = FMath::Abs(ZoomProgress - StageCentre(N));
		if (F > BestF + 1e-4f || (FMath::Abs(F - BestF) <= 1e-4f && (Best < 0 || Dc < BestD)))
		{ BestF = F; BestD = Dc; Best = N; }
	}
	if (Best < 0 || Best == AutoStagePrev) return;   // only ever fires on a CHANGE of stage
	AutoStagePrev = Best;
	if (!Handover.IsValidIndex(Best)) return;

	// The blend owns PPPreset when it is on, so the per-stage Preset is ignored rather than
	// fighting it — two writers of one value, alternating every handover, is a flicker.
	// StyleStep below is unaffected: the blend has no opinion about the style light.
	const int32 P = bPresetBlend ? -1 : Handover[Best].Preset;
	if (P >= 0 && P != PPPreset)
	{
		PPPreset = FMath::Clamp(P, 0, 9);
		ApplyPPPreset(PPPreset);
		UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] stage %d -> PP preset %d"), Best, PPPreset);
	}
	const int32 S = Handover[Best].StyleStep;
	if (S >= 0 && StyleLightLadder.Num() > 0)
	{
		StyleLightStep = FMath::Clamp(S, 0, StyleLightLadder.Num() - 1);
		ApplyStyleLight();
		UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] stage %d -> style light step %d"), Best, StyleLightStep);
	}
}

void AQZoomStagePawn::UpdateInfoLayer()
{
	LayoutInterface();
	if (StageTitle.Num() == 0) return;

	// WHICH STATION IS THE CAPTION ABOUT?
	// This used to derive an index from StageTitle.Num() and then compare it against StageCentre(),
	// which is spread over StationCount and log-spaced. Those are two different grids: six caption
	// rows against an eight-station ladder meant the caption on screen belonged to a different
	// station than the geometry on screen, and the drift grew with depth. The pawn already knows
	// the answer — ApplyStations writes every station's fade into StationFadeCache each frame, and
	// the loudest entry IS what you are looking at. Reading the caption off that makes the two
	// physically unable to disagree, and it fades with the station instead of on a second,
	// independent window (the old InfoFadeWidth, now gone).
	int32 i = -1;
	float Fade = 0.f, BestD = 0.f;
	for (int32 N = 0; N < StationCount; ++N)
	{
		const float F = StationFadeCache.IsValidIndex(N) ? StationFadeCache[N] : 0.f;
		if (F <= 0.002f) continue;
		// Fade decides; distance to the station centre breaks the tie. Several stations sit at fade
		// 1.0 at once during a long handover, and without the tie-break the caption would stick on
		// whichever of them came first in the ladder and never advance.
		const float Dc = FMath::Abs(ZoomProgress - StageCentre(N));
		if (F > Fade + 1e-4f || (FMath::Abs(F - Fade) <= 1e-4f && (i < 0 || Dc < BestD)))
		{ Fade = F; BestD = Dc; i = N; }
	}

	auto Line = [&](UTextRenderComponent* T, const TArray<FString>& Arr, const FLinearColor& Base, float Bright)
	{
		if (!T) return;
		T->SetText(FText::FromString(Arr.IsValidIndex(i) ? Arr[i] : FString()));
		// Vertex ALPHA is a DEAD lever on this text material (opacity = glyph coverage only; the component
		// does not carry a fadeable alpha into opacity — verified across three attempts, translucent + opaque
		// bases both). So fade the COLOUR from the defined text colour toward FadeToColor (the scene
		// background). The caption fades out BETWEEN stations, where the geometry has dissolved and the
		// flat background sits behind it, so the glyph blends into it and vanishes — no black, no pop.
		const float F = FMath::Clamp(Fade, 0.f, 1.f);
		FColor C = FMath::Lerp(FadeToColor, Base * Bright, F).ToFColor(true);
		C.A = 255;
		T->SetTextRenderColor(C);
	};
	Line(DetailTitle, StageTitle,      TextColor, 1.00f);
	Line(DetailSub,   StageSub,        TextColor, 0.80f);
	// The scale line carries the ACCENT, the same teal as the progress bar and the rules — so the
	// interface reads as one system rather than as a white block with a coloured line under it.
	Line(DetailScale, StageScaleLabel, AccentColor, 1.00f);
	Line(DetailProv,  StageProv,       TextColor, 0.45f);

	// "04 / 08" — where you are on the ladder. Nothing else on screen says this: DEPTH % on the left
	// is a continuous number and does not tell you how many stations are left.
	if (DetailIndex)
	{
		const FString Idx = (bShowStationIndex && i >= 0)
			? FString::Printf(TEXT("%02d / %02d"), i + 1, FMath::Max(StationCount, 1)) : FString();
		DetailIndex->SetText(FText::FromString(Idx));
		FColor C = FMath::Lerp(FadeToColor, TextColor * 0.40f, FMath::Clamp(Fade, 0.f, 1.f)).ToFColor(true);
		C.A = 255;
		DetailIndex->SetTextRenderColor(C);
	}

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
	// One star, StreakSegments instances. The chain is what lets a streak curve — see the note on
	// StreakSegments in the header. Instances for star k occupy [k*Seg, k*Seg + Seg).
	const int32 Seg = FMath::Clamp(StreakSegments, 1, 16);
	StarHistLen  = FMath::Clamp(StreakHistory, 4, 128);
	StarHistHead = 0;
	StarHist.SetNumUninitialized(StreakCount * StarHistLen);
	for (int32 k = 0; k < StreakCount; ++k)
	{
		const float R   = FMath::Sqrt(FMath::FRand()) * StreakRadius;   // uniform disc
		const float Ang = FMath::FRand() * 2.f * PI;
		const FVector P(StreakNear + FMath::FRand() * StreakRange, FMath::Cos(Ang) * R, FMath::Sin(Ang) * R);
		StarPos.Add(P);
		// Seed the whole history at the spawn point. A zero-filled buffer would draw every trail as
		// a line from the star back to the world origin on the first frames.
		for (int32 i = 0; i < StarHistLen; ++i) StarHist[k * StarHistLen + i] = P;
		for (int32 s = 0; s < Seg; ++s)
		{
			Streaks->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(0.3f, 0.05f, 0.05f)));
		}
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

	// A PARTICLE TRAIL, NOT A DRAWN CURVE.
	// Every earlier attempt computed the streak's shape from the CURRENT state — a signed length,
	// then a t^2 bow from the current orbit rate. Both share one flaw: the shape is a function of
	// this frame only, so the instant the orbit reverses, every streak in the field re-bends
	// together. Nothing in nature does that; it reads as the whole field flexing on a hinge.
	//
	// A real trail is memory. Each star records where it has been, and the ribbon is drawn THROUGH
	// those recorded points. Direction changes then enter at the star and travel down the ribbon as
	// the old samples age out — a reversal shows up as an S, because the tail is still the shape it
	// was drawn with. No term in here mentions the orbit at all: the shear moves the star, and the
	// history picks that up for free.
	const double Move = -(double)Vel * StreakSpeedScale * Dt;

	// Orbit drags the medium sideways. The field stays AIMED at the Anchor (that is what carries the
	// vanishing point onto the wall, so it must not be rotated off-axis) — instead the stars
	// themselves sweep across it, which is what an orbit through a dust field actually does. Field
	// local Y is right, Z is up; yaw sweeps horizontally, pitch vertically.
	// Doubles throughout: FVector components are doubles in UE5, and mixing them with float bounds
	// leaves FMath::Wrap's template argument ambiguous.
	const double ShearY = -(double)OrbitYawRate   * StreakOrbitShear * Dt;
	const double ShearZ =  (double)OrbitPitchRate * StreakOrbitShear * Dt;
	const double R      = (double)FMath::Max(StreakRadius, 1.f);

	// How much of the remembered path to draw. Intensity still sets the length, so all the existing
	// StreakLenScale / StreakLenMax tuning keeps its meaning — but it now trims the ribbon along the
	// real path instead of stretching a synthetic one, so a fade still retracts toward the star.
	const double Want = (double)FMath::Clamp(Intensity * StreakLenScale, 0.f, StreakLenMax);

	const int32 Seg   = FMath::Max(StreakSegments, 1);
	const int32 HLen  = StarHistLen;
	const int32 Count = FMath::Min3(Streaks->GetInstanceCount() / Seg, StarPos.Num(),
	                                HLen > 0 ? StarHist.Num() / FMath::Max(HLen, 1) : 0);
	StarHistHead = (StarHistHead + 1) % FMath::Max(HLen, 1);

	TArray<FVector, TInlineAllocator<136>> Path;      // newest -> oldest, trimmed to Want
	TArray<double,  TInlineAllocator<136>> Cum;       // arc length at each Path point

	for (int32 k = 0; k < Count; ++k)
	{
		FVector& P = StarPos[k];
		FVector* H = &StarHist[k * HLen];

		// Advance, and carry the WHOLE history through any wrap by the same delta. Moving only the
		// star would leave its recorded path on the far side of the field and draw a ribbon straight
		// across the screen. Shifting the history keeps the trail rigid through the teleport, which
		// is what the single-cube version did implicitly.
		FVector Delta(Move, ShearY, ShearZ);
		FVector Q = P + Delta;
		if      (Q.X < StreakNear)               Delta.X += StreakRange;
		else if (Q.X > StreakNear + StreakRange) Delta.X -= StreakRange;
		if (ShearY != 0.0)
		{
			const double Wr = FMath::Wrap(Q.Y, -R, R);
			Delta.Y += Wr - Q.Y;                       // wrap contributes its own jump to the delta
		}
		if (ShearZ != 0.0)
		{
			const double Wr = FMath::Wrap(Q.Z, -R, R);
			Delta.Z += Wr - Q.Z;
		}
		const bool bWrapped = (Delta.X != Move) || (Delta.Y != ShearY) || (Delta.Z != ShearZ);
		if (bWrapped)
		{
			const FVector Jump(Delta.X - Move, Delta.Y - ShearY, Delta.Z - ShearZ);
			for (int32 i = 0; i < HLen; ++i) H[i] += Jump;
		}
		P += Delta;
		H[StarHistHead] = P;

		// Walk backwards through the remembered path, accumulating arc length until Want is reached.
		Path.Reset();
		Cum.Reset();
		Path.Add(P);
		Cum.Add(0.0);
		double Acc = 0.0;
		for (int32 i = 1; i < HLen && Acc < Want; ++i)
		{
			const FVector& Older = H[(StarHistHead - i + HLen) % HLen];
			const double   D     = (Older - Path.Last()).Size();
			if (D <= KINDA_SMALL_NUMBER) continue;      // stationary frames add no path
			Acc += D;
			Path.Add(Older);
			Cum.Add(Acc);
		}

		// Resample that polyline into Seg equal pieces so the ribbon has even segments regardless of
		// how the frames happened to land.
		const double Total = Acc;
		auto At = [&Path, &Cum](double S) -> FVector
		{
			if (Path.Num() < 2) return Path[0];
			S = FMath::Clamp(S, 0.0, Cum.Last());
			int32 i = 1;
			while (i < Cum.Num() - 1 && Cum[i] < S) ++i;
			const double Seg0 = Cum[i] - Cum[i - 1];
			const double F    = (Seg0 > KINDA_SMALL_NUMBER) ? (S - Cum[i - 1]) / Seg0 : 0.0;
			return FMath::Lerp(Path[i - 1], Path[i], F);
		};

		FVector Prev = Path[0];
		for (int32 s = 0; s < Seg; ++s)
		{
			const FVector Cur = At(Total * (double)(s + 1) / (double)Seg);
			const FVector D   = Cur - Prev;
			const double  Mag = D.Size();
			// A zero-length piece has no direction and Rotation() would be meaningless, so it is
			// simply collapsed — which is also how a short trail hides its unused segments.
			const FRotator Rot = (Mag > KINDA_SMALL_NUMBER) ? D.Rotation() : FRotator::ZeroRotator;
			const FTransform T(Rot, (Prev + Cur) * 0.5,
			                   FVector(FMath::Max(Mag, 1.0) / 100.0, 0.05, 0.05));
			Streaks->UpdateInstanceTransform(k * Seg + s, T, /*bWorldSpace*/false,
			                                 /*bMarkDirty*/false, /*bTeleport*/false);
			Prev = Cur;
		}
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
	UE_LOG(LogTemp, Warning, TEXT("[QZoomStage] stage audio: %d continuous tracks (spatial=%d)"), StageAudio.Num(), (int)bSpatialAudio);
}

void AQZoomStagePawn::UpdateAudio()
{
	const int32 N = StageAudio.Num();
	int32 Live = 0;
	float Peak = 0.f;
	if (N == 0) { AudioLive = 0; AudioPeak = 0.f; return; }
	for (int32 i = 0; i < N; ++i)
	{
		if (!StageAudio[i]) continue;
		const float Centre = StageCentre(i);                               // this track's stage centre (incl. lead-in)
		const float D = FMath::Abs(ZoomProgress - Centre);
		float W = 1.f - FMath::Clamp(D / FMath::Max(StageAudioWidth, 1e-3f), 0.f, 1.f);
		W = W * W * (3.f - 2.f * W);                                        // smoothstep
		W = FMath::Max(W, AudioBed);                                        // continuous bed floor
		StageAudio[i]->SetVolumeMultiplier(W * MasterVolume);
		Peak = FMath::Max(Peak, W * MasterVolume);
		if (StageAudio[i]->IsPlaying()) ++Live;
	}
	// Published to the readout. "Sound is gone" has several causes that look identical from the
	// outside — no tracks spawned, tracks spawned but never playing, playing but faded to zero —
	// and they need completely different fixes. These two numbers separate them at a glance.
	AudioLive = Live;
	AudioPeak = Peak;
}

void AQZoomStagePawn::PushHudText(const FString& S)
{
	// ONE place decides what the interface says and where it is drawn. Both callers used to write
	// straight into the 3D component; routing them through here is what makes the pass a single
	// switch instead of two of them that can disagree.
	if (bHUDScreenSpace && HudTextWidget)
	{
		HudTextWidget->SetLine(S, TextColor, HUDScreenFontSize, HUDScreenPadding);
		HudTextWidget->SetVisibility(HUDMode == 0 ? ESlateVisibility::Collapsed
		                                          : ESlateVisibility::HitTestInvisible);
		// The 3D readout must go, or the same words are drawn twice — once graded and once not,
		// which reads as a ghost rather than as text. The big FPS counter goes with it: it is
		// anchored to the same corner and now lands ON the first lines, and its number is already
		// in the FPS row above. Enforced every frame, because SetCleanMode re-shows them.
		if (Readout && Readout->IsVisible()) Readout->SetVisibility(false);
		if (FpsBig  && FpsBig->IsVisible())  FpsBig->SetVisibility(false);
		return;
	}
	if (Readout) Readout->SetText(FText::FromString(S));
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
	// PresetTintStrength lerps the authored tint toward WHITE — 1 = as authored, 0 = the grade
	// keeps its exposure, contrast, bloom and vignette but stops recolouring. Applied here rather
	// than at each case, so the GradeComp calculation below reads the value that actually shipped
	// to the volume and the HUD compensation cannot drift out of step with it.
	auto Tint  = [&](float r, float g, float b)
	{
		S.bOverride_SceneColorTint = true;
		S.SceneColorTint = FMath::Lerp(FLinearColor::White, FLinearColor(r, g, b, 1.f),
		                               FMath::Clamp(PresetTintStrength, 0.f, 1.f));
	};
	switch (P)
	{
	case 1: Bloom(1.6f); DOF(2.0f); Temp(7600.f); Sat(0.88f); Con(1.10f); Vig(0.50f); Exp( 0.3f); break;                    // P1 Cinematic — warm, glow, shallow DOF
	case 2: Bloom(1.5f); Tint(1.0f, 0.22f, 0.18f); Temp(3200.f); Sat(1.30f); Con(1.28f); Vig(0.55f); Exp(0.1f); break;      // P2 Red Alert — crimson wash, warm, heavy vignette
	case 3: Bloom(2.6f); DOF(1.4f); Temp(4400.f); Sat(0.70f); Con(1.20f); Vig(0.85f); Exp(-0.7f); break; // P3 Deep Space — dark, heavy bloom+DOF
	case 4: Bloom(1.2f); DOF(1.8f); Temp(6200.f); Sat(1.55f); Con(1.15f); Vig(0.30f); Exp( 0.2f); break; // P4 Vivid — saturated showcase, mild DOF
	// P5 Infrared — SOFTENED. The old tint was (0.40, 0.02, 0.02): green and blue multiplied by a
	// fiftieth. Gold (1.00, 0.85, 0.15) came out at (0.400, 0.017, 0.003) and white at
	// (0.400, 0.020, 0.020) — the same red twice. The ratio between them was fine; the LEVEL was
	// not. Both sat so far under the contrast pivot that Con 1.60 finished the job, so no material
	// value could separate a golden sulfur from a white orbital: the grade had already thrown the
	// information away before the material's choice ever mattered.
	// Now G and B are lifted into a range the tonemapper can still resolve, and the contrast that
	// was crushing them is eased. Gold lands at (0.45, 0.136, 0.015), white at (0.45, 0.16, 0.10)
	// — same dominant red, but a blue channel that differs almost sevenfold and now survives.
	// Still infrared in character: red dominates by roughly three to one.
	case 5: Bloom(0.8f); Tint(0.45f, 0.16f, 0.10f); Temp(3400.f); Sat(0.95f); Con(1.30f); Vig(0.80f); Exp(-0.8f); break;
	case 6: Bloom(3.0f); Tint(0.14f, 0.55f, 1.00f); Temp(9800.f); Sat(1.10f); Con(1.25f); Vig(0.20f); Exp( 0.5f); break; // P6 Cyanotype — icy blueprint blue, big glow
	case 7: Bloom(4.0f); Sat(2.30f); Con(1.30f); Vig(0.00f); Exp( 1.2f); break;                                        // P7 Overdrive — blown-out neon, everything screaming
	case 8: Bloom(0.0f); Sat(0.00f); Con(1.90f); Vig(0.95f); Exp(-0.2f); break;                                        // P8 Noir — hard black & white, heavy vignette
	case 9: Bloom(2.6f); Tint(1.00f, 0.10f, 0.90f); Temp(5200.f); Sat(2.00f); Con(1.20f); Vig(0.35f); Exp( 0.3f); break; // P9 Psychedelic — magenta/green wash, max saturation
	default: break;  // 0 neutral — no overrides at all, the raw look
	}
	// ── THE MENU MUST BE READABLE, and one grade term cannot be compensated away.
	// GradeComp below pre-divides the text's emissive by the tint and the exposure, which is a
	// CONSTANT PER CHANNEL — and that is enough for anything the grade does uniformly. The
	// vignette is not uniform: it darkens by POSITION, hardest in the corners, which is exactly
	// where the readout and the bisect menu live. No constant multiplier can beat a gradient.
	// Bloom is the second one: text boosted a hundredfold to survive P5 smears into itself.
	// So while the menu is open both are suspended. Colour, exposure and contrast stay, so the
	// LOOK is still judgeable with the menu up — and their cost is a fixed full-screen pass
	// either way, so a frame-time comparison made with the menu open stays comparable.
	if (HUDMode == 2 && bMenuSuspendsVignette)
	{
		Vig(0.f);
		Bloom(0.f);
	}
	PPVolume->Settings = S;
	// Assigning a fresh FPostProcessSettings wiped WeightedBlendables, so if the (legacy, default-
	// off) stencil restore is in use it must be re-added. Idempotent.
	if (bHUDExemptFromPP && HUDPostMID) PPVolume->AddOrUpdateBlendable(HUDPostMID, 1.f);

	// GRADE COMPENSATION for the HUD text. The stencil/blendable restore was retired: the wall
	// viewport ignored the PP stencil test and any blendable at AFTER_TONEMAPPING disturbed the
	// nDisplay compositor (the border). Instead the text does what the HDR particles already do
	// to survive P5 — carry enough brightness that the grade lands it back where it was authored.
	// Emissive is pre-divided by the preset's tint and exposure; contrast still bends the result
	// slightly (reads as marginally brighter text, not a colour shift). Two honest limits: under
	// P8 Noir (saturation 0) no colour can survive — the text goes grey with the frame — and the
	// boosted value blooms a little, tunable via TextCompMax.
	if (TextMID)
	{
		const FLinearColor T = S.bOverride_SceneColorTint ? S.SceneColorTint : FLinearColor::White;
		const float Ex = S.bOverride_AutoExposureBias ? FMath::Pow(2.f, S.AutoExposureBias) : 1.f;
		// OVERDRIVE AS AN EXPONENT, not a factor. The particles survive P5 by CARRYING HDR, and
		// the text should do the same — but a flat multiplier would also blow the readout out on
		// P0 neutral, where there is nothing to compensate. Raising the compensation to a power
		// is inert exactly where the compensation is 1 (any preset that does not recolour) and
		// bites hardest where the grade bites hardest. One number, no special cases.
		auto Comp = [&](float c)
		{
			const float Need = 1.f / FMath::Max(c * Ex, 1e-3f);
			return FMath::Clamp(FMath::Pow(Need, FMath::Max(TextGradeCompPower, 1.f)),
			                    1.f, TextCompMax);
		};
		TextMID->SetVectorParameterValue(TEXT("GradeComp"),
			FLinearColor(Comp(T.R), Comp(T.G), Comp(T.B), 1.f));
	}
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
	// The component is still created so the mode switch, the colour writes and the fade all keep a
	// valid target; it is simply left EMPTY when the motes are off, which costs nothing to draw.
	UInstancedStaticMeshComponent* Motes = Ensure(FillerMotes, Sph, Emis);
	if (bFillerMotes)
	{
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
	}
	// 2) ENZYME-WORMS — long swirling backbone chains threading the void.
	// Same gate as the motes, and for the same reason: these beads are SPHERES too, so leaving them
	// on is indistinguishable from leaving the motes on. The component is still created so the mode
	// switch, the colour writes and the fade keep a valid target; it is just left empty.
	UInstancedStaticMeshComponent* Worm = Ensure(FillerStruct, Sph, Emis);
	if (bFillerStruct)
	{
		const int32 NWorm = FMath::RoundToInt(22 * dens);
		for (int32 i = 0; i < NWorm; ++i)
		{
			const FVector s(R.FRandRange(-6000.f, 6000.f), R.FRandRange(-6000.f, 6000.f), R.FRandRange(-6000.f, 6000.f));
			AddWorm(Worm, s, R.FRandRange(800.f, 1600.f), 24, R.FRandRange(0.55f, 0.95f));
		}
	}
	FillerGrid = nullptr;   // grid retired — the medium is now proteins + enzymes
	// DMIs so we can drive the fade per frame — and, now that the medium runs on M_StationMaster, so it has a
	// real authored BaseColor for the palette to squeeze FROM. Two different colours on purpose: the proteins
	// sit on the blue pole and the enzymes on the amber one, so the medium reads as part of the same palette
	// rather than a separate layer, and it responds to the squeeze like everything else.
	if (Motes) FillerMotesMID  = Motes->CreateDynamicMaterialInstance(0);
	if (Worm)  FillerStructMID = Worm ->CreateDynamicMaterialInstance(0);
	if (FillerMotesMID)  FillerMotesMID ->SetVectorParameterValue(TEXT("BaseColor"),
		FillerColorMotes * FillerBrightness);
	if (FillerStructMID) FillerStructMID->SetVectorParameterValue(TEXT("BaseColor"),
		FillerColorStruct * FillerBrightness);
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
	if (FillerMotes)  FillerMotes ->SetVisibility(bFillerMotes && (M == 1 || M == 3));   // proteins
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
	// The growth anchor below still reads the cell's centre — the medium's SIZE stays tied to the
	// world's own curve, only its presence became authored.
	const float c2 = StageCentre(2);
	// AUTHORED, not derived. The band used to be read off the station centres, so it moved
	// whenever the ladder moved and could not be dialled on its own. It is a dramatic decision.
	const float rise = FMath::Clamp((ZoomProgress - FillerInAt)
	                                / FMath::Max(FillerFullAt - FillerInAt, 1e-3f), 0.f, 1.f);
	const float fall = FMath::Clamp((FillerGoneAt - ZoomProgress)
	                                / FMath::Max(FillerGoneAt - FillerOutAt, 1e-3f), 0.f, 1.f);
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
	// Size and growth rate are two different things. fs is HOW FAST the medium tracks the zoom;
	// FillerSizeScale is HOW BIG each instance is. They used to be one number, so making the
	// medium follow the zoom properly also inflated it.
	const FVector FScale(fs * FMath::Max(FillerSizeScale, 0.01f));
	const bool bBand = presence > 0.005f;

	// PROTEINS — gentle drift with a soft two-frequency wobble (alive, but unhurried).
	if (FillerMotes)
	{
		FillerMotes->SetVisibility(bFillerMotes && bBand && (FillerMode == 1 || FillerMode == 3));
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
