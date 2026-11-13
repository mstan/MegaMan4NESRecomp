"""Flash Stopper/Rain Flush shared-world effects and personal costs.

Uses authored enemies; no freeze/expiry/result flags are supplied.
"""
from pathlib import Path
import argparse,json,os,subprocess,sys,shutil,wave,hashlib,struct
p=argparse.ArgumentParser()
for key in ['exe','rom','root']:p.add_argument('--'+key,required=True)
p.add_argument('--effect',choices=['bright','rain','mixed','withdraw'],default='bright')
p.add_argument('--qualify',action='store_true');a=p.parse_args()
weapon=4 if a.effect=='rain'else 12;cut=2600 if a.effect=='rain'else 2530
if a.effect=='rain':from PIL import Image
r=Path(__file__).parent;q=Path(a.root);q.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(r));import qa_route
from qa_coop_targets import personal,memory
checks={};observations={};exe_sha256=hashlib.sha256(Path(a.exe).read_bytes()).hexdigest();env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
if not (q/'mods/packages').is_dir():shutil.copytree(Path(a.exe).parent/'mods/packages',q/'mods/packages')
def record():
 (q/'checks.json').write_text(json.dumps(dict(checks=checks,observations=observations,exe_sha256=exe_sha256),indent=2)+'\n');print(json.dumps(checks),flush=True)
def frame_log(path):
 data=path.read_bytes();assert data[:8]==b'CYCFRAME'and struct.unpack_from('<I',data,8)[0]==2
 pos=12;out={}
 while pos<len(data):
  f,lo,hi,nram,nnt,nprg,nchr,npic,naudio=struct.unpack_from('<9I',data,pos);pos+=36
  palette=data[pos:pos+32];oam=data[pos+32:pos+288];pos+=288
  ram=data[pos:pos+nram];pos+=nram+nnt+nprg
  chrdata=data[pos:pos+nchr];pos+=nchr;pic=data[pos:pos+npic];pos+=npic+naudio
  out[f]=dict(ram=ram,palette=palette,oam=oam,chr=chrdata,pic=pic)
 return out
def run(name,end,lines,load=None,pokes=(),interp=False,saves=()):
 d=q/name;d.mkdir(exist_ok=True);route=d/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda s:int(s.split()[0])))+'\n')
 argv=[a.exe,a.rom,'--no-save','--config',str(d/'config.ini'),'--mods-root',str(q/'mods'),'--frames',str(end),'--input',str(route),'--coop','on','--weapon-cycle','on','--wide','on','--aspect','fit','--no-flicker','on','--slowdown-reduction','on','--present-size','1280x720','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'frames.hash'),'--present-out',str(d/'present.png'),'--wav-out',str(d/'audio.wav'),'--save-state',f'{end-1}:{d}/end.cycstate']
 if load:argv+=['--load-state',str(load)]
 if a.effect=='rain'and name!='boot':argv+=['--frame-log',str(d/'frames.bin'),'--frame-log-frames','2560:2690']
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


def qualify(name,n,end,lines,boot,pokes,saves,cut):
 i,_,_=run(name+'-interp',end,lines,boot,pokes,True,saves)
 c,_,_=run(name+'-cold',end,lines,n/f'at{cut}.cycstate')
 checks[name+'-backend']=all((n/f).read_bytes()==(i/f).read_bytes()for f in['frames.hash','end.cycstate','audio.wav','present.png'])
 full=qa_route.load_hashes(n/'frames.hash');tail=qa_route.load_hashes(c/'frames.hash');checks[name+'-cold-hash']=len(tail)==end-1-cut and tail=={f:h for f,h in full.items()if f>cut}
 checks[name+'-cold-firstPNG-state']=all((n/f).read_bytes()==(c/f).read_bytes()for f in['end.cycstate','present.png',f'present_{cut+1:05d}.png'])
 with wave.open(str(n/'audio.wav'))as x,wave.open(str(c/'audio.wav'))as y:pcm=x.readframes(x.getnframes());cold=y.readframes(y.getnframes())
 checks[name+'-cold-pcm']=bool(cold)and pcm.endswith(cold);record()

_,lines=qa_route.expand(r.parent/'tests/routes/coop-gallery.route');boot,_,_=run('boot',2501,[x for x in lines if int(x.split()[0])<=2200]);boot=boot/'end.cycstate'
if a.effect in('mixed','withdraw'):
 for caster in[0,1]:
  other=caster^1;name=f'{a.effect}-caster{caster}'
  setup={0x300:1,0xB0:0x9C,0x3C:127,0xA0:0,0x330:128,0x348:0,0x378:180,0x390:0,0x30:0,0x3D8:0,0x3F0:0,0x3E:180,0x3F:0,0x301:0,0x302:0,0x303:0,0x304:0}
  pokes=[(2501,s,z,v)for s in[0,1]for z,v in setup.items()]+[(2501,s,z,0)for s in[0,1]for z in range(0xB1,0xBE)]+[(2501,caster,0xBC,0x9C)]
  lines=[f'2503 X{caster+1}:R',f'2504 X{caster+1}:-',f'2512 {caster+1}:B',f'2513 {caster+1}:-']
  if a.effect=='mixed':
   pokes +=[(2501,other,0xB4,0x9C)];lines +=[f'2503 X{other+1}:R',f'2504 X{other+1}:-',f'2512 {other+1}:B',f'2513 {other+1}:-'];end=2661;cut=2600;saves=(2560,2600)
  else:
   pokes +=[(2501,other,0x330,64)];lines +=['2501 HOLD 0305-0317 00',f'2515 {caster+1}:SELECT',f'2630 {caster+1}:-',f'2840 {other+1}:START',f'2841 {other+1}:-'];end=2851;cut=2650;saves=(2560,2650,2830)
  d,v,b=run(name,end,lines,boot,pokes,saves=saves);m=memory(d/'end.cycstate')
  checks[name+'-native-owned-cost']=personal(d/'end.cycstate',caster,0xBC)&31==24 and not v['coop']['failed']
  if a.effect=='mixed':
   before=memory(d/'at2560.cycstate');during=memory(d/'at2600.cycstate')
   checks[name+'-actually-frozen-before-Rain']=before[0x300+23]==0x37 and before[0x450+23]==1 and before[0x5B8+23]in(0x81,0x82)
   checks[name+'-Rain-defeated-frozen-enemy']=during[0x13A]==44 and during[0x450+23]==0 and m[0x300+23]==0
   checks[name+'-independent-global-clocks']=during[0x13C]+256*during[0x13D]==212 and personal(d/'end.cycstate',other,0xB4)&31==24
  else:
   checks[name+'-real-90-update-withdrawal']=v['coop']['withdrawals'][caster]==1 and v['coop']['withdrawals'][other]==0
   checks[name+'-shared-freeze-expired']=not(m[0x13C]or m[0x13D])
   checks[name+'-survivor-native-inventory-unlocked']=v['coop']['menu']==1 and v['coop']['pauser']==other and v['coop']['lives']==2
  record()
  if a.qualify:qualify(name,d,end,lines,boot,pokes,saves,cut)
 sys.exit(not all(checks.values()))
for name,owners in [('primary',[0]),('guest',[1]),('both',[0,1]),('none',[])]:
 setup={0x300:1,0xB0:0x9C,0x3C:127,0xA0:0,0x330:128,0x348:0,0x378:180,0x390:0,0x30:0,0x3D8:0,0x3F0:0,0x3E:180,0x3F:0,0x301:0,0x302:0,0x303:0,0x304:0}
 pokes=[(2501,s,z,v)for s in[0,1]for z,v in setup.items()]+[(2501,s,z,0)for s in[0,1]for z in range(0xB1,0xBE)]
 pokes +=[(2501,s,0xB0+weapon,0x9C)for s in owners]
 if a.effect=='rain':pokes +=[(2501,s,0xB4,0x8D)for s in[0,1]if s not in owners]
 lines=['2501 1:-','2501 2:-']
 for owner in owners:lines +=[f'2503 X{owner+1}:R',f'2504 X{owner+1}:-',f'2512 {owner+1}:B',f'2513 {owner+1}:-']
 saves=(2511,2512,2530,2560,2600,2644,2800,2812,2820)
 d,v,b=run(name,2831,lines,boot,pokes,saves=saves)

 if not owners:continue
 if a.effect=='rain':
  frames=frame_log(d/'frames.bin');pulse=[(f,x['ram'][0x13A],x['ram'][0x13B],x['ram'][0x145])for f,x in frames.items()if x['ram'][0x13A]]
  checks[name+'-one-native-rain-clock']=pulse==[(2585+i,59-i,4*(i+1),1)for i in range(59)]and frames[2644]['ram'][0x13A]==0 and frames[2644]['ram'][0x13B]==240
  checks[name+'-personal-cost-and-other-fuel']=all(personal(d/'end.cycstate',s,0xB4)&31==(24 if s in owners else 13)for s in[0,1])and not v['coop']['failed']
  checks[name+'-actual-authored-enemy-defeat']=memory(d/'at2560.cycstate')[0x300+23]==0x37 and memory(d/'end.cycstate')[0x300+23]==0 and memory(d/'end.cycstate')[0x450+23]==0
 else:
  expected={2512:300,2530:282,2560:252,2800:12,2812:0,2820:0}
  checks[name+'-one-native-freeze-clock']=all(memory(d/f'at{f}.cycstate')[0x13C]+256*memory(d/f'at{f}.cycstate')[0x13D]==t for f,t in expected.items())
  checks[name+'-one-native-cost']=sum(personal(d/'end.cycstate',seat,0xBC)&31 for seat in owners)==28*len(owners)-4 and not v['coop']['failed']
  active=[memory(d/f'at{f}.cycstate')for f in[2530,2560,2800,2820]]
  checks[name+'-authored-enemy-frozen-resumed']=all(active[0][a]==active[1][a]==active[2][a]for a in range(0x300+22,0x5D0,24))and active[3][0x330+22]!=active[2][0x330+22]
  if name=='both':checks['simultaneous-second-request-native-Buster']=personal(d/'at2512.cycstate',0,0x301)==2
 if a.qualify:
  i,_,_=run(name+'-interp',2831,lines,boot,pokes,True,saves)
  c,_,_=run(name+'-cold',2831,lines,d/f'at{cut}.cycstate')
  checks[name+'-backend']=all((d/f).read_bytes()==(i/f).read_bytes()for f in['frames.hash','end.cycstate','audio.wav','present.png'])
  full=qa_route.load_hashes(d/'frames.hash');tail=qa_route.load_hashes(c/'frames.hash');checks[name+'-cold-hash']=len(tail)==2830-cut and tail=={f:h for f,h in full.items()if f>cut}
  checks[name+'-cold-firstPNG-state']=all((d/f).read_bytes()==(c/f).read_bytes()for f in['end.cycstate','present.png',f'present_{cut+1:05d}.png'])
  with wave.open(str(d/'audio.wav'))as x,wave.open(str(c/'audio.wav'))as y:pcm=x.readframes(x.getnframes());cold=y.readframes(y.getnframes())
  checks[name+'-cold-pcm']=bool(cold)and pcm.endswith(cold)
 record()
world=[base+slot for base in range(0x300,0x5B9,24)for slot in range(5,24)]
checks['guest-authored-world-exact-v-native-owner']=all(all(memory(q/'primary'/f'at{f}.cycstate')[a]==memory(q/'guest'/f'at{f}.cycstate')[a]for a in world)for f in[2512,2530,2560,2800,2812,2820])
checks['no-effect-authored-enemy-moves']=memory(q/'none/at2530.cycstate')[0x330+22]!=memory(q/'none/at2560.cycstate')[0x330+22]
if a.effect=='rain':
 ref=frame_log(q/'primary/frames.bin');other=frame_log(q/'guest/frames.bin')
 checks['guest-world-rain-render-phase-exact']=all(all(ref[f]['ram'][z]==other[f]['ram'][z]for z in world+[0x13A,0x13B,0x145])for f in ref)
 checks['no-effect-enemy-retained']=memory(q/'none/end.cycstate')[0x300+23]==0x37 and memory(q/'none/end.cycstate')[0x450+23]==1
 for name in['primary','guest','both']:
  count=wrong=0
  for f in[2590,2600,2644]:
   v=ref[f];assert not(v['ram'][0xF9]or v['ram'][0xFC]),'this fixture uses the original room left edge'
   im=Image.open(q/name/f'present_{f:05d}.png').convert('RGB');pixels=set()
   for i in range(0,120,4):
    top,tile,attr,x=v['oam'][i:i+4];assert tile==0x99 and attr==0
    for dy in range(8):
     y=top+1+dy
     if y>=240:continue
     lo=v['chr'][0x1000+tile*16+dy];hi=v['chr'][0x1008+tile*16+dy]
     for dx in range(8):
      px=x+dx;bit=7-dx;color=((lo>>bit)&1)|(((hi>>bit)&1)<<1)
      if color and 32<=px<256 and struct.unpack_from('<H',v['pic'],2*(y*256+px))[0]==0x20:pixels.add((px,y))
   count+=len(pixels);wrong+=sum(im.getpixel(p)!=(255,255,255)for p in pixels)
  checks[name+'-original-PPU-rain-pixels']=count>600 and not wrong
record();sys.exit(not all(checks.values()))
