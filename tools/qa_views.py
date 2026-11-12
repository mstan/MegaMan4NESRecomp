"""One-room split/individual views, both live players, backend and cold-state checks."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,wave
import qa_route
ROOT=Path(__file__).resolve().parent.parent
ap=argparse.ArgumentParser(description="Check independent co-op views and current-build continuation in one horizontal room.")
ap.add_argument('--exe',required=True)
ap.add_argument('--rom',required=True)
ap.add_argument('--out',required=True)
ap.add_argument('--slowdown-reduction',choices=['on','off'])
a=ap.parse_args();requested_slowdown=a.slowdown_reduction;out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True);exe=Path(a.exe).resolve();rom=Path(a.rom).resolve()
_,lines=qa_route.expand(ROOT/'tests/routes/coop-gallery.route');lines=[x for x in lines if int(x.split()[0])<2400]+['2500 2:LEFT','2504 2:-','2570 2:B','2571 2:-'];(out/'input.txt').write_text('\n'.join(lines)+'\n')
env={**os.environ,'NESRECOMP_NO_LAUNCHER':'1','SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy'}
for name in ['native','interp','cold','player1','player2']:
 d=out/name;d.mkdir(exist_ok=True);mode=name if name.startswith('player') else 'split'
 args=[str(exe),str(rom),'--no-save','--config',str(d/'config.ini'),'--frames','2800','--input',str(out/'input.txt'),'--weapon-cycle','off','--coop','on','--coop-view',mode,'--wide','on','--aspect','fit','--present-size','1280x720','--mm4-controls-log',str(d/'state.jsonl'),'--hash-out',str(d/'run.hash'),'--screenshot',str(d/'hardware.png'),'--present-out',str(d/'present.png'),'--present-every','2501','--wav-out',str(d/'audio.wav'),'--mem-frame','2799','--mem-out',str(d/'mem.txt'),'--save-state','2500:'+str(d/'moving.cycstate'),'--save-state','2799:'+str(d/'end.cycstate')]
 for address,v in [(0x330,0x60),(0x348,2),(0x378,148),(0x390,0),(0x318,0),(0x360,0),(0x3d8,0),(0x3f0,0)]:args+=['--mm4-coop-poke',f'2440:1:{address:X}:{v:X}']
 if requested_slowdown:args+=['--slowdown-reduction',requested_slowdown]
 if name=='interp':args+=['--interp-only']
 if name in ['cold','player1','player2']:args+=['--load-state',str(out/'native/moving.cycstate')]
 with (d/'run.log').open('w') as log:subprocess.run(args,check=True,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0)
 print(name,flush=True)
checks={};n=out/'native';i=out/'interp';c=out/'cold'
checks['backends']=all((n/p).read_bytes()==(i/p).read_bytes() for p in ['run.hash','present.png','audio.wav','mem.txt','end.cycstate'])
full=qa_route.load_hashes(n/'run.hash');tail=qa_route.load_hashes(c/'run.hash');checks['cold_frames']=len(tail)==299 and tail=={f:h for f,h in full.items() if f>2500}
checks['cold_outputs']=all((n/p).read_bytes()==(c/p).read_bytes() for p in ['present.png','mem.txt','end.cycstate','present_02501.png'])
with wave.open(str(n/'audio.wav')) as a,wave.open(str(c/'audio.wav')) as b:pcm=a.readframes(a.getnframes());tailpcm=b.readframes(b.getnframes())
checks['cold_pcm']=bool(tailpcm) and pcm.endswith(tailpcm)
w,h=struct.unpack('>II',(n/'present.png').read_bytes()[16:24]);checks['split_width']=w==512 and h==240
for seat in range(2):
 p=out/('player'+str(seat+1));pw,ph=struct.unpack('>II',(p/'present.png').read_bytes()[16:24]);checks[f'p{seat+1}_width']=pw==256 and ph==240
 checks[f'p{seat+1}_canonical_simulation']=all((p/f).read_bytes()==(c/f).read_bytes() for f in ['run.hash','mem.txt','audio.wav','end.cycstate'])
rows=[json.loads(x) for x in (n/'state.jsonl').read_text().splitlines()];last=rows[-1]
if requested_slowdown:checks['actual_cpu_budget']=last['timing']['extra_lines']==(262 if requested_slowdown=='on' else 0)
checks['both_living']=last['coop']['dead']==[0,0] and all(x[2]>0 for x in last['coop']['players'])
checks['separate_bodies']=last['coop']['players'][1][5]>0
checks['no_scope_failure']=all(x['coop']['failed']==0 and x['wide']['failed']==0 for x in rows)
result={'checks':checks,'passed':all(checks.values()),'last':last,'exe':hashlib.sha256(exe.read_bytes()).hexdigest()};(out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(result,flush=True);raise SystemExit(not result['passed'])
