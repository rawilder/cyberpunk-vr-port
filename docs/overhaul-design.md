# VR gameplay overhaul — design

Phase 4 of the Steam Frame plan. Goal: make Cyberpunk 2077 play like a game built for VR
(Half-Life: Alyx as the reference), on top of dariulone's port, without leaving the
engine's reachable surface or losing the ability to rebase on upstream.

Status: **draft for Adam's sign-off.** Each section ends with the decisions it needs.

## 1. Scope and constraints

| | |
|---|---|
| Target | Adam's PC → Steam Frame, PC streaming. No other headsets. |
| Assets | Game-shipped meshes, animations and props only (WolvenKit / ArchiveXL / TweakXL). |
| Mergeability | New systems live in **new modules** that replace upstream ones by configuration. The C++ plugin (`CyberpunkVR_Stereo.dll`) is edited only where a system cannot be built in CET/redscript. |
| Order | Melee → guns → locomotion → enemies → driving. Each is shippable on its own. |

### The engine ceiling

Everything below is designed against these limits, measured by upstream
(`mods/cet/CyberpunkVRPort_HandCollision/init.lua` header):

- Hands and held weapons are **kinematic** colliders. They push dynamic props; nothing pushes
  them back, and two kinematic bodies never interact. There is no physical blade-stops-in-body,
  no weapon knocked from the hand, no recoil from a parry other than what we fake.
- NPCs are animated, not simulated, until death. Hit reactions are a **fixed set** of animations;
  we choose among them, we cannot add to them. Ragdoll and dismemberment happen on kill, natively.
- Enemy AI stays the game's. We tune its numbers (damage, accuracy, cadence); we do not script new
  behaviour.
- Scripts can read tracked controller poses every frame (CET) and call native systems (redscript).
  Damage, status effects, hit reactions and stats are all reachable from redscript.

## 2. What exists today (upstream 0.1.7)

| System | Upstream module(s) | What it does | Keep / replace |
|---|---|---|---|
| Melee | `CyberpunkVRPort_Weapon` (CET) + `CyberpunkVRPort_Melee` (reds) | Swing detection ≥2.5 m/s; blade-segment contact test; then fires the game's native melee `Attack_GameEffect` on that enemy, re-injecting the weapon's native damage. | **Replace** the attack half; keep swing/contact detection. |
| Physical reload | `CyberpunkVRPort_Reload`, `TacticalBelt` | Drop mag (B), grab mag at belt (left grip), rack slide (R3). 13 weapons with authored hand poses. | **Keep**, extend to all weapons. |
| Two-hand hold | plugin + `mods/config/*TwoHandGrip*.ini` | Off hand locks to captured pose; ~50 weapons. | Keep. |
| Holsters | `CyberpunkVRPort_Holster` + Visual Holsters | Grip at body zone equips/unequips. | Keep; retune zones for Frame controllers. |
| Hand collision | `CyberpunkVRPort_HandCollision` | Hands/fingers/weapon stop at the body. | Keep. |
| Locomotion | plugin (`xr_movement_source`, snap turn, roomscale) | Stick, HMD/hand-relative, snap/smooth turn, native roomscale. | Keep; **add** teleport. |
| Driving | `CyberpunkVRPort_Driving` + plugin wheel grab | Grip at wheel, tilt of hands steers; horn, throttle latch. | Keep; tune. |
| Enemies | — | Nothing. | **Add** tuning layer. |

## 3. Melee v2

### Target feel
- Damage happens because *my blade touched them*, scaled by *how fast I swung* and *how well the
  edge was aligned*. No native attack state, no cooldown I didn't cause.
- Enemies react in the direction and strength of the hit, from the game's own reactions.
- Holding the blade in the path of an incoming strike **parries** it.
- No canned player animation. Ever.

### Design
1. **Hit source.** Keep upstream's per-frame blade segment vs. enemy test (`VRMeleeBladeHit`
   geometry). Add: edge alignment (angle between blade edge normal and velocity), contact
   point on the body (head / torso / limb via the hit bone or nearest body component).
2. **Damage model.** Our own `gameHitEvent` built from an `AttackData` on a **TweakXL attack
   record of ours** (`Attacks.VRMeleeContact_*` per weapon family), delivered through
   `DamageSystem`. Damage = weapon base damage × speed curve (0 below 1.5 m/s, 1.0 at 3 m/s,
   1.6 at 5 m/s, clamped) × edge factor (1.0 edge-on, 0.35 flat) × location (head ×1.5, limb ×0.7).
   Blunt weapons ignore edge factor. Crits, armor and damage numbers run natively because the
   hit goes through the normal pipeline.
3. **Reaction picker.** Choose the hit-reaction type and direction from contact point and
   impulse, instead of letting the native attack choose: light → flinch, medium → stagger in
   the direction of the swing, heavy (≥5 m/s or two-handed) → knockdown where the enemy type
   allows it. Implemented as the `hitReactionType` / `hitDirection` fields on our hit event.
4. **Cadence.** One hit per enemy per swing *episode* (speed crossing the threshold, re-armed
   when the hand slows), so a single stroke through three enemies hits all three once.
5. **Parry.** During an enemy melee attack's wind-up, if our blade segment intersects the
   enemy's weapon segment (or the attacking limb for unarmed), cancel their attack via the
   game's parry/block path, play the clash FX and sound, apply a short stagger to them and a
   wrist kick to us (reuse the recoil spring). Blocks by holding L-trigger stay as a fallback.
6. **Feedback.** Controller haptics scaled by impulse; whoosh per swing as today; hit sound by
   material from the game's own hit audio events.
7. **Cyberware arms** (Mantis, Monowire, Gorilla) use the same path; the "blade" is the arm
   segment.

### Not in scope
Blade stopping in the body; enemies physically pushed; stabbing depth; new animations.

### Spike before full build (1 day)
Prove on a street thug that a script-built hit event through `DamageSystem` (a) applies
damage with numbers and armor, (b) plays the reaction we asked for, (c) does not require the
player to be in a melee attack state. If (c) fails, fall back to upstream's re-injection trick
but with our damage model and reaction override.

**Decisions needed:** damage curve numbers (above are a starting point); whether a flat-side hit
should do token damage or none; whether knockdown is allowed on humans or reserved for heavy
weapons.

## 4. Guns

### Target feel
Every gun reloads by hand; every two-handed gun is held with two hands; guns live in holsters
on the body, not in a menu.

### Design
1. **Reload for every weapon.** Upstream's reload is data-driven per weapon
   (`reload/weapons/<name>.lua`: magazine slot, hand poses, stages). The 13 existing files are
   the template. Add the remaining weapons in priority order: everything in Adam's current
   loadout first, then by family. Where a weapon has no authored hand pose, reuse the closest
   family pose (the file format already allows it). Shotguns and revolvers use the existing
   shell/cylinder paths.
2. **Rack and slide.** Keep R3 as the rack for now; later experiment: rack by pulling the
   slide with the off hand (grip at the slide, pull back 6 cm) once the Frame mapping is settled.
3. **Holsters.** Keep immersive holsters. Retune zone radii and positions after VRIK
   calibration on the Frame; the current zones were tuned on Touch controllers.
4. **Weapon wheel in the world (Alyx-style).** Tested 2026-10-12: the game's wheel works on the
   Frame but it is the full-screen UI, which is the wrong thing in VR. The game's wheel is
   **not used at all**; the Y hold is intercepted before it reaches the game.
   - **What appears.** Holding Y spawns a ring of slots **anchored to the right hand**, ~12 cm
     across, facing the head, with the game slowed as the native wheel does (same timescale
     hook). Slots show the weapon icons the game already has (inkWidget atlas) for the three
     weapon slots, plus consumables/grenade, drawn as a world widget the way the wrist readout
     is (render-to-texture on a hand-attached plane: `CyberpunkVRPort_WristHud` is the template).
   - **Selection.** Hand displacement from where Y went down, projected onto the ring's plane.
     Inside a 3 cm dead zone nothing is lit; beyond it the nearest sector lights up (scale +
     brightness), with a haptic tick on every change. Release Y to equip; release inside the
     dead zone or press B to cancel.
   - **Equip.** Through the game's equipment system (`EquipmentSystem` request for slot N), the
     same call the holster module makes, so the draw animation, ammo and HUD all behave.
   - **Where it lives.** CET for the gesture and widget (it already has hand poses and the
     render-to-texture path), redscript for the equip call. No C++.
   - **Mergeability.** A new `CyberpunkVRPort_HandWheel` module; the Y masking is a one-line
     addition to the XInput merge next to the existing D-pad chord mask.
5. **Ammo.** Belt magazines as today. Consider a finite belt (N mags, refilled from inventory
   at a holster gesture) as an optional "hard" mode. Default: infinite belt.

**Decisions needed:** Adam's priority weapon list; finite belt yes/no.

## 5. Locomotion

### Target feel
Teleport as the default for comfort, with stick locomotion still available; turning by
snap. Roomscale stays native.

### Design (new plugin feature; this one needs C++)
1. **Arc teleport.** Hold left stick forward (or a dedicated button once mapping is redesigned):
   draw a parabola from the left hand; landing marker where it hits walkable ground
   (`SyncRaycastByCollisionGroup` against Static, slope check, headroom check with a second
   upward ray). Release to teleport.
2. **Execution.** Move the player capsule with the game's own teleport path (the roomscale
   module already documents that writing the camera position *teleports* the player, see
   `docs/mirror_re.md`). Fade to black for ~120 ms around the move. Enemies do not lose
   track of the player; no invulnerability.
3. **Blink turn.** Snap turn as today; optional fade.
4. **Rules.** No teleport while in a vehicle, mid-air, grappling, in cutscenes, or during
   scripted scenes where the game owns the camera (the port already tracks a cutscene-suspend
   tier; reuse it).
5. **Switch.** Overlay setting `locomotion = teleport | stick`, remembered in `vrport.ini`.

**Decisions needed:** which hand draws the arc; whether stick locomotion stays enabled at the
same time (both at once is the common choice).

## 6. Enemies

### Target feel
Fights paced for a player who aims and swings for real, without hit-scan reflexes. Not easier
in the "lower difficulty" sense; *fairer* for VR.

### Design (data only, TweakXL + one script)
1. **Ranged enemies.** Lower accuracy at range and longer burst pauses: scale
   `accuracy`/`spread` stats and the AI `shootingPattern` cooldowns per enemy archetype.
2. **Melee enemies.** Longer wind-up before the strike lands so a parry is possible; lower
   attack cadence; smaller lunge distance. These are attack-record and AI-preset values.
3. **Incoming damage.** A global multiplier (default 0.8) as the comfort knob, separate from the
   game's difficulty setting.
4. **Reaction picker** (shared with Melee v2, §3.3).
5. **Difficulty preset.** Ship as `r6/tweaks/vrport_enemies/*.yaml` with the multipliers at the
   top of each file so Adam can tune without a rebuild.

**Decisions needed:** starting multipliers; whether to touch netrunner enemies at all (their
quickhack cadence is a separate set of values).

## 7. Driving

Upstream already does grip-the-wheel steering. Work here is tuning on the Frame: wheel grab
radius, steering lock angle, deadzone, and the one-hand/two-hand feel. A hand-over-hand mode
(steering by rotating the grabbed hand around the wheel hub instead of tilting the line between
controllers) is a candidate if the tilt model feels wrong; measure first.

## 8. Architecture

```
mods/cet/CyberpunkVRPort_MeleeV2/          replaces CyberpunkVRPort_Weapon's melee half
mods/redscript/CyberpunkVRPort_MeleeV2/     our attack record + hit path + parry
mods/tweaks/vrport_melee/                   Attacks.VRMeleeContact_*.yaml
mods/tweaks/vrport_enemies/                 enemy tuning
mods/cet/CyberpunkVRPort_Reload/reload/weapons/<new>.lua   reload data for more weapons
src/Runtimes/Teleport*.cpp                  the only planned C++ addition
```

- Upstream's melee stays in the tree but is disabled by a flag it already honours
  (`xr_melee` / the Weapon module's melee section), so a rebase never conflicts on it.
- All new redscript lives under its own module names; no `@replaceMethod` on classes upstream
  already wraps, to avoid double-wrapping after a rebase.
- Every tunable number sits in `vrport.ini` or a YAML file, read live by the overlay where
  upstream already supports live reads.

## 9. Test protocol per system

Each milestone ships with a one-page test script (location, actions, pass criteria) and Adam
reports in headset. Every input change re-runs the Phase 0 control table.

Melee v2 acceptance: a slow touch does nothing; a real swing kills a thug in the same number
of hits as the flat game ±1; a head hit staggers; a parry visibly stops an incoming swing;
three enemies in one stroke each take one hit.

## 10. Open questions

1. Can a script-built hit event play a chosen reaction without the native attack state? (§3 spike)
2. Does the game's parry path accept a script trigger, or do we fake it with a stagger + attack cancel?
3. Does the roomscale capsule teleport path survive a 10 m jump without the camera filters smoothing it? (§5.2)
4. How much of the enemy AI cadence is in TweakDB vs. baked into AI trees? Determines how far §6 can go.
