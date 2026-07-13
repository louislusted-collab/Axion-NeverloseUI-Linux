# Axion NeverloseUI — Native Linux

Native Linux port of Axion with a Vulkan/SDL3 renderer and a minimal GTK4
loader. This repository is under active development; gameplay features that
depend on old signatures or layouts may not work until the validated updater
is complete.

## Build

Arch Linux packages used by the current build include GTK4, SDL3, Vulkan,
FreeType, GDB, and a C++20 compiler.

```bash
make
```

This creates:

- `cs2_axion.so` — the injected library.
- `axion_loader` — the graphical loader.

## Run

Start native CS2 normally through Steam, then launch:

```bash
./run_loader.sh
```

Press **Inject** and approve the graphical privilege prompt. Insert toggles the
menu after injection. Renderer diagnostics are written to
`/tmp/cs2_vulkan_debug.log`.

## Offset updater

Every injection checks the public native-Linux signature manifest. Updates are
downloaded into `.axion-cache`, validated before activation, and backed up as a
last-known-good copy. Network or validation failures never replace the active
manifest. The loader also has an explicit **Update full Linux dump** button.
With native CS2 running, it builds and runs the bundled Linux branch of
`catpetter1999/cs2-dumper`, then saves JSON for offsets/signatures, interfaces,
buttons, and every runtime schema field under `.axion-cache/cs2-dumper-output`.
It also writes `everything.json`, a single unfiltered bundle of every JSON
artifact emitted by the dumper, including entries Axion does not currently use.
The upstream Linux `config.json` is checked on every run and its live offsets
are imported into Axion's runtime cache. Set
`AXION_SKIP_UPDATE=1` to skip the automatic check.

Clone this repository with `--recurse-submodules`, or run
`git submodule update --init` once. The full dumper requires native CS2 to be
running. After injection, the current native `VEngineCvar007` linked registry is
enumerated separately into `~/.cs2/convars.txt`; the full dumper imports that
file into its output directory when available.

The validator rejects Windows modules and manifests that do not require a
unique match. Windows `client.dll` dumps are never applied to native Linux
`libclient.so`. Schema fields now resolve directly from CS2's runtime schema,
so they do not need static offsets. The bundled manifest contains only patterns
that match uniquely against current native ELF files; downloaded updates must
also resolve locally before activation. Patterns were initially derived from
the MIT-licensed `a2x/cs2-dumper` Linux branch and independently validated.
Native gameplay hooks are still required before all old gameplay features work.

## Upstream

Based on the original Axion project:

# Axion-CS2-RAGE-CHEAT
 + The source code of AXION CS2 internal rage cheat.
 + It's made on asphyxia base and has some features implemented from csgo cheats like Pandora.
 + I decided to make it public beacause it has been sold for 75$ and then leaked on the internet.
 + It has some great features but it can't be used for hvh because its preety buggy.
 + The cheat might get you VAC banned so only use it at your own risk (better on non prime accounts).

# Status: Needs update!
I don't have time to work on it anymore because of lack of time and other personal projects but I'll let it for the community.

If you are trying to fix it, you have to:
- Update the schema (take it from asphyxia because it's the same)
- Update the signatures (You can find them in hooks.cpp/hooks.h).
- Make sure you build it on publish and not on release. (If you build on release you will get a lot of errors)
- Make sure you include everything required in the project settings!

# PREVIEW
![Screenshot 2024-04-14 175107](https://github.com/T1GxR/AxionCS2-RAGE-CHEAT/assets/106729571/df318ac7-1723-43c0-bc00-d5900812df52)
![Screenshot 2024-04-14 175057](https://github.com/T1GxR/AxionCS2-RAGE-CHEAT/assets/106729571/2496ee46-667e-47b6-b1c7-8c97c18f88bf)
![Screenshot 2024-04-14 175032](https://github.com/T1GxR/AxionCS2-RAGE-CHEAT/assets/106729571/719246a9-4812-4134-a269-efadf648d78c)
![Screenshot 2024-04-14 175018](https://github.com/T1GxR/AxionCS2-RAGE-CHEAT/assets/106729571/3f80c0a0-96f4-432f-b55f-e2232297b323)
![Screenshot 2024-04-14 175010](https://github.com/T1GxR/AxionCS2-RAGE-CHEAT/assets/106729571/7535baf2-577a-4ae5-8bd3-39cf2a53a6d6)

# Features:
 + RAGE BOT:
   - Rapid Fire
   - Anti Aim (Jitter)
   - Multipoint
   - PSilent
 + Visual:
    - ESP (BOX, SKELETON AND MORE)
    - CHAMS
    - ITEM ESP
    - FLAGS
    - FOV changer
 + Misc:
    - Night mode
    - Sky box changer
    - No flash
    - No smoke
    - Third person
    - No impact
 + Other features:
    - Inventory changer with item dump
    - In game skin changer
    - Configs tab (Path: Documents/.cs2/settings)
    - Plant bomb anywhere (only works at the beginning of the round for a second only)
    - AND MORE

# TO-DO
 + Improve the rage tab and some miscellaneous features because they were created to manipulate the game server and they only work on local games/practice games. Basically on higher ping games, the rage is completely useless, as well as bhop which makes the character fly while holding space. All those bugs can be fixed if you know what to do.
   
