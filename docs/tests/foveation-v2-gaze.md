# Test: eye-tracked foveation v2

Build: upstream a0c23fe + Foveation v2 (gaze). v1 (fixed) passed on 2026-10-12: the
substitution reaches the scene passes (ring visible at small radii).

New in v2: the full-detail region follows the gaze from `XR_EXT_eye_gaze_interaction`.
Falls back to the centre of each eye when the runtime gives no tracked gaze, and holds
the last gaze for 300 ms through a blink. New key: `xr_foveation_gaze=1`.

## Setup
- Eye tracking calibrated in the Frame (SteamVR settings; redo it if the headset was
  re-seated). Same resolution/refresh as the v1 run so numbers compare.

## Steps
1. Launch, load a save, stand still on the busy street.
2. Overlay -> STEREO -> PERFORMANCE. "Foveated rendering" and "Follow eye tracking" both ticked.
   Status line should start with `eye-tracked |`. If it says `fixed (no eye data)` or
   `fixed (no tracked gaze)`, note it and continue; I'll read the log.
3. **See it follow.** Set "Full-detail radius" 0.15 and "Coarsest beyond" 0.30 so the ring is
   obvious. Keep your head still and **move only your eyes** to a sign at the edge of view.
   The sharp region should move with your eyes; the blocky ring should always sit around
   wherever you are looking, never on it. Blink: it should not jump.
4. Put the sliders back (0.35 / 0.65). Play 2 minutes normally. Can you catch the ring in
   peripheral vision at all? Any flicker at the boundary when your eyes move?
5. **GPU ms**, same spot, three readings: gaze on / gaze off (untick "Follow eye tracking",
   the pattern recentres) / foveation off.
6. Quit.

## Report
- Status line text.
- Did the sharp region follow your eyes (step 3)? Any lag you could notice?
- GPU ms for the three states.
- Artifacts / flicker.
