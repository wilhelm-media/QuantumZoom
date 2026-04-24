# QuantumZOOM — Zoom System Spec
**Version:** 0.2 — 2026-04-24
**Engine:** Unreal Engine 5.7
**Target:** Ars Electronica Deep Space 8K — nDisplay cluster (Wall + Floor, 3D Stereo)

---

## 1. Concept

The audience experiences a continuous zoom journey through scales of matter. The zoom is driven by gamepad input. The system manages DCRA movement within a stage and seamless level transitions between stages.

Two layers:

| Layer | Responsibility |
|---|---|
| **Navigation** | DCRA position/orientation — driven by controller input |
| **Stage System** | Level streaming, cross-fade transitions between Zoom Stages |

Both layers are controlled by a single authoritative controller running on the **nDisplay primary node** (Node_Wall) only.

---

## 2. Input

| Button | Action |
|---|---|
| R1 / RB (hold) | Zoom In |
| L1 / LB (hold) | Zoom Out |

- Continuous motion while held — no step presses
- Direction can be reversed at any time
- Input processed on primary node only — replicated via nDisplay cluster event system

---

## 3. ZoomLevel

A single global unbounded float: `ZoomLevel`.

- Increases while zooming in, decreases while zooming out
- Rate is parameterized: `ZoomSpeed` (units/sec)
- `ZoomLevel` is divided into discrete **Zoom Stages** via fixed thresholds defined in the DataAsset

```
ZoomLevel:   0.0────────1.0────────2.0────────3.0
Stage:          [  0  ]      [  1  ]      [  2  ]
```

**Within a stage**, `ZoomLevel` maps to DCRA offset (navigation through the scene content).
**At stage boundaries**, the Stage System handles level transition.

---

## 4. DCRA Navigation

The `DisplayClusterRootActor` is the sole "camera" in Deep Space. Moving it shifts the entire rendered world relative to the fixed screens.

- ZoomDirector maps `ZoomLevel` → DCRA world position each frame
- Mapping is defined per-stage in the DataAsset (start position, end position, easing curve)
- Only primary node writes to DCRA — nDisplay replicates automatically

```
Stage 0: DCRA moves from PosA → PosB as ZoomLevel 0.0 → 1.0
Stage 1: DCRA moves from PosC → PosD as ZoomLevel 1.0 → 2.0
```

---

## 5. Stage Definition (DataAsset)

Each `FZoomStage` entry in the `DA_ZoomStages` DataAsset:

| Field | Type | Description |
|---|---|---|
| `StageName` | FName | Identifier |
| `Level` | TSoftObjectPtr<UWorld> | Streamable sublevel |
| `DCRA_Start` | FTransform | DCRA transform at stage entry |
| `DCRA_End` | FTransform | DCRA transform at stage exit |
| `PreloadThreshold` | float | ZoomLevel distance before boundary to begin preload |
| `FadeDuration` | float | Seconds for cross-fade transition |

Adding a new stage = one new DataAsset entry. No code changes required.

---

## 6. State Machine

```
        ┌─────────────────────────────────────────────┐
        ▼                                             │
      IDLE ──approach boundary──► PRELOADING          │
        ▲                              │              │
        │                         load complete       │ reversal
        │                              ▼              │
        │                       TRANSITIONING ────────┘
        │                         (FadeIn + FadeOut concurrent)
        │                              │
        │                       fade complete
        │                              ▼
        └──────────────────────── UNLOADING
```

**Reversal handling:**

| Reversal during | Behaviour |
|---|---|
| PRELOADING | Cancel async load, return to IDLE |
| TRANSITIONING | Complete fade-out of incoming stage, snap back to previous — never leave a half-visible state |
| UNLOADING | Abort unload, keep level in memory |

**Invariant:** At all times, at least one stage is fully visible. No void frames.

---

## 7. Transition Sequence

1. **Preload** — async stream next stage level (hidden, no blocking)
2. **Gate** — wait for load complete before starting fade
3. **FadeIn** — next stage fades in over `FadeDuration`
4. **FadeOut** — current stage fades out concurrently
5. **Unload** — previous stage unloaded after fade-out completes

Cross-fade is time-based. Duration parameterized per stage.

---

## 8. System Components

| Component | Type | Role |
|---|---|---|
| `ZoomDirector` | C++ Actor | Reads input, maintains ZoomLevel, drives DCRA, triggers StageManager |
| `StageManager` | C++ Subsystem | Async load/unload of stage levels, exposes load state |
| `TransitionController` | C++ / BP | Executes fade timeline, manages stage visibility |
| `DA_ZoomStages` | DataAsset | Ordered stage list with levels, DCRA transforms, thresholds |
| `Debug_ZoomHUD` | UMG Widget | Live state overlay (see §9) |

All components communicate through the `ZoomDirector` as the single authority. No component drives another directly except through `ZoomDirector`.

---

## 9. Debug HUD

Minimal overlay — always available in non-shipping builds:

```
ZoomLevel:        1.47
Stage Current:    1 (Methionin)
Stage Target:     2 (CarbonCore)
State:            TRANSITIONING
Fade:             0.62s / 1.5s
DCRA:             X=330 Y=0 Z=180
```

---

## 10. nDisplay Constraints

- All game logic runs on **primary node** (Node_Wall) only
- DCRA writes from primary — propagated automatically
- Level streaming must be triggered on **all cluster nodes** simultaneously via cluster events — not just primary
- Fade/visibility state broadcast via `DisplayClusterClusterEvent` to keep all nodes in sync
- No blocking loads on any node

---

## 11. Acceptance Tests

| ID | Test | Pass Condition |
|---|---|---|
| AT1 | Hold RB from Stage 0 → N | Transitions via cross-fade, no hard cuts |
| AT2 | Reverse during PRELOADING | Load cancels, returns to current stage cleanly |
| AT3 | Reverse during TRANSITIONING | No void frames, valid stage always visible |
| AT4 | Unload timing | Previous stage unloaded only after fade-out complete (verify via HUD) |
| AT5 | Rapid oscillation | No crashes, leaks, or orphaned levels after 60s stress test |
| AT6 | New stage via DataAsset | Add entry only — transitions correctly without code changes |
| AT7 | Floor + Wall sync | Both displays transition simultaneously, no desync |

---

## 12. Out of Scope

- Project-specific art, scientific content, story logic
- World origin shifting / origin rebasing
- Pharus tracking integration (separate system)
- Audio transitions
