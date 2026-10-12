# Baseline — Cyberpunk 2077 VR on the Steam Frame

Phase 0 deliverable. Recorded 2026-10-09 to 2026-10-12.

## Versions

| Component | Version | Note |
|---|---|---|
| Cyberpunk 2077 | 2.31 (Steam build 20383525) | Launch option `--launcher-skip`; auto-update set to "only when I launch it" |
| Port | 0.1.7 release, then self-built from a0c23fe + foveation | Release DLL kept at `Downloads\cp-vr-staging\port\release-0.1.7-dll\` |
| RED4ext | 1.30.0 | |
| Cyber Engine Tweaks | 1.37.1 | |
| redscript | **0.5.31** | 1.0.0-preview.22 fails: Codeware's `@addField` on a native type is rejected by the 1.0 compiler |
| ArchiveXL | 1.27.4 | |
| TweakXL | 1.11.4 | |
| Codeware | 1.20.5 | |
| Visual Holsters | 1.2 | Nexus 21936 |
| Visible Bullets | 2.31 | Nexus 22251; one YAML had a 3-space indent typo (Borg4a_Default), fixed locally |
| Equipment-EX | 1.2.9 | Nexus 6945 |
| Nova Optics | 1.4.1 | Nexus 29190 |
| Input Loader | 0.2.3 (reports 0.1.1 to RED4ext) | Nexus 4575 |
| More Occluders | Scripted v0.3 | Nexus 33136; NOT the ArchiveXL v0.4 file |
| SteamVR | 2.17.10 (OpenXR runtime "SteamVR/OpenXR") | System default OpenXR runtime = SteamVR |
| NVIDIA driver | 617.42 | |

Install bookkeeping: `Downloads\cp-vr-staging\tools\Install-Mod.ps1`, one manifest per mod in
`cp-vr-staging\installed\`. Save and settings backups in `Documents\Cyberpunk Save Backups\`.

## Hardware

- RTX 3090 (24 GB), VRS tier 2, 16 px shading-rate tiles, additional rates (4x4) supported
- i7-12700K, 64 GB RAM
- Steam Frame, 90 Hz, SteamVR recommended render 2644x2644 per eye, runtime HFOV 109.5°
- Frame wireless adapter: Realtek 8832CU USB ("Wi-Fi 2"), on a motherboard USB port

## Things that cost a session each

1. **Streaming went through the home router, not the adapter.** Symptom: constant hitching,
   black edges on fast head turns, smooth on the desktop mirror. SteamVR's `driver_vrlink.txt`
   showed 500+ "Bad link" events in 17 min and a primary link of 192.168.1.145 (Ethernet).
   Fix: pair the headset to the adapter. Afterwards the primary link is 10.35.x.x and bad-link
   events drop to a handful at pairing time.
2. **Eye gaze arrives only with "Share eye tracking data to other apps on this PC" on**, in
   SteamVR -> Settings -> Steam Link. Default off. With it off the gaze action binds but stays
   inactive (`actionActive=0 flags=0x0` in the log).
3. **redscript 1.0 previews break Codeware** (see table).
4. **Launcher default was 2048x2048 with DLSS Balanced** = ~1190 px per eye rendered. Pixelated.

## Checklist results

| Check | Result |
|---|---|
| Stereo, comfortable, no double vision | Pass |
| HUD readable | Pass |
| Hands track | Pass. VRIK auto-calibration NOT yet run (calibration file at defaults) |
| Native Frame controller profile | Active (`frame_controller_valve`, 24 bindings) |
| Controls | "Mostly mapped, unintuitive" — full per-row table still to be done |
| Weapon wheel | Hold Y opens the game's full-screen wheel; works, wrong for VR (overhaul §4.4) |
| Physical reload | Not exercised: Copperhead/Satara are not in the 13 supported weapons |
| Motion melee | Not exercised with a swing |
| Driving wheel grab | Not exercised |

## Frame times

Not yet measured with numbers. Observations: at 2560x2560 / DLSS Quality / framegen on, GPU
~85% utilisation and the game's main thread ~90% of one core (before the streaming fix). To do:
GPU ms at one fixed busy spot for foveation on / off, at 2560 + DLSS Quality + 90 Hz.

## Settings that work (so far)

- Launcher: Steam Frame, 2560x2560 (2160 while diagnosing streaming); SteamVR 90 Hz
- `vrport.ini`: `xr_framegen=1`, `xr_framegen_overlay=1`, `xr_nvidia_reflex=0`,
  `xr_foveation=1`, `xr_foveation_gaze=1`, radii being tuned (0.15/0.30 recommended)
- Game: DLSS Quality, SSR off, volumetric fog Low, contact shadows off, facial tangents off
