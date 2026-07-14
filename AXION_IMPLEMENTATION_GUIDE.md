# Axion implementation guide and recent references

Date rule for this guide: references must have project activity on or after
2024-07-13. A recent repository is still only a reference; its code is not
automatically correct for the current native Linux CS2 build.

## Implementation state on 2026-07-14

The native library includes the repair paths described by this guide. CS2 is
currently closed, so runtime-dependent rows remain `live verification` in
`SELECTED_FEATURES.md` until a new process loads this rebuild.

Clean debug and optimized release builds pass. Targeted Clang analysis of the
modified renderer, ESP, chams, trace, menu and memory paths reports no project
defects (only a vendored ImGui dead-store warning), and `ldd -r` reports no
unresolved symbols. Debug/release objects live in separate directories so
switching build modes cannot reuse objects compiled with the wrong flags;
generated dependency files also force rebuilds after header changes.

- `CCSGOInput::CreateMove` is hooked at the current primary vtable index 26
  with the native `void(this, slot, CUserCmd*)` ABI. Command sequence, protobuf
  base pointer, button states, movement and view angles are validated and
  logged before mutation. The old raw input-object offsets were removed.
- CVar features use exact-name typed reads/writes, mapping checks, readback and
  original-value restoration. A missing or type-mismatched CVar fails closed.
- Native trace is gated specifically to current `libclient.so` Build ID
  `dc8a23833b40d5eced92e34487dc9061a72d6de7`. The wrapper, entity-handle
  helper, collision helper, manager reference and filter vtable must all match
  independent current-binary addresses before a zero-length self-test can open
  the gate. The verified native layouts are a 0x30 ray/hull descriptor, 0x40
  filter, pointer-based six-argument call and 0xC0 result. An update mismatch
  leaves visibility and grenade prediction disabled rather than calling an old
  ABI.
- Legit and trigger visibility, rage visible/exposed scoring and swept-hull
  grenade prediction now consume that gated trace path. Grenade prediction
  reads live throw strength/velocity and `sv_gravity`, integrates at the game
  tick interval, reflects velocity on collision normals and records bounce and
  landing points.
- Legit profiles now cover the complete target-response surface rather than a
  partial subset: smoothing, FoV, ranking, hitbox mode, gates, reaction,
  acceleration/deceleration, overshoot/recovery, bone groups, prediction,
  recoil correction and auto-fire all follow the active weapon group. The FoV
  preview uses that same active profile.
- Smoke gates retain the local pawn's validated smoke-overlay state and also
  scan live `smokegrenade_projectile` entities through checked identity, effect
  tick and scene-origin reads. A cached active-volume test rejects every aim or
  trigger point whose complete eye-to-target segment intersects the cloud; an
  unavailable schema/timing state fails closed instead of allowing the shot.
- Artificial humanized overshoot is opt-in and bounded to one event per target
  lock. It records the incoming angular direction, crosses by the configured
  small offset, releases after one short input interval and returns through the
  existing time-based recovery response; changing or releasing the lock clears
  all phase state.
- Rage ballistics now reads current schema weapon damage, head/armor ratios,
  penetration, range falloff, two-mode spread and movement/air/penalty
  inaccuracy. Damage scaling reads the live team head/body CVars and validated
  armor/helmet/heavy-armor state. The exact-build trace result supplies
  hitgroup and surface penetration/damage modifiers; a bounded trace-to-exit
  loop applies thickness, surface, penetration and range losses. Hitchance
  performs 128 deterministic live collision samples. Minimum damage, autowall,
  damage preview, lethal body, highest damage and delay-until-accurate consume
  those results and fail closed when any required source is unavailable.
- Post-injection resize now hooks swapchain creation even on a late attach,
  retains the ImGui context and SDL backend, rebuilds only Vulkan resources,
  binds rendering to the actual present queue/family and checks every relevant
  Vulkan result. Out-of-date, suboptimal, surface-lost and device-lost results
  retire resources in a synchronized fail-closed path.
- Bomb chams track the live planted-C4 entity instead of the planter or carrier,
  so planter death cannot remove the effect. Knife models are classified before
  generic held weapons and use an independent toggle and color.
- Held weapons, knives, grenades and bombs each expose independent live mesh
  colors. Enemy visible/occluded colors and glow intensity are also applied as
  live mesh tint, so dragging a color or intensity control never compiles or
  swaps engine materials from the render thread. The full Rage editor exposes
  every runtime hitbox, including neck and both chest groups.
- Rage target selection also exposes per-weapon head/neck, torso and limb
  priority groups. Its decision overlay/log includes target index, bone,
  visibility/penetration state, damage, hitchance and the fire/hold reason.
- Render-frame input/chams/skin state is cleared before every loading or
  disconnect early return, preventing commands or raw entity identities from a
  prior map from staying latched. Anti-aim also checks the grenade pin/throw
  transition after attack release instead of redirecting the throw.
- Native entity enumeration snapshots each 512-entry bucket once per render
  generation, then validates non-null entities before use. Scene nodes, bone
  caches, origins, view offsets, view angles, smoke/removal writes and local
  controller/view-matrix reads now fail closed through checked process reads or
  writes instead of directly dereferencing a stale schema address. Printable
  entity names are copied in one bounded read rather than one syscall per byte.
- Player health/team/flags/velocity/armor/flash, recoil counters, controller
  equipment flags, chams owner chains and camera-service pointers now use
  checked reads. CVar-backed smoke, scope, blur, FOV, viewmodel and third-person
  paths retry failed captures, mutate only captured values and restore the true
  originals on disable. Smoke-grenade volume/overlay materials use a narrow
  transparent replacement based on assets present in the installed VPK; other
  particle smoke, fire, map ambience and weapon models are not suppressed.
- The ordinary-gun skin path validates every required econ offset, weapon and
  viewmodel handle, entity-generation token, attribute vector and write. It
  preserves the true original state while a skinned weapon is dropped, rejects
  a recycled pointer generation, restores partial writes on failure and never
  leaves the engine pointing at a temporary attribute vector after refresh.
- Planted-bomb chams now require the live planted entity to be ticking,
  non-defused and before its blow time. The target remains independent of the
  planter, while terminal bomb state clears it even if the entity object lingers.
- The `make release` target now recursively selects the release object tree;
  the previous target could silently relink debug objects despite its name.

## Reference projects

| Project | Recency checked | Useful examples | Compatibility rule |
|---|---|---|---|
| [Osiris](https://github.com/danielkrupinski/Osiris) | latest cloned commit 2026-05-31 | Native Linux entity/schema access, player state icons, weapon ammo, offscreen arrows, model glow, bomb timer, viewmodel FOV and config/UI architecture | Primary architecture reference because it supports native Linux; adapt to Axion rather than copying hooks blindly. |
| [AimStar](https://github.com/PrintN/AimStar) | latest cloned commit 2024-11-02 | ESP/ammo layout, legit target flow, recoil, triggerbot, bomb timer and config manager | Windows external: algorithms/UI concepts only. Never copy its memory or mouse-input path. |
| [TKazer CS2 External](https://github.com/TKazer/CS2_External) | latest cloned commit 2024-07-24 | Aim calculations, bunny-hop condition flow, triggerbot and config save/load | Windows external: concepts only; its process-memory and input code cannot be used for native Linux internal hooks. |
| [cs2-dumper](https://github.com/a2x/cs2-dumper) | active in 2026 | Current schemas, offsets, buttons and interfaces | Generate/import data; never treat a nonzero offset as proof that a pointer, vtable index or calling convention is valid. |
| [GameTracking-CS2](https://github.com/SteamTracking/GameTracking-CS2) | active in 2026 | Current resource/CVar names and Valve data definitions | Validation source for names and engine state, not a feature implementation. |
| [demoinfocs-golang](https://github.com/markus-wa/demoinfocs-golang) | active 2026 release line | Grenade projectile/trajectory and bomb-state data modeling | Physics/state reference only; it parses demos rather than running inside CS2. |
| [TempleWare-Public](https://github.com/TempleDevelopment/TempleWare-Public) | release activity in 2025 | Recent internal feature organization and menu surface | Windows DLL only; no native Linux address, hook or renderer code was adopted. |
| [litware-internal](https://github.com/t3rmynal/litware-internal) | archived in 2025 | Compact internal organization | Windows/DX11 only and archived; no runtime code was adopted. |

Useful direct source examples:

- [Osiris weapon ammo panel](https://github.com/danielkrupinski/Osiris/blob/master/Source/Features/Visuals/PlayerInfoInWorld/ActiveWeaponAmmo/PlayerActiveWeaponAmmoPanelContext.h)
- [Osiris offscreen player arrow](https://github.com/danielkrupinski/Osiris/blob/master/Source/Features/Visuals/PlayerInfoInWorld/PlayerPositionArrow/PlayerPositionArrowPanelContext.h)
- [Osiris player state icons](https://github.com/danielkrupinski/Osiris/blob/master/Source/Features/Visuals/PlayerInfoInWorld/PlayerStateIcons/PlayerStateIconsPanelContext.h)
- [Osiris model glow](https://github.com/danielkrupinski/Osiris/blob/master/Source/Features/Visuals/ModelGlow/ModelGlow.h)
- [Osiris bomb timer](https://github.com/danielkrupinski/Osiris/blob/master/Source/Features/Hud/BombTimer/BombTimer.h)
- [Osiris viewmodel modification](https://github.com/danielkrupinski/Osiris/blob/master/Source/Features/Visuals/ViewmodelMod/ViewmodelMod.h)
- [AimStar ESP](https://github.com/PrintN/AimStar/blob/main/CS2_External/Features/ESP.h)
- [AimStar legitbot](https://github.com/PrintN/AimStar/blob/main/CS2_External/Features/Aimbot/Legitbot.hpp)
- [AimStar recoil control](https://github.com/PrintN/AimStar/blob/main/CS2_External/Features/RCS.h)
- [AimStar triggerbot](https://github.com/PrintN/AimStar/blob/main/CS2_External/TriggerBot.cpp)
- [AimStar config saver](https://github.com/PrintN/AimStar/blob/main/CS2_External/Utils/ConfigSaver.cpp)
- [TKazer bunny hop](https://github.com/TKazer/CS2_External/blob/master/CS2_External/Bunnyhop.hpp)

## Implementation map

### Crash blockers and ESP (#141-178, U1-U4, U10-U11)

1. Resolve weapon services, active-weapon handle, weapon entity, weapon vdata,
   clip and maximum clip in separate checked steps. A failed step skips only
   that player's weapon decoration. Cache validated schema offsets, never a
   raw entity pointer across frames. Test weapon text, icon and ammo separately,
   then together through switching, death and respawn. The Osiris ammo panel is
   the closest native-Linux structural example; AimStar shows a simpler display
   flow but not safe Linux access.
2. Make full and corner boxes separate renderer branches. Corner mode draws
   eight short edge segments and must not fall through to full-box drawing.
3. Implement the optional fill with `AddRectFilledMultiColor`; preserve a solid
   fill mode. Store top and bottom colors in config and apply alpha after color
   interpolation.
4. Read scoped/defusing from each current pawn only after the corresponding
   schema offset resolves. If an offset is missing or a raw value is invalid,
   draw no flag and log once. Never read offset zero.
5. For offscreen arrows, project first; draw only if outside the viewport. Use
   `atan2(delta.y, delta.x) - viewYaw`, normalize to `[-pi, pi]`, clamp the arrow
   center to a screen-edge ellipse and rotate its three vertices. Test all four
   sides and the `+180/-180` transition. Osiris has a current player-arrow state
   and panel split worth following.

### Chams (#188-200, U5-U7, U9)

1. Classify player, arms, sleeves, held weapon, grenade and bomb before choosing
   a material. Give arms and sleeves independent variables and color pickers;
   do not reuse the enemy or weapon color.
2. Keep flat and metallic as distinct material definitions. Flat should have
   no phong/envmap/specular contribution. Metallic should expose controlled
   phong/envmap parameters rather than adding a bright overlay shell.
3. Replace fixed glow brightness/self-illumination with a 0-100 intensity
   setting mapped to a conservative shader range. Default near the dark/subtle
   end and ensure the original material is restored when disabled.
4. Through-wall player chams need an occluded pass plus a visible pass for every
   valid enemy scene object. Do not stop after the first mesh/object and do not
   keep scene-object pointers between frames. Smoke and distance must not change
   eligibility. Osiris model glow is useful for entity lifecycle/state handling,
   though Axion's two-pass chams material path remains project-specific.

### World, grenades and bomb (#223, #225, #233, #244-256)

1. World/sky modulation: classify materials by shader group/name, back up each
   mesh color for the current draw call, apply world and sky colors independently,
   then restore immediately after the original draw. Exclude HUD, players and
   particles; this transient path needs no stale map-lifetime material cache.
2. Dropped weapons: enumerate current weapon entities, require an invalid/null
   owner handle, validate the scene node and project a current origin. Obtain
   display name/ammo only through the same fail-closed weapon helper used by
   player ESP.
3. Grenade prediction: start at the eye/throw origin, integrate in a fixed tick
   step, trace the swept hull each step, apply gravity, and on impact reflect
   velocity with `v' = v - (1 + elasticity) * dot(v, normal) * normal`. Record
   collision positions as bounce markers; stop on low speed/fuse expiry and use
   the final point as the landing marker. Validate current gravity/trajectory
   names against GameTracking-CS2; demoinfocs is useful for projectile state and
   event terminology.
4. Smoke/Molotov timers: validate the concrete projectile/inferno class, read a
   current spawn/start time and lifetime, calculate `max(0, endTime-curTime)`,
   and discard the entry as soon as its handle becomes invalid. Rescan entities;
   never mutate projectile/particle internals from the render hook.
5. Bomb timer: locate the current planted-C4 entity, require ticking state, then
   display remaining blow time and optional defuse time from current game time.
   Clear state on defuse/explosion/round reset. Osiris's current bomb timer is the
   primary lifecycle example.

### Removals (#261-271)

Resolve each CVar or material state once per generation/map, verify its expected
type, save the original, write only when enabled, read back for diagnostics and
restore when disabled. Smoke removal must hide the rendering layer or use a safe
CVar/material route; it must never repeatedly traverse or overwrite active
smoke-particle objects. Flash reduction/opacity is user-verified; preserve that
working path while repairing scope overlay, aim punch and motion blur.

### Camera and movement (#281-287)

Third person, camera FOV, viewmodel FOV, bunny hop and auto-strafe all depend on
verified native input/CVar plumbing. First log CreateMove installation, calls,
command numbers, requested buttons/angles and applied values. Bunny hop changes
`IN_JUMP` only when airborne; auto-strafe derives side movement from velocity and
mouse/yaw delta while airborne and clamps to engine limits. Third-person needs a
valid observer/camera state and collision trace before distance adjustment.
Osiris's viewmodel modifier is native-Linux relevant; TKazer's bunny-hop code is
only a compact condition-flow reference.

### Legitbot, triggerbot and recoil (#1-60)

Build the smallest verifiable path first: one valid enemy head, angular delta,
normalized/clamped aim and an applied CreateMove angle. Then add nearest-FOV /
distance / health selection, hitbox choice, verified trace visibility, smoke and
flash gates, reaction delay, per-weapon profiles and recoil. Express smoothing
in milliseconds as requested: compute a time-based interpolation factor from
frame/tick delta instead of a frame-count divisor. Acceleration/deceleration and
overshoot recovery operate on angular velocity and must be bounded. Triggerbot
queues `IN_ATTACK` only after its delay and all gates pass. Log a reason for every
rejection and every attempted/applied command. AimStar's legit/recoil/trigger
files are algorithm references; do not use their Windows input route.

### Ragebot (#61-100)

Do not expand until the same CreateMove and trace foundations make legit aim and
fire work. Hitscan evaluates enabled hitboxes; multipoint samples bounded points;
minimum damage and hitchance reject shots before command mutation. Autowall must
use a verified trace-to-exit, surface material, weapon penetration and damage
falloff implementation—never label a distance heuristic as autowall. Auto-stop,
crouch and scope are queued command requests. Decision overlay and logs must show
target, point, visibility, damage, hitchance and the exact rejection/fire reason.

### Anti-aim (#101-132)

Apply pitch/yaw only in a verified CreateMove command and only when alive and not
attacking, using, throwing or otherwise excluded. Clamp/normalize angles to the
current safe range. Implement standing/moving/air/crouch/slow-walk profiles from
current pawn velocity/flags; manual directions override profile yaw. Jitter and
spin are bounded deterministic state updates. #128 remains explicitly excluded.

### Skin changer (U8)

There is no trustworthy recent native-Linux GitHub skin-changer example in the
reviewed set, so do not force a fake/download-only repository into the plan.
Use current cs2-dumper schema data and validate Axion's own Linux entity path:
active weapon handle, attribute container/item, definition index, paint kit,
seed, wear, StatTrak and the game's refresh/regeneration path. Save originals
per entity generation and restore safely. Prove one ordinary gun across equip,
switch, death and respawn before broadening the table. Knives, gloves and an
inventory changer stay later because they require model/inventory work beyond a
paint-kit override.

### Config manager and keybinds (U3 plus existing controls)

Route SDL3 text-input events to ImGui before Axion consumes hotkeys; verify
keyboard focus, text color, clipping and persistent string storage. Sanitize the
config filename, save atomically under the Linux config directory, enumerate
files after save/delete and surface errors in the UI. Keybind capture must accept
keyboard, right mouse, mouse 4 and mouse 5 using SDL button events and display the
resolved name. Osiris's schema/UI separation and AimStar's config saver are useful
structure references, but paths and event handling must stay native Linux.

## Required verification order

1. Stop ammo/weapon crashes and run a clean build.
2. Verify native CreateMove, CVar and trace foundations with counters/readback.
3. Repair existing ESP/chams/config behavior.
4. Repair removals, camera and movement one group at a time.
5. Make minimal legit aim work, then trigger/recoil, then the detailed options.
6. Only then implement rage and anti-aim behavior.
7. Repair one ordinary-gun skin and lifecycle before broader skin coverage.
8. Implement world/grenade/bomb visuals with entity lifecycle diagnostics.
9. Recreate the Vulkan swapchain after injection, then verify bomb and knife
   chams through the lifecycle cases listed in U12-U14.

Nothing moves from repair/new to done merely because a menu control compiles.
It needs a clean build and the acceptance condition in `SELECTED_FEATURES.md`;
runtime-dependent items also need an actual native Linux CS2 test.
