# Mega Man 4 — NESRecomp

NESRecomp cycle-backend project for the original USA cartridge. Supply your own ROM; the project and releases contain no cartridge data.

Version 0.2.0 adds optional Proto Man, local and online co-op, quick weapon switching and adaptive widescreen while retaining the original game mechanics. Fresh profiles enable Fit to screen, No sprite flicker, Reduce slowdown, quick weapon switching and VSync. Saved choices remain in effect.

Configure with CMake 3.20+, Python 3.11+, and a C/C++ toolchain:

```text
cmake -S . -B build -DNESRECOMP_ROM="/path/to/Mega Man 4 (USA).nes"
cmake --build build --config Release --parallel 2
```

Use `NESRECOMP_ROOT` and `NESRECOMP_RECOMP_UI` to select existing engine and launcher checkouts. Runtime testing uses isolated configuration and explicit ROM paths.

## Quick weapon switching

The default-on package maps L/R (LB/RB) to the previous/next acquired weapon or helper. Selection wraps, skips locked equipment and includes empty weapons. Each press changes once; simultaneous shoulders cancel. Weapon changes clear the previous weapon's personal actors and charge, use native cartridge graphics and palettes, and preserve HP and ammunition. Switching out of Rush Marine returns to native foot controls and artwork, as native inventory selection does. Start opens the native inventory. Both stick clicks open the system menu, with customized bindings retained.

The original slide and charged Mega Buster use their native mechanics. The project validates the USA cartridge payload before generation and keeps generated code in the build directory.

Run the functional controls checks with an explicit ROM path:

```text
python tools/qa_controls.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/controls
```

The check exercises acquired, locked and empty equipment, native/interpreter execution, graphics/palettes, held and simultaneous inputs, player isolation and current-build held-input continuation.

For fresh-boot QA, `--mm4-qa-entry stage,page,room,art,palette,checkpoint` supplies a one-time entry to the original level initializer. Values are decimal. Initial areas use page, room, art, palette and checkpoint zero; checkpoint records come from the cartridge's native entry table. The original routines still load graphics, terrain, palettes, actors and the arrival sequence. This option is separate from the launcher and disables netplay. It does not write boss defeat, unlock or progression flags, and is not reapplied after a stage clear.

## Sprite display

No sprite flicker is enabled by default and works independently of widescreen. It draws complete native actors in stable priority beyond the original scanline and OAM display limits. Intentional animation hiding, the native PPU sprite-enable flag and native-width left-edge masking remain intact. Co-op renders each personal actor separately so a busy helper cannot truncate later weapon artwork. Use `--no-flicker on|off` for development comparisons.

Wider rooms retain their geometry while player sprites are hidden. Authored
edge columns replace the hardware's masked and unfinished viewport columns.
Personal meters follow native HUD visibility and keep their screen positions
through Bright's blackout and recovery. Native OAM writes cannot inherit stale
actor ownership. Targeted checks cover the native blackout, spike death and
survivor handoff, both CPU backends and current-build picture/state/audio replay.
Fresh profiles enable VSync; existing saved choices remain in effect.

`tools/qa_presentation.py` exercises that native route with both controllers.
`tools/make_release.ps1` builds the ROM-free Windows package using the pinned
engine packager and its library/font notices.

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

The native inventory supplies Proto Man's life portrait and character preview on its original pixel grid. Rush and adapter previews retain their native graphics.

## Adaptive widescreen

Fresh profiles enable Fit to screen. The aspect option also offers 16:9, 21:9 and 32:9. Horizontal rooms extend their authored terrain and native actors into the margins, with native slot identities, animation and collision. Original viewport pixels and the inventory's layout remain intact. Vertical rooms and scripted transitions retain their native camera and layout.

```text
python tools/qa_wide.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/wide
```

The check compares native and interpreter execution, decodes terrain against the live viewport, checks native margin activation, and cold-loads a moving scene with a different capture schedule. It records the original game's brief right-edge palette-attribute streaming differences separately. Enable Co-op before creating an online room. Peers agree on a fixed gameplay profile before the match; leaving the session restores offline preferences.

## Co-op

Enable the optional Co-op package in Mods, or use `--coop on` for development checks. It provides separate native player, weapon and helper contexts, guest sprite rendering, and either player's native inventory with input assigned to its owner. `--coop-roster protoman-megaman` reverses the default Mega Man / Proto Man order. Personal actors are slots 0–4; native world actors in slots 5–23 remain shared. Guest weapon artwork is read from cartridge packets without private CHR-RAM uploads.

Marine boarding and fuel stay personal. Each controller runs the original native boarding and fuel cost. A guest's last-unit dismount retains the original cost and animation while its immutable cartridge artwork replaces the unnecessary guest PPU upload wait. Mounted and on-foot Marine body patterns use their original asset streams, independently of the other player's current form. Proto Man replaces only the cockpit helmet. The complete 621-pixel native Marine sprite matches the original viewport's decoded OAM; separate cockpit checks cover both players, shared/split views and No sprite flicker off. Native world sprites retain their occlusion.

```text
python tools/qa_coop_helpers.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --root qa/marine --helper marine --qualify
```

This bounded check uses native summon/boarding input, L/R dismount and returned Buster fire, then a separate one-unit fuel depletion while moving inside the water. Both ownership directions must dismount alive and retain the other player's fuel. Native/interpreter state, pictures and audio agree, including current-build first-picture/PCM continuation after switching and near depletion. Concurrent-form checks also cover one player returning to foot controls and firing while the other keeps piloting, with independent native body artwork and fuel clocks.

Flash Stopper freezes the shared native world from either seat. Its original timer advances once per controller update even when both players equip it; simultaneous requests spend one activation cost and the second controller retains the original active-freeze Buster behavior. `tools/qa_coop_effects.py --exe ... --rom ... --root ... --qualify` verifies authored enemy freeze/resume, exact shared world behavior for either owner, personal costs, backend agreement and current-build first-picture/state/PCM continuation.

Rain Flush applies its native damage and screen effect from either seat, retaining personal energy costs and one native pulse clock. The rain uses its own cartridge graphics and palette across shared, split and adaptive views, including with sprite flicker enabled. `tools/qa_coop_effects.py --effect rain --exe ... --rom ... --root ... --qualify` checks authored enemy defeat, original PPU rain pixels, personal costs, simultaneous activations, backend agreement and current-build first-picture/state/PCM continuation mid-effect. Pillow is required for its pixel comparisons. Current snapshots include the latched effect palette and remain build-specific.

Balloon platforms support either living player. The native floor probe includes the partner's Balloons, and each Balloon runs its original motion, landing response and lifetime once with the touching rider's body. Personal fuel and the owner's helper pool remain separate. `--helper balloon --qualify` covers both rider/owner directions, exact native helper fields, jump-off, expiry, the three-Balloon limit and a rejected fourth summon without extra cost, backend agreement and current-build first-picture/state/PCM continuation.

Wire uses its original grapple controller for both players. A focused `--helper wire --qualify` check covers native Up+B attachment and ascent, Buster fire while hanging, Down detachment, personal energy, backend agreement and current-build first-picture/state/PCM continuation mid-grapple.

Changing a presentation option in Mods retains live co-op bodies, inventories and pending wait-for-both positions when co-op remains selected. The catalog's synchronous reset/activation pass does not create a new team.

Split and individual views give each player a usable native inventory page with their own cursor, health, ammo, equipment and character portrait. Either living player can pause; only the pauser can resume, using A or Start on an equipment row. The other player can choose equipment without resuming. Lives and E-tanks stay shared. Simultaneous E-tank requests use player order and the original refill routine, with one tank spent for one recipient. Both equipment choices are retained when they arrive on the pauser's closing tick. Shared view retains the original single-page inventory.

```text
python tools/qa_coop_inventory.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/inventory
```

This check reuses the actual pit/checkpoint setup, supplies low health and one E-tank before pausing, and uses native cursor/confirm input. It covers both pause owners, a contested refill, closing-tick choices, one frozen world clock, interpreter agreement and current-build first-picture/state/PCM continuation during the refill.

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

This focused regression uses real Right input from the existing Bright Man checkpoint entry, with either player leading and the other idle. It checks the native boss intro, personal inventory, one world clock, interpreter agreement and a cold continuation during the actual shutter script.

Distant independent-view exits refill the selected source page with the original native column loader before resuming the original room director. The refill drains pending VRAM writes, permits NMI publication and restores the caller's stack, registers, mapper and update lock. Disabling co-op during a pending refill retains its return guards, including after a current-build cold load.

```text
python tools/qa_coop_source.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/coop-source
```

The bounded route has the distant guest lead Bright's original page3-to4 exit, then the other player lead the authored page6-to7 ladder from a different source camera. Both keep personal health, ammo and equipment and fire native shots afterward. Checks cover the native scroll background, interpreter agreement and current-build state, first picture and PCM during the queue drain and both refills. A separate native near-exit comparison matches the horizontal nametable data for all127 scrolling frames and the ladder's destination nametables and background patterns.

Either player can also lead native upward and downward ladder scrolling. Both bodies ride the original script on its real clock and retain personal HP, weapon and ammo. They share the authored ladder column during the scroll, then resume their own controls. The scroll's saved return context distinguishes its completion from nested tile-streaming calls.

```text
python tools/qa_coop_exit.py --vertical --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/coop-up
python tools/qa_coop_exit.py --vertical --return-down --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/coop-return
```

The ladder checks enter the original Bright Man room initializer, supply explicit player positions on native ladder/floor tiles and use actual directional input. The return follows the completed climb, with the opposite player leading. Both directions have interpreter and mid-scroll cold picture/state/PCM coverage. These are representative transition checks, separate from stage smoke coverage and the optional wait-for-both policy.

Personal button edges advance when that player's native controller runs. A new held press that begins during an output-only lag frame remains available to the next controller update, including during dense boss effects. Current-build continuation retains that input history.

An individual death keeps the partner in control without spending a life. Both the native collision and render-time pit paths retain their native death handling; the living partner keeps stage music. Personal countdowns execute the original timer routine. Once both players are down, one original shared retry spends a life and revives both at full health, with Buster selected and each player's own ammo retained. Either controller can operate global screens; each personal inventory keeps its owner's input.

After Game Over, the password/continue screen uses its centered native PPU picture in solo and co-op. It does not inherit gameplay terrain or the previous camera. Continue and stage selection reinitialize both co-op players at full health with Buster and their own remaining ammo. Original boss rewards and stage-entry refills apply to both players.

When the survivor reaches a new native checkpoint, a fallen partner returns at full health with Buster selected and their remaining ammo intact. Recovery waits for checked standing room and uses the native entry beam. It spends no life and preserves the survivor's health. A player who deliberately withdrew continues to use Select to return.

```text
python tools/qa_coop_checkpoint.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/checkpoint
```

The focused check uses an actual pit death, then real slide/Right input through the original low corridor and checkpoint transition. It covers both survivor roles, full-health/Buster recovery, personal ammo, returned movement/L/R/inventory, native/interpreter agreement and current-build first-picture/PCM continuation during the beam. Entry positioning is explicit; no death, checkpoint or recovery result flag is supplied.

Stage select retains its original picture after either character's finisher. Personal inventory artwork applies only to the actual inventory. The completed stage releases its player contexts before stage selection and reinitializes both players on the next native arrival. Both controllers can choose a stage after a partner's finisher; their matched inputs reach identical runtime state, picture and PCM. The native inventory's owner input and original solo artwork have separate regression coverage.

Targeted checks cover both death orders, actual lethal projectile contacts with sixteen native particles, pits including recoil, survivor movement, both players firing after retry, native/interpreter agreement, and current-build first-picture/PCM continuation. The previous native boss-finisher/victory route remains unchanged. Distant horizontal camera handoff now retains the living native hero after either player dies.

The guest's airborne shield blocks supported native straight bullets in either direction. Reflections use the original Buster constructor and native overlap/damage routines, including when Rain Flush is selected. A constructor packet is prepared before the guest collision scope; the original native contact check decides whether to consume it. Reflected shots stay separate from the selected weapon's collision path. Rear hits, firing and the normal-stat reskin retain native damage and recoil. Targeted checks include a real reflected kill, both living native heroes, interpreter agreement, and current-build continuation with a reflected shot in flight. The boss-finisher and next-stage checks also pass with this change.

Independent split screen presents two native 256-by-240 panes; player-one and player-two views each retain one 256-by-240 pane. Select the view under Display in the runtime menu, or use `--coop-view shared|split|player1|player2`. Window resizing and aspect changes keep these grids intact. Returning to the shared view restores the selected aspect policy. Each pane has its player's native health and weapon meters, with a shared boss meter. Ordinary horizontal terrain is decoded from the native cartridge tables; the left eight pixels are revealed rather than blanked by the hardware edge mask.

In shared view, the camera follows the living team in ordinary horizontal rooms. Movement stops at the shared separation limit so both players stay visible; moving inward lets the team regroup and advance. A survivor remains free to move after the partner leaves. Native vertical and scripted room transitions retain their own handling.

Native actor activation uses the union of the two view rectangles and excludes their empty gap. An authored enemy is not repeatedly constructed while its spawn remains visible; leaving and returning allows the original constructor to create it again. Both live player bodies, separate terrain, native actor identities, distant survivor control, native/interpreter agreement and current-build continuation have targeted coverage. Scripted and vertical scenes use the native source camera; the tested distant exits first refill that source with native terrain.

```text
python tools/qa_views.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/views
```

The targeted check uses declared personal positioning in one ordinary horizontal room. It does not force native hero types or mark stage completion. It checks fixed view widths, both living players, identical simulation across split and individual layouts, and cold continuation with matching state, first picture and audio.

Hold Select for 90 gameplay updates to withdraw with the native departure beam. The partner continues without losing a life. Release Select, then press it again to return beside the survivor on checked solid ground. HP, selected weapon and ammo stay personal; the last active player cannot withdraw. The return uses the original entry beam and restores normal controls afterward.

```text
python tools/qa_coop_withdraw.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --out qa/withdraw
```

This targeted check exercises each player, last-player protection, retained equipment, native beam scripts, native/interpreter agreement, and current-build state/picture/PCM continuation during departure and return. Its stationary room suppresses enemies and supplies explicit HP/ammo prerequisites.

Native enemy AI targets the nearest living player, including that player's consumed input. Either player can enter and return from native refight portals; original map and boss-intro scripts carry both players while preserving personal equipment. The online settings adapter seals the gameplay profile and restores offline runtime and catalog preferences on exit.

Flash Stopper and Rain Flush share one native world clock while retaining personal costs. Rain can damage frozen enemies. A pending freeze expires even if its caster withdraws or co-op is disabled. Current-build snapshots retain the effect clock across native controller waits.

Pharaoh Shot retains each player's linked charge orb, release projectile and energy. Guest weapons use the primary renderer's widened bounds, so shots remain active and visible through adaptive margins before native retirement. The focused check covers both owners, simultaneous charging, NoF on/off, native/interpreter agreement and cold continuation:

```text
python tools/qa_coop_linked.py --exe build/Release/MegaMan4Recomp.exe --rom "/path/to/Mega Man 4 (USA).nes" --root qa/linked --qualify
```
