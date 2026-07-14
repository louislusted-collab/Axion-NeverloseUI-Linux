# Selected feature implementation checklist

This checklist records the feature numbers selected in the July 2026 Axion
repair session. A menu control is not considered complete unless its native
Linux runtime path is implemented and survives a clean build. Live-only items
remain marked for verification until tested inside CS2.

Tracked total: **123 numbered features** — 121 unique numbers from the original
selection (113 was repeated), plus the later explicit additions 35 and 157.
There are also **14 mandatory unnumbered repairs/extensions** recorded at the
end. They are part of the work, not optional notes.

Legend: **done** = implemented and statically verified; **repair** = existing
code needs Linux repair or live verification; **new** = not implemented yet;
**live verification** = native runtime is implemented and builds, but the
effect must still be confirmed after restarting CS2;
**deferred** = intentionally waiting on a prerequisite; **excluded** = do not
implement; **extension** = requested behavior must be added to an existing
feature; **unimplemented** = its control exists but no working runtime path was
verified; **blocked: native ballistics** = deliberately held because real
penetration/damage/spread support is not yet safe; **partial** = a narrower
safe implementation exists but does not meet the full acceptance condition;
**user verified** = the user confirmed the effect in CS2.

## Legitbot and smoothing

| # | Feature | Status |
|---:|---|---|
| 1 | Master Legitbot toggle | live verification |
| 2 | Per-weapon profiles | live verification |
| 4 | Closest-to-crosshair targeting | live verification |
| 5 | Closest-distance targeting | live verification |
| 6 | Lowest-health targeting | live verification |
| 10 | Visibility check | live verification |
| 11 | Smoke check | live verification |
| 12 | Flash check | live verification |
| 16 | Hitbox priority | live verification |
| 17 | Nearest-hitbox selection | live verification |
| 18 | Reaction delay | live verification |
| 25 | Acceleration control | live verification |
| 26 | Deceleration control | live verification |
| 34 | Artificial humanized overshoot | live verification |
| 35 | Natural overshoot recovery | live verification |

## Triggerbot and recoil

| # | Feature | Status |
|---:|---|---|
| 41 | Triggerbot master toggle | live verification |
| 42 | Trigger delay | live verification |
| 47 | Trigger hitbox filter | live verification |
| 48 | Trigger visibility check | live verification |
| 49 | Trigger smoke check | live verification |
| 50 | Trigger scoped-only mode | live verification |
| 51 | Standalone recoil control | live verification |
| 55 | Recoil smoothing | live verification |
| 60 | Trigger/recoil diagnostics | live verification |

## Ragebot

| # | Feature | Status |
|---:|---|---|
| 61 | Ragebot master toggle | live verification |
| 62 | Hitscan | live verification |
| 63 | Hitbox selection | live verification |
| 64 | Hitbox priority groups | live verification |
| 65 | Multipoint | live verification |
| 69 | Minimum damage | live verification |
| 71 | Hitchance | live verification |
| 73 | Autowall | live verification |
| 74 | Penetration-damage preview | live verification |
| 75 | Auto-stop | live verification |
| 76 | Auto-crouch | live verification |
| 77 | Auto-scope | live verification |
| 78 | Force-body bind | live verification |
| 79 | Force-head bind | live verification |
| 81 | Lethal-body preference | live verification |
| 82 | Prefer exposed hitboxes | live verification |
| 83 | Prefer low-health enemies | live verification |
| 86 | Prefer highest damage | live verification |
| 89 | Delay shot until accurate | live verification |
| 90 | Delay shot until visible | live verification |
| 98 | Per-weapon Rage profiles | live verification |
| 99 | Rage decision overlay | live verification |
| 100 | Shot-reason logging | live verification |

## Anti-Aim

| # | Feature | Status |
|---:|---|---|
| 101 | Anti-Aim master toggle | live verification |
| 102 | Pitch down | live verification |
| 103 | Pitch up | live verification |
| 104 | Pitch zero | live verification |
| 105 | Custom pitch | live verification |
| 106 | Backward yaw | live verification |
| 107 | Sideways yaw | live verification |
| 108 | Custom yaw offset | live verification |
| 109 | Static jitter | live verification |
| 110 | Random jitter | live verification |
| 113 | Spin mode | live verification |
| 116 | Standing profile | live verification |
| 117 | Moving profile | live verification |
| 118 | Airborne profile | live verification |
| 119 | Crouching profile | live verification |
| 120 | Slow-walking profile | live verification |
| 123 | Manual-back bind | live verification |
| 124 | Manual-forward bind | live verification |
| 128 | Anti-backstab | excluded |
| 132 | Disable while using | live verification |

## Player ESP and details

| # | Feature | Status |
|---:|---|---|
| 141 | Player ESP master toggle | done |
| 146 | Corner box | live verification |
| 148 | Filled box background + gradient option | live verification |
| 149 | Box thickness control | done |
| 151 | Health bar | done |
| 152 | Health number | done |
| 153 | Gradient health color | done |
| 154 | Armor bar | done |
| 156 | Weapon name | live verification |
| 157 | Equipped weapon icon (later explicit request) | live verification |
| 158 | Ammunition bar | live verification |
| 159 | Distance text | done |
| 163 | Head circle | done |
| 165 | View-direction line | done |
| 166 | Snaplines | done |
| 167 | Offscreen arrows | live verification |
| 170 | Scoped flag | live verification |
| 171 | Flashed flag | done |
| 172 | Defusing flag | live verification |
| 173 | Planting flag | done |
| 174 | Reloading flag | done |
| 175 | Bomb-carrier flag | done |
| 178 | Ping flag | done |

## Chams

| # | Feature | Status |
|---:|---|---|
| 188 | Arms chams with independent color | live verification |
| 189 | Sleeve chams with independent color | live verification |
| 190 | Held-weapon chams | live verification |
| 193 | Grenade chams | live verification |
| 194 | Carried, dropped and planted-bomb chams | live verification |
| 196 | Flat material | live verification |
| 197 | Metallic material | live verification |
| 198 | Glow material with usable intensity control | live verification |
| 199 | Glass material | live verification |
| 200 | Wireframe material | live verification |

Existing enemy visible/through-wall chams are also part of the repair-first
prerequisite even though their original list numbers were not repeated.

## World, grenade, and bomb visuals

| # | Feature | Status |
|---:|---|---|
| 223 | World-color modulation | live verification |
| 225 | Sky-color modulation | live verification |
| 233 | Dropped-weapon ESP | live verification |
| 244 | Grenade trajectory | live verification |
| 245 | Trajectory bounce markers | live verification |
| 246 | Trajectory landing marker | live verification |
| 251 | Smoke duration timer | live verification |
| 253 | Molotov expiration timer | live verification |
| 256 | Planted-bomb timer | live verification |

## Removals

| # | Feature | Status |
|---:|---|---|
| 261 | Smoke removal | live verification |
| 262 | Flash reduction | user verified |
| 263 | Adjustable flash opacity | user verified |
| 264 | Scope-overlay removal | live verification |
| 268 | Aim-punch removal | live verification |
| 271 | Motion-blur removal | live verification |
| 278 | Hands removal | excluded |

## Camera and movement

| # | Feature | Status |
|---:|---|---|
| 281 | Third-person camera | live verification |
| 282 | Third-person distance | live verification |
| 283 | Third-person collision handling | live verification |
| 284 | Camera FOV | live verification |
| 285 | Viewmodel FOV | live verification |
| 286 | Bunny hop | live verification |
| 287 | Auto-strafe | live verification |

## Mandatory unnumbered repairs and extensions

| ID | Requirement | Status | Acceptance condition |
|---:|---|---|---|
| U1 | Ammo-bar crash | live verification | Ammo-only and combined ESP survive weapon switch, death and respawn. |
| U2 | Weapon name/icon crash | live verification | Invalid weapon handles fail closed; text and icon work independently and together. |
| U3 | Config-name text input invisible/broken | live verification | Typed name is visible, saves, reloads and persists after restart. |
| U4 | Gradient filled box | live verification | Optional top/bottom or four-corner gradient; solid mode remains available. |
| U5 | Independent arms-chams color | live verification | Arms color changes without changing sleeves, weapon or player chams. |
| U6 | Independent sleeve-chams color | live verification | Sleeve color changes without changing arms, weapon or player chams. |
| U7 | Glow intensity too strong | live verification | Intensity is adjustable down to subtle/dark and does not wash out models. |
| U8 | Native ordinary-gun skin changer | live verification | One gun paint kit works through equip, switch, death and respawn before expanding coverage. |
| U9 | Enemy chams coverage | live verification | Every valid enemy is rendered through walls, smoke and distance with visible/occluded colors. |
| U10 | Scoped/defusing false-positive flags | live verification | Flags appear only when that pawn's resolved state is true; unresolved offsets draw nothing. |
| U11 | Offscreen-arrow stability | live verification | Correct direction and edge clamp across viewport boundaries and ±180° yaw. |
| U12 | Post-injection resize stability | live verification | Inject first, then resize, minimize/restore and cross a window/fullscreen transition without a crash or stale framebuffer. |
| U13 | Planted-bomb chams lifetime | live verification | Chams remain attached to the planted C4 after the planter dies, then clear after defuse, explosion or entity removal. |
| U14 | Independent knife chams | live verification | Knife enable/color are independent of generic held-weapon chams across weapon switch, death and respawn. |

## Current test truth

The user report above describes a previously injected binary. CS2 is currently
closed, so the rebuilt `cs2_axion.so` has not been live-verified. It contains the
native CreateMove hook, typed CVar readback/restoration, build-gated trace
self-test, crash-hardened weapon ESP, bounded one-shot overshoot/recovery,
full eye-to-target active-smoke intersection, repaired ESP/chams/config paths, world
entities/timers, swept-hull grenade prediction, swapchain-recreation handling,
generation-checked entity/skin reads, planted-C4 terminal-state tracking and
independent knife chams. Restart and inject
the new library before promoting any `live verification` row to `user verified`
or `done`. The current-build native ballistics path reads live weapon damage,
armor, spread/inaccuracy and damage-scale CVars; consumes gated trace hitgroups
and surface modifiers; performs bounded trace-to-exit penetration and range
falloff; and evaluates a complete 128-seed hitchance set. A missing/mismatched
schema, CVar, surface layout or trace ABI holds the affected shot. Clean debug
and optimized release builds pass, the modified runtime sources pass Clang's
static analyzer with only a vendored ImGui warning, and `ldd -r` reports no
unresolved symbols. Build checks prove integrity, not the runtime acceptance
conditions above.
