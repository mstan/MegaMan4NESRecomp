# Mega Man 4 — NESRecomp

NESRecomp cycle-backend project for the original USA cartridge. Supply your own ROM; the project and releases contain no cartridge data.

Development takes place on the feature branch. Native gameplay is the baseline for the character, co-op, controls and adaptive widescreen implementation.

Configure with CMake 3.20+, Python 3.11+, and a C/C++ toolchain:

```text
cmake -S . -B build -DNESRECOMP_ROM="/path/to/Mega Man 4 (USA).nes"
cmake --build build --config Release --parallel 2
```

Use `NESRECOMP_ROOT` and `NESRECOMP_RECOMP_UI` to select existing engine and launcher checkouts. Runtime testing uses isolated configuration and explicit ROM paths.

## Quick weapon switching

The default-on package maps L/R (LB/RB) to the previous/next acquired weapon or helper. Selection wraps, skips locked equipment and includes empty weapons. Each press changes once; simultaneous shoulders cancel. Weapon changes clear the previous weapon's personal actors and charge, use native cartridge graphics and palettes, and preserve HP and ammunition. Start opens the native inventory. Both stick clicks open the system menu, with customized bindings retained.

The original slide and charged Mega Buster use their native mechanics. The project validates the USA cartridge payload before generation and keeps generated code in the build directory.

Run the functional controls checks with an explicit ROM path:

```text
python tools/qa_controls.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/controls
```

The check exercises acquired, locked and empty equipment, native/interpreter execution, graphics/palettes, held and simultaneous inputs, player isolation and current-build held-input continuation.

## Proto Man

Enable the optional Proto Man package in Mods. Its starting-character setting chooses Mega Man or Proto Man. Select swaps characters with a 32-frame teleport, preserves HP and equipment, cancels charge, and protects the player during the beam animation. Native slide, charge, weapon and helper mechanics remain available to both characters.

Proto Man mode supplies two uncharged shots, double incoming damage with native recoil, and a front airborne shield for supported small bullets. Reflected shots use native Buster damage independently of the selected weapon. The behavior setting also offers a reskin with Mega Man's stats. Included artwork is credited in the package's `assets/CREDITS.txt`.

```text
python tools/qa_proto.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/proto
```

The native inventory supplies Proto Man's life portrait and character preview on its original pixel grid. Rush and adapter previews retain their native graphics. This development slice covers solo play; multiplayer and full-stage qualification are still in progress.

## Adaptive widescreen

Fresh profiles enable Fit to screen. The aspect option also offers 16:9, 21:9 and 32:9. Horizontal rooms extend their authored terrain and native actors into the margins, with native slot identities, animation and collision. Original viewport pixels and the inventory's layout remain intact. Vertical scenes and transitions still need full-stage qualification.

```text
python tools/qa_wide.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/wide
```

The check compares native and interpreter execution, decodes terrain against the live viewport, checks native margin activation, and cold-loads a moving scene with a different capture schedule. It records the original game's brief right-edge palette-attribute streaming differences separately. Online play remains unavailable while the widened world and character implementation are unqualified for netplay.

## Co-op development

The development `--coop on` switch provides separate native player, weapon and helper contexts, guest sprite rendering, and either player's native inventory with input assigned to its owner. `--coop-roster protoman-megaman` reverses the default Mega Man / Proto Man order. Personal actors are slots 0–4; native world actors in slots 5–23 remain shared. Guest weapon artwork is read from cartridge packets without private CHR-RAM uploads.

The control slice is qualified through independent movement, jump, slide, firing, inventory ownership and current-build continuation. Each player's Buster and contact hitbox now use the native collision path against the shared world. Completed native horizontal room transitions bring the partner into the destination while preserving personal equipment. A boss finisher becomes the native player for victory, weapon-get and the following menus.

```text
python tools/qa_route.py tests/routes/coop-boss.route --frames 6200 --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/coop-boss --extra=--coop --extra=on --extra=--wide --extra=off
```

The targeted boss route initializes the native pre-boss checkpoint and uses temporary invulnerability, boss health and positioning fixtures. Player two's actual shot must trigger defeat; original scripts handle victory, teleport, weapon-get and stage select. Native/interpreter state, pictures and PCM agree, including a cold continuation during the native door script. These bypasses do not establish full-stage or normal boss-fight coverage.

Enemy targeting, complete guest weapon/shield behavior, death/rejoin/checkpoint lifecycle, all views and online behavior remain in development. Co-op is not yet exposed as a launcher package.
