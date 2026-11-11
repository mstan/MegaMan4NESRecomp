"""Check solo selection, native abilities, READY and current-build continuation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import qa_route

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--rom', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--baseline-hash')
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, 'NESRECOMP_NO_LAUNCHER': '1',
           'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy'}
    checks = {}
    for name, mode in [('off', None), ('reskin', 'megaman'), ('traits', 'protoman')]:
        case = out / name
        route, frames = ('proto-gallery', 4520) if mode else ('boot-play', 4120)
        cmd = [sys.executable, str(ROOT / 'tools/qa_route.py'),
               str(ROOT / f'tests/routes/{route}.route'), '--exe', a.exe, '--rom', a.rom,
               '--out', str(case), '--frames', str(frames), '--shot-every', '200']
        if not mode:
            cmd.append('--no-interp')
        options = ['--config', str(case / 'config.ini'), '--protoman', 'on' if mode else 'off',
                   '--mm4-controls-log', str(case / 'state.jsonl'),
                   '--present-out', str(case / 'present.png'), '--present-every', '200']
        options += ['--proto-mode', mode] if mode else ['--weapon-cycle', 'off']
        cmd += ['--extra=' + option for option in options]
        with (out / f'{name}.log').open('w') as log:
            subprocess.run(cmd, check=True, stdout=log, stderr=subprocess.STDOUT, env=env)
        if not mode:
            if a.baseline_hash:
                checks['stock_identity'] = (qa_route.load_hashes(case / 'native.hash') ==
                                            qa_route.load_hashes(Path(a.baseline_hash)))
            continue
        rows = [json.loads(s) for s in (case / 'state.jsonl').read_text().splitlines()]
        checks[name + '_swap_1_0_1'] = (rows[3449]['proto'] == 1 and rows[3500]['proto'] == 0 and
                                       rows[3600]['proto'] == 1 and rows[-1]['switches'] == 2)
        checks[name + '_preserved_hp_equipment'] = all(r['hp'] == 156 and r['weapon'] == 0 for r in rows[2450:])
        checks[name + '_native_slide_charge'] = rows[3900]['state'] == 2 and rows[4190]['charge'] >= 32
        checks[name + '_native_ready'] = (rows[2200]['state'] == 10 and rows[2200]['pose'] == 0x56 and
                                           (case / 's_02200.png').read_bytes() == (case / 'present_02200.png').read_bytes())
        cold = case / 'cold'
        cold.mkdir(exist_ok=True)
        cmd = [a.exe, a.rom, '--no-save', '--config', str(cold / 'config.ini'),
               '--frames', '4520', '--input', str(case / 'input.txt'), '--protoman', 'on',
               '--proto-mode', mode, '--load-state', str(case / 'swapping.cycstate'),
               '--hash-out', str(cold / 'cold.hash'), '--present-out', str(cold / 'present.png'),
               '--mem-frame', '4519', '--mem-out', str(cold / 'mem.txt')]
        with (cold / 'run.log').open('w') as log:
            subprocess.run(cmd, check=True, stdout=log, stderr=subprocess.STDOUT, env=env)
        full, tail = qa_route.load_hashes(case / 'native.hash'), qa_route.load_hashes(cold / 'cold.hash')
        checks[name + '_cold_hashes'] = len(tail) == 1059 and tail == {f: h for f, h in full.items() if f > 3460}
        checks[name + '_cold_memory'] = (cold / 'mem.txt').read_bytes() == (case / 'mem_end.txt').read_bytes()
        checks[name + '_cold_picture'] = (cold / 'present.png').read_bytes() == (case / 'present.png').read_bytes()
    result = dict(checks=checks, passed=all(checks.values()),
                  executable_sha256=hashlib.sha256(Path(a.exe).read_bytes()).hexdigest())
    (out / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
