# QuantumZOOM

Interactive installation for **Ars Electronica Center – Deep Space 8K**, Linz.
Wall + floor projection, each 16m × 9m at 8K resolution.

## Base

Forked from [ArsElectronicaFuturelab/UE-DeepSpace-Starter](https://github.com/ArsElectronicaFuturelab/UE-DeepSpace-Starter)
GitHub: [wilhelm-media/QuantumZoom](https://github.com/wilhelm-media/QuantumZoom)

## Tech Stack

| Component | Details |
|---|---|
| Engine | Unreal Engine 5.7 |
| Rendering | nDisplay – multi-node cluster (wall + floor) |
| Tracking | Pharus laser tracking – IR visitor detection on floor |
| Cluster mgmt | Switchboard |
| Plugin | AefPharus (C++ + Blueprints, pre-compiled binaries included) |
| Platforms | Windows 64-bit / Linux |

## Key Files

```
DeepSpaceStarter/
├── Project/
│   ├── DeepSpaceStarter.uproject        ← open this in UE 5.7
│   ├── Config/AefConfig.ini             ← Pharus tracking config
│   └── Content/DeepSpace/
│       ├── Maps/Deep_Space_8K.umap      ← main level
│       ├── Library/BP_DeepSpace_Library ← blueprint library
│       └── Switchboard/                 ← nDisplay assets
├── Deploy/
│   ├── Build/                           ← packaged output goes here
│   ├── Switchboard/nDisplay_Deep_Space_8K.ndisplay
│   └── project.json                     ← deployment config
└── Pharus/DeepSpace/                    ← simulator tools (dev/testing)
```

## Branches

| Branch | Purpose |
|---|---|
| `main` | active development |
| `UE-5.7-DeepSpace-Starter` | base upstream template |
| `Quantum` | reserved for custom feature work |

## Backup

Tag `upstream-sync-2026-04-23` = clean verified state, identical to upstream.
Restore: `git reset --hard upstream-sync-2026-04-23`

## Deployment

1. Package project → `Deploy/Build/Windows/`
2. Export nDisplay config → `Deploy/Switchboard/`
3. Update `Deploy/project.json` with correct `startMap`
4. Zip `Deploy/` folder: `QuantumZOOM_YYYY-MM-DD_2D_v1.0.zip`
5. Hand off to Ars Electronica

## Performance Settings (Deep Space)

| Setting | Value |
|---|---|
| Framerate | 30 or 60 FPS fixed |
| Render API | DirectX 12 |
| Anti-Aliasing | FXAA |
| Ambient Occlusion | Disabled |
| Dynamic GI | None |
| Reflections | None |
