"""Shared-screen edges, inward movement and current-build continuation."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,wave
import qa_route

p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','rom','out'):p.add_argument('--'+key,required=True)
p.add_argument('--width',type=int,choices=(256,512),help='Run one width; by default qualify both.')
a=p.parse_args();exe=Path(a.exe).resolve();rom=Path(a.rom).resolve();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
_,prefix=qa_route.expand(Path(__file__).resolve().parent.parent/'tests/routes/coop-gallery.route');prefix=[x for x in prefix if int(x.split()[0])<2400]
env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');checks={};cases=[]
def positions(row):return [p[0]+256*p[5] for p in row['coop']['players']]
for width in ((a.width,) if a.width else (256,512)):
 for flicker in ('off','on'):
  root=out/f'{width}-{flicker}';root.mkdir();route=root/'input.txt'
  # Place the wider pair on the platform around world608.
  lag=64 if width==256 else 144
  inward_end=2640 if width==256 else 2620
  forward_end=2760 if width==256 else 2720
  route.write_text('\n'.join(prefix+['2400 HOLD 0308-0317 00','2500 LEFT','2500 2:RIGHT','2600 RIGHT','2600 2:LEFT',f'{inward_end} 2:-','2700 -','2700 2:RIGHT',f'{forward_end} 2:-'])+'\n')
  for mode in ('native','interpreter','cold','instant'):
   d=root/mode;d.mkdir()
   args=[str(exe),str(rom),'--no-save','--config',str(d/'config.ini'),'--frames','2551' if mode=='instant' else '2800','--input',str(route),'--coop','on','--coop-view','shared','--wide','on' if width==512 else 'off','--aspect','fit','--present-size','1152x540' if width==512 else '768x720','--no-flicker',flicker,'--slowdown-reduction','on','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--wav-out',str(d/'audio.wav'),'--present-out',str(d/'scene.png'),'--save-state',f'2430:{d}/seed.cycstate','--save-state',f'2550:{d}/limit.cycstate','--save-state',f'2799:{d}/end.cycstate']
   for seat,x in ((0,lag),(1,800)):
    for addr,val in {0x330:x&255,0x348:x>>8,0x378:148,0x390:0,0x318:0,0x360:0,0x3D8:0,0x3F0:0,0x3C:255,0xB0:0x9C}.items():args+=['--mm4-coop-poke',f'2440:{seat}:{addr:X}:{val:X}']
   if mode=='native':args+=['--present-every','50']
   if mode=='interpreter':args+=['--interp-only','--load-state',str(root/'native/seed.cycstate')]
   if mode in ('cold','instant'):args+=['--load-state',str(root/'native/limit.cycstate')]
   (d/'argv.json').write_text(json.dumps(args,indent=2))
   with(d/'run.log').open('w') as log:ret=subprocess.run(args,cwd=exe.parent,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),timeout=180)
   assert ret.returncode==0,(root.name,mode,(d/'run.log').read_text()[-1400:]);print(root.name,mode,flush=True)
  n=root/'native';i=root/'interpreter';c=root/'cold';rows=[json.loads(x) for x in(n/'frames.jsonl').read_text().splitlines()];by={x['frame']:x for x in rows};active=[x for x in rows if 2442<=x['frame']<=2799];tag=root.name+'-'
  checks[tag+'both-live-and-in-view']=all(x['coop']['dead']==[0,0] and all(x['wide']['left']+16<=v<x['wide']['left']+width-15 for v in positions(x)) for x in active)
  checks[tag+'separation-bound']=all(abs(positions(x)[0]-positions(x)[1])<=width-48 for x in active)
  checks[tag+'both-outward-controls-stop']=all(positions(by[f])==positions(by[2520]) for f in range(2520,2590)) and abs(positions(by[2520])[0]-positions(by[2520])[1])==width-48
  checks[tag+'camera-stops-for-trailing-player']=len({by[f]['wide']['left'] for f in range(2520,2590)})==1
  distance=20 if width==256 else 12
  checks[tag+'both-can-move-inward']=positions(by[2640])[0]>positions(by[2590])[0]+distance and positions(by[2640])[1]<positions(by[2590])[1]-distance
  checks[tag+'catchup-allows-progress']=positions(by[2760])[1]>positions(by[2700])[1]+distance and by[2760]['wide']['left']>by[2590]['wide']['left']+20
  checks[tag+'actual-profile-and-clock']=all(x['wide']['noflicker']==(flicker=='on') and x['wide']['width']==width and x['timing']['extra_lines']==262 and x['coop']['world']==x['coop']['updates'][0]==x['coop']['updates'][1] and not x['coop']['failed'] and not x['wide']['failed'] for x in active)
  checks[tag+'Native-interpreter-state-picture']=all((n/f).read_bytes()==(i/f).read_bytes() for f in ('end.cycstate','scene.png'))
  full=(n/'run.hash').read_text().splitlines()
  for suffix,d,cut in [('interpreter',i,2430),('cold',c,2550)]:
   checks[tag+suffix+'-hash-tail']=[x for x in full if int(x.split()[0])>cut]==(d/'run.hash').read_text().splitlines()
   with wave.open(str(n/'audio.wav')) as a,wave.open(str(d/'audio.wav')) as b:pcm=b.readframes(b.getnframes());checks[tag+suffix+'-PCM-tail']=bool(pcm) and a.readframes(a.getnframes()).endswith(pcm)
  checks[tag+'cold-state-picture']=all((n/f).read_bytes()==(c/f).read_bytes() for f in ('end.cycstate','scene.png'))
  checks[tag+'instant-first-picture']=(root/'instant/scene.png').read_bytes()==(n/'scene_02550.png').read_bytes()
  cases.append(dict(case=root.name,edge=positions(by[2550]),left=by[2550]['wide']['left'],inward=positions(by[2640]),after_catchup=positions(by[2760])))
result=dict(passed=all(checks.values()),checks=checks,cases=cases,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),scope='Fresh native stage entry; explicit body/HP placement and shared-enemy suppression as prerequisites. Actual two-seat outward/inward input, native and adaptive widths, NoF on/off; interpreter/cold/instant use current native pre-edge or edge snapshots. No victory flags or backward state compatibility claim.');(out/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True);raise SystemExit(not result['passed'])
