"""Usable personal native inventories and a contested last shared E-tank.

Reuse the pit/checkpoint setup, then open the menu with either living seat.
Both request the last E-tank on one tick; the native refill heals seat0 only.
The non-pauser's Start cannot resume. Both choose different equipment on the
pauser's closing tick. Check current mid-refill cold continuation and backends.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,wave
import qa_route
from qa_coop_targets import memory,personal


def main():
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('exe','rom','out'):p.add_argument('--'+key,required=True)
 v=p.parse_args();exe=Path(v.exe);out=Path(v.out);out.mkdir(parents=True,exist_ok=True)
 _,prefix=qa_route.expand(Path(__file__).resolve().parent.parent/'tests/routes/coop-boss.route')
 prefix=[x for x in prefix if int(x.split()[0])<2400 and '001F' not in x]
 prefix+=['2390 POKE 002E 02','2600 RIGHT+DOWN+A','2640 RIGHT','2930 -','3000 HOLD 0305-0317 00','3110 2:RIGHT','3130 2:-','3150 X2:R','3152 X2:-']
 env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
 checks={};cases=[]
 for owner in (0,1):
  root=out/f'owner{owner}';root.mkdir(exist_ok=True);pad='2:' if owner else '';other='' if owner else '2:'
  lines=prefix+['3160 POKE 00A2 01',f'3180 {pad}START',f'3182 {pad}-','3300 DOWN','3300 2:DOWN','3301 -','3301 2:-','3340 A','3340 2:A','3341 -','3341 2:-',f'3650 {other}START',f'3651 {other}-','3680 RIGHT','3680 2:UP','3681 -','3681 2:-','3720 A','3720 2:A','3721 -','3721 2:-']
  route=root/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda x:int(x.split()[0])))+'\n')
  for name in ('native','interp','cold'):
   d=root/name;d.mkdir(exist_ok=True)
   a=[str(exe),v.rom,'--no-save','--config',str(d/'config.ini'),'--frames','3850','--input',str(route),'--coop','on','--coop-view','split','--protoman','on','--weapon-cycle','on','--wide','on','--no-flicker','on','--slowdown-reduction','on','--mm4-qa-entry','0,10,3,0,0,0','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--present-out',str(d/'present.png'),'--present-every','20','--wav-out',str(d/'audio.wav')]
   for seat in (0,1):
    for addr,val in ((0x330,160 if not seat else 32),(0x378,116 if not seat else 250),(0x390,0),(0x30,0 if not seat else 1),(0x3D8,0),(0x3F0,0 if not seat else 0xFC),(0xB0,0x94 if not seat else 0x91),(0xB1,0x8D if not seat else 0x91),(0xA0,0 if not seat else 1),(0x3C,60)):a+=['--mm4-coop-poke',f'2390:{seat}:{addr:X}:{val:X}']
    a+=['--mm4-coop-poke',f'3160:{seat}:B0:{0x85 if not seat else 0x87:X}']
   for f,tag in ((3339,'before'),(3419,'pending'),(3640,'healed'),(3719,'choices'),(3849,'end')):a+=['--save-state',f'{f}:{d}/{tag}.cycstate']
   if name=='interp':a+=['--interp-only']
   if name=='cold':a+=['--load-state',str(root/'native/pending.cycstate')]
   (d/'argv.json').write_text(json.dumps(a,indent=2)+'\n')
   with(d/'run.log').open('w')as log:q=subprocess.run(a,cwd=exe.parent,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
   print(owner,name,q.returncode,flush=True);assert q.returncode==0
  n=root/'native';i=root/'interp';c=root/'cold';tag=f'owner{owner}-'
  rows=[json.loads(x)for x in(n/'frames.jsonl').read_text().splitlines()];by={x['frame']:x['coop']for x in rows}
  checks[tag+'own-cursors-and-low-hp']=by[3339]['pauser']==owner and by[3339]['inventory'][3:5]==[7,7] and by[3339]['inventory'][7:]==[5,7,1]
  checks[tag+'one-native-fill-wins-seat0']=5<(personal(n/'pending.cycstate',0,0xB0)&31)<28 and personal(n/'pending.cycstate',1,0xB0)&31==7 and memory(n/'pending.cycstate')[0xA2]==0
  checks[tag+'heal-completes-once']=by[3640]['inventory'][7:]==[28,7,0]
  checks[tag+'world-frozen-and-no-failures']=len({by[f]['world']for f in range(3260,3661)})==1 and all(not x['coop']['failed'] and not x['wide']['failed']for x in rows)
  checks[tag+'only-owner-resumes']=by[3660]['menu']==1 and by[3849]['menu']==0 and by[3849]['world']>by[3660]['world']
  checks[tag+'same-tick-equipment-both-kept']=by[3719]['inventory'][3:5]==[10,0] and all(personal(n/'end.cycstate',s,0xA0)==w for s,w in((0,1),(1,0)))
  checks[tag+'personal-ammo-retained']=all(personal(n/'end.cycstate',s,0xB1)==v for s,v in((0,0x8D),(1,0x91)))
  checks[tag+'backend-exact']=all((n/f).read_bytes()==(i/f).read_bytes()for f in('run.hash','end.cycstate','present.png','audio.wav'))
  full=qa_route.load_hashes(n/'run.hash');tail=qa_route.load_hashes(c/'run.hash')
  checks[tag+'cold-hash-tail']=len(tail)==430 and tail=={f:h for f,h in full.items()if f>3419}
  checks[tag+'cold-first-picture-and-state']=all((n/f).read_bytes()==(c/f).read_bytes()for f in('present_03420.png','present.png','end.cycstate'))
  with wave.open(str(n/'audio.wav'))as a,wave.open(str(c/'audio.wav'))as b:pcm=a.readframes(a.getnframes());cold=b.readframes(b.getnframes())
  checks[tag+'cold-pcm-tail']=bool(cold) and pcm.endswith(cold)
  cases.append({'owner':owner,'last':rows[-1]})
 result={'passed':all(checks.values()),'checks':checks,'cases':cases,'exe':hashlib.sha256(exe.read_bytes()).hexdigest(),'scope':__doc__}
 (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
 return not result['passed']


if __name__=='__main__':raise SystemExit(main())
