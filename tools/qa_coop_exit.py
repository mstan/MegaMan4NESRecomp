"""Either living player leads an original Bright shutter or ladder scroll.

The default uses the existing retail boot/checkpoint route and real Right input.
HP, ammo and brief invulnerability are prerequisites; no room, door, scene,
boss or clear flag is supplied. This is a focused co-op exit regression.
--vertical uses the original page7 initializer and teleports the leader to
the authored upper ladder, while the partner stands on checked native floor.
--return-down then has the other player lead the original return ladder.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,wave
import qa_route
from qa_coop_targets import memory,personal

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ['exe','rom','out']:p.add_argument('--'+k,required=True)
    p.add_argument('--vertical',action='store_true')
    p.add_argument('--return-down',action='store_true',help='after the native climb, the other player leads the actual return ladder')
    v=p.parse_args();exe=Path(v.exe);out=Path(v.out);out.mkdir(parents=True,exist_ok=True)
    if v.return_down and not v.vertical:p.error('--return-down requires --vertical')
    _,prefix=qa_route.expand(Path(__file__).resolve().parent.parent/'tests/routes/coop-boss.route')
    prefix=[x for x in prefix if int(x.split()[0])<2400]
    if v.vertical:prefix=[x for x in prefix if '001F' not in x]
    stop,cut,release,page=(2700,2489,2550,8) if v.vertical else (2750,2549,2700,26)
    if v.return_down:stop,cut,page=2920,2789,7
    checks={};cases=[]
    env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    for seat in [0,1]:
        root=out/f'seat{seat}';root.mkdir(exist_ok=True);pad='2:' if seat else ''
        action='UP' if v.vertical else 'RIGHT'
        lines=prefix+[f'2400 {pad}{action}',f'{release} {pad}-']
        if v.return_down:
            other='' if seat else '2:';lines += [f'2701 {other}DOWN',f'2850 {other}-']
        route=root/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda x:int(x.split()[0])))+'\n')
        for name in ['native','interp','cold']:
            d=root/name;d.mkdir(exist_ok=True)
            argv=[str(exe),v.rom,'--no-save','--config',str(d/'config.ini'),'--frames',str(stop),'--input',str(route),'--coop','on','--coop-view','split','--protoman','on','--weapon-cycle','on','--wide','on','--no-flicker','on','--slowdown-reduction','on','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--present-out',str(d/'present.png'),'--present-every','30','--wav-out',str(d/'audio.wav')]
            if v.vertical:argv+=['--mm4-qa-entry','0,7,2,0,0,0']
            for f,tag in [(2399,'entry'),(cut,'scroll'),(stop-1,'end')]:argv+=['--save-state',f'{f}:{d}/{tag}.cycstate']
            for who in [0,1]:
                pokes=[(0xB0,0x94 if not who else 0x91),(0xB1,0x8D if not who else 0x91),(0x3C,60)]
                if v.vertical:pokes += [(0x330,216 if who==seat else 144),(0x378,40 if who==seat else 164),(0x390,0),(0x30,0),(0x3D8,0),(0x3F0,0)]
                for a,val in pokes:argv+=['--mm4-coop-poke',f'2390:{who}:{a:X}:{val:X}']
            if name=='interp':argv+=['--interp-only']
            if name=='cold':argv+=['--load-state',str(root/'native/scroll.cycstate')]
            with (d/'run.log').open('w') as log:
                q=subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0,timeout=120)
            assert q.returncode==0,(name,q.returncode)
            print(seat,name,flush=True)
        n=root/'native';i=root/'interp';c=root/'cold';end=n/'end.cycstate'
        rows=[json.loads(x) for x in (n/'frames.jsonl').read_text().splitlines()];last=rows[-1]['coop'];m=memory(end)
        label=f'seat{seat}-';raw=(n/'scroll.cycstate').read_bytes();at=raw.find(b'M4CB')
        leader=seat^int(v.return_down)
        during=0x82 if v.return_down else (0xA3 if v.vertical else 0x38)
        destination=0x82 if v.return_down else (0x23 if v.vertical else 0x38)
        checks[label+'actual-pending-native-scroll']=at>=0 and raw[at+13]==(2 if v.vertical else 1) and memory(n/'scroll.cycstate')[0x2B]==during
        checks[label+'native-destination']=m[0x22]==0 and m[0xF9]==page and m[0x2B]==destination and m[0x30]==(3 if v.vertical else 9) and last['anchor']==leader
        checks[label+'both-bodies-arrived-alive']=last['dead']==[0,0] and all(personal(end,s,0x300)==1 and personal(end,s,0x348)==page for s in [0,1])
        if v.vertical:
            checks[label+'both-bodies-ride-native-ladder-animation']=all(personal(n/'scroll.cycstate',0,a)==personal(n/'scroll.cycstate',1,a) for a in [0x330,0x348,0x378,0x390,0x30]) and all(personal(end,s,0x330)==216 and personal(end,s,0x30)==3 for s in [0,1])
        checks[label+'personal-inventory-retained']=all(personal(end,s,a)==value for s in [0,1] for a,value in [(0xB0,0x94 if not s else 0x91),(0xB1,0x8D if not s else 0x91),(0xA0,0)])
        checks[label+'one-world-and-leader-clock']=last['updates'][leader]==last['world'] and last['updates'][leader^1]<=last['world']
        checks[label+'no-native-failure-or-life-loss']=all(x['coop']['failed']==0 and x['wide']['failed']==0 and x['coop']['lives']==2 for x in rows if x['coop']['ready'])
        checks[label+'backend-exact']=all((n/f).read_bytes()==(i/f).read_bytes() for f in ['run.hash','end.cycstate','present.png','audio.wav'])
        full=qa_route.load_hashes(n/'run.hash');tail=qa_route.load_hashes(c/'run.hash')
        checks[label+'cold-hash-tail']=len(tail)==stop-cut-1 and tail=={f:h for f,h in full.items() if f>cut}
        checks[label+'cold-first-picture-and-final-state']=all((n/f).read_bytes()==(c/f).read_bytes() for f in [f'present_{cut+1:05d}.png','present.png','end.cycstate'])
        with wave.open(str(n/'audio.wav')) as a,wave.open(str(c/'audio.wav')) as b:pcm=a.readframes(a.getnframes());cold=b.readframes(b.getnframes())
        checks[label+'cold-pcm-tail']=bool(cold) and pcm.endswith(cold)
        cases.append({'leader':seat,'last':last})
    result={'passed':all(checks.values()),'checks':checks,'cases':cases,'vertical':v.vertical,'return_down':v.return_down,'exe':hashlib.sha256(exe.read_bytes()).hexdigest(),'scope':__doc__}
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
    return not result['passed']

if __name__=='__main__':raise SystemExit(main())
