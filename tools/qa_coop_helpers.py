"""Native Coil summon/contact in both ownership directions.

Uses the normal reset route, then explicit body/energy prerequisites. Real
native helper contact must spring either rider and spend only owner energy.
--qualify adds exact native/interpreter and current-build cold continuations.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,shutil,wave,hashlib
p=argparse.ArgumentParser()
for key in ['exe','rom','root']:p.add_argument('--'+key,required=True)
p.add_argument('--helper',choices=['coil'],default='coil')
p.add_argument('--qualify',action='store_true');a=p.parse_args()
r=Path(__file__).parent;q=Path(a.root);q.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(r));import qa_route
from qa_coop_targets import personal
checks={};observations={};exe_sha256=hashlib.sha256(Path(a.exe).read_bytes()).hexdigest();env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
if not (q/'mods/packages').is_dir():shutil.copytree(Path(a.exe).parent/'mods/packages',q/'mods/packages')
def record():
 (q/'checks.json').write_text(json.dumps(dict(checks=checks,observations=observations,exe_sha256=exe_sha256),indent=2)+'\n');print(json.dumps(checks),flush=True)
def run(name,end,lines,load=None,pokes=(),interp=False,saves=()):
 d=q/name;d.mkdir(exist_ok=True);route=d/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
 argv=[a.exe,a.rom,'--no-save','--config',str(d/'config.ini'),'--mods-root',str(q/'mods'),'--frames',str(end),'--input',str(route),'--coop','on','--weapon-cycle','on','--wide','on','--aspect','fit','--no-flicker','on','--slowdown-reduction','on','--present-size','1280x720','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'frames.hash'),'--present-out',str(d/'present.png'),'--wav-out',str(d/'audio.wav'),'--save-state',f'{end-1}:{d}/end.cycstate']
 if load:argv+=['--load-state',str(load)]
 if interp:argv+=['--interp-only']
 for f in saves:argv+=['--save-state',f'{f}:{d}/at{f}.cycstate']
 if saves or '-cold' in name:argv+=['--present-every','1']
 for frame,seat,addr,value in pokes:argv+=['--mm4-coop-poke',f'{frame}:{seat}:{addr:X}:{value:X}']
 with (d/'run.log').open('w') as log:rc=subprocess.run(argv,cwd=d,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW,timeout=120).returncode
 print(json.dumps({'name':name,'exit':rc}),flush=True)
 if rc:print((d/'run.log').read_text(errors='replace')[-2400:]);sys.exit(rc)
 rows=[json.loads(x) for x in (d/'frames.jsonl').read_text().splitlines()];v=rows[-1];state=d/'end.cycstate'
 bodies=[dict(x=personal(state,s,0x330)+256*personal(state,s,0x348),y=personal(state,s,0x378),state=personal(state,s,0x30),weapon=personal(state,s,0xA0),ammo=[personal(state,s,z)&31 for z in range(0xB1,0xBE)],helper=[personal(state,s,z+4) for z in range(0x300,0x5B9,24)]) for s in [0,1]]
 observations[name]=dict(row=v,players=bodies);record();return d,v,bodies
boot=q/'boot/end.cycstate'
if not boot.is_file():
 _,lines=qa_route.expand(r.parent/'tests/routes/coop-gallery.route');run('boot',2501,[x for x in lines if int(x.split()[0])<=2200])
weapon=1 if a.helper=='coil' else 2
for owner in [0,1]:
 guest=owner^1;tag=f'{a.helper}-owner{owner}'
 setup={0x300:1,0xB0:0x9C,0x3C:127,0xA0:0,0x330:128,0x348:0,0x378:148,0x390:0,0x30:0,0x3D8:0,0x3F0:0,0x3E:148,0x3F:0,0x301:0,0x302:0,0x303:0,0x304:0}
 pokes=[(2501,s,z,v) for s in [0,1] for z,v in setup.items()]
 pokes +=[(2501,owner,0xB1,0x9C if weapon==1 else 0),(2501,owner,0xB2,0x9C if weapon==2 else 0),(2501,owner,0xB3,0),(2501,guest,0x330,64)]
 d,v,b=run(tag+'-summon',2581,['2501 HOLD 0305-0317 00',f'2503 X{owner+1}:R',f'2509 X{owner+1}:-',f'2512 {owner+1}:B',f'2513 {owner+1}:-'],boot,pokes)
 h=b[owner]['helper'];x=h[2]+256*h[3];y=h[5];waiting=d/'end.cycstate'
 checks[tag+'-real-native-summon']=b[owner]['weapon']==weapon and h[0]!=0 and v['coop']['failed']==0;record()
 if not checks[tag+'-real-native-summon']:sys.exit(1)
 for rider,label in [(owner,'owner-contact'),(guest,'partner-contact')]:
  other=rider^1;placed={0x330:x&255,0x348:x>>8,0x378:y-18,0x390:0,0x30:1,0x3D8:0,0x3F0:255,0x3E:y-18,0x3F:0,0x3C:127}
  pokes=[(2581,rider,z,val) for z,val in placed.items()]+[(2581,other,0x330,64),(2581,other,0x348,0),(2581,other,0x378,148),(2581,other,0x30,0),(2581,other,0x3C,127)]
  lines=['2581 HOLD 0305-0317 00','2581 1:-','2581 2:-']
  cut=2595
  d,v,body=run(tag+'-'+label,2621,lines,waiting,pokes,saves=(cut,) if a.qualify and rider!=owner else ())
  checks[tag+'-'+label+'-native-rise-and-owned-fuel']=body[rider]['y']<y-35 and body[owner]['ammo'][weapon-1]<28 and v['coop']['failed']==0;record()
  if a.helper=='coil':
   checks[tag+'-'+label+'-exact-native-cost']=body[owner]['ammo'][0]==26 and body[owner^1]['ammo'][0]==28
   checks[tag+'-'+label+'-personal-equipment']=all(body[s]['weapon']==(1 if s==owner else 0) and body[s]['state']<8 for s in [0,1]);record()
  if a.qualify and rider!=owner:
   i,_,_=run(tag+'-partner-interp',2621,lines,waiting,pokes,True,(cut,))
   c,_,_=run(tag+'-partner-cold',2621,lines,d/f'at{cut}.cycstate')
   checks[tag+'-backend-exact']=all((d/f).read_bytes()==(i/f).read_bytes() for f in ['frames.hash','end.cycstate','audio.wav','present.png'])
   tail=qa_route.load_hashes(c/'frames.hash');full=qa_route.load_hashes(d/'frames.hash')
   checks[tag+'-cold-hash']=len(tail)==2621-cut-1 and tail=={f:h for f,h in full.items() if f>cut}
   checks[tag+'-cold-state-pixels']=all((d/f).read_bytes()==(c/f).read_bytes() for f in ['end.cycstate','present.png',f'present_{cut+1:05d}.png'])
   with wave.open(str(d/'audio.wav')) as warm,wave.open(str(c/'audio.wav')) as cold:pcm=warm.readframes(warm.getnframes());tailpcm=cold.readframes(cold.getnframes())
   checks[tag+'-cold-pcm']=bool(tailpcm) and pcm.endswith(tailpcm);record()
sys.exit(0 if all(checks.values()) else 1)
