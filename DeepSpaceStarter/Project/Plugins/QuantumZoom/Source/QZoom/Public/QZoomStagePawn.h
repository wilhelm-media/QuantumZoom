#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "QZoomStagePawn.generated.h"

/**
 * FQZHandover — the SIMPLIFIED per-station tuning set Michael asked for (item 4). One entry per station.
 * Everything that shapes how a station HANDS OVER from the previous one, in one place, instead of the four
 * scattered globals (StationZoomK / MinVisScale / MaxVisScale / StationFadeWidth). Leave Enabled off to
 * fall back to the global behaviour for that station.
 */
USTRUCT(BlueprintType)
struct FQZHandover
{
	GENERATED_BODY()

	/** Use this per-station override. Off = the station uses the global StationZoomK / MinVis / fade values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover") bool bEnabled = false;

	/** TIMING — how steep this station's growth is. Higher = it stays a tiny dot longer, then blooms fast
	 *  (later, snappier handover). This is the per-station ZoomK. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="1.0", ClampMax="80.0")) float Timing = 12.f;

	/** INITIAL RESIZE — how small the station first appears (its 'dot' size when it blooms in). Smaller =
	 *  it starts tinier and further away. This is the per-station MinVisScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="0.001", ClampMax="1.0")) float InitialSize = 0.03f;

	/** FADE IN — width of the appear ramp (log-scale). Bigger = a longer, softer dissolve-IN. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="0.1", ClampMax="6.0")) float FadeIn = 2.2f;

	/** DISSOLVE (out) — the scale at which the station fades OUT as the next one takes over. Bigger = it
	 *  lingers longer before dissolving. This is the per-station MaxVis (like the QZMaxVis tag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="1.0")) float Dissolve = 35.f;
};

/**
 * FQZComms — one station's share of the COMMUNICATION layer.
 *
 * The through-line is that everything on the ladder is talking: a mycelium signalling across a
 * colony, a spore broadcasting, a protein passing electrons, a nucleus exchanging gluons. Same
 * gesture at every scale — packets running OUTWARD along lanes, like traffic on routes rather
 * than a uniform cloud, so it reads as directed communication and not as dust.
 *
 * Purely aesthetic. Nothing here claims to model a real signalling pathway, and the numbers are
 * chosen for how each stage should FEEL, not measured from anything.
 *
 * Stations differ by RHYTHM and DENSITY, never by being a different idea — that is what keeps
 * thirteen decades feeling like one continuous piece rather than seven unrelated effects.
 */
USTRUCT(BlueprintType)
struct FQZComms
{
	GENERATED_BODY()

	/** Discrete routes out of the centre. Few = sparse, legible traffic; many = a busy network. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0", ClampMax="64"))
	int32 Lanes = 14;

	/** Packets in flight per lane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0", ClampMax="80"))
	int32 PerLane = 22;

	/** Trips per second along a lane. Higher = more urgent traffic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.005", ClampMax="4.0"))
	float Speed = 0.16f;

	/** Packet length in uu; they are drawn as dashes aligned to travel, not as dots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="1.0"))
	float LengthUU = 90.f;

	/** Dash thickness as a fraction of its length. Low = a fast streak, high = a chunky packet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.02", ClampMax="1.0"))
	float Thickness = 0.14f;

	/** How far a lane wanders off straight. 0 = radial spokes, high = a loose organic spray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Wander = 0.18f;

	/** Helical twist along the run, so traffic curves instead of firing straight out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="3.0"))
	float Twist = 0.4f;

	/** 0 = continuous flow. Above 0, packets leave in BURSTS — the higher the gappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="0.95"))
	float Burst = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms")
	FLinearColor Color = FLinearColor(0.55f, 0.85f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="30.0"))
	float Brightness = 4.0f;
};

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
class UHeterogeneousVolumeComponent;

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
	int32 StationCount = 7;

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

	/** PER-STATION zoom steepness — the dial for how each individual leg FEELS. Index-aligned to the stations;
	 *  any entry <= 0 (or a short/empty array) falls back to the global ZoomK, so leaving this empty preserves
	 *  the old uniform behaviour exactly.
	 *
	 *  WHY PER-STATION AND NOT ONE GLOBAL SLIDER: the legs are wildly unequal in reality —
	 *      S0->S1  9cm -> 3um    ~4.5 decades
	 *      S1->S2  3um -> 10nm   ~2.5
	 *      S2->S3  10nm -> 0.2nm ~1.7
	 *      S3->S4  0.2nm -> 1A   ~0.3   <- barely a scale change: a shift of DESCRIPTION, not a dive
	 *      S4->S5  1A -> 8fm     ~6.1   <- the huge plunge into the subatomic void
	 *  StageCentre() spaces all six EVENLY, so a single K gives the 0.3-decade step and the 6.1-decade abyss
	 *  identical travel. Lower K on S4 = slow, cinematic, perceptual. Higher K on S5 = dramatic bridging of
	 *  the real distance. One global knob cannot express both.
	 *
	 *  NOTE: MinVisScale/MaxVisScale/QZMaxVis are log-scale bands that assume a uniform K, so changing a
	 *  station's K also moves WHEN it fades in/out — expect to re-tune its QZMaxVis after a big change. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Zoom Feel")
	TArray<float> StationZoomK;

	/** SIMPLIFIED per-station handover tuning (item 4): timing / initial resize / fade-in / dissolve, one
	 *  entry per station. An entry with bEnabled=true overrides the scattered globals for that station.
	 *  Index-aligned to the stations (0=Lab .. 5=Quarks). Empty/disabled = old global behaviour. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Handover")
	TArray<FQZHandover> Handover;

	/** Global multiplier on every station's effective K — the single "zoom intensity" dial, on top of whatever
	 *  StationZoomK/ZoomK resolve to. 1 = as authored. Handy for dialling the whole descent in one go. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Zoom Feel", meta=(ClampMin="0.1", ClampMax="4.0"))
	float ZoomIntensity = 1.0f;

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

	/** Space the stations by their PHYSICAL SCALE instead of by their index.
	 *
	 *  Off (the original behaviour) StageCentre is N/(StationCount-1): every leg gets an equal slice of
	 *  ZoomProgress no matter how far it actually travels. ScaleMeters is never consulted. With the current
	 *  ladder that means S0->S1 covers 4.48 decades in the same time S3->S4 covers 0.30 — the apparent zoom
	 *  rate swings by 15x, which is what reads as the descent surging and stalling.
	 *
	 *  On, each station sits at its log-scale position, so equal ZoomProgress == equal decades == a constant
	 *  perceived rate of magnification. That is the Powers-of-Ten pacing a continuous zoom needs to feel
	 *  credible. It does NOT require renumbering and does not touch any station.
	 *
	 *  Left as a switch on purpose: flip it in the Details panel to A/B the two pacings on the wall without
	 *  a rebuild. */
	UPROPERTY(EditAnywhere, Category="QZoomStage")
	bool bLogSpacedStations = true;

	/** CH4 REDOX CYCLE — FmoB docks and oxidises Met169, a reductase strips the oxygen back off.
	 *  Loops forever while the NirA station is in view, which is also the honest depiction: the
	 *  switch is REVERSIBLE, so a one-shot event would misrepresent it.
	 *
	 *  Driven off ZoomProgress, not a wall clock: the phase only advances while the station is
	 *  actually in its visibility band, so arriving at NirA starts the reaction and leaving
	 *  freezes it rather than letting it run unseen.
	 *
	 *  DOCK POSITIONS ARE SURFACE CONTACT ON THE Met169 AXIS, and that is a correction, not a
	 *  guess. The CH4 scene docked FmoB 11.49 nm from NirA's centre, but two non-overlapping
	 *  bodies of radius 8.6 and 6.0 nm cannot be closer than 14.60 — it overlapped the protein
	 *  by 3.11 nm. Worse, the two active sites cannot meet at all as modelled: FAD sits 0.8 nm
	 *  inside FmoB and Met169 2.83 nm inside NirA, so oxygen transfer would need the centres
	 *  3.93 nm apart against a hard floor of 14.60 — short by 10.67 nm.
	 *
	 *  The resolution is in the biology. Met169 sits in a leucine-rich NES on a FLEXIBLE linker
	 *  (AlphaFold pLDDT 66) that the NiRD domain masks and unmasks; oxidation REQUIRES it
	 *  presented outward. MICAL, the real precedent for enzymatic methionine oxidation, targets
	 *  Met44/Met47 in actin's D-loop — surface-exposed residues. So the enzyme meets a presented
	 *  residue at the surface, and surface contact along the Met169 axis is the defensible pose. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4")
	bool bCH4Cycle = true;

	/** Seconds for one full oxidise-and-recover cycle. 90 matches the Blender pre-vis. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="5.0", ClampMax="600.0"))
	float CH4CycleSeconds = 90.f;

	/** Which station the reaction belongs to. 3 = NirA: at Met169 (4) you are inside the residue
	 *  and the whole enzymes are off screen. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="0", ClampMax="11"))
	int32 CH4Station = 3;

	/** Surface contact on the Met169 axis, in the station pivot's local space (uu). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4")
	FVector CH4FmobDock = FVector(1435.f, -2537.f, -385.f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4")
	FVector CH4MsraDock = FVector(1288.f, -2277.f, -345.f);

	/** How far out along the axis they wait between turns. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="500.0"))
	float CH4ApproachUU = 9000.f;

	/** Swing the reductase's approach off the oxidase's line so the two arrivals read as separate
	 *  events. Both still dock on the same site, which is correct — they act on the same residue. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="0.0", ClampMax="180.0"))
	float CH4MsraApproachDeg = 60.f;

	/** REFERENCE PARTICLES — a scale reference, not decoration.
	 *
	 *  The descent has legs with nothing in them: S5->S6 is 4.10 decades of genuinely empty atom
	 *  and 27% of the runtime. With nothing passing the eye, a continuous zoom reads as a freeze
	 *  or a cut however evenly it is paced.
	 *
	 *  Radii are LOG-spaced and advance with ZoomProgress, which is the only distribution that
	 *  keeps apparent density constant in a logarithmic zoom — linear spacing would bunch up at
	 *  one end and thin out at the other as you descend. Each mote's size grows in proportion to
	 *  its radius, so its ANGULAR size stays constant and the field reads as self-similar: matter
	 *  structured the same way at every scale, which is the actual claim of a powers-of-ten zoom.
	 *  Motes fade in at the far end and out at the near end, so recycling never pops. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles")
	bool bRefParticles = true;

	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles", meta=(ClampMin="0", ClampMax="4000"))
	int32 RefParticleCount = 700;

	/** Inner / outer radius of the shell, in uu. Outer should sit beyond the station envelope so
	 *  motes leave frame rather than vanishing mid-view. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles", meta=(ClampMin="10.0"))
	float RefParticleMinUU = 260.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles", meta=(ClampMin="100.0"))
	float RefParticleMaxUU = 14000.f;

	/** How many times the whole field recycles across the full descent. Higher = faster streaming. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles", meta=(ClampMin="1.0", ClampMax="200.0"))
	float RefParticleCycles = 42.f;

	/** Mote size as a fraction of its own radius. Constant angular size comes from this being fixed. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles", meta=(ClampMin="0.0005", ClampMax="0.08"))
	float RefParticleSizeFrac = 0.006f;

	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles", meta=(ClampMin="0.0", ClampMax="20.0"))
	float RefParticleBrightness = 2.2f;

	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles")
	FLinearColor RefParticleColor = FLinearColor(0.62f, 0.78f, 1.0f);

	/** Keep a cone clear of the view axis so motes never sit on top of the hero. 0 = no clearing. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Reference Particles", meta=(ClampMin="0.0", ClampMax="0.9"))
	float RefParticleClearAxis = 0.22f;

	/** COMMUNICATION LAYER — one entry per station, index-aligned.
	 *
	 *  Everything on this ladder is talking to something, so every stage carries the same gesture:
	 *  packets running outward along lanes. What changes between stations is rhythm and density,
	 *  not the idea — which is what makes thirteen decades read as one piece.
	 *
	 *  Defaults are filled in the constructor. Aesthetic only; nothing here models a real pathway. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Comms")
	bool bCommsStreams = true;

	UPROPERTY(EditAnywhere, Category="QZoomStage|Comms")
	TArray<FQZComms> Comms;

	/** Inner and outer radius of the traffic, as a fraction of the station's own visible size, so
	 *  the streams stay tied to the object at every scale instead of drifting off it. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Comms", meta=(ClampMin="0.05", ClampMax="3.0"))
	float CommsInnerFrac = 0.42f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Comms", meta=(ClampMin="0.2", ClampMax="8.0"))
	float CommsOuterFrac = 2.6f;

	/** Cross-fade width (natural-log scale units) at each edge of the visible band — the pawn pushes a
	 *  'StationFade' 0..1 onto each station's materials so prev/next dissolve instead of popping.
	 *  Materials without a StationFade param just hard-hide as before. Bigger = longer dissolve. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.1", ClampMax="6.0"))
	float StationFadeWidth = 2.2f;

	/** Shader warm-up: for this many frames at start, EVERY station is rendered once — dissolved to nothing at
	 *  a subpixel scale — so its material shaders/PSOs compile up front instead of flashing the default grey
	 *  material for a few frames the first time it enters the band. 0 = off. ~90 = ~1.5 s at 60 fps. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0"))
	int32 ShaderWarmupFrames = 240;
	int32 WarmupLeft = -1;   // runtime counter, lazy-init from ShaderWarmupFrames on first ApplyStations

	/** Scale used to prime a not-yet-visible station during warm-up. MUST stay subpixel (1e-3). I raised this
	 *  to 0.02 to try to force Nanite residency and it was a bad trade: every hidden station then draws real
	 *  geometry (2.5M tris across the set) for the whole warm-up — the frame rate collapsed and the popping got
	 *  WORSE. The prime exists to compile shaders, nothing more; it must cost ~zero. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.0001"))
	float WarmupPrimeScale = 0.001f;

	/** Re-arm the warm-up when ZoomProgress returns below this. 0 = OFF (one-shot at launch only), which is the
	 *  default: re-arming re-pays the whole prime cost every time the zoom returns to the top, and with a
	 *  non-trivial prime scale that is a recurring hitch rather than a one-time startup cost. */
	UPROPERTY(EditAnywhere, Category="QZoomStage")
	float WarmupRearmBelow = 0.f;
	bool bWasAboveRearm = false;

	// ── FRONT-FACING indicator ───────────────────────────────────────────────────────────────────────────
	// A fixed marker for where the WALL's front (the audience) is, sitting alongside the zoom centre at the
	// Anchor. It does NOT orbit: the subject turns under it, so it reads as "this edge faces the room".
	// Derived from the nDisplay config, not guessed: TF_Root (330,0,-180) + TF_Wall (422.5,0,422.5) puts the
	// wall plane at X=752.5 with yaw 0 (facing +X), the viewpoint at the origin, and the Anchor at X=1500 —
	// i.e. the subject sits BEYOND the wall and the audience looks down +X. So "front" = -X from the subject.
	//
	// Pawn-owned ON PURPOSE: the per-station GUIDE_center/GUIDE_volume actors have no StationFade param, so
	// they cannot dissolve — one indicator on the pawn avoids six copies fighting the fade system.
	/** Show the front-facing marker + zoom-centre reticle (an authoring aid — turn OFF for the show). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Guides") bool bShowFrontIndicator = false;
	/** Direction of the audience/wall front, from the subject. Default -X = the room's horizontal bearing,
	 *  derived from the nDisplay layout. NOTE the true line from the Anchor (z=650) to the viewpoint (z=0)
	 *  tilts down 23.4°; flat -X is its horizontal projection, which is usually what you want for a
	 *  "which edge faces the room" marker. Tick bAimFrontAtViewpoint below for the exact eye-line instead. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Guides") FVector FrontDirection = FVector(-1.f, 0.f, 0.f);
	/** Aim the arrow at the actual nDisplay viewpoint (the origin) instead of using flat FrontDirection —
	 *  the true eye-line, tilted ~23° down from the Anchor. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Guides") bool bAimFrontAtViewpoint = false;
	/** The room's viewpoint in world space (nDisplay DefaultViewPoint = the origin). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Guides") FVector RoomViewpoint = FVector(0.f, 0.f, 0.f);
	/** Length of the front arrow in world units. Default 900 so it visibly CROSSES the wall plane (X=752.5,
	 *  i.e. 747 UU in front of the Anchor) — at 600 it stopped short and read as floating behind the screen. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Guides", meta=(ClampMin="10.0")) float FrontIndicatorSize = 900.f;
	/** Colour of the front arrow. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Guides") FLinearColor FrontIndicatorColor = FLinearColor(1.f, 0.25f, 0.1f);
	/** Colour of the zoom-centre reticle at the Anchor. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Guides") FLinearColor CentreIndicatorColor = FLinearColor(0.02f, 0.91f, 0.83f);
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GuideISM = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GuideFrontMID = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> GuideCentreISM = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GuideCentreMID = nullptr;
	bool bGuidePrev = false;   // F2 edge-detect

	/** NirA outer shells (the 'ribbon' + 'VOLUME' surfaces on SM_S2_NirA). They ENCLOSE the whole protein, and
	 *  MET169 sits at its centre — so at the S2->S3 hand-off you are looking at the residue from INSIDE them
	 *  and they obscure it (Michael). Only variant 2 has these slots; the high-res version is atoms only,
	 *  which is why the versions disagree. false = hide the shells so all three read the same.
	 *  Hidden by SLOT (swapping in a null material), so the C/N/O/S atoms are untouched. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|NirA") bool bNiraShells = false;
	bool bNiraShellsApplied = false;

	// ── PALETTE squeeze ──────────────────────────────────────────────────────────────────────────────────
	// Michael: "the overall material color is too spread — I need a tighter color palette", ideally on the
	// controller. Measured, the authored palette spans 240 deg of hue — but NOT randomly: it sits in two
	// poles, amber/gold (~40 deg, 7 params) and blue (~215 deg, 7 params), with the reds (O, 0 deg), the
	// green mold (92 deg) and the teals (168-193 deg) breaking them.
	//
	// So this squeezes each colour toward its NEAREST pole, not toward one global centre. Collapsing to a
	// single centre destroys the amber/blue relationship the palette is built on (at width 0.5 the golds
	// land on 125 deg = green — verified by modelling it against the real values first).
	// PaletteWidth 1 = as authored; 0 = every colour snapped onto its pole. Measured concentration:
	// 1.0 -> R=0.10 (the current spread) ... 0.35 -> R=0.81 ... 0.15 -> R=0.96 (near-monochrome).
	/** 1 = authored palette, 0 = fully collapsed onto the two poles. R3 resets to 1; Back/Start step it. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Palette", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PaletteWidth = 1.f;
	/** The two hue poles the palette is squeezed toward, in degrees. Defaults measured from the assets. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Palette", meta=(ClampMin="0.0", ClampMax="360.0"))
	float PaletteHueA = 40.f;    // amber/gold: S, HL, agar, MET169, density
	UPROPERTY(EditAnywhere, Category="QZoomStage|Palette", meta=(ClampMin="0.0", ClampMax="360.0"))
	float PaletteHueB = 215.f;   // blue: N, C, dish, cell, NirA, quarks
	/** Pull saturation toward this as the palette tightens (1 = leave saturation alone). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Palette", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PaletteSatTarget = 0.55f;
	/** How much of the saturation move to apply at full squeeze. 0 = hue only. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Palette", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PaletteSatAmount = 0.6f;
	/** Step size for the Back/Start buttons. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Palette", meta=(ClampMin="0.01", ClampMax="0.5"))
	float PaletteStep = 0.1f;
	float PaletteApplied = -1.f;              // last-applied width (re-apply only on change)
	bool  bPalMinusPrev = false, bPalPlusPrev = false, bPalResetPrev = false;
	int32 NaniteDiagStep = 0;        // R3 cycles r.Nanite.ProxyRenderMode: 0 normal / 2 fallback / 3 full-Nanite
	bool  bNaniteDiagPrev = false;

	// ── DMI CACHE ────────────────────────────────────────────────────────────────────────────────────────
	// THE STARTUP STALL: SetStationFade ran CreateDynamicMaterialInstance() on first touch of every material
	// slot, from inside a per-frame loop over every station + child. The warm-up unhides all six stations at
	// frame 1, so ~144 DMIs were created AT ONCE, each forcing a shader/PSO compile — a multi-second hitch
	// that vanished once they were all cached. Exactly Michael's "weak at the start, then smooth".
	// Now: build every DMI ONCE (BuildMaterialCache), then only ever SET parameters on them.
	struct FQZMat
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> DMI;
		FLinearColor BaseColor = FLinearColor::White;   // authored colour, for the palette squeeze
		FName ColorParam = NAME_None;                   // "BaseColor" / "Color" / none
		bool  bHasFade = false;                         // exposes StationFade?
	};
	TArray<FQZMat> MatCache;
	TMap<TWeakObjectPtr<AActor>, TArray<int32>> ActorMats;   // actor -> indices into MatCache
	bool bMatCacheBuilt = false;
	int32 MatCacheStations = -1;   // station count the cache was built for; grows as sublevels stream in
	void BuildMaterialCache();

	// ── HITCH DIAGNOSTICS ────────────────────────────────────────────────────────────────────────────────
	// The startup stall has survived four fixes. This logs any slow frame and splits the time into "inside
	// this pawn's Tick" vs "outside it" — the outside number covers the render thread, GPU, streaming and
	// PSO compiles. Whichever side is large IS the answer, instead of another guess.
	/** Log frames slower than HitchMs. Leave ON until the stall is found; it costs nothing when quiet. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Debug") bool bLogHitches = true;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Debug", meta=(ClampMin="4.0")) float HitchMs = 33.f;
	int32 HitchesLogged = 0;


	// ── LEFT-STICK FREE LOOK ─────────────────────────────────────────────────────────────────────────────
	// Michael: left stick turns the CAMERA (free look) while the orbit POSITION stays locked to the zoom
	// path — the viewer holds their spot and turns their head. After 2 s of no input, ease smoothly back to
	// looking dead centre. Distinct from the right stick, which ORBITS the world around the anchor.
	// NOTE (nDisplay): this rotates the pawn's Camera component. In PIE that turns the view; in a real
	// Deep Space cluster the DCRA defines the frustum, so this MUST be verified on the wall — it may need to
	// drive the DCRA's view rotation instead. Flagged, not assumed.
	/** Free-look speed, degrees/sec at full stick deflection. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|FreeLook", meta=(ClampMin="0.0")) float FreeLookRate = 55.f;
	/** Max yaw/pitch the free look can reach, degrees (so you can't spin past the content). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|FreeLook", meta=(ClampMin="0.0", ClampMax="89.0")) float FreeLookMaxYaw = 45.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|FreeLook", meta=(ClampMin="0.0", ClampMax="89.0")) float FreeLookMaxPitch = 30.f;
	/** Seconds of no left-stick input before the ease-back begins. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|FreeLook", meta=(ClampMin="0.0")) float FreeLookHold = 2.0f;
	/** How long the ease back to centre takes, seconds (smootherstep). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|FreeLook", meta=(ClampMin="0.1")) float FreeLookReturn = 1.5f;
	float LookYaw = 0.f, LookPitch = 0.f;   // current free-look offset (deg)
	float LookIdle = 0.f;                    // seconds since last left-stick input
	float LookRetFromYaw = 0.f, LookRetFromPitch = 0.f, LookRetT = -1.f;   // ease-back state
	TWeakObjectPtr<AActor> DCRA;             // the nDisplay root actor (frustum source) — found lazily
	FRotator DCRABaseRot = FRotator::ZeroRotator;   // its authored orientation, captured once
	bool DCRABaseSet = false;
	TArray<TWeakObjectPtr<USceneComponent>> DCRAViewpoints;   // per-viewport view origins (wall + floor)
	TArray<FRotator> DCRAViewpointBase;                        // their authored rotations, captured once
	void ApplyFreeLook(float Yaw, float Pitch);   // rotate the DCRA (cluster) or the camera (PIE)
	void ApplyNaniteDiag();                       // set r.Nanite.ProxyRenderMode on this node (idempotent, all nodes)

	/** Look-around orbit speed (deg/sec at full stick) + pitch clamp. */
	UPROPERTY(EditAnywhere, Category="QZoomStage")
	float OrbitRate = 60.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.0", ClampMax="89.0"))
	float MaxPitch = 70.f;

	// ── Constant scale/zoom readout ───────────────────────────────────────────
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	// 7 stations: MYCELIUM (300 um) inserted at index 1. It halves the worst leg —
	// S0->Cell was 4.48 decades, the longest on the ladder and flagged in the code as a
	// problem; it is now 2.48 + 2.00. Note these are CLASS DEFAULTS: a pawn already
	// placed in a level carries its own serialised copy and ignores them.
	TArray<float> ScaleMeters = {0.09f, 3e-4f, 3e-6f, 1e-8f, 2e-10f, 1e-10f, 8e-15f};
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
	/** Filler growth as a FRACTION of the world's own zoom rate (LocalK ~12). 1.0 = scales exactly like the
	 *  stations.
	 *  PERFORMANCE WARNING (learned the hard way — this caused a "massive performance drop at the start"):
	 *  the fillers are THOUSANDS of ISM instances. Feeding them the world's full exponential blows every
	 *  instance up until it covers the screen — enormous overdraw, worst at the start where the exponent is
	 *  furthest from the NirA anchor. The original hard-coded 2.4 against a world K of 12 (= ratio 0.2) was
	 *  not a bug; it was tuned to keep the cloud cheap. 0.2 restores that. Raise slowly and watch the frame
	 *  time — this is the knob that will cost you FPS. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="0.05", ClampMax="2.0"))
	float FillerScaleRatio = 0.2f;

	/** Clamp on the filler cloud's scale. Keep the max LOW: at 400 the instances inflate until each one fills
	 *  the frame (that was the startup stall). 12 is the proven value — the medium fades out via FillerFade
	 *  well before it would need to grow past it. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="0.01")) float FillerScaleMin = 0.5f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="1.0"))  float FillerScaleMax = 12.f;

	/** Filler colours. The medium runs on M_StationMaster now (M_Filler had NO colour param — its colour was
	 *  baked into the graph, which is exactly why the palette squeeze never reached the fillers). These are
	 *  the AUTHORED values the squeeze derives from: proteins on the blue pole, enzymes on the amber one. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers") FLinearColor FillerColorMotes  = FLinearColor(0.30f, 0.52f, 0.90f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers") FLinearColor FillerColorStruct = FLinearColor(0.90f, 0.62f, 0.18f);

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
	int32 NiraVersion = 1;   // S2 NirA representation: 0 high-res FBX / 1 low-res FBX (default) / 2 procedural / 3 hull
	/** How many NirA versions X cycles through. Bump to 4 for the tight-hull variant (QZVer3). */
	UPROPERTY(EditAnywhere, Category="QZoomStage") int32 NiraVersionCount = 4;
	bool bXPrev = false;     // X button (version cycle) latch

	// STYLE LIGHT — the freed S4 key light, dialled on the LB/RB shoulders for on-site look variation.
	// Absolute intensity ladder (Michael's spec): 0 off / 2k / 5k / 10k / 25k / 50k(=current, full). LB down, RB up.
	int32 StyleLightStep = 0;    // index into StyleLightLadder; DEFAULT 0 = starts dark (Michael), ease up with RB
	/** How fast the style light EASES DOWN / holds (FInterpTo speed). Higher = snappier. The fade-OUT Michael likes. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|StyleLight", meta=(ClampMin="0.1", ClampMax="20.0"))
	float StyleLightEaseSpeed = 1.0f;   // 3x slower than the original 3.0 (Michael: prolong the back-button light)
	/** How fast the style light EASES UP (fade-IN). Lower than EaseSpeed so the rise isn't front-loaded/snappy on
	 *  the huge non-linear ladder — Michael: "fade-in is still too quick, should match the fade-out". */
	UPROPERTY(EditAnywhere, Category="QZoomStage|StyleLight", meta=(ClampMin="0.1", ClampMax="20.0"))
	float StyleLightRiseSpeed = 0.45f;  // ~2x slower than the fall, so the fade-in duration matches the fade-out
	float StyleLightEased = 0.f;   // current eased intensity (interpolated toward the ladder target each frame)

	// ── STYLE LIGHT (LB / RB) ────────────────────────────────────────────────────────────────────────────
	// Lifted OUT of L_QZ_S4_Density into the persistent level (Michael: "at the moment it is limiting").
	// It was bound to S4's streaming/visibility, had attenuation_radius 7182 (so it could not reach once the
	// world scaled up at depth) and was STATIONARY (the expensive mobility for something driven every frame).
	// Now: persistent, MOVABLE, huge reach, and it rides the Anchor so the key light stays on the hero.
	/** The intensity ladder LB/RB steps through, in candelas. Editable — add steps for more range.
	 *  Default: off / 2k / 5k / 10k / 25k / 50k (the old authored value) / 120k / 300k. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|StyleLight")
	TArray<float> StyleLightLadder = { 0.f, 2000.f, 5000.f, 10000.f, 25000.f, 50000.f, 120000.f, 300000.f };
	/** Where the light sits relative to the Anchor. Rotated by the orbit, so it keeps its angle on the
	 *  subject as the world turns. (0,0,0) = dead centre inside the hero. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|StyleLight")
	FVector StyleLightOffset = FVector(-10.f, -10.f, 0.f);
	/** Attenuation radius. The authored asset had 7182 (too small — the scene outgrew it at depth). I then
	 *  swung to 200000, which was reckless: a point light's SHADOW work scales with what its radius reaches,
	 *  and ApplyStations rewrites every station transform each frame, so nothing shadow-related can cache.
	 *  30000 covers the stage without paying for an enormous shadow volume. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|StyleLight", meta=(ClampMin="100.0"))
	float StyleLightRadius = 30000.f;

	/** Style light casts shadows. OFF by default: it is a STYLE key that moves with the Anchor every frame
	 *  in a scene whose geometry also rescales every frame — so its shadows can never cache and cost full
	 *  regeneration continuously. Key_Directional still casts the scene's real shadows. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|StyleLight")
	bool bStyleLightShadows = false;
	TWeakObjectPtr<ULightComponent> StyleLight;   // cached, found by the 'QZStyleLight' actor tag

	// CLEAN MODE — Y hides the whole editorial HUD for clean photography plates (cluster-synced).
	bool bCleanMode = false;
	bool bYPrev = false;

	// S3 OXIDATION director — ping-pong the sulfur-switch SVT frame (dwell -> snap -> dwell = the click);
	// L3 (left-stick click) freezes/resumes for a held hero moment. Cluster-synced via OxTime.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Oxidation", meta=(ClampMin="0.5"))
	float OxPeriod = 6.0f;             // seconds per full oxidize<->reduce ping-pong cycle
	UPROPERTY(EditAnywhere, Category="QZoomStage|Oxidation", meta=(ClampMin="2"))
	int32 OxFrames = 300;             // SVT frame count (matches the sulfur_switch bake)
	/** SVT streaming bandwidth cap in MiB/s, applied at BeginPlay on every node. The engine SERVES LOWER MIPS
	 *  when over this budget (blur pop near the ping-pong's fast sweep at the 512 default). NVMe easily does
	 *  multiple GiB/s — 4096 gives the two sulfur-switch volumes full-res headroom through the snap. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Oxidation", meta=(ClampMin="512", ClampMax="16384"))
	int32 SVTBandwidthMiB = 4096;
	/** During the ping-pong SNAP the SVT is streamed with BLOCKING requests (full mip, same frame — no blur pop).
	 *  This is the frames/sec threshold above which "snapping" is detected; the dwell (~0 fps) streams async. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Oxidation", meta=(ClampMin="1.0"))
	float OxBlockFrameVel = 40.f;
	float OxPrevFrame = 0.f;          // last tick's SVT frame, for the sweep-velocity (snap vs dwell) test
	TWeakObjectPtr<class UHeterogeneousVolumeComponent> OxVolume;   // cached, found by the 'QZOxSwitch' tag
	float OxTime = 0.f;               // ping-pong clock (primary advances; secondaries receive it)
	bool  bOxFrozen = false;          // L3 freeze/resume

	bool  bL3Prev = false;

	// Per-frame light fade: each station sublevel's lights fade with that station's visibility (no pop-in).
	TMap<ULevel*, float> LevelFade;                                   // rebuilt each frame in ApplyStations
	TSet<ULevel*> TrackedLevels;                                      // every station sublevel ever seen (persistent)
	TMap<TWeakObjectPtr<ULightComponent>, float> LightBaseIntensity;  // captured base intensity, persistent
	TMap<TWeakObjectPtr<AActor>, float> PPBaseWeight;                 // PostProcessVolume base BlendWeight, persistent
	TMap<TWeakObjectPtr<ULightComponent>, float> LightFadeSmoothed;  // per-light eased fade (prolongs the ramp)
	TMap<TWeakObjectPtr<AActor>, float> PPFadeSmoothed;              // per-PP eased fade
	/** Speed the map-light + PP fade EASES toward the station's visibility. Lower = slower/prolonged ramp.
	 *  Michael wanted this ~3x slower than the instant snap it replaced. */
	UPROPERTY(EditAnywhere, Category="QZoomStage") float LightFadeSpeed = 1.2f;

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
	void    UpdateCH4Cycle(float Dt);                // looping redox cycle at the NirA station
	float   CH4Phase = 0.f;                          // 0..1, wraps forever

	void    UpdateRefParticles();                    // self-similar scale-reference field
	void    UpdateCommsStreams(float Dt);            // per-station outward data traffic
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CommsISM = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic>      CommsMID = nullptr;
	TArray<FVector> CommsDirs;                       // lane directions, generated once
	TArray<float>   CommsOffsets;
	float           CommsClock = 0.f;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RefISM = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic>      RefMID = nullptr;
	TArray<FVector> RefDirs;                         // unit directions, generated once
	TArray<float>   RefOffsets;                      // per-mote phase offset, generated once
	TArray<float>   RefSizeJitter;
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
	class UHeterogeneousVolumeComponent* GetOxVolume();   // find+cache the 'QZOxSwitch'-tagged S3 volume
	void    UpdateOxidation(float Dt);   // ping-pong the sulfur-switch SVT frame (all nodes)
	void    UpdateS3Focus();             // NirA hull dissolves -> orbital SVT blooms in, on MET169

	// ── S3 FOCUS (NirA -> orbital reveal) ────────────────────────────────────────────────────────────────
	/** Width of the focus band as a fraction of the station spacing. Bigger = the reveal eases in earlier. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|S3Focus", meta=(ClampMin="0.2", ClampMax="3.0")) float S3FocusWidth = 1.4f;
	/** Peak opacity the S3 NirA hull reaches when NOT focused (so it can read strong before dissolving). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|S3Focus", meta=(ClampMin="0.0", ClampMax="1.0")) float S3NirABaseVis = 1.0f;
	float S3Focus = 0.f;   // 0 = NirA shown, 1 = orbital shown (read-only, for debug)
	void    UpdateGuides();              // front-facing marker + zoom-centre reticle at the Anchor
	void    ApplyNiraShells();           // show/hide SM_S2_NirA's enclosing ribbon+VOLUME surfaces
	void    ApplyPalette();              // squeeze every station colour toward the nearest hue pole
	float   StationScale(int32 N) const;
	float   StageCentre(int32 N) const;   // ZoomProgress centre of stage N, incl. the ZoomLeadIn shift
	float   StationK(int32 N) const;      // per-station ZoomK (StationZoomK[N] or ZoomK) * ZoomIntensity
	float   LocalK() const;               // K at the CURRENT depth, blended across the leg — for the fillers
	float   CurrentScaleMeters() const;
	FString FormatScale(float Metres) const;
	FString FormatRate(float PerSec) const;   // SI-suffixed rate for the Observer Speed readout
	FString FormatZoom(float Power) const;    // 10^Power -> "m.m × 10^e" with superscript exponent
	static FString Superscript(int32 N);      // integer -> superscript-glyph string
	void    SetupAccentMesh(class UStaticMeshComponent* M, TObjectPtr<class UMaterialInstanceDynamic>& MID,
	                        class UStaticMesh* Cube, class UMaterialInterface* Mat, const FLinearColor& Col, float Bright);
};
