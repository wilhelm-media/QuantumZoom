#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "QZoomStagePawn.generated.h"

class UCameraComponent;
class USceneComponent;
class UTextRenderComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USoundBase;
class UAudioComponent;
class APostProcessVolume;
class ULightComponent;

/**
 * AQZoomStagePawn — "the world scales, the camera stays."
 *
 * The camera NEVER translates, so your nDisplay DCRA can sit still and never needs driving (this is
 * what fixes the "view doesn't follow" problem — there is no camera path any more).
 *
 * All stations sit CONCENTRIC at Anchor. ZoomProgress (0..1, Right/Left trigger or W/S) drives a
 * continuous NESTED scale: station N is frame-filling only near its own ZoomProgress centre
 * (N/(StationCount-1)), a tiny dot before it, and huge (hidden) after it. So zooming grows the
 * current scale up while the next scale blooms out of its centre — the Eames "Powers of Ten" move,
 * done by scaling content instead of flying through it. No fly-through, no float drift.
 *
 * Look-around (right stick) ORBITS the content about the anchor — camera stays put → DCRA stays put.
 *
 * Station content lives in streaming sublevels, each hero actor TAGGED "QZStation" plus a numeric
 * tag = its station index. This pawn finds those, and sets scale + visibility every frame.
 */
UCLASS()
class QZOOM_API AQZoomStagePawn : public APawn
{
	GENERATED_BODY()

public:
	AQZoomStagePawn();

	/** Where the concentric stations sit, in front of the fixed camera. */
	UPROPERTY(EditAnywhere, Category="QZoomStage")
	FVector Anchor = FVector(1500.f, 0.f, 120.f);

	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="2", ClampMax="12"))
	int32 StationCount = 6;

	/** 0..1 across all stations. Trigger-driven on the primary node, cluster-synced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="QZoomStage", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ZoomProgress = 0.f;

	/** Zoom speed (progress/sec at full trigger) with eased accel/decel. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.005", ClampMax="1.0"))
	float BaseZoomRate = 0.05f;

	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.5", ClampMax="20.0"))
	float SpeedDamping = 3.5f;

	/** Steepness of the nested scale: displayScale = exp((ZoomProgress - stationCentre) * ZoomK).
	 *  LOWER = stations overlap more (next detail is already growing while the current engulfs+dissolves →
	 *  continuous zoom, no empty gap); higher = snappier but leaves dead space between scales. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="1.0", ClampMax="80.0"))
	float ZoomK = 12.f;

	/** Hide a station whose display scale leaves this band (below = a dot, above = engulfing you). Low
	 *  MinVis = the next station appears early (as a growing detail) so there's never an empty frame. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.001"))
	float MinVisScale = 0.02f;
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="1.0"))
	float MaxVisScale = 18.f;

	/** Lead-in before the first station: at ZoomProgress 0 you start OUTSIDE S0's dissolve-in (S0 blooms in
	 *  as you begin zooming). Shifts every stage centre into [ZoomLeadIn, 1]. 0 = start right on S0 as before. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.0", ClampMax="0.4"))
	float ZoomLeadIn = 0.15f;

	/** Cross-fade width (natural-log scale units) at each edge of the visible band — the pawn pushes a
	 *  'StationFade' 0..1 onto each station's materials so prev/next dissolve instead of popping.
	 *  Materials without a StationFade param just hard-hide as before. Bigger = longer dissolve. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.1", ClampMax="6.0"))
	float StationFadeWidth = 2.2f;

	/** Shader warm-up: for this many frames at start, EVERY station is rendered once — dissolved to nothing at
	 *  a subpixel scale — so its material shaders/PSOs compile up front instead of flashing the default grey
	 *  material for a few frames the first time it enters the band. 0 = off. ~90 = ~1.5 s at 60 fps. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0"))
	int32 ShaderWarmupFrames = 90;
	int32 WarmupLeft = -1;   // runtime counter, lazy-init from ShaderWarmupFrames on first ApplyStations

	/** Look-around orbit speed (deg/sec at full stick) + pitch clamp. */
	UPROPERTY(EditAnywhere, Category="QZoomStage")
	float OrbitRate = 60.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.0", ClampMax="89.0"))
	float MaxPitch = 70.f;

	// ── Constant scale/zoom readout ───────────────────────────────────────────
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	TArray<float> ScaleMeters = {0.09f, 3e-6f, 1e-8f, 2e-10f, 1e-10f, 8e-15f};
	/** Camera-relative (forward, right, up). Negative right = LEFT; positive up keeps it high on the
	 *  WALL and out of the floor frustum. Default = top-left of the wall. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	FVector ReadoutOffset = FVector(900.f, -650.f, 520.f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	float ReadoutSize = 26.f;

	/** Always-on-top unlit text material (M_ReadoutText). Auto-loaded if empty; used for readout + detail. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	TObjectPtr<UMaterialInterface> ReadoutMaterial;

	/** Text colour for the whole zoom interface (readout + detail). Default white. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	FLinearColor TextColor = FLinearColor::White;

	/** The per-stage detail text fades toward THIS colour as it leaves (vertex alpha can't fade the text
	 *  material, so we blend the COLOUR into the scene background instead — set this to the DCRA clear/
	 *  background colour so the glyphs vanish between stations). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	FLinearColor FadeToColor = FLinearColor(0.02f, 0.035f, 0.03f);

	// ── Observer model (drives the Size/Speed readout + streak intensity) ──────
	// You begin human-scale and shrink to keep the hero object wall-sized. Holding a fixed real-world
	// pace as you shrink makes the RELATIVE speed (pace / size) blow up with depth — that is the cue.
	/** Observer size at the start of the zoom (metres) — human scale. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Observer", meta=(ClampMin="0.001")) float ObserverStartSize = 1.70f;
	/** Reference real-world pace (m/s) the observer keeps as it shrinks — source of the relative-speed blow-up. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Observer", meta=(ClampMin="0.0")) float ObserverRefSpeed = 0.10f;

	// ── Info layer (fixed in view, fades in per stage, decoupled from the scaling geometry) ──
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageTitle;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageSub;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageScaleLabel;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageProv;

	/** Camera-relative anchor of the detail block (forward, right, up) — FIXED; never moves with geometry. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") FVector DetailOffset = FVector(950.f, 480.f, 220.f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") float DetailSize = 34.f;
	/** Half-width in ZoomProgress of the fade window around each stage centre. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info", meta=(ClampMin="0.02", ClampMax="0.5"))
	float InfoFadeWidth = 0.09f;

	// ── Info BACKGROUND (separate depth layer → tunable stereo pop) ────────────
	// Editorial layout default: OFF (no boxes — hairline rules carry the structure). Flip on for the panelled look.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Background") bool bShowBackground = false;
	/** THE stereo knob: how far BEHIND the text the panel sits. Bigger = stronger parallax pop — but more
	 *  text/panel separation in a MONO preview (it fuses in stereo). Lower it if the panel looks offset. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Background") float BackgroundDepthOffset = 120.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Background") FLinearColor BackgroundColor = FLinearColor(0.f, 0.02f, 0.06f, 0.55f);
	/** Panel CENTRE in the view plane (right, up), camera-relative — nudge to sit each panel behind its text. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Background") FVector2D ReadoutBGCenter = FVector2D(-545.f, 442.f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Background") FVector2D DetailBGCenter  = FVector2D(730.f, 130.f);
	/** Panel size in world units (width, height). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Background") FVector2D ReadoutBGSize = FVector2D(320.f, 210.f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Background") FVector2D DetailBGSize = FVector2D(580.f, 250.f);

	// ── Editorial accent (single brand colour; rules + progress as thin emissive GEOMETRY, not glyphs) ──
	/** Single brand accent for the hairline rules + progress fill (Wilhelm teal #02E8D3). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Accent") FLinearColor AccentColor = FLinearColor(0.0f, 0.806f, 0.648f);
	/** Hairline thickness (world units) of the rules + progress bar. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Accent", meta=(ClampMin="0.25")) float RuleThickness = 3.0f;
	/** Width (world units) of the readout progress track and the detail-title underline rule. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Accent") float ReadoutRuleWidth = 300.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Accent") float DetailRuleWidth  = 520.f;
	/** Dimming of the progress TRACK relative to the fill (0..1). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Accent", meta=(ClampMin="0.0", ClampMax="1.0")) float TrackDim = 0.18f;
	/** Optional serif UFont for the specimen title (editorial). Loaded from /Game/QuantumZoom/BLOCKOUT/_fonts/F_Cormorant if empty. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info|Accent") TObjectPtr<UFont> TitleFont = nullptr;

	// ── Zoom-speed particle streaks ───────────────────────────────────────────
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") int32 StreakCount = 220;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") float StreakRadius = 1300.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") float StreakRange = 3200.f;
	/** Star drift speed: world units the stars travel per unit zoom-velocity (the flow past you). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") float StreakSpeedScale = 30000.f;
	/** Streak length ramp with intensity (0 -> a dot, 1 -> ~StreakLenMax). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") float StreakLenScale = 850.f;
	/** Max streak length — keep well below StreakRange so streaks don't span the whole field. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") float StreakLenMax = 900.f;
	/** Near clip: stars that pass this X wrap to the far side. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") float StreakNear = 60.f;
	/** Relative speed (observer-sizes/sec) at which streaks BEGIN — the macro start stays clean below this.
	 *  Depth-dependence emerges from the physics (relative speed climbs as the observer shrinks). Wide
	 *  Lo->Hi range = gentle build over the deep stages. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="1.0")) float StreakSpeedLo = 1.0e5f;
	/** ...and reach full strength (observer-sizes/sec). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="10.0")) float StreakSpeedHi = 1.0e12f;
	/** Temporal fade speed of the streaks — LOW = gentle glide in/out (1.5 ~ 1s), high = snappy. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="0.2", ClampMax="12.0")) float StreakFade = 1.5f;
	// Retract-on-stop rate (higher = snappier). Decoupled from StreakFade so build-up stays gentle. Tune live in Details.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="0.5", ClampMax="20.0")) float StreakFadeOut = 3.5f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") FLinearColor StreakColor = FLinearColor(0.6f, 0.8f, 1.0f, 1.0f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") TObjectPtr<UStaticMesh> StreakMesh;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks") TObjectPtr<UMaterialInterface> StreakMaterial;

	// ── Stage soundscape ─────────────────────────────────────────────────────
	// One looping track per stage, index-aligned to the stations. Every track plays CONTINUOUSLY from
	// BeginPlay (never re-triggered — that was the unreliable part); only VOLUME is cross-faded by zoom.
	// Import the SoundWaves with bVirtualizeWhenSilent so a volume-0 track keeps running instead of stopping.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Audio") TArray<TObjectPtr<USoundBase>> StageSounds;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Audio", meta=(ClampMin="0.0", ClampMax="4.0")) float MasterVolume = 1.0f;
	/** Half-width in ZoomProgress of each track's volume envelope (bigger = more overlap/crossfade between stages). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Audio", meta=(ClampMin="0.03", ClampMax="0.6")) float StageAudioWidth = 0.18f;
	/** Floor volume every track keeps (0 = fully silent between stages; small value = a continuous bed). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Audio", meta=(ClampMin="0.0", ClampMax="1.0")) float AudioBed = 0.0f;
	/** Spatialise each track at the Anchor (sound comes from the zoom target ahead) vs flat 2D. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Audio") bool bSpatialAudio = true;

	/** Read live by AZoomStreamer for load/unload decisions. */
	float GetZoomProgress() const { return ZoomProgress; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float Dt) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent>      Root = nullptr;
	UPROPERTY() TObjectPtr<UCameraComponent>     Camera = nullptr;
	UPROPERTY() TObjectPtr<UTextRenderComponent> Readout = nullptr;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DetailTitle = nullptr;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DetailSub = nullptr;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DetailScale = nullptr;
	UPROPERTY() TObjectPtr<UTextRenderComponent> DetailProv = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Streaks = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StreakMID = nullptr;
	float StreakIntensitySmoothed = 0.f;   // time-smoothed for a gentle fade in/out
	float StreakDir = 1.f;                 // LATCHED flow sign; holds through the fade so a stop retracts to ONE end (not the raw sign(VisVel) which snaps to 0 on release and re-centres the streak)
	TArray<FVector> StarPos;               // star positions (decoupled from the rendered, anchored cube)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ReadoutBG = nullptr;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> DetailBG = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ReadoutBGMID = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DetailBGMID = nullptr;
	// Editorial hairline rules + progress bar (thin emissive geometry, camera-parented).
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ReadoutBar = nullptr;       // progress track (dim)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ReadoutBarFill = nullptr;   // progress fill (accent, width scales with progress)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> DetailRule = nullptr;       // hairline under the specimen title
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ReadoutBarMID = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ReadoutBarFillMID = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DetailRuleMID = nullptr;
	float ReadoutBarLeft = 0.f, ReadoutBarUp = 0.f, ReadoutBarFwd = 0.f;     // cached track geometry for the fill
	UPROPERTY() TArray<TObjectPtr<UAudioComponent>> StageAudio;             // one per StageSounds (index-aligned), always playing; volume-faded

	// ── Cinematic post-processing: 4 presets, cycle with Up/Down arrows (0 Clean / 1 Cinematic / 2 Scientific / 3 Deep Space) ──
	int32 PPPreset = 0;
	UPROPERTY() TObjectPtr<class APostProcessVolume> PPVolume = nullptr;
	// ── Scientific space fillers: cycle with F (0 off / 1 motes / 2 grid / 3 structures) ──
	int32 FillerMode = 3;   // default: full medium (proteins + enzymes), scale-gated to the molecular band
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FillerMotes = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FillerGrid = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FillerStruct = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FillerMotesMID = nullptr;   // drives FillerFade (opacity)
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FillerStructMID = nullptr;
	float FillerSwirl = 0.f;    // accumulated self-rotation of the filler clouds (deg), on top of the orbit
	int32 FillerDensity = 1;    // index into the density table (0 sparse .. 4 EXTREME); A/B step it
	bool bUpPrev = false, bDownPrev = false, bLeftPrev = false, bRightPrev = false;   // D-Pad rising-edge latches
	bool bLBPrev = false, bRBPrev = false;                                            // LB/RB style-light latches
	bool bAPrev  = false, bBPrev  = false;                                            // A/B density latches
	int32 NiraVersion = 1;   // S2 NirA representation: 0 high-res FBX / 1 low-res FBX (default) / 2 procedural
	bool bXPrev = false;     // X button (version cycle) latch

	// STYLE LIGHT — the freed S4 key light, dialled on the LB/RB shoulders for on-site look variation.
	// Absolute intensity ladder (Michael's spec): 0 off / 2k / 5k / 10k / 25k / 50k(=current, full). LB down, RB up.
	int32 StyleLightStep = 5;    // default = 50k (matches the current authored intensity)
	TWeakObjectPtr<ULightComponent> StyleLight;   // cached, found by the 'QZStyleLight' actor tag

	// CLEAN MODE — Y hides the whole editorial HUD for clean photography plates (cluster-synced).
	bool bCleanMode = false;
	bool bYPrev = false;

	// Per-frame light fade: each station sublevel's lights fade with that station's visibility (no pop-in).
	TMap<ULevel*, float> LevelFade;                                   // rebuilt each frame in ApplyStations
	TMap<TWeakObjectPtr<ULightComponent>, float> LightBaseIntensity;  // captured base intensity, persistent

	float OrbitYaw = 0.f, OrbitPitch = 0.f, ZoomVel = 0.f;
	float PrevZoom = 0.f;      // for a cluster-consistent visual velocity (ZoomProgress delta, valid on every node)
	float ObserverSize = 1.70f;
	float ObserverSpeed = 0.f; // relative speed: observer-sizes / second
	bool  bIsPrimary = false, bInCluster = false;
	FOnClusterEventJsonListener ClusterListener;
	static const FString EventName;

	void    PollInput(float Dt);      // primary only
	void    Broadcast();              // primary -> all nodes
	void    OnClusterEvent(const FDisplayClusterClusterEventJson& E);
	void    ApplyStations();          // scale/hide/orbit the tagged station heroes (every node)
	void    SetStationFade(AActor* A, float Fade);   // push StationFade onto an actor's materials
	void    UpdateReadout();
	void    UpdateInfoLayer();         // fixed per-stage detail text, faded by proximity to a stage centre
	void    InitStreaks();
	void    UpdateStreaks(float Dt, float Vel, float Intensity);   // speed particles, 0..1 intensity
	void    InitStageAudio();     // primary/PIE only: spawn one continuous looping source per stage track
	void    UpdateAudio();        // cross-fade each track's volume by ZoomProgress proximity to its stage centre
	void    ApplyPPPreset(int32 P);   // write one of the 4 cinematic post-process presets onto PPVolume
	void    InitFillers();            // build the molecular space-filler ISMs (deterministic scatter)
	void    SetFillerMode(int32 M);   // 0 off / 1 proteins / 2 enzymes / 3 full medium
	void    UpdateFillers(float Dt);  // per-frame: inherit orbit yaw/pitch + self-swirl + zoom-scale
	void    UpdateLights();           // fade each station-sublevel light by its station's visibility
	void    ApplyStyleLight();        // set the style light's intensity from StyleLightStep
	class ULightComponent* GetStyleLight();   // find+cache the 'QZStyleLight'-tagged light
	void    SetCleanMode(bool bOn);   // hide/show the whole editorial HUD (readout + specimen text + rules/bar)
	float   StationScale(int32 N) const;
	float   StageCentre(int32 N) const;   // ZoomProgress centre of stage N, incl. the ZoomLeadIn shift
	float   CurrentScaleMeters() const;
	FString FormatScale(float Metres) const;
	FString FormatRate(float PerSec) const;   // SI-suffixed rate for the Observer Speed readout
	FString FormatZoom(float Power) const;    // 10^Power -> "m.m × 10^e" with superscript exponent
	static FString Superscript(int32 N);      // integer -> superscript-glyph string
	void    SetupAccentMesh(class UStaticMeshComponent* M, TObjectPtr<class UMaterialInstanceDynamic>& MID,
	                        class UStaticMesh* Cube, class UMaterialInterface* Mat, const FLinearColor& Col, float Bright);
};
