"""Real Pharaoh charge/link/release ownership and adaptive shot retirement.

Uses explicit body/ammo prerequisites, native controls, and current-build
Native/interpreter/cold continuations. No result or progression flags are set.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,shutil,wave,hashlib
p=argparse.ArgumentParser()
for key in ['exe','rom','root']:p.add_argument('--'+key,required=True)
p.add_argument('--qualify',action='store_true');p.add_argument('--no-flicker',choices=['on','off'],default='on');a=p.parse_args()
r=Path(__file__).parent;q=Path(a.root);q.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(r));import qa_route
from qa_coop_targets import personal
checks={};observations={};exe_sha256=hashlib.sha256(Path(a.exe).read_bytes()).hexdigest();env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
if not (q/'mods/packages').is_dir():shutil.copytree(Path(a.exe).parent/'mods/packages',q/'mods/packages')
def record():
 (q/'checks.json').write_text(json.dumps(dict(checks=checks,observations=observations,exe_sha256=exe_sha256),indent=2)+'\n');print(json.dumps({'passed':sum(bool(v)for v in checks.values()),'total':len(checks),'failed':[k for k,v in checks.items()if not v]}),flush=True)
def run(name,end,lines,load=None,pokes=(),interp=False,saves=()):
 d=q/name;d.mkdir(exist_ok=True);route=d/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
 argv=[a.exe,a.rom,'--no-save','--config',str(d/'config.ini'),'--mods-root',str(q/'mods'),'--frames',str(end),'--input',str(route),'--coop','on','--weapon-cycle','on','--wide','on','--aspect','fit','--no-flicker',a.no_flicker,'--slowdown-reduction','on','--present-size','1280x720','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'frames.hash'),'--present-out',str(d/'present.png'),'--wav-out',str(d/'audio.wav'),'--save-state',f'{end-1}:{d}/end.cycstate']
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

for case in ['owner0','owner1','both']:
 owners=[0,1] if case=='both' else [int(case[-1])]
 owner=owners[0];other=owner^1;tag='pharaoh-'+case
 setup={0x300:1,0xB0:0x9C,0x3C:127,0xA0:0,0x330:128,0x348:0,0x378:180,0x390:0,0x30:0,0x3D8:0,0x3F0:0,0x3E:180,0x3F:0,0x301:0,0x302:0,0x303:0,0x304:0}
 pokes=[(2501,s,z,v)for s in[0,1]for z,v in setup.items()]+[(2501,s,z,0)for s in[0,1]for z in range(0xB1,0xBE)]+[(2501,owner,0xBB,0x9C),(2501,other,0xBB,0x9C if case=='both' else 0x8D),(2501,other,0x330,64)]
 lines=['2501 HOLD 0305-0317 00']
 for seat in owners:lines +=[f'2503 X{seat+1}:R',f'2504 X{seat+1}:-',f'2512 {seat+1}:B',f'2600 {seat+1}:-']
 saves=(2512,2560,2599,2600,2610)
 n,v,b=run(tag,2701,lines,boot,pokes,saves=saves)
 for seat in owners:
  at=lambda f,z:personal(n/f'at{f}.cycstate',seat,z)
  checks[tag+f'-seat{seat}-native-charge-link']=at(2599,0x4C8)==2 and at(2599,0x302)==13
  checks[tag+f'-seat{seat}-actual-release']=at(2600,0x4C8)==0 and at(2600,0x302)==12
  checks[tag+f'-seat{seat}-personal-cost']=at(2512,0xBB)&31==27 and at(2599,0xBB)&31==27 and at(2600,0xBB)&31==25
  checks[tag+f'-seat{seat}-eventual-native-retirement']=b[seat]['ammo'][10]==25 and personal(n/'end.cycstate',seat,0x301)==0 and personal(n/'end.cycstate',seat,0x302)==0 and personal(n/'end.cycstate',seat,0x303)==0
  if case!='both':checks[tag+'-shot-in-adaptive-margin']=at(2560,0x303)==12 and at(2560,0x333)+256*at(2560,0x34B)>256
 checks[tag+'-personal-health-and-world']=all(personal(n/'end.cycstate',s,0xB0)&31==28 for s in[0,1])and not v['coop']['failed']
 if case!='both':checks[tag+'-other-ammo-intact']=personal(n/'end.cycstate',other,0xBB)&31==13
 if a.qualify:
  i,_,_=run(tag+'-interp',2701,lines,boot,pokes,True,saves)
  c,_,_=run(tag+'-cold',2701,lines,n/'at2599.cycstate')
  checks[tag+'-backend']=all((n/z).read_bytes()==(i/z).read_bytes()for z in['frames.hash','end.cycstate','present.png','audio.wav'])
  full=qa_route.load_hashes(n/'frames.hash');tail=qa_route.load_hashes(c/'frames.hash');checks[tag+'-cold-hash']=tail=={f:h for f,h in full.items()if f>2599}and len(tail)==101
  checks[tag+'-cold-first-picture-state']=all((n/z).read_bytes()==(c/z).read_bytes()for z in['end.cycstate','present.png','present_02600.png'])
  with wave.open(str(n/'audio.wav'))as x,wave.open(str(c/'audio.wav'))as y:pcm=x.readframes(x.getnframes());cold=y.readframes(y.getnframes())
  checks[tag+'-cold-PCM']=bool(cold)and pcm.endswith(cold)
 record()
fields=[base+slot for base in range(0x300,0x5B9,24)for slot in[1,2,3]]+[0x4C8]
for f in[2512,2560,2599,2600,2610]:
 checks[f'both-ownership-paths-native-fields-{f}']=all(personal(q/'pharaoh-owner0'/f'at{f}.cycstate',0,z)==personal(q/'pharaoh-owner1'/f'at{f}.cycstate',1,z)for z in fields)
record();assert all(checks.values()),checks
