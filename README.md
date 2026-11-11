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

This development slice covers solo play. Menu portraits, adaptive world rendering, multiplayer and full-stage qualification are still in progress; online play is unavailable while this character implementation remains unqualified for netplay.
