# VR gameplay overhaul — design

Phase 4 of the Steam Frame plan. Goal: make Cyberpunk 2077 play like a game built for VR
(Half-Life: Alyx as the reference), on top of dariulone's port, without leaving the
engine's reachable surface or losing the ability to rebase on upstream.

Status: **agreed order as of 2026-10-12.** Sections carry the decisions still open.

## 1. Order

Adam's priority is quality of life, in this order:

1. **Graphics** — eye-tracked foveation (done, tuning), then spend the headroom.
2. **UI** — the hand-anchored weapon wheel first; menus later.
3. **Grav gloves** — highlight and pull things to the hand.
4. **True melee** — Melee v2, preceded by the one-day spike.
5. The rest: reload for every weapon, two-hand holds, holsters retuned.

**Deferred, not planned** until Adam has played enough to judge them: teleport locomotion,
enemy tuning, driving tuning. The notes for those live in §9 so nothing is lost.

## 2. Scope and constraints

| | |
|---|---|
| Target | Adam's PC → Steam Frame, PC streaming. No other headsets. |
| Assets | Game-shipped meshes, animations and props only (WolvenKit / ArchiveXL / TweakXL). |
| Mergeability | New systems live in **new modules** that replace upstream ones by configuration. The C++ plugin is edited only where a system cannot be built in CET/redscript. Branch `frame` on the fork, rebased on upstream main. |

### The engine ceiling

Everything below is designed against these limits, measured by upstream
(`mods/cet/CyberpunkVRPort_HandCollision/init.lua` header, `vrport_basketball.reds`):

- Hands and held weapons are **kinematic** colliders. They push dynamic props; nothing pushes
  them back, and two kinematic bodies never interact.
- **Dynamic props can be driven**: redscript reaches the physical body of a dynamic entity and
  `AddLinearImpulse` works (the basketball module is built on it). That is what grav gloves pull with.
- NPCs are animated, not simulated, until death. Hit reactions are a **fixed set**; we choose
  among them. Ragdoll and dismemberment happen on kill, natively.
- The game's **entity outline** (`HighlightInstance`, what the scanner uses) can be driven from
  script per entity. That is the grav-glove highlight.
- Scripts read tracked controller poses every frame (CET) and call native systems (redscript).

## 3. Graphics

- **Done:** eye-tracked foveation (`src/Render/Foveation.cpp`), on the engine's own VRS passes.
  Recommended 0.15 / 0.30 / 4x4 with gaze; Adam is tuning upward after driving looked off.
- **Next:** measure GPU ms on/off at 2560 + DLSS Quality + 90 Hz, then spend the headroom:
  3072 or DLAA, framegen quality, sharpening. Any setting that survives goes in `docs/setup.md`.
- **Open:** "looks weird driving" — establish whether that is periphery blockiness at speed
  (radius) or a per-eye offset of the fovea (a bug in the eye→image mapping; VRCAM vs MAIN eye
  assignment is `CyberpunkVR_MainIsRightEye`-dependent).
- Candidate for an upstream PR once numbers exist (issue #86).

## 4. UI

### 4.1 Weapon wheel in the world (Alyx-style) — first
Tested 2026-10-12: the game's wheel works on the Frame but it is the full-screen UI, which is
wrong in VR. The game's wheel is **not used**; the Y hold is intercepted before it reaches the game.
- **What appears.** Holding Y spawns a ring of slots **anchored to the right hand**, ~12 cm
  across, facing the head, with the game slowed as the native wheel does (same timescale hook).
  Slots show the game's own weapon icons for the three weapon slots plus consumable/grenade,
  drawn as a world widget the way the wrist readout is (render-to-texture on a hand-attached
  plane; `CyberpunkVRPort_WristHud` is the template).
- **Selection.** Hand displacement from where Y went down, projected onto the ring's plane.
  Inside a 3 cm dead zone nothing is lit; beyond it the nearest sector lights (scale +
  brightness) with a haptic tick on every change. Release Y to equip; release inside the dead
  zone or press B to cancel.
- **Equip.** Through the game's equipment system (`EquipmentSystem` request for slot N), the
  same call the holster module makes, so draw animation, ammo and HUD behave.
- **Where it lives.** CET for gesture and widget, redscript for the equip call. No C++. New
  module `CyberpunkVRPort_HandWheel`; the Y mask is one line in the XInput merge beside the
  existing D-pad chord mask.

### 4.2 Later
Inventory/map/phone as hand- or world-anchored panels instead of the head-locked quad.
Not designed yet.

## 5. Grav gloves

### Target feel
Point a hand at something in reach of the gloves (~6 m): it lights up. Flick the wrist or pull
the hand back with the grip held: it flies to the hand; close the grip as it arrives to catch.
Miss the catch and it drops where it is. Works on loot and on loose physical objects.

### Design
1. **Candidate search (CET, each frame per hand).** A cone from the hand's aim pose (12° half
   angle, 6 m) over (a) lootable world items and containers' visible items, via the targeting
   system / interaction candidates the game already maintains, and (b) dynamic physical entities
   (`gamePhysicalSystem` query or the entity list the basketball module walks). Nearest to the
   cone axis wins; hysteresis so the highlight doesn't flicker between neighbours.
2. **Highlight.** The game's outline (`HighlightInstance`) on the candidate, in the loot colour
   for items and white for props; a short haptic tick when the candidate changes.
3. **Pull gesture.** With a candidate lit and the grip held: hand velocity toward the body
   ≥ 1.2 m/s along the hand→head line, or a wrist flick (angular speed ≥ 6 rad/s). One pull per
   gesture episode.
4. **Flight.**
   - *Props:* `AddLinearImpulse` for a ballistic arc that lands at the hand's predicted position
     ~0.5 s out (solve the impulse from mass and the target point; clamp so a heavy object
     just slides). Mid-flight correction once at the apex, as Alyx does, so it arrives.
   - *Loot items:* the item is not a physics body. Spawn the item's own world mesh as a
     temporary dynamic prop at its location (the port already spawns props for the belt and
     the ball), fly it the same way, and on arrival call the game's take-item interaction and
     despawn the prop. If the spawn path proves unreliable for a given item, fall back to:
     no flight, the item highlights and a pull takes it directly (still Alyx-like in function).
5. **Catch.** Grip closed when the object is within 15 cm of the palm → attach to the hand
   (held props use the existing hold mechanism from `CyberpunkVRPort_Hold`; weapons on the
   ground equip through the equipment system). Grip open → it drops.
6. **Throw** is the existing hand-release velocity path (basketball already throws).

### Not in scope
Pulling NPCs or anything the engine treats as static. Enemies' dropped weapons count as loot.

### Spike (half a day, before building)
On one street: list the interaction candidates the game exposes for a pointed hand; apply an
impulse to a bottle toward the hand and see it arrive; spawn a loot item's mesh as a prop.
Three yes/no answers decide how much of §5.4 is real.

**Decisions needed:** which hand(s) (both, default); pull range 6 m ok; whether loot flight is
worth the spawn complexity or direct take is enough for v1.

## 6. Melee v2

### Target feel
- Damage happens because *my blade touched them*, scaled by *how fast I swung* and *how well
  the edge was aligned*. No native attack state, no cooldown I didn't cause.
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
   impulse: light → flinch, medium → stagger in the swing direction, heavy (≥5 m/s or
   two-handed) → knockdown where the enemy type allows it.
4. **Cadence.** One hit per enemy per swing *episode* (speed crossing the threshold, re-armed
   when the hand slows), so a single stroke through three enemies hits all three once.
5. **Parry.** During an enemy melee attack's wind-up, if our blade segment intersects the
   enemy's weapon segment (or the attacking limb for unarmed), cancel their attack via the
   game's parry/block path, play the clash FX and sound, a short stagger to them and a wrist
   kick to us (reuse the recoil spring). L-trigger block stays as a fallback.
6. **Feedback.** Haptics scaled by impulse; whoosh per swing as today; hit sound by material.
7. **Cyberware arms** (Mantis, Monowire, Gorilla) use the same path; the "blade" is the arm.

### Not in scope
Blade stopping in the body; enemies physically pushed; stabbing depth; new animations.

### Spike (1 day, before building)
Prove on a street thug that a script-built hit event through `DamageSystem` (a) applies
damage with numbers and armor, (b) plays the reaction we asked for, (c) does not require the
player to be in a melee attack state. If (c) fails, fall back to upstream's re-injection trick
but with our damage model and reaction override.

**Decisions (defaults unless Adam says otherwise):** flat-side hit does 35% damage; knockdown
only from heavy two-handed swings, never from one-handed on humans; curve numbers as above.

## 7. Guns (after melee)

1. **Reload for every weapon.** Upstream's reload is data-driven per weapon
   (`reload/weapons/<name>.lua`). The 13 existing files are the template; add Adam's loadout
   first, then by family. Where a weapon has no authored pose, reuse the closest family pose.
2. **Rack and slide.** Keep R3 for now; later: rack by pulling the slide with the off hand.
3. **Holsters.** Retune zone radii after VRIK calibration on the Frame.
4. **Ammo.** Infinite belt by default; finite belt as an optional hard mode.

## 8. Architecture

```
mods/cet/CyberpunkVRPort_HandWheel/        §4.1
mods/cet/CyberpunkVRPort_GravGloves/       §5 gesture, candidates, flight
mods/redscript/CyberpunkVRPort_GravGloves/ §5 impulses, spawn, take, highlight
mods/cet/CyberpunkVRPort_MeleeV2/          §6, replaces the Weapon module's melee half
mods/redscript/CyberpunkVRPort_MeleeV2/    §6 attack record, hit path, parry
mods/tweaks/vrport_melee/                  Attacks.VRMeleeContact_*.yaml
src/Render/Foveation.cpp                   §3 (done)
```
- Upstream modules stay in the tree, disabled by flags they already honour; no
  `@replaceMethod` on classes upstream already wraps.
- Every tunable sits in `vrport.ini` or YAML, live where upstream already supports live reads.

## 9. Deferred notes (not planned)

- **Teleport:** arc from the left hand, landing marker via `SyncRaycastByCollisionGroup`,
  fade, capsule teleport through the roomscale path (`docs/mirror_re.md`); blocked in
  vehicles, mid-air, cutscenes. Needs C++.
- **Enemies:** TweakXL multipliers for accuracy, burst pauses, melee wind-up and cadence; a
  global incoming-damage multiplier; shares the reaction picker with §6.
- **Driving:** tune wheel grab radius, lock angle and deadzone on the Frame; hand-over-hand
  only if tilt steering proves wrong.

## 10. Test protocol per system

Each milestone ships with a one-page test script (location, actions, pass criteria) in
`docs/tests/`, and Adam reports in headset. Every input change re-runs the control table.
