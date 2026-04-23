# QuantumZOOM

[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.7-blue)](https://unrealengine.com)
[![Status](https://img.shields.io/badge/status-under%20construction-orange)]()

Interactive installation for **Ars Electronica Center – Deep Space 8K**, Linz.

> **This repository is under active development.** Structure and content will change significantly. Not ready for external use.

---

## About

QuantumZOOM is an interactive real-time installation built for the [Deep Space 8K](https://ars.electronica.art/center/de/deepspace/) at the Ars Electronica Center — a room-scale projection environment with wall and floor screens, each 16m × 9m at 8K resolution.

Built on top of the [UE-DeepSpace-Starter](https://github.com/ArsElectronicaFuturelab/UE-DeepSpace-Starter) template by Ars Electronica Futurelab.

---

## Tech Stack

| Component | Details |
|---|---|
| Engine | Unreal Engine 5.7 |
| Rendering | nDisplay – multi-node cluster (wall + floor) |
| Tracking | Pharus laser tracking |
| Cluster management | Switchboard |
| Plugin | AefPharus (C++ + Blueprints) |
| Platforms | Windows 64-bit / Linux |

---

## Requirements

- Unreal Engine 5.7
- Windows 64-bit (Linux supported)
- Git LFS

---

## Getting Started

```bash
git clone https://github.com/wilhelm-media/QuantumZoom.git
cd QuantumZoom
```

Open the project:
1. Navigate to `DeepSpaceStarter/Project/`
2. Open `DeepSpaceStarter.uproject` with UE 5.7

Test locally via Switchboard:
1. Launch Switchboard and Switchboard Listener
2. Add nDisplay Cluster → Populate
3. Select `nDisplay_Desktop.uasset` for local testing
4. Connect listener → Start all display devices

---

## Repository Structure

```
DeepSpaceStarter/
├── Project/               ← UE5 project (open this)
│   ├── Config/            ← Engine + Pharus config
│   ├── Content/DeepSpace/ ← Maps, Blueprints, Materials
│   └── Plugins/AefPharus/ ← Pharus tracking plugin
├── Pharus/                ← Simulator tools for local testing
└── Deploy/                ← Deployment package for Deep Space
    ├── Build/             ← Packaged output (generated)
    ├── Switchboard/       ← nDisplay production config
    └── project.json       ← Deployment config
```

---

## Collaboration

Contact: [wilhelm-media](https://github.com/wilhelm-media)

---

## Credits

Base template by [Ars Electronica Futurelab](https://ars.electronica.art/futurelab/)
AefPharus plugin by Ars Electronica Futurelab


---

## License

MIT — see [LICENSE](LICENSE)
