"""Run one QA route natively and interpreter-only, and compare them.

A route is a cycle-host `--input` schedule (pad lines and cyc_cheat.h
POKE/HOLD/HOLD_MAX/HOLD_MIN/UNHOLD lines) with two conveniences:

  +N <command>        N frames after the previous line's frame
  SAVESTATE <name>    (as a command) write <out>/<name>.cycstate after that
                      frame, for loading in a windowed build (eyeball hand-off)
  INCLUDE <file>      splice another route (relative to this file); its
                      frames continue from the current frame
  # ...               comment

Output directory: expanded input.txt, native.hash / interp.hash, numbered
native screenshots, native.prev.hash (the previous run's, compared as
matches_previous_native: a rebuild with more seeds must not change it), miss.txt (instruction starts the interpreter ran in the
native run), mem_end.txt, ring.txt, sheet.png, summary.json.

Exit status: 0 parity and the run completed; 1 native/interpreter divergence;
2 a run failed.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATES = []


def expand(path, frame=0, out=None, depth=0):
    if depth > 8:
        raise SystemExit(f'{path}: INCLUDE nesting too deep')
    out = [] if out is None else out
    for raw in Path(path).read_text().splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if line.upper().startswith('INCLUDE '):
            frame = expand(Path(path).parent / line.split(None, 1)[1], frame, out, depth + 1)
            continue
        head, _, rest = line.partition(' ')
        if head.startswith('+'):
            frame += int(head[1:])
        else:
            frame = int(head)
        rest = rest.strip()
        if rest.upper().startswith('SAVESTATE '):
            STATES.append((frame, rest.split(None, 1)[1].strip()))
            continue
        out.append(f'{frame} {rest}')
    return frame if depth else (frame, out)


def run(cmd, log):
    with open(log, 'w') as f:
        return subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT,
            env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'),
            creationflags=(subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            if os.name == 'nt' else 0).returncode


def load_hashes(p):
    rows = {}
    for line in Path(p).read_text().splitlines():
        parts = line.split(' ', 1)
        if len(parts) == 2 and parts[0].isdigit():
            rows[int(parts[0])] = parts[1]
    return rows


def coverage(log):
    text = Path(log).read_text(errors='replace')
    m = re.search(r'native_cycles=(\d+) \(([\d.]+)%\)', text)
    i = re.search(r'interpreted: ROM (\d+) \(([\d.]+)%', text)
    return {
        'native_pct': float(m.group(2)) if m else None,
        'interpreted_rom_cycles': int(i.group(1)) if i else None,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('route')
    ap.add_argument('--frames', type=int, help='default: last route frame + 120')
    ap.add_argument('--out', help='output directory (default qa/routes/<route stem>)')
    ap.add_argument('--exe', default=str(ROOT / 'build' / 'Release' / 'MegaMan4Recomp.exe'))
    ap.add_argument('--rom', required=True, help='explicit original Mega Man 4 USA ROM path')
    ap.add_argument('--shot-every', type=int, default=120)
    ap.add_argument('--no-interp', action='store_true', help='skip the interpreter-only parity run')
    ap.add_argument('--extra', action='append', default=[], help='extra argument for the game exe (repeatable), e.g. --extra=--slide --extra=on')
    a = ap.parse_args()

    last, lines = expand(a.route)
    frames = a.frames or last + 120
    out = Path(a.out) if a.out else ROOT / 'qa' / 'routes' / Path(a.route).stem
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob('s_*.png'):
        old.unlink()
    (out / 'input.txt').write_text('\n'.join(lines) + '\n')
    (out / 'miss.txt').unlink(missing_ok=True)
    prev = out / 'native.prev.hash'
    if (out / 'native.hash').exists():
        (out / 'native.hash').replace(prev)

    base = [a.exe, a.rom, '--frames', str(frames), '--input', str(out / 'input.txt'), '--no-save'] + a.extra
    native = base + ['--hash-out', str(out / 'native.hash'), '--screenshot', str(out / 's.png'),
                     '--shot-every', str(a.shot_every), '--miss-log', str(out / 'miss.txt'),
                     '--mem-frame', str(frames - 1), '--mem-out', str(out / 'mem_end.txt'),
                     '--ring-out', str(out / 'ring.txt')]
    for f, name in STATES:
        native += ['--save-state', f'{f}:{out / (name + ".cycstate")}']
    summary = {'route': str(a.route), 'frames': frames, 'states': [f'{n}@{f}' for f, n in STATES]}
    rc = run(native, out / 'native.log')
    summary['native_exit'] = rc
    summary.update(coverage(out / 'native.log'))
    if rc != 0:
        summary['result'] = 'native run failed'
    elif not a.no_interp:
        irc = run(base + ['--interp-only', '--hash-out', str(out / 'interp.hash')], out / 'interp.log')
        summary['interp_exit'] = irc
        if irc != 0:
            summary['result'] = 'interpreter run failed'
        else:
            n, i = load_hashes(out / 'native.hash'), load_hashes(out / 'interp.hash')
            diverge = next((f for f in sorted(n) if n[f] != i.get(f)), None)
            summary['hash_frames'] = len(n)
            summary['first_divergence'] = diverge
            if diverge is not None:
                summary['native_at_divergence'] = n[diverge]
                summary['interp_at_divergence'] = i.get(diverge)
            summary['result'] = 'parity' if diverge is None and len(n) == len(i) else 'DIVERGED'
    else:
        summary['result'] = 'native only'
    if prev.exists() and (out / 'native.hash').exists() and rc == 0:
        p, n = load_hashes(prev), load_hashes(out / 'native.hash')
        changed = next((f for f in sorted(n) if n[f] != p.get(f)), None)
        summary['matches_previous_native'] = changed is None and len(n) == len(p)
        summary['first_change_vs_previous'] = changed
    misses = [l for l in (out / 'miss.txt').read_text().splitlines() if l.startswith('4k:')] \
        if (out / 'miss.txt').exists() else []
    summary['rom_miss_lines'] = len(misses)
    shots = sorted(out.glob('s_*.png'))
    if shots:
        try:
            from PIL import Image, ImageDraw
            cols, w, h = 8, 256, 240
            rows = (len(shots) + cols - 1) // cols
            sheet = Image.new('RGB', (cols * w // 2, rows * (h // 2 + 10)), (40, 40, 40))
            d = ImageDraw.Draw(sheet)
            for k, s in enumerate(shots):
                x, y = (k % cols) * w // 2, (k // cols) * (h // 2 + 10)
                sheet.paste(Image.open(s).convert('RGB').resize((w // 2, h // 2)), (x, y + 10))
                d.text((x + 2, y), s.stem[2:], fill=(255, 255, 0))
            sheet.save(out / 'sheet.png')
        except ImportError:
            pass
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary))
    if summary['result'] in ('parity', 'native only'):
        return 0
    return 1 if summary['result'] == 'DIVERGED' else 2


if __name__ == '__main__':
    sys.exit(main())
