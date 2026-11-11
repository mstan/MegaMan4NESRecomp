"""Check authored terrain, native margin actors and current-build continuation."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import qa_route

ROOT=Path(__file__).resolve().parent.parent

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe',required=True)
    ap.add_argument('--rom',required=True)
    ap.add_argument('--out',required=True)
    a=ap.parse_args()
    out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
    env={**os.environ,'NESRECOMP_NO_LAUNCHER':'1','SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy'}
    cmd=[sys.executable,str(ROOT/'tools/qa_route.py'),str(ROOT/'tests/routes/wide-gallery.route'),
         '--exe',a.exe,'--rom',a.rom,'--frames','4120','--out',str(out),'--shot-every','100']
    extras=['--config',str(out/'config.ini'),'--wide','on','--aspect','16-9','--protoman','off',
            '--world-audit',str(out/'world.csv'),'--mm4-controls-log',str(out/'state.jsonl'),
            '--present-out',str(out/'present.png'),'--present-every','100']
    for s in extras:cmd.append('--extra='+s)
    with (out/'run.log').open('w') as log:
        subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
    rows=list(csv.DictReader((out/'world.csv').open()))
    states=[json.loads(s) for s in (out/'state.jsonl').read_text().splitlines()]
    checks={'native_interpreter_exact':True,
            'terrain_native_or_proven_streaming_attribute':bool(rows) and all(int(x['mismatched'])==int(x['native_edge_attribute_pixels']) for x in rows),
            'native_margin_actor_spawned':any(x['wide']['spawns'] for x in states),
            'no_projection_failures':all(x['wide']['failed']==0 for x in states),
            'widened_426_pixels':states[-1]['wide']['width']==426}
    cold=out/'cold';cold.mkdir(exist_ok=True)
    cmd=[a.exe,a.rom,'--no-save','--config',str(cold/'config.ini'),'--wide','on','--aspect','16-9',
         '--protoman','off','--frames','4120','--input',str(out/'input.txt'),
         '--load-state',str(out/'moving.cycstate'),'--hash-out',str(cold/'cold.hash'),
         '--present-out',str(cold/'present.png'),'--mem-frame','4119','--mem-out',str(cold/'mem.txt')]
    # Deliberately omit periodic presentation: render history must not move actors.
    with (cold/'run.log').open('w') as log:
        subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,env=env)
    full=qa_route.load_hashes(out/'native.hash');tail=qa_route.load_hashes(cold/'cold.hash')
    checks['cold_1319_hashes']=len(tail)==1319 and tail=={f:h for f,h in full.items() if f>2800}
    checks['cold_memory']=(cold/'mem.txt').read_bytes()==(out/'mem_end.txt').read_bytes()
    checks['cold_picture']=(cold/'present.png').read_bytes()==(out/'present.png').read_bytes()
    result={'checks':checks,'passed':all(checks.values()),'terrain_frames':len(rows),
            'native_streaming_attribute_pixels':sum(int(x['mismatched']) for x in rows),
            'executable_sha256':hashlib.sha256(Path(a.exe).read_bytes()).hexdigest()}
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
