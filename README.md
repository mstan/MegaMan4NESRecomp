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

For fresh-boot QA, `--mm4-qa-entry stage,page,room,art,palette,checkpoint` supplies a one-time entry to the original level initializer. Values are decimal. Initial areas use page, room, art, palette and checkpoint zero; checkpoint records come from the cartridge's native entry table. The original routines still load graphics, terrain, palettes, actors and the arrival sequence. This option is separate from the launcher and disables netplay. It does not write boss defeat, unlock or progression flags, and is not reapplied after a stage clear.

## Sprite display

No sprite flicker is enabled by default and works independently of widescreen. It draws complete native actors in stable priority beyond the original scanline and OAM display limits. Intentional animation hiding, the native PPU sprite-enable flag and native-width left-edge masking remain intact. Co-op renders each personal actor separately so a busy helper cannot truncate later weapon artwork. Use `--no-flicker on|off` for development comparisons.

```text
python -m pip install Pillow
python tools/qa_noflicker.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/noflicker
```

This check supplies an explicit native-actor crowd, verifies the fresh catalog default, native and wide composition, intentional hiding, interpreter agreement, and current-build state/picture/PCM continuation. It compares the original NES frame, CPU/RAM and audio with the mod disabled. The controlled population is a rendering prerequisite, separate from authored spawning and stage coverage.

## Proto Man

Enable the optional Proto Man package in Mods. Its starting-character setting chooses Mega Man or Proto Man. Select swaps characters with a 32-frame teleport, preserves HP and equipment, cancels charge, and protects the player during the beam animation. Native slide, charge, weapon and helper mechanics remain available to both characters.

Proto Man mode supplies two uncharged shots, double incoming damage with native recoil, and a front airborne shield for supported small bullets. Reflected shots use native Buster damage independently of the selected weapon. The behavior setting also offers a reskin with Mega Man's stats. Included artwork is credited in the package's `assets/CREDITS.txt`.

Airborne firing keeps the open-jump helmet and boot baseline, with the Buster leaving the cannon at the matching height in either direction. Ground, ladder and special-weapon origins retain their own rules.

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

Enable the optional Co-op package in Mods, or use `--coop on` for development checks. It provides separate native player, weapon and helper contexts, guest sprite rendering, and either player's native inventory with input assigned to its owner. `--coop-roster protoman-megaman` reverses the default Mega Man / Proto Man order. Personal actors are slots 0–4; native world actors in slots 5–23 remain shared. Guest weapon artwork is read from cartridge packets without private CHR-RAM uploads.

The control slice is qualified through independent movement, jump, slide, firing, inventory ownership and current-build continuation. Each player's Buster and contact hitbox now use the native collision path against the shared world. Completed native horizontal room transitions bring the partner into the destination while preserving personal equipment. A boss finisher becomes the native player for victory and weapon-get, with that player's native weapon graphics and palette restored immediately.

```text
python tools/qa_route.py tests/routes/coop-boss.route --frames 6200 --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/coop-boss --extra=--coop --extra=on --extra=--wide --extra=off
python tools/qa_route.py tests/routes/coop-boss-equipment.route --frames 6200 --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/coop-boss-equipment --extra=--coop --extra=on --extra=--coop-roster --extra=protoman-megaman --extra=--wide --extra=off
```

The targeted boss route initializes the native pre-boss checkpoint and uses temporary invulnerability, boss health and positioning fixtures. Player two's actual shot must trigger defeat; original scripts handle victory, teleport, weapon-get and stage select. Native/interpreter state, pictures and PCM agree, including a cold continuation during the native door script. These bypasses do not establish full-stage or normal boss-fight coverage.

Either living player can lead the tested right-hand boss shutter. The stage director receives the entering player's actual movement and terrain probe after both personal controllers have run; the original native room selection and scroll script still decide the transition. Both characters arrive with their own health, weapons and ammo.

```text
python tools/qa_coop_exit.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/coop-exit
```

This focused regression uses real Right input from the existing Bright Man checkpoint entry, with either player leading and the other idle. It checks the native boss intro, personal inventory, one world clock, interpreter agreement and a cold continuation during the actual shutter script. Vertical, alternate and distant exits retain their separate qualification requirements.

Personal button edges advance when that player's native controller runs. A new held press that begins during an output-only lag frame remains available to the next controller update, including during dense boss effects. Current-build continuation retains that input history.

An individual death keeps the partner in control without spending a life. Both the native collision and render-time pit paths retain their native death handling; the living partner keeps stage music. Personal countdowns execute the original timer routine. Once both players are down, one original shared retry spends a life and revives both at full health, with Buster selected and each player's own ammo retained. Either controller can operate global screens; each personal inventory keeps its owner's input.

Stage select retains its original picture after either character's finisher. Personal inventory artwork applies only to the actual inventory. The completed stage releases its player contexts before stage selection and reinitializes both players on the next native arrival. Both controllers can choose a stage after a partner's finisher; their matched inputs reach identical runtime state, picture and PCM. The native inventory's owner input and original solo artwork have separate regression coverage.

Targeted checks cover both death orders, actual lethal projectile contacts with sixteen native particles, pits including recoil, survivor movement, both players firing after retry, native/interpreter agreement, and current-build first-picture/PCM continuation. The previous native boss-finisher/victory route remains unchanged. Distant horizontal camera handoff now retains the living native hero after either player dies. Checkpoint/rejoin variants and complete stage coverage remain open.

The guest's airborne shield blocks supported native straight bullets in either direction. Reflections use the original Buster constructor and native overlap/damage routines, including when Rain Flush is selected. A constructor packet is prepared before the guest collision scope; the original native contact check decides whether to consume it. Reflected shots stay separate from the selected weapon's collision path. Rear hits, firing and the normal-stat reskin retain native damage and recoil. Targeted checks include a real reflected kill, both living native heroes, interpreter agreement, and current-build continuation with a reflected shot in flight. The boss-finisher and next-stage checks also pass with this change.

Independent split screen presents two native 256-by-240 panes; player-one and player-two views each retain one 256-by-240 pane. Select the view under Display in the runtime menu, or use `--coop-view shared|split|player1|player2`. Window resizing and aspect changes keep these grids intact. Returning to the shared view restores the selected aspect policy. Each pane has its player's native health and weapon meters, with a shared boss meter. Ordinary horizontal terrain is decoded from the native cartridge tables; the left eight pixels are revealed rather than blanked by the hardware edge mask.

Native actor activation uses the union of the two view rectangles and excludes their empty gap. An authored enemy is not repeatedly constructed while its spawn remains visible; leaving and returning allows the original constructor to create it again. Both live player bodies, separate terrain, native actor identities, distant survivor control, native/interpreter agreement and current-build continuation have targeted coverage. Scripted and vertical scenes currently retain the source camera; complete independent transition and inventory presentation remain open.

```text
python tools/qa_views.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/views
```

The targeted check uses declared personal positioning in one ordinary horizontal room. It does not force native hero types or mark stage completion. It checks fixed view widths, both living players, identical simulation across split and individual layouts, and cold continuation with matching state, first picture and audio.

Hold Select for 90 gameplay updates to withdraw with the native departure beam. The partner continues without losing a life. Release Select, then press it again to return beside the survivor on checked solid ground. HP, selected weapon and ammo stay personal; the last active player cannot withdraw. The return uses the original entry beam and restores normal controls afterward.

```text
python tools/qa_coop_withdraw.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/withdraw
```

This targeted check exercises each player, last-player protection, retained equipment, native beam scripts, native/interpreter agreement, and current-build state/picture/PCM continuation during departure and return. Its stationary room suppresses enemies and supplies explicit HP/ammo prerequisites.

Native enemy AI targets the nearest living player, including that player's consumed input. Either player can enter and return from native refight portals; original map and boss-intro scripts carry both players while preserving personal equipment. Complete guest weapon/helper behavior, checkpoint variants, independent inventories, vertical/wait-for-both transitions, and the final online UI/lobby/reconnect qualification remain in development. The online settings adapter seals the gameplay profile and restores offline runtime and catalog preferences on exit; final release qualification remains separate.
