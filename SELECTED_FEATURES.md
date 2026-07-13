# Selected feature implementation checklist

This checklist records the feature numbers selected in the July 2026 Axion
repair session. A menu control is not considered complete unless its native
Linux runtime path is implemented and survives a clean build. Live-only items
remain marked for verification until tested inside CS2.

Tracked total: **123 features** — 121 unique numbers from the original
selection (113 was repeated), plus the later explicit additions 35 and 157.

Legend: **done** = implemented and statically verified; **repair** = existing
code needs Linux repair or live verification; **new** = not implemented yet;
**live verification** = native runtime is implemented and builds, but the
effect must still be confirmed after restarting CS2;
**deferred** = intentionally waiting on a prerequisite; **excluded** = do not
implement.

## Legitbot and smoothing

| # | Feature | Status |
|---:|---|---|
| 1 | Master Legitbot toggle | repair |
| 2 | Per-weapon profiles | new |
| 4 | Closest-to-crosshair targeting | repair |
| 5 | Closest-distance targeting | new |
| 6 | Lowest-health targeting | new |
| 10 | Visibility check | new |
| 11 | Smoke check | new |
| 12 | Flash check | new |
| 16 | Hitbox priority | repair |
| 17 | Nearest-hitbox selection | repair |
| 18 | Reaction delay | new |
| 25 | Acceleration control | done |
| 26 | Deceleration control | done |
| 34 | Artificial humanized overshoot | deferred |
| 35 | Natural overshoot recovery | done |

## Triggerbot and recoil

| # | Feature | Status |
|---:|---|---|
| 41 | Triggerbot master toggle | new |
| 42 | Trigger delay | new |
| 47 | Trigger hitbox filter | new |
| 48 | Trigger visibility check | new |
| 49 | Trigger smoke check | new |
| 50 | Trigger scoped-only mode | new |
| 51 | Standalone recoil control | repair |
| 55 | Recoil smoothing | new |
| 60 | Trigger/recoil diagnostics | new |

## Ragebot

| # | Feature | Status |
|---:|---|---|
| 61 | Ragebot master toggle | done |
| 62 | Hitscan | done |
| 63 | Hitbox selection | done |
| 64 | Hitbox priority groups | repair |
| 65 | Multipoint | new |
| 69 | Minimum damage | repair |
| 71 | Hitchance | repair |
| 73 | Autowall | repair |
| 74 | Penetration-damage preview | new |
| 75 | Auto-stop | repair |
| 76 | Auto-crouch | new |
| 77 | Auto-scope | repair |
| 78 | Force-body bind | new |
| 79 | Force-head bind | new |
| 81 | Lethal-body preference | new |
| 82 | Prefer exposed hitboxes | new |
| 83 | Prefer low-health enemies | new |
| 86 | Prefer highest damage | new |
| 89 | Delay shot until accurate | new |
| 90 | Delay shot until visible | new |
| 98 | Per-weapon Rage profiles | repair |
| 99 | Rage decision overlay | new |
| 100 | Shot-reason logging | repair |

## Anti-Aim

| # | Feature | Status |
|---:|---|---|
| 101 | Anti-Aim master toggle | repair |
| 102 | Pitch down | done |
| 103 | Pitch up | done |
| 104 | Pitch zero | done |
| 105 | Custom pitch | new |
| 106 | Backward yaw | done |
| 107 | Sideways yaw | new |
| 108 | Custom yaw offset | new |
| 109 | Static jitter | new |
| 110 | Random jitter | new |
| 113 | Spin mode | new |
| 116 | Standing profile | new |
| 117 | Moving profile | new |
| 118 | Airborne profile | new |
| 119 | Crouching profile | new |
| 120 | Slow-walking profile | new |
| 123 | Manual-back bind | new |
| 124 | Manual-forward bind | new |
| 128 | Anti-backstab | excluded |
| 132 | Disable while using | new |

## Player ESP and details

| # | Feature | Status |
|---:|---|---|
| 141 | Player ESP master toggle | done |
| 146 | Corner box | done |
| 148 | Filled box background | done |
| 149 | Box thickness control | done |
| 151 | Health bar | done |
| 152 | Health number | done |
| 153 | Gradient health color | done |
| 154 | Armor bar | done |
| 156 | Weapon name | done |
| 157 | Equipped weapon icon (later explicit request) | done |
| 158 | Ammunition bar | done |
| 159 | Distance text | done |
| 163 | Head circle | done |
| 165 | View-direction line | done |
| 166 | Snaplines | done |
| 167 | Offscreen arrows | done |
| 170 | Scoped flag | done |
| 171 | Flashed flag | done |
| 172 | Defusing flag | done |
| 173 | Planting flag | done |
| 174 | Reloading flag | done |
| 175 | Bomb-carrier flag | done |
| 178 | Ping flag | done |

## Chams

| # | Feature | Status |
|---:|---|---|
| 188 | Arms chams | live verification |
| 189 | Sleeve chams | live verification |
| 190 | Held-weapon chams | live verification |
| 193 | Grenade chams | live verification |
| 194 | Bomb chams | live verification |
| 196 | Flat material | live verification |
| 197 | Metallic material | live verification |
| 198 | Glow material | live verification |
| 199 | Glass material | live verification |
| 200 | Wireframe material | live verification |

Existing enemy visible/through-wall chams are also part of the repair-first
prerequisite even though their original list numbers were not repeated.

## World, grenade, and bomb visuals

| # | Feature | Status |
|---:|---|---|
| 223 | World-color modulation | new |
| 225 | Sky-color modulation | new |
| 233 | Dropped-weapon ESP | new |
| 244 | Grenade trajectory | new |
| 245 | Trajectory bounce markers | new |
| 246 | Trajectory landing marker | new |
| 251 | Smoke duration timer | new |
| 253 | Molotov expiration timer | new |
| 256 | Planted-bomb timer | new |

## Removals

| # | Feature | Status |
|---:|---|---|
| 261 | Smoke removal | repair/live verification |
| 262 | Flash reduction | live verification |
| 263 | Adjustable flash opacity | live verification |
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
