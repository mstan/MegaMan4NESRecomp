"""Either living player admits the original Bright checkpoint boss shutter.

Use the existing retail boot/checkpoint route, then real Right input only.
HP, ammo and brief invulnerability are prerequisites; no room, door, scene,
boss or clear flag is supplied. This is a focused co-op exit regression.
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
    prefix=[x for x in prefix if int(x.split()[0])<2400]
    checks={};cases=[]
    env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    for seat in [0,1]:
        root=out/f'seat{seat}';root.mkdir(exist_ok=True);pad='2:' if seat else ''
        route=root/'input.txt';route.write_text('\n'.join(sorted(prefix+[f'2400 {pad}RIGHT',f'2700 {pad}-'],key=lambda x:int(x.split()[0])))+'\n')
        for name in ['native','interp','cold']:
            d=root/name;d.mkdir(exist_ok=True)
            argv=[str(exe),v.rom,'--no-save','--config',str(d/'config.ini'),'--frames','2750','--input',str(route),'--coop','on','--coop-view','split','--protoman','on','--weapon-cycle','on','--wide','on','--no-flicker','on','--slowdown-reduction','on','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--present-out',str(d/'present.png'),'--present-every','30','--wav-out',str(d/'audio.wav')]
            for f,tag in [(2399,'entry'),(2549,'scroll'),(2749,'end')]:argv+=['--save-state',f'{f}:{d}/{tag}.cycstate']
            for who in [0,1]:
                for a,val in [(0xB0,0x94 if not who else 0x91),(0xB1,0x8D if not who else 0x91),(0x3C,60)]:argv+=['--mm4-coop-poke',f'2390:{who}:{a:X}:{val:X}']
            if name=='interp':argv+=['--interp-only']
            if name=='cold':argv+=['--load-state',str(root/'native/scroll.cycstate')]
            with (d/'run.log').open('w') as log:
                q=subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0,timeout=120)
            assert q.returncode==0,(name,q.returncode)
            print(seat,name,flush=True)
        n=root/'native';i=root/'interp';c=root/'cold';end=n/'end.cycstate'
        rows=[json.loads(x) for x in (n/'frames.jsonl').read_text().splitlines()];last=rows[-1]['coop'];m=memory(end)
        label=f'seat{seat}-';raw=(n/'scroll.cycstate').read_bytes();at=raw.find(b'M4C9')
        checks[label+'actual-pending-native-scroll']=at>=0 and raw[at+13]==1 and memory(n/'scroll.cycstate')[0x2B]==0x38
        checks[label+'native-destination-and-boss-intro']=m[0x22]==0 and m[0xF9]==26 and m[0x2B]==0x38 and m[0x30]==9 and last['anchor']==seat
        checks[label+'both-bodies-arrived-alive']=last['dead']==[0,0] and all(personal(end,s,0x300)==1 and personal(end,s,0x348)==26 for s in [0,1])
        checks[label+'personal-inventory-retained']=all(personal(end,s,a)==value for s in [0,1] for a,value in [(0xB0,0x94 if not s else 0x91),(0xB1,0x8D if not s else 0x91),(0xA0,0)])
        checks[label+'one-world-and-leader-clock']=last['updates'][seat]==last['world'] and last['updates'][seat^1]<=last['world']
        checks[label+'no-native-failure-or-life-loss']=all(x['coop']['failed']==0 and x['wide']['failed']==0 and x['coop']['lives']==2 for x in rows if x['coop']['ready'])
        checks[label+'backend-exact']=all((n/f).read_bytes()==(i/f).read_bytes() for f in ['run.hash','end.cycstate','present.png','audio.wav'])
        full=qa_route.load_hashes(n/'run.hash');tail=qa_route.load_hashes(c/'run.hash')
        checks[label+'cold-hash-tail']=len(tail)==200 and tail=={f:h for f,h in full.items() if f>2549}
        checks[label+'cold-first-picture-and-final-state']=all((n/f).read_bytes()==(c/f).read_bytes() for f in ['present_02550.png','present.png','end.cycstate'])
        with wave.open(str(n/'audio.wav')) as a,wave.open(str(c/'audio.wav')) as b:pcm=a.readframes(a.getnframes());cold=b.readframes(b.getnframes())
        checks[label+'cold-pcm-tail']=bool(cold) and pcm.endswith(cold)
        cases.append({'leader':seat,'last':last})
    result={'passed':all(checks.values()),'checks':checks,'cases':cases,'exe':hashlib.sha256(exe.read_bytes()).hexdigest(),'scope':__doc__}
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
    return not result['passed']

if __name__=='__main__':raise SystemExit(main())
