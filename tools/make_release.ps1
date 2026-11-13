param([string]$Rom,[string]$BuildDir='build_release',[string]$EngineRoot,[string]$RecompUi,
      [string[]]$CMakeArgs=@(),[switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(-not $EngineRoot){$EngineRoot=Join-Path $root 'nesrecomp'}
$notes=@'
Fit to screen, L/R weapon switching, Reduce slowdown and No sprite flicker
start enabled for a fresh profile. Existing saved choices take precedence.

Enable Proto Man in Mods for Select teleport swapping, the airborne front
shield, two Buster shots and double incoming damage. Normal-stat mode is
also available. Native charge shots and sliding remain available.

Enable Co-op in Mods and assign each player's controls in Controls. Health,
equipment, ammunition and native inventories are personal. Only the player
who paused can resume. Shared, split and individual views are available.
Hold Select to withdraw while your partner remains; release and press it
again to rejoin beside a safely grounded partner.

Enable Co-op before creating an online room. Shared or independent gameplay
policies stay fixed for the match. Individual layouts can change locally
within an independent match.

Preserve existing saves and settings when replacing an older installation.
Save states belong to this build. No ROM is included.
'@
& (Join-Path $EngineRoot 'tools/package_cycle_windows.ps1') -ProjectRoot $root -Target 'MegaMan4Recomp' -Title 'Mega Man 4' -BuildDir $BuildDir -Rom $Rom -EngineRoot $EngineRoot -RecompUi $RecompUi -CMakeArgs $CMakeArgs -SkipBuild:$SkipBuild -GameNotes $notes
