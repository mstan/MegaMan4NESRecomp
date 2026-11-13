"""Flash Stopper native shared-world freeze, personal cost and one timer.

Uses authored enemies; no freeze/expiry/result flags are supplied.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,shutil,wave,hashlib
p=argparse.ArgumentParser()
for key in ['exe','rom','root']:p.add_argument('--'+key,required=True)
p.add_argument('--qualify',action='store_true');a=p.parse_args()
r=Path(__file__).parent;q=Path(a.root);q.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(r));import qa_route
from qa_coop_targets import personal,memory
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


_,lines=qa_route.expand(r.parent/'tests/routes/coop-gallery.route');boot,_,_=run('boot',2501,[x for x in lines if int(x.split()[0])<=2200]);boot=boot/'end.cycstate'
for name,owners in [('primary',[0]),('guest',[1]),('both',[0,1]),('none',[])]:
 setup={0x300:1,0xB0:0x9C,0x3C:127,0xA0:0,0x330:128,0x348:0,0x378:180,0x390:0,0x30:0,0x3D8:0,0x3F0:0,0x3E:180,0x3F:0,0x301:0,0x302:0,0x303:0,0x304:0}
 pokes=[(2501,s,z,v)for s in[0,1]for z,v in setup.items()]+[(2501,s,z,0)for s in[0,1]for z in range(0xB1,0xBE)]
 pokes +=[(2501,s,0xBC,0x9C)for s in owners]
 lines=['2501 1:-','2501 2:-']
 for owner in owners:lines +=[f'2503 X{owner+1}:R',f'2504 X{owner+1}:-',f'2512 {owner+1}:B',f'2513 {owner+1}:-']
 d,v,b=run(name,2831,lines,boot,pokes,saves=(2511,2512,2530,2560,2800,2812,2820))
 from qa_coop_targets import memory

 if not owners:continue
 expected={2512:300,2530:282,2560:252,2800:12,2812:0,2820:0}
 checks[name+'-one-native-freeze-clock']=all(memory(d/f'at{f}.cycstate')[0x13C]+256*memory(d/f'at{f}.cycstate')[0x13D]==t for f,t in expected.items())
 checks[name+'-one-native-cost']=sum(personal(d/'end.cycstate',seat,0xBC)&31 for seat in owners)==28*len(owners)-4 and not v['coop']['failed']
 active=[memory(d/f'at{f}.cycstate')for f in[2530,2560,2800,2820]]
 checks[name+'-authored-enemy-frozen-resumed']=all(active[0][a]==active[1][a]==active[2][a]for a in range(0x300+22,0x5D0,24))and active[3][0x330+22]!=active[2][0x330+22]
 if name=='both':checks['simultaneous-second-request-native-Buster']=personal(d/'at2512.cycstate',0,0x301)==2
 if a.qualify:
  i,_,_=run(name+'-interp',2831,lines,boot,pokes,True,(2511,2512,2530,2560,2800,2812,2820))
  c,_,_=run(name+'-cold',2831,lines,d/'at2530.cycstate')
  checks[name+'-backend']=all((d/f).read_bytes()==(i/f).read_bytes()for f in['frames.hash','end.cycstate','audio.wav','present.png'])
  full=qa_route.load_hashes(d/'frames.hash');tail=qa_route.load_hashes(c/'frames.hash');checks[name+'-cold-hash']=len(tail)==300 and tail=={f:h for f,h in full.items()if f>2530}
  checks[name+'-cold-firstPNG-state']=all((d/f).read_bytes()==(c/f).read_bytes()for f in['end.cycstate','present.png','present_02531.png'])
  with wave.open(str(d/'audio.wav'))as x,wave.open(str(c/'audio.wav'))as y:pcm=x.readframes(x.getnframes());cold=y.readframes(y.getnframes())
  checks[name+'-cold-pcm']=bool(cold)and pcm.endswith(cold)
 record()
world=[base+slot for base in range(0x300,0x5B9,24)for slot in range(5,24)]
checks['guest-authored-world-exact-v-native-owner']=all(all(memory(q/'primary'/f'at{f}.cycstate')[a]==memory(q/'guest'/f'at{f}.cycstate')[a]for a in world)for f in[2512,2530,2560,2800,2812,2820])
checks['no-effect-authored-enemy-moves']=memory(q/'none/at2530.cycstate')[0x330+22]!=memory(q/'none/at2560.cycstate')[0x330+22]
record();sys.exit(not all(checks.values()))
