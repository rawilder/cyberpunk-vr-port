# Performance measurement, one sitting

Goal: real fps, CPU ms, GPU ms and 1% lows for a fixed set of configurations, from the
`[framegen] stats` line the plugin logs every 5 s (`xr_framegen_log=5`). Nothing to read in
the headset; just play each configuration for ~3 minutes in the same area.

Keep constant: busy street area, 2560x2560, DLSS Quality, framegen on, SSR off.

## Before the run (Adam, once)
- SteamVR -> Settings -> Video: resolution/supersampling 100%, refresh 90 Hz.
- SteamVR per-app (Cyberpunk) settings: Motion Smoothing OFF.

## Configurations (3 min each, in this order; quit the game between A and B only if told)
- **A** `xr_cpu_affinity=1` (default in this build): P-cores + above-normal priority.
- **B** `xr_cpu_affinity=0`: the switch is live; change it in vrport.ini while in the main
  menu and reload a save. (I can do the edit between runs if you prefer.)
- **C** like A, with the HandCollision and TacticalBelt CET mods disabled (I'll rename their
  folders between runs; tells us what per-frame Lua costs).
- **D** like A at 72 Hz.

## After
`pwsh scripts\perf_summary.ps1` prints a summary per log; I'll copy each run's log aside
between configurations so they don't overwrite each other.

Decision rules:
- CPU ms > GPU ms -> we're CPU-bound: keep affinity, cut per-frame Lua, 72 Hz + framegen.
- GPU ms > CPU ms -> DLSS Balanced / smaller foveation radii / 2x2->4x4 buy real headroom.
- real fps 1% low >= 45 at 90 Hz -> stay at 90; otherwise 72 Hz.
