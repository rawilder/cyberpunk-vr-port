# Test: hand-anchored weapon wheel v1

Build: branch `frame`, `src/Overlay/HandWheel.cpp` + `CyberpunkVRPort_HandWheel` (CET + reds).

What it should do: **hold right Y** ~0.2 s → a ring appears on your right hand facing you,
with the three weapon slots and HOLSTER, and the game slows. **Move the hand** toward a sector
to light it. **Release Y** to equip it. **B** cancels. A quick **tap** of Y still cycles weapons
as before. The game's own full-screen wheel should never appear.

## Steps
1. Launch, load a save, stand somewhere safe.
2. Tap Y: the old weapon cycle should still happen. The full-screen wheel must NOT open.
3. Hold Y: the ring appears on the hand. Does it face you? Is it readable (names on the
   sectors)? Does the game slow down?
4. With Y held, move the hand up/left/right/down a few cm: the nearest sector should light
   (yellow). Back to the start position: nothing lit.
5. Release on a weapon: it equips. Release in the middle: nothing happens. Hold again and
   press B: the ring closes with no change.
6. Try it while walking and while in a car.
7. Quit.

## Report
- Did the full-screen wheel ever appear?
- Ring position/size/facing, readability. Too big / small / far from the hand?
- Did the lit sector match where your hand went? Dead zone too big/small?
- Did the equip happen on release, and was the slow-mo noticeable?
- `cyberpunkvrport.log` lines starting `[handwheel]` tell me the opens/releases; CET's
  `CyberpunkVRPort_HandWheel` log will show any Lua errors.

## Known limits (v1)
- Labels are the 5x7 pixel font in capitals; icons come with the in-world version.
- Dead zone 3 cm, hold 180 ms, size 16 cm: exported as `CyberpunkVR_HandWheel*` for tuning.
