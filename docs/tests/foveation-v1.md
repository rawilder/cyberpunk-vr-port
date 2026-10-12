# Test: fixed foveated rendering v1

Build: self-built from upstream a0c23fe + `src/Render/Foveation.cpp`. Release DLL kept at
`Downloads\cp-vr-staging\port\release-0.1.7-dll\` for rollback.

Defaults: on, 1x1 inside 0.35, 2x2 to 0.65, 4x4 beyond. Keys in `bin\x64\vrport.ini`:
`xr_foveation`, `xr_foveation_inner`, `xr_foveation_outer`, `xr_foveation_rate`.

## Setup
- SteamVR 90 Hz, launcher 2560 x 2560, DLSS Quality, framegen on (more pixels = more to save).
- Pick one busy street spot and stand still for every reading. Don't move between readings.

## Steps
1. Launch. Load a save, stand at the spot.
2. Open the VR overlay (hold L3+R3 1 s, or F10) -> STEREO tab -> PERFORMANCE.
   - "Foveated rendering (fixed)" should be ticked and the line under it should read
     `Foveation: 4x4 beyond 0.65, 2x2 beyond 0.35, NxN tiles` (N about 107 at 2560 + DLSS Quality).
   - If it says `off: ...` or `off`, stop and report.
3. **Reading A (on):** close the overlay, read the perf overlay's `GPU ... MS` figure, wait 10 s, read again.
4. **Reading B (off):** untick the checkbox, close the overlay, read GPU ms twice the same way.
5. Tick it back on. **Look for artifacts:** edges of the view, signs/text at the periphery, the HUD,
   specular highlights, anything that shimmers or blocks up. Turn your head: does the centre
   stay sharp?
6. **Make it obvious (validation only):** drag "Coarsest beyond" down to 0.30 and "Full-detail
   radius" to 0.15. You should clearly see a sharp centre and a blocky ring. If you don't, the
   substitution isn't reaching the passes that matter -> report. Put the sliders back
   (0.35 / 0.65) or just say so and I will reset the ini.
7. Quit.

## Report
- GPU ms A vs B (two readings each).
- Artifacts seen, where.
- Whether step 6 showed the ring.
- Any crash, freeze or black frame (I'll read `cyberpunkvrport.log`).

## Pass
- No crash; step 6 shows the ring; GPU ms measurably lower with it on; nothing you'd notice in
  normal play at the defaults.
