# Changelog

## [0.1] — 2026-04-24

### QuantumZOOM

- Renamed project from `DeepSpaceStarter` to `QuantumZoom` (`.uproject`, content folder)
- Added `QuantumZoom/TestAssets/` content folder
- Updated `Deep_Space_8K.umap` with DCRALoopTest actor
- Updated `project.json` with correct executable path (`QuantumZoom.exe`)
- First packaged build: `QuantumZOOM_2026-04-24_3D_v0.1.zip`

### QuantumTools Plugin

- Added `DCRALoopTest` C++ actor — drives `DisplayClusterRootActor` on a sine loop
- Confirmed DCRA-based navigation works in UE 5.7 nDisplay cluster
- Sphere visual reference with configurable show/hide, size, color
- Configurable `OscillationCenter` (X,Y,Z), amplitude, cycle duration, axis
- Mesh loaded at runtime for reliability in nDisplay context
- Added `nDisplay` plugin dependency to `QuantumTools.uplugin`

### Local Cluster Testing

- Added `nDisplay_LocalTest.ndisplay` — 2-node cluster config on a single machine (both nodes `127.0.0.1`)
- Removed `dc_dev_mono` launch flag to enable true cluster mode locally
- Wall: window (0,0) — Floor: window (960,0), side by side

### Repo & Project Setup

- Forked from `ArsElectronicaFuturelab/UE-DeepSpace-Starter`, synced to upstream `main` (2026-03-11)
- Backup tag: `upstream-sync-2026-04-23`
- Renamed default branch to `QuantumZoom_dev`
- Added `SUMMARY.md` and `CHANGELOG.md`
- Cleaned up README for QuantumZOOM context

---

## Upstream — ArsElectronicaFuturelab/UE-DeepSpace-Starter

### 2026-03-11
- Updated descriptions and naming consistency in AefPharus docs

### 2026-03-05
- Updated nDisplay configs and paths
- Added GitHub Actions release workflow
- Renamed `BP_MozXR_Library` → `BP_DeepSpace_Library`
- Added `nDisplay_Deep_Space_8K.ndisplay` to `Content/Switchboard/`
- Removed `project-test.json`

### 2026-02-06
- Updated AefPharus plugin to latest state
- Removed obsolete files

### 2026-02-05
- Initial UE 5.7 state
- Added Pharus plugin and simulators
- Updated documentation and binaries
