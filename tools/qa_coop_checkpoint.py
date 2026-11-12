"""Actual pit death, survivor checkpoint crossing and personal recovery.

The original initializer enters the last page of Bright room3; room offset2
and body positions are teleport prerequisites. Real slide/Right passes the
native low corridor into checkpoint1. No death/checkpoint/recovery result
flag is supplied. Enemies are suppressed after recovery for controls/menu QA.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,wave
import qa_route
from qa_coop_targets import memory,personal

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ['exe','rom','out']:p.add_argument('--'+k,required=True)
 v=p.parse_args();exe=Path(v.exe);out=Path(v.out);out.mkdir(parents=True,exist_ok=True)
 _,prefix=qa_route.expand(Path(__file__).resolve().parent.parent/'tests/routes/coop-boss.route')
 prefix=[x for x in prefix if int(x.split()[0])<2400 and '001F' not in x]
 checks={};cases=[];env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
 for leader in [0,1]:
  root=out/f'leader{leader}';root.mkdir(exist_ok=True);pad='2:' if leader else '';revived=leader^1;other='2:' if revived else ''
  lines=prefix+['2390 POKE 002E 02',f'2600 {pad}RIGHT+DOWN+A',f'2640 {pad}RIGHT',f'2930 {pad}-','3000 HOLD 0305-0317 00',f'3110 {other}RIGHT',f'3130 {other}-',f'3150 X{revived+1}:R',f'3152 X{revived+1}:-',f'3180 {other}START',f'3182 {other}-']
  route=root/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda x:int(x.split()[0])))+'\n')
  for name in ['native','interp','cold']:
   d=root/name;d.mkdir(exist_ok=True)
   argv=[str(exe),v.rom,'--no-save','--config',str(d/'config.ini'),'--frames','3260','--input',str(route),'--coop','on','--coop-view','split','--protoman','on','--weapon-cycle','on','--wide','on','--no-flicker','on','--slowdown-reduction','on','--mm4-qa-entry','0,10,3,0,0,0','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--present-out',str(d/'present.png'),'--present-every','30','--wav-out',str(d/'audio.wav')]
   for f,tag in [(2599,'dead'),(2819,'beam'),(3009,'returned'),(3259,'end')]:argv+=['--save-state',f'{f}:{d}/{tag}.cycstate']
   for who in [0,1]:
    live=who==leader
    for a,val in [(0x330,160 if live else 32),(0x378,116 if live else 250),(0x390,0),(0x30,0 if live else 1),(0x3D8,0),(0x3F0,0 if live else 0xFC),(0xB0,0x94 if not who else 0x91),(0xB1,0x8D if not who else 0x91),(0xA0,0 if live else 1),(0x3C,60)]:argv+=['--mm4-coop-poke',f'2390:{who}:{a:X}:{val:X}']
   if name=='interp':argv+=['--interp-only']
   if name=='cold':argv+=['--load-state',str(root/'native/beam.cycstate')]
   with (d/'run.log').open('w') as log:q=subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0,timeout=150)
   assert q.returncode==0,(leader,name,q.returncode)
   print(leader,name,flush=True)
  n=root/'native';i=root/'interp';c=root/'cold';end=n/'end.cycstate';beam=n/'beam.cycstate';dead=n/'dead.cycstate';returned=n/'returned.cycstate'
  rows=[json.loads(x) for x in (n/'frames.jsonl').read_text().splitlines()];by={x['frame']:x['coop'] for x in rows};last=rows[-1]['coop'];label=f'leader{leader}-'
  checks[label+'real-death-before-checkpoint']=memory(dead)[0x1F]==0 and by[2599]['dead'][revived]>0 and personal(dead,revived,0x30)==7 and personal(dead,revived,0xA0)==1
  checks[label+'native-checkpoint-and-entry-beam']=memory(beam)[0x1F]==1 and by[2819]['transport'][1+revived]==3 and personal(beam,revived,0x30)==10
  checks[label+'recovered-full-hp-buster']=personal(returned,revived,0xB0)==0x9C and personal(returned,revived,0xA0)==0 and by[3009]['dead']==[0,0] and by[3009]['transport'][:3]==[0,0,0]
  checks[label+'own-ammo-and-survivor-hp']=all(personal(end,s,0xB1)==(0x8D if not s else 0x91) for s in [0,1]) and personal(end,leader,0xB0)==(0x94 if not leader else 0x91)
  checks[label+'returned-native-controls']=by[3131]['players'][revived][0]-by[3109]['players'][revived][0]>=16
  checks[label+'real-shoulder-and-native-inventory']=personal(end,revived,0xA0)==1 and personal(end,leader,0xA0)==0 and last['menu']==1 and last['pauser']==revived and rows[-1]['stage']==16
  checks[label+'no-life-cost-or-failure']=all(x['coop']['lives']==2 and not x['coop']['failed'] and not x['wide']['failed'] for x in rows if x['coop']['ready'])
  checks[label+'one-world-clock']=last['updates'][leader]==last['world'] and last['updates'][revived]<=last['world']+1
  checks[label+'backend-exact']=all((n/f).read_bytes()==(i/f).read_bytes() for f in ['run.hash','end.cycstate','present.png','audio.wav'])
  full=qa_route.load_hashes(n/'run.hash');tail=qa_route.load_hashes(c/'run.hash')
  checks[label+'cold-hash-tail']=len(tail)==440 and tail=={f:h for f,h in full.items() if f>2819}
  checks[label+'cold-first-picture-and-state']=all((n/f).read_bytes()==(c/f).read_bytes() for f in ['present_02820.png','present.png','end.cycstate'])
  with wave.open(str(n/'audio.wav')) as a,wave.open(str(c/'audio.wav')) as b:pcm=a.readframes(a.getnframes());cold=b.readframes(b.getnframes())
  checks[label+'cold-pcm-tail']=bool(cold) and pcm.endswith(cold)
  cases.append({'leader':leader,'last':last})
 result={'passed':all(checks.values()),'checks':checks,'cases':cases,'exe':hashlib.sha256(exe.read_bytes()).hexdigest(),'scope':__doc__}
 (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
 return not result['passed']

if __name__=='__main__':raise SystemExit(main())
