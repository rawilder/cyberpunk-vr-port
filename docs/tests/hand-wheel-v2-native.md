# Test: the game's weapon wheel on the hand (v2)

Build: branch `frame`. The drawn wheel (v1) is still in the DLL behind `CyberpunkVR_HandWheelMode=0`;
the default is now mode 1: the GAME's radial wheel, re-hosted on the right hand as its own HUD
channel, steered by hand displacement through a synthesized right stick.

Caveat for THIS run: the wheel's HUD entry name is a guess (`radial_wheel`) until the log
confirms it. If it's wrong, the wheel still opens full-screen as before, but the selection
should already follow your hand. Either way, this run produces the two log lines I need.

## Steps
1. Launch, load a save, stand somewhere safe.
2. Hold Y. Note: (a) does the wheel appear on your hand or full-screen? (b) with Y held, move
   the hand toward a slot: does the game's own highlight follow the hand? (c) release: equips?
3. Tap Y: still cycles.
4. Hold Y and, while held, check that your view does NOT snap-turn or dash/crouch when you move
   the hand (those are gated while the wheel is up).
5. Quit.

## Report
- (a)(b)(c) above, and whether anything felt laggy.
- I'll read `cyberpunkvrport.log` for `[hud-panel] N HUD entries` and CET's
  `CyberpunkVRPort_Stereo` log for `[HudPanel] controllers`.
