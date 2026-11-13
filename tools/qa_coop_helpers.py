"""Native Coil/Jet summon/contact in both ownership directions.

Uses the normal reset route, then explicit body/energy prerequisites. Real
native helper contact must spring/carry either rider, spending owner energy.
--qualify adds exact native/interpreter and current-build cold continuations.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,shutil,wave,hashlib
p=argparse.ArgumentParser()
for key in ['exe','rom','root']:p.add_argument('--'+key,required=True)
p.add_argument('--helper',choices=['coil','jet','marine'],default='coil')
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
 if a.helper=='marine':
  argv+=['--screenshot',str(d/'native.png')]
  if not load:argv+=['--mm4-qa-entry','6,0,0,0,0,0']
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
if a.helper=='marine':
 for owner in [0,1]:
  other=owner^1;tag=f'marine-owner{owner}'
  setup={0x300:1,0xB0:0x9C,0x3C:127,0xA0:0,0x330:200,0x348:0,0x378:160,0x390:0,0x30:0,0x3D8:0,0x3F0:0,0x3E:160,0x3F:0,0x301:0,0x302:0,0x303:0,0x304:0}
  pokes=[(2501,s,z,v)for s in [0,1]for z,v in setup.items()]
  pokes +=[(2501,owner,0xB1,0),(2501,owner,0xB2,0),(2501,owner,0xB3,0x9C),(2501,other,0xB3,0x8D),(2501,other,0x330,64),(2501,other,0x378,80)]
  lines=['2501 HOLD 0305-0317 00',f'2503 X{owner+1}:R',f'2509 X{owner+1}:-',f'2512 {owner+1}:B',f'2513 {owner+1}:-']
  d,v,b=run(tag+'-summon',2581,lines,boot,pokes)
  h=b[owner]['helper'];x=h[2]+256*h[3];y=h[5]
  checks[tag+'-native-summon']=b[owner]['weapon']==3 and (h[0]==4 or b[owner]['state']==4) and not v['coop']['failed'];record()
  if not checks[tag+'-native-summon']:sys.exit(1)
  pokes=[] if b[owner]['state']==4 else [(2581,owner,z,val)for z,val in [(0x330,x&255),(0x348,x>>8),(0x378,y),(0x390,0),(0x30,0),(0x3E,y),(0x3F,0)]]
  pokes +=[(2581,s,0x3C,0)for s in [0,1]]
  board,v,b=run(tag+'-board',2661,['2581 HOLD 0305-0317 00','2581 1:-','2581 2:-'],d/'end.cycstate',pokes,saves=(2659,))
  checks[tag+'-actual-native-boarding']=b[owner]['state']==4 and b[owner]['ammo'][2]>0 and not v['coop']['failed'];record()
  # Keep moving inside the water, avoiding the shared-screen edge clamp.
  lines=['2661 HOLD 0305-0317 00']+[f'{f} {owner+1}:'+('LEFT'if ((f-2661)//12)%2==0 else 'RIGHT')for f in range(2661,2860,12)]+[f'2860 {owner+1}:-']
  pokes=[(2661,owner,0xB3,0x81),(2661,other,0xB3,0x8D)];cut=2676
  n,v,b=run(tag+'-deplete',2861,lines,board/'end.cycstate',pokes,saves=(cut,)if a.qualify else ())
  checks[tag+'-native-depletion-and-dismount']=b[owner]['ammo'][2]==0 and b[owner]['state']<4 and not v['coop']['failed']
  checks[tag+'-other-fuel-retained']=b[other]['ammo'][2]==13
  checks[tag+'-alive-no-life-cost']=v['coop']['dead']==[0,0] and v['coop']['lives']==2 and all(personal(n/'end.cycstate',s,0xB0)&31==28 for s in [0,1]);record()
  if a.qualify:
   i,_,_=run(tag+'-deplete-interp',2861,lines,board/'end.cycstate',pokes,True,(cut,))
   c,_,_=run(tag+'-deplete-cold',2861,lines,n/f'at{cut}.cycstate')
   checks[tag+'-backend-exact']=all((n/f).read_bytes()==(i/f).read_bytes()for f in ['frames.hash','end.cycstate','audio.wav','present.png'])
   full=qa_route.load_hashes(n/'frames.hash');tail=qa_route.load_hashes(c/'frames.hash')
   checks[tag+'-cold-hash']=len(tail)==2860-cut and tail=={f:h for f,h in full.items()if f>cut}
   checks[tag+'-cold-first-picture-state']=all((n/f).read_bytes()==(c/f).read_bytes()for f in ['end.cycstate','present.png',f'present_{cut+1:05d}.png'])
   with wave.open(str(n/'audio.wav'))as x,wave.open(str(c/'audio.wav'))as y:pcm=x.readframes(x.getnframes());cold=y.readframes(y.getnframes())
   checks[tag+'-cold-pcm']=bool(cold)and pcm.endswith(cold);record()
 sys.exit(not all(checks.values()))
weapon=1 if a.helper=='coil' else 2
end=2621 if weapon==1 else 2661
cut=end-26
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
  other=rider^1;placed={0x330:x&255,0x348:x>>8,0x378:y-18 if weapon==1 else y,0x390:0,0x30:1 if weapon==1 else 0,0x3D8:0,0x3F0:255 if weapon==1 else 0,0x3E:y-18 if weapon==1 else y,0x3F:0,0x3C:127}
  pokes=[(2581,rider,z,val) for z,val in placed.items()]+[(2581,other,0x330,64),(2581,other,0x348,0),(2581,other,0x378,148),(2581,other,0x30,0),(2581,other,0x3C,127)]
  lines=['2581 HOLD 0305-0317 00','2581 1:-','2581 2:-']
  d,v,body=run(tag+'-'+label,end,lines,waiting,pokes,saves=(cut,) if a.qualify and rider!=owner else ())
  if weapon==1:
   checks[tag+'-'+label+'-native-rise-and-owned-fuel']=body[rider]['y']<y-35 and body[owner]['ammo'][0]<28 and v['coop']['failed']==0
  else:
   h=body[owner]['helper']
   checks[tag+'-'+label+'-native-flight']=h[0]==4 and h[27:29]==[0x86,0x8A] and h[2]+256*h[3]>x+50 and v['coop']['failed']==0
   checks[tag+'-'+label+'-native-passenger']=body[rider]['x']>x+50
   checks[tag+'-'+label+'-exact-native-cost']=body[owner]['ammo'][1]==27 and body[owner^1]['ammo'][1]==0
   checks[tag+'-'+label+'-personal-equipment']=all(body[s]['weapon']==(2 if s==owner else 0) and body[s]['state']<8 for s in [0,1])
  record()
  if a.helper=='coil':
   checks[tag+'-'+label+'-exact-native-cost']=body[owner]['ammo'][0]==26 and body[owner^1]['ammo'][0]==28
   checks[tag+'-'+label+'-personal-equipment']=all(body[s]['weapon']==(1 if s==owner else 0) and body[s]['state']<8 for s in [0,1]);record()
  if a.qualify and rider!=owner:
   i,_,_=run(tag+'-partner-interp',end,lines,waiting,pokes,True,(cut,))
   c,_,_=run(tag+'-partner-cold',end,lines,d/f'at{cut}.cycstate')
   checks[tag+'-backend-exact']=all((d/f).read_bytes()==(i/f).read_bytes() for f in ['frames.hash','end.cycstate','audio.wav','present.png'])
   tail=qa_route.load_hashes(c/'frames.hash');full=qa_route.load_hashes(d/'frames.hash')
   checks[tag+'-cold-hash']=len(tail)==end-cut-1 and tail=={f:h for f,h in full.items() if f>cut}
   checks[tag+'-cold-state-pixels']=all((d/f).read_bytes()==(c/f).read_bytes() for f in ['end.cycstate','present.png',f'present_{cut+1:05d}.png'])
   with wave.open(str(d/'audio.wav')) as warm,wave.open(str(c/'audio.wav')) as cold:pcm=warm.readframes(warm.getnframes());tailpcm=cold.readframes(cold.getnframes())
   checks[tag+'-cold-pcm']=bool(tailpcm) and pcm.endswith(tailpcm);record()
 if weapon==2:
  # Fresh boarding can happen on different controller updates. Compare the
  # owner clock from one genuine already-active Jet, with the same position
  # and one-tick fuel prerequisite in both passenger cases.
  active=q/(tag+'-owner-contact')/'end.cycstate'
  for rider,label in [(owner,'clock-own'),(guest,'clock-partner')]:
   placed={0x330:152,0x348:0,0x378:180,0x390:0,0x30:0,0x3D8:0,0x3F0:0,0x3E:180,0x3F:0,0x3C:127}
   pokes=[(end,rider,z,val) for z,val in placed.items()]+[(end,rider^1,0x330,64),(end,rider^1,0x348,0),(end,rider^1,0x378,180),(end,rider^1,0x30,0),(end,rider^1,0x3C,127)]
   pokes +=[(end,owner,z,val) for z,val in [(0x31C,0),(0x334,152),(0x34C,0),(0x4B4,1)]]
   _,v,b=run(tag+'-'+label,end+30,[f'{end} HOLD 0305-0317 00',f'{end} 1:-',f'{end} 2:-'],active,pokes)
   checks[tag+'-'+label+'-ride-and-native-cost']=b[rider]['x']>=188 and b[owner]['ammo'][1]==26 and b[owner^1]['ammo'][1]==0 and not v['coop']['failed'];record()
  checks[tag+'-one-native-helper-clock']=observations[tag+'-clock-own']['players'][owner]['helper']==observations[tag+'-clock-partner']['players'][owner]['helper'];record()
if weapon==2:
 # Explicit overlap prerequisite: copy one genuinely active native Jet into
 # the second personal pool. Summoning/boarding each seat is checked above.
 active=q/'jet-owner0-clock-own/end.cycstate';start=end+30;stop=start+10;cut=start+4
 pokes=[(start,1,z+4,personal(active,0,z+4)) for z in range(0x300,0x5B9,24)]
 for seat in [0,1]:
  pokes +=[(start,seat,z,val) for z,val in [(0x330,190),(0x348,0),(0x378,180),(0x390,0),(0x30,0),(0x3D8,0),(0x3F0,0),(0x3C,127),(0x334,190),(0x34C,0),(0x31C,0)]]
 pokes +=[(start,1,0xA0,2),(start,1,0xB2,0x9A)]
 lines=[f'{start} HOLD 0305-0317 00',f'{start} 1:-',f'{start} 2:-']
 n,v,b=run('two-active-jets',stop,lines,active,pokes,saves=(cut,) if a.qualify else ())
 checks['overlapping-own-jets-carry-once']=all(b[s]['x']==b[s]['helper'][2]+256*b[s]['helper'][3] for s in [0,1])
 checks['overlapping-own-jets-one-clock']=b[0]['helper']==b[1]['helper'] and all(b[s]['ammo'][1]==26 for s in [0,1]) and not v['coop']['failed'];record()
 if a.qualify:
  i,_,_=run('two-active-jets-interp',stop,lines,active,pokes,True,(cut,))
  c,_,_=run('two-active-jets-cold',stop,lines,n/f'at{cut}.cycstate')
  checks['overlap-backend-exact']=all((n/f).read_bytes()==(i/f).read_bytes() for f in ['frames.hash','end.cycstate','audio.wav','present.png'])
  full=qa_route.load_hashes(n/'frames.hash');tail=qa_route.load_hashes(c/'frames.hash')
  checks['overlap-cold-hash']=len(tail)==stop-cut-1 and tail=={f:h for f,h in full.items() if f>cut}
  checks['overlap-cold-state-pixels']=all((n/f).read_bytes()==(c/f).read_bytes() for f in ['end.cycstate','present.png',f'present_{cut+1:05d}.png'])
  with wave.open(str(n/'audio.wav')) as warm,wave.open(str(c/'audio.wav')) as cold:pcm=warm.readframes(warm.getnframes());tailpcm=cold.readframes(cold.getnframes())
  checks['overlap-cold-pcm']=bool(tailpcm) and pcm.endswith(tailpcm);record()
sys.exit(0 if all(checks.values()) else 1)
