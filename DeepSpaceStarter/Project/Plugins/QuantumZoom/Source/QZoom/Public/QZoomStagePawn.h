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

	/** WHAT THIS STAGE IS. A label only — the ladder position is this row's INDEX in the array, and
	 *  that index is the number in each actor's QZStation tag. Renaming is free. REORDERING IS NOT:
	 *  it re-numbers everything below and the actors' tags would then point at the wrong stage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FName Name;

	/** The sublevel that holds this stage's actors. Documentation, not streaming — it is here so the
	 *  ladder reads as a map of the project instead of a column of anonymous numbers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") FName Level;

	/** REAL-WORLD SIZE of the framed subject, in metres. This is what PLACES the stage on the ladder:
	 *  with bLogSpacedStations on, the spacing is derived from these, so equal ZoomProgress means
	 *  equal decades and the perceived zoom rate stays constant.
	 *  0 = fall back to the legacy ScaleMeters array for this slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") float ScaleMeters = 0.f;

	/** Off = this stage keeps its slot but NEVER renders. Retire a stage here rather than deleting the
	 *  row — deleting renumbers every stage below it, and those numbers are baked into actor tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage") bool bActive = true;

	/** HOW BIG THE CAMERA'S DISSOLVE BUBBLE IS FOR THIS STAGE. Multiplier on CamGateStartUU and
	 *  CamGateRangeUU, which the pawn scales by the station's own size:
	 *
	 *      CamFadeStart = CamGateStartUU * StationScale * NearDissolve
	 *
	 *  THIS, NOT `Dissolve`, IS USUALLY WHAT MAKES A STAGE "VANISH TOO EARLY". Because the bubble
	 *  grows with the station, a hero that has swollen to 7x is measured against a bubble that has
	 *  also swollen 7x — at the lab that put the entire mushroom (6000 uu across) inside a fade band
	 *  reaching 10500 uu, so it was being eaten from the inside out while its station fade said it
	 *  was fully present. Raising `Dissolve` cannot help: the camera gate gets there first.
	 *
	 *  1.0 = the shared behaviour. LOWER = a tighter hole, so the stage survives closer to the lens
	 *  and you fly INTO it rather than watching it evaporate ahead of you. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="0.01", ClampMax="4.0"))
	float NearDissolve = 1.f;

	/** HOW MUCH OF THE DEFAULT ENVIRONMENT THIS STAGE WANTS: the two QZGlobalLight directionals, the
	 *  sky light, the sky atmosphere and the height fog, all together.
	 *
	 *  1 = the shared rig at full strength. 0 = only this stage's OWN lights illuminate it.
	 *
	 *  UpdateLights deliberately skips anything tagged QZGlobalLight, so before this existed the key
	 *  and fill were on at full strength in every scene — which is why a set of lab point lights at
	 *  intensity 8 could not be seen against a key at 3 plus a sky. The value cross-fades between
	 *  stages weighted by their current fades, so it never snaps at a handover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage", meta=(ClampMin="0.0", ClampMax="2.0"))
	float GlobalLight = 1.f;

	/** Use the timing values below. Off = the station uses the global StationZoomK / MinVis / fade values. */
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

	/**
	 * How big this station RENDERS, as a multiple of its authored size. Purely a size knob: it does
	 * NOT touch the fade windows.
	 *
	 * The distinction matters. StationScale() — exp((Progress - Centre) * K) — is the number the
	 * whole ladder is built on: InitialSize, Dissolve, FadeIn and FadeOut are all thresholds
	 * against it, and the drill, the lights and the fog read it too. Shifting a station's centre to
	 * make it smaller would move all of those with it and re-time the leg. So the size multiplier
	 * is applied where the geometry is actually placed and nowhere else: the station appears and
	 * dissolves at exactly the same points on the zoom bar, it is simply smaller while it is there.
	 *
	 * Set it on a RUN of stations, not one: halving NirA alone would leave MET169 oversized inside
	 * it. Halving NirA and everything after keeps every relative proportion and just makes the
	 * back half of the dive less massive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="0.05", ClampMax="20.0")) float SizeMul = 1.f;

	/** FADE OUT — width of the DISSOLVE ramp, separate from FadeIn. These used to be one number, which is
	 *  why lengthening the lab's dissolve also made it bloom in slowly: appearing and departing want very
	 *  different durations. You arrive at a scene quickly and leave it slowly. 0 = follow FadeIn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="0.0", ClampMax="6.0")) float FadeOut = 0.f;

	/** LOOK — applied ONCE, when this stage takes over as the dominant station.
	 *  -1 means "leave whatever is set", which is what every stage does by default: the look then
	 *  simply carries forward from the last stage that had an opinion. So the lab can ask for
	 *  neutral, the structure can ask for P5, and everything after it inherits P5 without each
	 *  row having to repeat it.
	 *  Manual D-Pad Up/Down and LB still work — they are overridden at the next handover, which
	 *  is the behaviour a show wants: the operator can look around, and the piece pulls itself
	 *  back onto the authored look at the next stage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="-1", ClampMax="9")) int32 Preset = -1;
	/** Style-light step to apply when this stage takes over. -1 = leave alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Handover", meta=(ClampMin="-1")) int32 StyleStep = -1;
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

	/** How hard the trail MEANDERS while it searches. This is the ant-path dial: 0 is a straight
	 *  spoke (mechanical, wrong), high is a path that casts about before committing outward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Wander = 0.42f;

	/** How strongly the trail is pulled back on course after wandering. Low = it keeps drifting and
	 *  never arrives; high = it corrects hard and looks purposeful. The tension between this and
	 *  Wander is what reads as SEARCHING rather than either drifting or marching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Commit = 0.30f;

	/** Fraction of trails that BRANCH off another trail instead of leaving the centre. Branching is
	 *  most of what makes a foraging network look explored rather than radiated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="0.9"))
	float Branch = 0.45f;

	/** Blink rate in Hz. Packets wink on and off as they travel — the "signal" read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="30.0"))
	float BlinkHz = 3.2f;

	/** How deep the blink cuts. 0 = steady, 1 = fully off between pulses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BlinkDepth = 0.75f;

	/** Packets grow brighter and longer as they get further out, so the trail reads directional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Comms", meta=(ClampMin="0.0", ClampMax="4.0"))
	float OutwardGain = 1.4f;

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
class UPointLightComponent;   // the CH4 energy light lives on the pawn
class UExponentialHeightFogComponent;   // the shared environment rig, scaled per stage
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
	int32 StationCount = 9;

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

	/** ═══ THE LADDER. ONE ROW PER STAGE, AND THE ONLY PLACE TIMING IS AUTHORED. ═══
	 *
	 *  Everything that decides WHERE a stage sits and HOW it hands over now lives on its own named
	 *  row: the name, the level that holds it, its real-world size, its steepness and its two fade
	 *  widths. Open this one array and the whole descent is in front of you, in order.
	 *
	 *  It replaces four parallel arrays that had to be kept index-aligned by hand — ScaleMeters,
	 *  StationZoomK, Handover and the QZMaxVis tags scattered across actors in eight sublevels. None
	 *  of those carried a name, so row 5 meant nothing until you counted.
	 *
	 *  HOW TO USE IT
	 *    - the stage's position on the ladder comes from ScaleMeters (real metres), not its index
	 *    - Name and Level are labels; they cost nothing and make the array readable
	 *    - bActive off retires a stage without deleting the row
	 *    - bEnabled off ignores this row's timing and falls back to the globals
	 *
	 *  THE ONE RULE: the row's INDEX is the number in each actor's QZStation tag. Renaming a row is
	 *  free. Reordering or deleting one re-numbers everything below it and silently re-points those
	 *  tags at the wrong stage. Retire with bActive instead. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Ladder")
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
	// 0.15 meant the first 15% of the bar was spent approaching stage 0 rather than travelling
	// through it — dead runway the operator skipped every time. 0 puts stage 0's centre at the
	// very start, so the mushroom is already at working size on frame one and the whole bar is
	// useful. Everything below redistributes across 0..1 automatically.
	float ZoomLeadIn = 0.f;

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

	/** "Let's watch that whole process again, faster this time" (Markos, 4.6). Rather than a cue the
	 *  operator has to hit, the cycle ACCELERATES THE LONGER YOU STAY: the first pass runs at full length
	 *  so it can be read, and repeats compress toward this multiple. Dwell is the trigger, so 4.6 happens
	 *  by itself if the operator holds on the switch, and never happens if they move on. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="1.0", ClampMax="10.0"))
	float CH4SpeedMax = 3.0f;

	/** How many completed cycles it takes to reach CH4SpeedMax. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="0.25", ClampMax="8.0"))
	float CH4SpeedRampCycles = 1.5f;

	/** Hand the docking MOTION to a Level Sequence so it can be adjusted in Sequencer instead of
	 *  in C++ keyframes. The pawn stops writing the transform and instead SCRUBS the sequence to
	 *  CH4Phase — so the animation is still driven by zoom depth and still freezes when you leave
	 *  the station, it is just authored on a timeline. Presence and fade stay in code, because
	 *  they drive a material parameter rather than a transform. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4")
	bool bCH4UseSequencer = false;

	/** Length of that sequence in seconds. CH4Phase 0..1 maps onto 0..this. Must match the
	 *  sequence's own duration or the docking lands at the wrong moment. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="0.1"))
	float CH4SequenceSeconds = 10.f;

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

	// ── Presence: when each enzyme is ON SCREEN, as fractions of the cycle ────────────────
	// These were hard-coded (0.20/0.80 and 0.622/1.00 with a 0.06 ramp) and the ramp was far too
	// short — the enzymes reached full opacity in 6% of the cycle while still at their far
	// distance, which is a pop with a fade in front of it. They are properties now so the
	// sequence and the visibility can be re-timed together without a rebuild.
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Beats", meta=(ClampMin="0.0", ClampMax="1.0")) float CH4FmobIn  = 0.030f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Beats", meta=(ClampMin="0.0", ClampMax="1.0")) float CH4FmobOut = 0.660f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Beats", meta=(ClampMin="0.0", ClampMax="1.0")) float CH4MsraIn  = 0.550f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Beats", meta=(ClampMin="0.0", ClampMax="1.0")) float CH4MsraOut = 0.995f;
	/** Ramp length at each end of those windows. Long: they arrive out of the distance. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Beats", meta=(ClampMin="0.01", ClampMax="0.45"))
	float CH4PresenceRamp = 0.150f;

	/** The two moments the reaction turns over: oxygen lands, and oxygen leaves. The energy layer
	 *  reads its whole state from these, so they are the single place the beats are defined. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Beats", meta=(ClampMin="0.0", ClampMax="1.0")) float CH4DockPhase    = 0.360f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Beats", meta=(ClampMin="0.0", ClampMax="1.0")) float CH4ReleasePhase = 0.860f;

	/** Leave the enzymes to the LADDER while the cycle is stopped. UpdateCH4Cycle writes
	 *  StationFade a second time, after ApplyStations, as StationFade * Presence(phase) — and at
	 *  phase 0 Presence is exactly 0, so a stopped cycle force-hid both enzymes every frame no
	 *  matter what the ladder said. Two writers, one parameter. With this on, a stopped cycle
	 *  stops writing at all and the ladder's value survives.
	 *
	 *  DEFAULT BACK TO FALSE — turning this on was my mistake. The double write is real, but the
	 *  OUTCOME it produced was the right one: a stopped cycle should HIDE the enzymes. With it on
	 *  they instead inherit station 4's ladder fade and sit at their authored local offsets
	 *  (~120,000 uu), so as MET169 blows up through the handover to nucleus they scale outward
	 *  and sweep past the lens — "a strange copy of MET169 floating towards the camera". Leave it
	 *  off unless you specifically want the enzymes parked visible with the clock stopped. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4") bool bCH4HoldWhenStopped = false;

	// ── The energy layer: activation and deactivation, not a generic sparkle ──────────────
	// One shell of radial shards around the residue, driven entirely by the two beats above.
	// DEACTIVATION is an IMPLOSION: the shell rushes inward, the shards shorten, the colour
	// falls to cold. ACTIVATION is the reverse and louder — an outward shock that overshoots
	// before settling. Between the two the shell simply breathes, dim while deactivated and
	// bright while active, so the state is readable at any moment and not only at the turnover.
	// Built as an ISM on the pawn rather than Niagara, because that is what this project can
	// author end to end without a published parameter set.
	// OFF by default — Michael's call after seeing it at MET169. The system stays in the build so
	// it can be switched back on per-pawn without a rebuild; nothing runs while this is false.
	// ── Niagara user parameters, published to every emitter alongside StationFade ───────────
	// The sulfur system exposes two size handles rather than one, because its emitters are not
	// all the same kind of thing: four are the particles proper and one is a background glow
	// that has to be far larger to read at all. One shared number cannot serve both — at the
	// value that makes the glow visible the particles are absurd, and vice versa. The defaults
	// are the values Michael tuned in the Niagara editor, not guesses.
	/** User.ParticleScale — drives the four particle emitters. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Niagara", meta=(ClampMin="0.0", ClampMax="200")) float ParticleScale = 1.f;
	/** User.ParticleGlowScale — the background glow emitter, which needs to be much larger. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Niagara", meta=(ClampMin="0.0", ClampMax="500")) float ParticleGlowScale = 40.f;
	/**
	 * User.ParticleColor — the emitters' tint, published like the two sizes.
	 *
	 * It is a LADDER value rather than an emitter setting because the look it has to survive is a
	 * ladder value. P5 Infrared sets SceneColorTint to (0.40, 0.02, 0.02): a straight per-channel
	 * multiply that leaves 2% of green and 40% of red. Add exposure -1.2 and contrast 1.60 on top
	 * and a green particle keeps under 1% of itself, while the same particle in red keeps 17% —
	 * twenty times more, for no change to the preset and no extra brightness anywhere.
	 *
	 * Amber rather than pure red so it still reads as its own colour under the neutral presets
	 * instead of vanishing into P2's crimson wash. Under an infrared look this is also the honest
	 * reading: emission from the sulfur shows as heat, not as green.
	 */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Niagara") FLinearColor ParticleColor = FLinearColor(1.0f, 0.45f, 0.10f, 1.f);
	// The emitter is parented to the sulfur lobe, so it ALREADY inherits the station's scale
	// through the component transform — multiplying the published sizes by the scale again would
	// apply it twice and the cloud would grow as the square. Left off; turn it on only if the
	// particles are found not to follow the zoom, which would mean the emitters are world-space.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Niagara") bool bParticleScaleTracksZoom = false;

	// ══ THE QUARK TRIAD ═══════════════════════════════════════════════════════════════════
	// Three valence quarks that wander independently, and three gluon strings that ANSWER to
	// where they went.
	//
	// This cannot be done in a material, and it is worth being clear why, because the previous
	// version tried. A material moves vertices; it has no way to tell a beam where the sphere at
	// its far end has drifted to. So the spheres could only pulse in place, and the strings could
	// only writhe between two points that never moved — which is decoration, not the physics.
	// Rebuilding each string from the two quark positions every frame is the whole point, and
	// that has to happen here.
	//
	// WHAT THE MOTION IS SAYING
	// Quarks are never at rest and never alone. Each one wanders on its own three incommensurable
	// frequencies, so the triad never repeats and never reads as a mechanism. The strings stretch
	// to follow, and — this is the part that carries the story — a stretched string gets BRIGHTER
	// and more violent, not weaker. That is colour confinement: unlike every other force, the
	// gluon field does not fall off with distance, it builds. Pull two quarks apart and the string
	// between them stores more energy until it would rather make new particles than let go. So a
	// quark drifting outward is visibly punished for it, and nothing ever escapes the hull.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Quarks") bool bQuarkMotion = true;
	/** Ladder row the triad belongs to; the motion only runs while that station is on screen. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Quarks") int32 QuarkStation = 7;
	/** How far a quark strays, as a fraction of its own rest radius. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Quarks", meta=(ClampMin="0.0", ClampMax="1.0")) float QuarkWander = 0.42f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Quarks", meta=(ClampMin="0.0", ClampMax="4.0")) float QuarkSpeed = 0.5f;
	/** Extra brightness per unit of stretch: 0 = a dumb line, high = confinement made obvious. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Quarks", meta=(ClampMin="0.0", ClampMax="6.0")) float GluonTension = 2.4f;
	/** How far the (invisible) string junction wanders off the live centroid, as a fraction of the
	 *  triad's rest radius. 0 = nailed to the average, which reads as mechanical. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Quarks", meta=(ClampMin="0.0", ClampMax="0.5")) float GluonJunctionWander = 0.12f;
	/** String thickness, in the beam mesh's own local units. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Quarks", meta=(ClampMin="0.005", ClampMax="1.0")) float GluonThickness = 0.075f;
	float QuarkClock = 0.f;
	/** Authored rest positions, captured the first frame each quark is seen. */
	TArray<FVector> QuarkHome;

	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy") bool bCH4Energy = false;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0", ClampMax="4000")) int32 CH4EnergyCount = 900;
	/** Shell radius at rest, as a fraction of the station span (3464 uu at scale 1). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0.02", ClampMax="3.0")) float CH4EnergyRadius = 0.42f;
	/** How far the shock overshoots the resting shell at the peak of a turnover. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0.0", ClampMax="4.0")) float CH4EnergyBurst = 1.35f;
	/** Width of a turnover, in cycle fractions. Small = a crack, large = a swell. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0.005", ClampMax="0.30")) float CH4EnergyBurstWidth = 0.055f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0.0001", ClampMax="0.2")) float CH4EnergyThickness = 0.012f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0.0", ClampMax="40.0")) float CH4EnergyBrightness = 7.0f;
	/** Idle drift of the shell, revolutions per cycle — the shell is never quite still. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0.0", ClampMax="6.0")) float CH4EnergySwirl = 0.65f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy") FLinearColor CH4EnergyHot  = FLinearColor(1.00f, 0.74f, 0.28f, 1.f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy") FLinearColor CH4EnergyCold = FLinearColor(0.16f, 0.34f, 0.78f, 1.f);
	/** A light on the same envelope, so the energy shift lands on the surrounding geometry too
	 *  and not only as an overlay. 0 disables it. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4|Energy", meta=(ClampMin="0.0")) float CH4EnergyLightIntensity = 9000.f;

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
	/** OFF. Two attempts at the communication layer — radial lanes, then foraging trails — both read as
	 *  noise laid over the zoom rather than a hidden layer inside it (Michael: "this is getting worse").
	 *  The code stays so the idea can be picked up again from a different angle; it does not run. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Comms")
	bool bCommsStreams = false;

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

	/** Same, but for the DISSOLVE edge. Split from StationFadeWidth because one number cannot serve both:
	 *  a station should bloom in fast and dissolve out slowly. 0 = fall back to StationFadeWidth. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.0", ClampMax="6.0"))
	float StationFadeOutWidth = 0.f;

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
	// Nine stations since chapter 5 became one continuous flight rather than a single stop.
	// The last three come from zoom_detail's own ruler, in femtometres:
	//   S6  8.42e-15  the S-32 nucleus       (this slot used to be labelled "quarks" at 8e-15,
	//                                         which was always nucleus scale, not quark scale)
	//   S7  1.68e-15  one nucleon
	//   S8  3.0e-16   the quark / flux-tube region
	// S5 -> S6 is 4.4 decades with nothing in it. That gap is the script's "vast, open space"
	// and it is carried by a mote field, not by an asset.
	TArray<float> ScaleMeters = {0.09f, 3e-4f, 3e-6f, 1e-8f, 2e-10f, 1e-10f,
	                             8.42e-15f, 1.68e-15f, 3.0e-16f};
	/** Camera-relative (forward, right, up) of the LEFT column. DERIVED from the Interface numbers
	 *  below — shown here because it is what the component actually receives. */
	UPROPERTY(VisibleAnywhere, Category="QZoomStage|Readout")
	FVector ReadoutOffset = FVector(950.f, -820.f, 560.f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	float ReadoutSize = 26.f;

	// ── Interface placement: ONE control surface for BOTH columns ─────────────
	// The left readout and the right caption are a PAIR, and they had drifted apart into two
	// independently authored offsets: forward 900 vs 950 (two different stereo planes, two
	// different apparent sizes) and up 520 vs 220 (300 uu apart vertically). That is why only
	// one of them ever read as sitting in a corner — the other was never near one.
	// Three numbers now place both, mirrored about the view centre, and they are re-applied
	// every frame so they can be tuned live in PIE instead of only at BeginPlay.
	/** Camera-forward depth of both columns. One depth = one stereo plane for the whole interface. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Interface", meta=(ClampMin="200.0"))
	float UIDepth = 950.f;
	/** View centre -> each column's OUTER edge, in uu at UIDepth. THE knob for "further into the
	 *  corner": raise it until the text sits where you want against the bezel. The wall frustum is
	 *  the cluster's, not this camera's, so there is no safe number to compute — it is judged by eye. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Interface", meta=(ClampMin="0.0"))
	float UIMarginRight = 820.f;
	/** View centre -> the TOP line of both columns, in uu at UIDepth. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Interface", meta=(ClampMin="0.0"))
	float UIMarginUp = 560.f;

	/** Always-on-top unlit text material (M_ReadoutText). Auto-loaded if empty; used for readout + detail. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Readout")
	TObjectPtr<UMaterialInterface> ReadoutMaterial;
	/** The readout texts render through ONE dynamic instance of M_ReadoutText so ApplyPPPreset can
	 *  drive its GradeComp parameter — the text's emissive is pre-divided by the preset's tint and
	 *  exposure, so the grade lands it back near its authored colour. Same mechanism the HDR
	 *  particles use to survive P5; replaces the stencil/blendable restore, which proved unreliable
	 *  on the nDisplay viewports. */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TextMID;
	/** Ceiling for the per-channel compensation. Higher = truer colour under crushing presets but
	 *  more bloom halo around the glyphs (bloom sees the HDR value); lower = calmer, greyer text. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look", meta=(ClampMin="1.0", ClampMax="1000.0"))
	float TextCompMax = 400.f;

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

	// ── Scene caption (upper right): what you are looking at, in the ladder's own words ──────
	// INDEX-ALIGNED TO THE LADDER, one entry per station. SyncLadder resizes all four to
	// StationCount so a ladder edit can never leave a caption pointing at a station that has
	// been retired — that misalignment is exactly what made the captions describe the wrong
	// stage once the ladder grew from six entries to eight.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageTitle;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageSub;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageScaleLabel;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") TArray<FString> StageProv;

	/** Camera-relative anchor of the caption (forward, right, up). DERIVED from the Interface
	 *  numbers — mirror of ReadoutOffset. */
	UPROPERTY(VisibleAnywhere, Category="QZoomStage|Info") FVector DetailOffset = FVector(950.f, 820.f, 560.f);
	/** Base type size of the caption. Every line is a multiple of it, so this one number scales
	 *  the whole block and keeps the hierarchy intact. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") float DetailSize = 34.f;
	/** "04 / 08" above the title — where you are on the ladder. Nothing else on screen says this. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") bool bShowStationIndex = true;

	/** Let the ladder drive the look: apply each stage's Preset / StyleStep at its handover.
	 *  Off = presets and the style light are manual only, as they were. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info") bool bAutoStagePreset = true;

	// ── The grade, BLENDED IN rather than switched ────────────────────────────────────────
	// A per-stage preset is a step: at the handover the whole frame changes grade in one frame.
	// Blending is better here and simpler — APostProcessVolume already has BlendWeight, which
	// is exactly "how much of this volume applies". Hold the preset's settings on the volume
	// permanently and ramp the WEIGHT from 0 to 1 across a zoom range: 0 is untouched scene,
	// 1 is the full preset, and everything between is the engine's own interpolation. No
	// per-field lerp to write and nothing to keep in sync when a preset is edited.
	//
	// It also fixes the fragility of the per-stage route: BlendWeight is derived from
	// ZoomProgress, which is already synced to every node, so each node computes the same
	// number locally. Nothing to broadcast, and no primary-only path to fail quietly.
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look") bool bPresetBlend = true;
	/** Which preset the blend targets. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look", meta=(ClampMin="0", ClampMax="9")) int32 PresetBlendPreset = 5;
	/** Neutral below Start, full preset at End. Both are ZoomProgress, 0..1. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look", meta=(ClampMin="0.0", ClampMax="1.0")) float PresetBlendStart = 0.08f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look", meta=(ClampMin="0.0", ClampMax="1.0")) float PresetBlendEnd = 0.15f;
	int32 PresetBlendApplied = -1;   // last PresetBlendPreset pushed onto PPPreset

	/** The style light stays OFF below this depth, whatever step LB is on. The mushroom is lit
	 *  by its own rig and the style light only muddies it; this makes "off until the structure"
	 *  a property of the piece rather than something the operator has to remember. LB still
	 *  cycles below the threshold — the step is remembered, it just is not applied yet. */
	// ── HUD exemption from the grade ─────────────────────────────────────────────────────
	// The readout is world-space geometry, so the tonemapper grades it with the scene. P5's
	// SceneColorTint (0.40, 0.02, 0.02) is a MULTIPLY: white text lands near (0.18, 0.01, 0.01)
	// and no colour choice survives it. M_ReadoutText has no parameters to drive above white
	// either — verified, zero scalars and zero vectors.
	// So the HUD writes CUSTOM STENCIL, and M_PP_HUD runs AFTER the tonemapper and re-draws
	// those pixels at their authored colour. Post-tonemap is the only place a grade cannot
	// reach, which is why this is the fix and brightness tricks are not.
	// Cost, stated: CustomStencil is one bit per pixel, so glyph edges lose antialiasing and go
	// hard. HUDRestoreSoft below 1 blends back toward the graded pixel — softer, less immune.
	/** OFF by default since the GradeComp route replaced it: the stencil restore was ignored on the
	 *  nDisplay wall viewport, and any blendable at AFTER_TONEMAPPING upset the compositor (border). */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look") bool bHUDExemptFromPP = false;

	/** The hairline rules and the progress bar under the readout. Off on request — the text carries
	 *  the information, the lines were furniture. Flip back on here if a layout ever needs them. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look") bool bShowHUDLines = false;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look", meta=(ClampMin="1", ClampMax="255")) int32 HUDStencil = 7;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look") TObjectPtr<UMaterialInterface> HUDPostMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HUDPostMID;

	UPROPERTY(EditAnywhere, Category="QZoomStage|Look") bool bStyleLightZoomGate = true;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Look", meta=(ClampMin="0.0", ClampMax="1.0")) float StyleLightZoomOn = 0.13f;

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

	/** Lateral drag on the star field per deg/s of orbit — the medium gets swept when you swing.
	 *  0 restores the old behaviour, where the field ignored orbit completely. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="0.0", ClampMax="40.0"))
	float StreakOrbitShear = 6.0f;
	/** Orbit rate (deg/s) that counts as a full-speed swing for the boost below. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="5.0", ClampMax="360.0"))
	float StreakOrbitFull = 80.f;
	/** Peak streak intensity an orbit can produce on its own, with the zoom completely still.
	 *  Kept below 1 on purpose: orbiting is a look-around, not a descent, and it should not read
	 *  as loudly as the zoom does. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StreakOrbitBoost = 0.5f;

	/** Pieces each streak is built from. A streak is a chain of cubes, and a chain is the only way
	 *  it can bend: the mesh is /Engine/BasicShapes/Cube, which has EIGHT vertices, so no amount of
	 *  vertex offset in the material can curve a single instance — it would only shear the box.
	 *  1 = a straight segment. 6-10 reads as a smooth ribbon.
	 *  CHANGING THIS NEEDS A RESTART: the instances are allocated once in InitStreaks. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="1", ClampMax="16"))
	int32 StreakSegments = 8;
	/** How many past positions each star remembers. THE TRAIL IS THIS HISTORY — the shape is not
	 *  computed from the current orbit rate, it is where the particle actually went.
	 *  That distinction is the whole point: an analytic bow re-bends every streak at once the moment
	 *  the orbit direction changes, which reads as the entire field flexing in sync. A remembered
	 *  path cannot do that. The old tail keeps the shape it was drawn with and only the newest
	 *  samples follow the new direction, so a direction change travels DOWN the ribbon as an S — the
	 *  way a real trail behaves.
	 *  More history = a longer possible trail at low speed. Costs 12 bytes per star per entry.
	 *  CHANGING THIS NEEDS A RESTART. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Streaks", meta=(ClampMin="4", ClampMax="128"))
	int32 StreakHistory = 32;

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
	UPROPERTY() TObjectPtr<UTextRenderComponent> DetailIndex = nullptr;   // "04 / 08"
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Streaks = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StreakMID = nullptr;
	float StreakIntensitySmoothed = 0.f;   // time-smoothed for a gentle fade in/out
	float OrbitYawRate = 0.f, OrbitPitchRate = 0.f;   // deg/s, measured per frame; drives both the lateral shear and the orbit contribution to intensity
	float PrevOrbitYaw = 0.f, PrevOrbitPitch = 0.f;
	TArray<FVector> StarPos;               // star positions (decoupled from the rendered, anchored cube)
	TArray<FVector> StarHist;              // ring buffer of past positions, star k at [k*StarHistLen ..). The trail is drawn through these, so its shape IS the path travelled.
	int32 StarHistLen = 0;                 // entries per star (0 until InitStreaks runs)
	int32 StarHistHead = 0;                // newest slot; shared by all stars since they all advance together
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
	float FillerScaleRatio = 0.35f;

	/** Instance SIZE, separate from the growth rate. Raising FillerScaleRatio so the medium tracks the zoom
	 *  also makes every instance bigger, which is not what was wanted — the two were the same number and had
	 *  to be split. Cutting size here pays back the overdraw that raising the ratio costs, so the cloud can
	 *  follow the zoom without filling the frame. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="0.05", ClampMax="4.0"))
	float FillerSizeScale = 0.55f;

	/** Brightness of the medium. The colours below are hues; this is how hard they are pushed. The palette
	 *  squeeze reads the authored colour, so dimming happens at write time rather than by desaturating the
	 *  authored value — otherwise the squeeze would pull the dimmed colour back up. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="0.0", ClampMax="3.0"))
	float FillerBrightness = 0.45f;

	/** Clamp on the filler cloud's scale. Keep the max LOW: at 400 the instances inflate until each one fills
	 *  the frame (that was the startup stall). 12 is the proven value — the medium fades out via FillerFade
	 *  well before it would need to grow past it. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="0.01")) float FillerScaleMin = 0.5f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="1.0"))  float FillerScaleMax = 14.f;

	/** Filler colours. The medium runs on M_StationMaster now (M_Filler had NO colour param — its colour was
	 *  baked into the graph, which is exactly why the palette squeeze never reached the fillers). These are
	 *  the AUTHORED values the squeeze derives from: proteins on the blue pole, enzymes on the amber one. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers") FLinearColor FillerColorMotes  = FLinearColor(0.30f, 0.52f, 0.90f);
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers") FLinearColor FillerColorStruct = FLinearColor(0.90f, 0.62f, 0.18f);

	/** The spherical protein globules — the "motes" half of the medium. OFF: they read as floating
	 *  balls rather than as a medium, and the enzyme-worms carry the depth cue on their own.
	 *  Turning this off skips BUILDING them as well as drawing them, so the instances are not
	 *  allocated at all — it is a saving, not just a hidden component. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers") bool bFillerMotes = false;

	/** THE MEDIUM'S MASTER SWITCH. 0 off / 1 motes / 2 grid (retired) / 3 motes + structures.
	 *  Cycled at runtime with F, and now editable here — it was previously a plain member with no
	 *  UPROPERTY, so the only way to turn the medium off was to press a key every run.
	 *
	 *  BOTH HALVES ARE SPHERES. The "structures" are enzyme-worms built as chains of 24 beads from
	 *  /Engine/BasicShapes/Sphere — the same mesh as the motes. So switching bFillerMotes off removes
	 *  the globule clouds and leaves just as many balls on screen, which is why the motes kept
	 *  appearing to come back. Set this to 0 to remove the medium entirely. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers", meta=(ClampMin="0", ClampMax="3"))
	int32 FillerMode = 3;

	/** The enzyme-worms. Same note as bFillerMotes: these are bead chains of spheres, not filaments. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Fillers") bool bFillerStruct = true;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FillerMotes = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FillerGrid = nullptr;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FillerStruct = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FillerMotesMID = nullptr;   // drives FillerFade (opacity)
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FillerStructMID = nullptr;
	float FillerSwirl = 0.f;    // accumulated self-rotation of the filler clouds (deg), on top of the orbit
	int32 FillerDensity = 1;    // index into the density table (0 sparse .. 4 EXTREME); A/B step it
	bool bUpPrev = false, bDownPrev = false, bLeftPrev = false, bRightPrev = false;   // D-Pad rising-edge latches
	bool bLBPrev = false, bRBPrev = false;   // LB = style-light cycle, RB = reaction tap/hold

public:
	/** How long RB may be held and still count as a TAP. Above it the press is a hold and ramps
	 *  the reaction to CH4SpeedMax instead of toggling. Generous on purpose — a decisive tap is
	 *  well under 0.35 s, and a hold is not a gesture anyone performs by accident. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="0.05", ClampMax="1.5"))
	float CH4HoldToTap = 0.35f;
	/** Is the reaction running? Toggled by an RB tap; also settable here for a fixed show. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4")
	bool bCH4Running = false;

private:
	float CH4HoldT = 0.f;        // how long RB has been down this press
	bool bCH4SeqPrimed = false;  // the sequence player needs ONE Play/Pause before scrubs evaluate
	/** Actors tagged QZCH4FadeIn (the visiting enzymes) materialise in over this many seconds of
	 *  sequence time — their materials' SeqFade parameter rides the same noise erosion as the
	 *  station dissolve. At phase 0 (stopped) they are fully eroded away. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|CH4", meta=(ClampMin="0.1", ClampMax="20.0"))
	float CH4IntroFadeSeconds = 3.f;
	float CH4SpeedNow = 1.f;     // eased speed multiplier: 1 normally, CH4SpeedMax while held
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
	TMap<ULevel*, int32> LevelStationIdx;                             // which station owns each sublevel — lets a level-bound light use that station's SCALE, not just its fade
	TSet<ULevel*> TrackedLevels;                                      // every station sublevel ever seen (persistent)
	TMap<TWeakObjectPtr<ULightComponent>, float> LightBaseIntensity;  // captured base intensity, persistent
	/** A station light has to RIDE its station. The pawn scales the world about the Anchor, but a light
	 *  keeps whatever world position and attenuation radius it was authored with — so as the hero grows
	 *  the light illuminates a fixed sphere of space that the geometry sweeps through, which reads as
	 *  lighting that only works from certain angles. Both are captured once, relative to the Anchor, and
	 *  re-applied each frame scaled by the station. */
	TMap<TWeakObjectPtr<ULightComponent>, FVector> LightBaseOffset;   // authored position, relative to the Anchor
	TMap<TWeakObjectPtr<ULightComponent>, float>   LightBaseRadius;   // authored attenuation radius

public:
	/** KEEPS THE LOOK CONSTANT AS THE WORLD GROWS. A point light obeys inverse-square falloff, so
	 *  once its offset and attenuation both scale by S, the illuminance at the (also scaled)
	 *  surface falls by S^2 — the scene you art-directed at scale 1 goes progressively darker as
	 *  you descend, without any setting having changed.
	 *
	 *  Intensity is multiplied by S raised to this power to cancel that.
	 *      2 = physically exact: the lighting looks identical at every depth.
	 *      0 = no compensation (what it did before this existed).
	 *      between = a deliberate darkening or brightening with depth, if you want one. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Lights", meta=(ClampMin="0.0", ClampMax="3.0"))
	float LightScalePower = 2.f;
	/** Ceiling on that multiplier, so a station at 1500x cannot ask for an intensity that blows
	 *  the whole frame out through bloom before its own fade has taken it away. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Lights", meta=(ClampMin="1.0"))
	float LightScaleMaxMul = 250000.f;

private:
	TMap<TWeakObjectPtr<AActor>, float> PPBaseWeight;                 // PostProcessVolume base BlendWeight, persistent
	TMap<TWeakObjectPtr<ULightComponent>, float> LightFadeSmoothed;  // per-light eased fade (prolongs the ramp)
	TMap<TWeakObjectPtr<AActor>, float> PPFadeSmoothed;              // per-PP eased fade
	/** Speed the map-light + PP fade EASES toward the station's visibility. Lower = slower/prolonged ramp.
	 *  Michael wanted this ~3x slower than the instant snap it replaced. */
	UPROPERTY(EditAnywhere, Category="QZoomStage") float LightFadeSpeed = 1.2f;

	/** Lights reach FULL brightness once the station's fade passes this, instead of tracking it all the way
	 *  to 1. Tying light intensity directly to the dissolve ramp meant the lab stayed dark until the mushroom
	 *  was fully solid, and the 1.2 interpolator added ~2 s of lag on top — the first quarter of the descent
	 *  was pitch black. A scene should be LIT while it dissolves in, not lit because it finished. On the way
	 *  out this also keeps the light up until the station is nearly gone, so it erodes in daylight. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.05", ClampMax="1.0"))
	float LightFadeKnee = 0.30f;

	/** CAMERA DISSOLVE — surfaces melt open this close to the lens so you ENTER things instead of
	 *  the near plane slicing them. Expressed as a fraction of the station's own size, not in
	 *  absolute uu: the pawn multiplies these by the station's current scale before writing them,
	 *  because a fixed bubble is meaningless when the world spans four orders of magnitude.
	 *  Sized against STATION_SPAN_UU 3464: 350 is a tenth of a station, 1200 about a third. The
	 *  first values (120/600) were tuned while the distance term was PixelDepth — depth along the
	 *  view axis. Radial distance is the hypotenuse of that, so every off-axis surface reads as
	 *  further away and the same bubble stopped reaching the conidium wall entirely. Same bubble,
	 *  different ruler. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.0", ClampMax="4000.0"))
	float CamGateStartUU = 350.f;

	/** Distance over which a surface returns to solid, same scaling. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="1.0", ClampMax="12000.0"))
	float CamGateRangeUU = 1200.f;

	/** TUNNEL — the camera dissolve only bites inside a cone around the zoom axis, so it reads as
	 *  drilling in rather than the surface evaporating everywhere at once. Deliberately a cone in
	 *  WORLD space and not a screen vignette: Deep Space is an L, wall plus floor, rendered as
	 *  separate nDisplay views, and anything keyed off screen UV would give each viewport its own
	 *  centre — two bright spots instead of one opening, and the L stops reading as one space.
	 *  Fully open within this half-angle of the axis. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="1.0", ClampMax="89.0"))
	float TunnelInnerDeg = 40.f;

	/** Beyond this half-angle the surface stays solid no matter how close it is. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="2.0", ClampMax="180.0"))
	float TunnelOuterDeg = 85.f;

	/** Tunnel follows where you are LOOKING rather than being pinned to the anchor. On, the axis is
	 *  the current view forward, so free-look carries the opening with it; off, it always drills
	 *  toward the anchor whatever the operator does. Either way it stays a world-space cone, which
	 *  is what keeps wall and floor sharing one opening. */
	UPROPERTY(EditAnywhere, Category="QZoomStage")
	bool bTunnelFollowsView = true;

	/** "Lights turn on one by one" (Markos, 4.1). Each light on a station waits this much further into the
	 *  station's fade before it starts coming up, in first-seen order — so a scene switches itself on in
	 *  sequence instead of all at once. 0 = all together, as before. */
	UPROPERTY(EditAnywhere, Category="QZoomStage", meta=(ClampMin="0.0", ClampMax="0.5"))
	float LightStaggerStep = 0.07f;

	/** Detents: depths where the descent gets sticky, so the operator can settle on a beat instead of
	 *  holding the trigger against a moving target. These are the script's [PAUSE] marks. Empty = off. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Zoom Feel")
	TArray<float> ZoomDetents;

	/** How wide a detent's pull is, in ZoomProgress. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Zoom Feel", meta=(ClampMin="0.002", ClampMax="0.2"))
	float DetentWidth = 0.025f;

	/** Zoom rate multiplier at the exact centre of a detent. NOT zero — a detent that traps you is a bug,
	 *  not a feature; you should always be able to push through by holding the trigger. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Zoom Feel", meta=(ClampMin="0.05", ClampMax="1.0"))
	float DetentDamping = 0.35f;

	/** Slow automatic orbit, degrees/sec, added to the operator's stick input. The camera still never
	 *  moves — this rotates the world on the anchor, which is how 5.2's "orbit the nucleus" is done. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Zoom Feel", meta=(ClampMin="-20.0", ClampMax="20.0"))
	float OrbitAutoYawDegPerSec = 0.f;

	/** ═══ IDLE ORBIT — the show never sits dead still. ═══
	 *  After IdleOrbitAfterSeconds without ANY operator input (Slate's last-interaction clock, so every
	 *  binding present and future counts) the world starts a slow yaw drift with a gentle pitch breath,
	 *  eased in over IdleOrbitRampIn. Any input eases it back out over IdleOrbitRampOut — there is no
	 *  "return": the stick simply takes over from wherever the drift left the orbit, which is why the
	 *  pitch breath is applied as a DELTA around the operator's own pitch, never an absolute angle.
	 *  ZoomProgress is deliberately untouched — zoom is narrative, idle is only the camera wandering.
	 *  Runs in PollInput on the primary; wall + floor follow through the existing yaw/pitch broadcast. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Idle Orbit")
	bool bIdleOrbit = true;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Idle Orbit", meta=(ClampMin="1.0", ClampMax="300.0"))
	float IdleOrbitAfterSeconds = 10.f;
	/** Drift rate. 2.5 deg/s = one revolution in about 2.4 minutes. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Idle Orbit", meta=(ClampMin="-20.0", ClampMax="20.0"))
	float IdleOrbitYawDegPerSec = 2.5f;
	/** Pitch breath amplitude (± deg, around wherever the operator left the pitch). 0 = yaw only. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Idle Orbit", meta=(ClampMin="0.0", ClampMax="30.0"))
	float IdleOrbitPitchDeg = 4.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Idle Orbit", meta=(ClampMin="1.0", ClampMax="120.0"))
	float IdleOrbitPitchPeriod = 18.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Idle Orbit", meta=(ClampMin="0.05", ClampMax="30.0"))
	float IdleOrbitRampIn = 4.f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Idle Orbit", meta=(ClampMin="0.05", ClampMax="10.0"))
	float IdleOrbitRampOut = 0.6f;
	float IdleWeight = 0.f;      // 0 = operator, 1 = fully drifting; ramped, then smoothstepped
	float IdlePhase = 0.f;       // seconds into the pitch breath
	float IdlePitchPrev = 0.f;   // last applied breath offset, so only the delta is added each frame

	/** ═══ PERF BISECT — number keys 1-9 mute ladder rows 0-8 at runtime. ═══
	 *  A muted row goes down the same path as bActive=false in ApplyStations (hidden, children hidden,
	 *  fade cache zeroed), so its sublevel's whole draw cost leaves the frame — that is the point: watch
	 *  the FPS line (F2) while muting rows one by one and the expensive station names itself. Runtime
	 *  only and deliberately NOT saved with the level: bActive stays the authored truth, this is a
	 *  diagnostic overlay on top of it. Cluster-synced, so the wall bisects the same frame you do. */
	UPROPERTY(EditAnywhere, Transient, Category="QZoomStage|Perf Bisect", meta=(ClampMin="0"))
	int32 StationMuteMask = 0;
	uint16 PrevDigitBits = 0;    // rising-edge state for the 1-9 keys

	/** Y cycles the HUD through three states: 0 = default, nothing on screen (clean); 1 = the
	 *  editorial HUD exactly as before; 2 = the MUTE MENU — the readout becomes a station list,
	 *  D-Pad Up/Down moves the cursor, A mutes/unmutes the selected row, B unmutes everything.
	 *  While the menu is up the D-Pad and A/B are the menu's (PP preset and filler density are
	 *  guarded off so a bisect can't accidentally rebuild the fillers); LB/RB/X/triggers/sticks
	 *  keep working, so you can zoom to the bad spot WITH the menu open. Keys 1-9 work in every
	 *  mode. Startup state is this property — 0 per the show default, set 1 to boot with HUD. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Perf Bisect", meta=(ClampMin="0", ClampMax="2"))
	int32 HUDMode = 0;
	int32 MuteSel = 0;           // menu cursor, cluster-synced so the wall shows the same row

	/** Mute-menu [X]: swap every station material for one flat unlit opaque. The second axis of
	 *  the bisect — muting rows asks "which STATION costs", this asks "is it the SHADERS at all":
	 *  frame rate jumps on toggle = shader cost; stays put = geometry/overdraw/volumes. The
	 *  heterogeneous volumes are hidden while on (a ray-marcher has no simple version), Niagara
	 *  is left alone, and station fades stop landing (their DMIs are unbound) — diagnostic mode,
	 *  not show mode. Everything restores exactly on toggle-off. */
	UPROPERTY(EditAnywhere, Transient, Category="QZoomStage|Perf Bisect")
	bool bSimpleShaders = false;
	/** Mute-menu [D-Pad Right]: every Niagara system off/on. The third bisect axis — stations
	 *  (1-9/A), shaders (X), particles (this). Enforced per frame in the fade loop, so nothing
	 *  reactivates a muted system; cluster-synced. */
	UPROPERTY(EditAnywhere, Transient, Category="QZoomStage|Perf Bisect")
	bool bParticlesMuted = false;
	/** The stand-in. Auto-loaded from M_QZ_SimpleShader if unset. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Perf Bisect")
	TObjectPtr<UMaterialInterface> SimpleShaderMaterial;
	// flat save-state: strong refs on the materials (a swapped-out DMI has no other owner and
	// would be GC'd before restore), weak on the components (a streamed-out level must not leak)
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> SimpleSavedMats;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SimpleSavedComps;
	TArray<int32> SimpleSavedStart;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> SimpleHiddenVols;
	void ApplySimpleShaders(bool bOn);

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
	/** Push StationFade onto an actor's materials. GateMul scales the camera-dissolve bubble for this
	 *  stage only (FQZHandover::NearDissolve) — 1 keeps the shared behaviour. */
	void    SetStationFade(AActor* A, float Fade, float GateMul = 1.f);
	void    UpdateQuarkTriad(float Dt);              // wandering valence quarks + gluon strings that follow
	void    UpdateCH4Cycle(float Dt);                // looping redox cycle at the NirA station
	float   CH4Phase = 0.f;                          // 0..1, wraps forever
	FVector TunnelAxis = FVector::ForwardVector;     // camera -> anchor, world space, shared by every viewport
	int32   AudioLive = 0;                          // tracks actually spawned, for the readout
	float   AudioPeak = 0.f;                        // loudest track's current multiplier
	float   GateScale = 1.f;                         // station scale in flight, for the camera-dissolve bubble
	float   CH4Cycles = 0.f;                         // cycles completed THIS visit; resets when the station leaves
	TArray<float> StationFadeCache;                  // per-station fade, written by ApplyStations, read by UpdateLights
	TMap<TWeakObjectPtr<ULightComponent>, int32> LightOrder;   // first-seen index, drives the one-by-one stagger
	int32   LightOrderNext = 0;

	/** FPS on the scene readout: instantaneous, and the MEDIAN over a window.
	 *
	 *  Median rather than mean on purpose — a single 200 ms hitch drags a mean down for the whole
	 *  window and hides what the show actually runs at, while the median ignores it and reports
	 *  the typical frame. The hitch is still visible in the CURRENT figure as it happens. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info")
	bool bShowFPS = true;

	UPROPERTY(EditAnywhere, Category="QZoomStage|Info", meta=(ClampMin="1.0", ClampMax="120.0"))
	float FpsWindowSeconds = 20.f;

	void    SampleFPS(float Dt);
	TArray<float> FpsRing;                           // circular buffer of frame deltas
	int32   FpsHead = 0;
	int32   FpsFilled = 0;
	float   FpsCurrent = 0.f;                        // lightly smoothed, or it is unreadable
	float   FpsMedian = 0.f;
	float   FpsMedianTimer = 0.f;                    // resort a few times a second, not every frame
	double  FpsLastWall = 0.0;                       // real wall-clock of the last sample — Dt is a LIE under bUseFixedFrameRate

	/** Readout diagnostic: which station currently dominates and what fade it is being given,
	 *  next to how many cached DMIs actually carry a StationFade parameter. If a station is
	 *  fading in the numbers but not on screen, this says whether the parameter has anywhere
	 *  to land — SetStationFade only writes to materials that are ALREADY dynamic instances. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Info")
	bool bShowFadeDiag = true;
	int32 DiagStation = -1;
	float DiagFade = 0.f;

	void    UpdateRefParticles();                    // self-similar scale-reference field
	/** The activation/deactivation shell. Driven only by CH4Phase against CH4DockPhase /
	 *  CH4ReleasePhase, so re-timing the sequence re-times the energy with it. */
	void    UpdateCH4Energy(float Dt);
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CH4EnergyISM = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic>      CH4EnergyMID = nullptr;
	UPROPERTY() TObjectPtr<UPointLightComponent>          CH4EnergyLight = nullptr;
	TArray<FVector> CH4EnergyDir;      // unit direction per shard, generated once
	TArray<float>   CH4EnergyJitter;   // per-shard radius scatter, so the shell has thickness
	TArray<float>   CH4EnergyPhase;    // per-shard shimmer offset
	float           CH4EnergyClock = 0.f;
	/** Fade any actor tagged QZFade<N> with station N — visibility + StationFade on its materials,
	 *  WITHOUT moving or scaling it. Lights already had a level-independent route (QZLight<N>);
	 *  meshes, volumes and effects had none, so an environment asset dropped into a station
	 *  sublevel just sat there at full brightness through the whole descent. */
	void    UpdateFadeTagged();

	void    UpdateCommsStreams(float Dt);            // per-station outward data traffic
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CommsISM = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic>      CommsMID = nullptr;
	TArray<FVector> CommsDirs;                       // lane seed directions, generated once
	TArray<float>   CommsOffsets;
	/** Meandering trails in UNIT space (radius 0..1), CommsSteps points per trail, laid out flat.
	 *  Generated once from a fixed seed: a foraging path that is re-rolled every frame is not a
	 *  path, it is noise, and the whole point is that these routes persist and are followed. */
	TArray<FVector> CommsTrail;
	int32           CommsTrailLanes = 0;
	float           CommsClock = 0.f;
	static constexpr int32 CommsSteps = 24;          // points per trail
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> RefISM = nullptr;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic>      RefMID = nullptr;
	TArray<FVector> RefDirs;                         // unit directions, generated once
	TArray<float>   RefOffsets;                      // per-mote phase offset, generated once
	TArray<float>   RefSizeJitter;
	void    UpdateReadout();
	void    UpdateInfoLayer();         // the caption, driven by whichever station is actually on screen
	/** Apply FQZHandover::Preset / StyleStep when the dominant station changes. Primary only:
	 *  PPPreset and StyleLightStep are already broadcast to the other nodes, so letting every
	 *  node decide for itself would mean two sources of truth for the same value. */
	void    UpdateStagePresets();
	int32   AutoStagePrev = -2;        // last dominant station acted on; -2 = nothing yet
	/** Place both interface columns from UIDepth/UIMarginRight/UIMarginUp. Cheap and idempotent: it
	 *  early-outs unless one of the three changed, so it can run every frame and the numbers can be
	 *  dragged live in the Details panel during PIE. */
	void    LayoutInterface();
	float   UILaidDepth = -1.f, UILaidRight = -1.f, UILaidUp = -1.f;   // last applied triple
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
	/** Station N's authored size multiplier (Handover[N].SizeMul), 1 where no row is enabled. */
	float   StationSizeMul(int32 N) const;
	/**
	 * The scale the station is actually DRAWN at: StationScale(N) * StationSizeMul(N).
	 * Everything that positions or measures geometry uses this — the station transform, the drill
	 * bubble, light offsets and attenuation, the CH4 shell. Everything that decides WHEN a station
	 * is on screen keeps using StationScale, so a size change never re-times the ladder.
	 */
	float   StationRenderScale(int32 N) const;
	/** Make the derived ladder agree with the authored one. Fills StationCount and ScaleMeters from the
	 *  Handover rows, so those two stop being things anyone has to keep in sync by hand. Rows that leave
	 *  ScaleMeters at 0 keep whatever the legacy array had, which is what makes this safe to add to a
	 *  pawn that was already tuned. */
	/** Scale the shared environment rig by the stages currently on screen (FQZHandover::GlobalLight).
	 *  Authored intensities are captured once and everything is driven as a multiple of those, so
	 *  repeated frames cannot ratchet a value down to nothing. */
	void    UpdateGlobalEnv();
	TMap<TWeakObjectPtr<ULightComponent>, float> EnvBaseIntensity;   // authored values, captured once
	TMap<TWeakObjectPtr<UExponentialHeightFogComponent>, float> EnvBaseFog;
	float   EnvMul = 1.f;                  // smoothed, so a handover does not step the lighting
	float   FogScaleSmoothed = -1.f;       // ditto for the fog; -1 = not yet seeded

public:
	/** FOG IS MEASURED IN WORLD UNITS AND THE WORLD SCALES BY exp().
	 *  DepthFog is authored at FogDensity 1.0 — fifty times UE's default 0.02 — with
	 *  StartDistance 0 and MaxOpacity 1. That is the teal the whole piece sits in, and it is
	 *  correct at StationScale 1. But ApplyStations scales the world: the NirA station renders
	 *  at x15 around 56% and MET169 at x80 by 70%, so geometry that sat 1,000 uu away is
	 *  suddenly 80,000 uu away and far past fog saturation. The frame flattens to one hue —
	 *  and a flat frame is one a colour grade cannot act on, which is exactly why the presets
	 *  read as working on some stages and not others.
	 *
	 *  Nothing compensated for this: UpdateGlobalEnv only multiplied density by EnvMul, which
	 *  is permanently 1.0 because every Handover row leaves GlobalLight at 1.
	 *
	 *  Dividing density by the dominant station's scale keeps the fog DEPTH constant in
	 *  perceived terms — the same correction the camera-dissolve bubble already gets, and for
	 *  the same reason: "near" has to keep meaning the same fraction of whatever you are inside.
	 *  Turn this off to get the old behaviour back in one click. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Env")
	bool bFogTracksScale = true;
	/** Bounds on that correction, as multiples of the authored density. Without them a station
	 *  at x1e5 would divide the fog to nothing and one at x0.01 would make it opaque. */
	UPROPERTY(EditAnywhere, Category="QZoomStage|Env", meta=(ClampMin="0.001", ClampMax="1.0"))
	float FogScaleMin = 0.02f;
	UPROPERTY(EditAnywhere, Category="QZoomStage|Env", meta=(ClampMin="1.0", ClampMax="100.0"))
	float FogScaleMax = 4.0f;

private:

	void    SyncLadder();
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
