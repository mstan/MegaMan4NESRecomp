"""Two native Select hold/depart/return checks; no stage traversal matrix.

Uses the established reset route and a stationary, enemy-free room. Personal
HP/ammo are explicit prerequisites. Movement, native beam scripts, control
handoff, inventory retention and current-build continuations are observed.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,wave
import qa_route

ROOT=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__)
for key in ['exe','rom','out']:p.add_argument('--'+key,required=True)
v=p.parse_args();out=Path(v.out).resolve();out.mkdir(parents=True,exist_ok=True)
exe=Path(v.exe).resolve();rom=Path(v.rom).resolve()
_,prefix=qa_route.expand(ROOT/'tests/routes/coop-gallery.route')
prefix=[x for x in prefix if int(x.split()[0])<2400]
env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
addresses=[base+slot for base in range(0x300,0x5B9,24) for slot in range(5)]+list(range(0x30,0x40))+[0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0xA0,0xAD]+list(range(0xB0,0xBE))+[a+i for i in range(8) for a in [0x610,0x630]]+[0x131,0x138,0x133,0x134]

def personal(path,seat,address):
    b=path.read_bytes();pos=b.find(b'M4CA');assert pos>=0
    if b[pos+8]!=seat:return b[pos+20+seat*386+addresses.index(address)]
    at=32
    while at<len(b):
        tag,n=struct.unpack_from('<4sI',b,at)
        if tag==b'HW  ':return b[at+31+address]
        at+=8+n
    raise AssertionError('native RAM missing')

checks={};cases=[]
for seat in [0,1]:
    root=out/f'seat{seat}';root.mkdir(exist_ok=True)
    pad='' if not seat else '2:';other='2:' if not seat else ''
    lines=prefix+['2400 HOLD 0308-0317 00',f'2400 X{seat+1}:R',f'2406 X{seat+1}:-',f'2450 {pad}SELECT',f'2700 {other}SELECT',f'2850 {other}-',f'2860 {pad}START+SELECT',f'2862 {pad}SELECT',f'2900 {pad}-',f'3000 {pad}SELECT',f'3006 {pad}-',f'3200 {pad}RIGHT',f'3220 {pad}-',f'3260 {pad}START',f'3262 {pad}-']
    lines.sort(key=lambda x:int(x.split()[0]));route=root/'input.txt';route.write_text('\n'.join(lines)+'\n')
    for name,load in [('native',None),('interp',None),('cold-out','depart'),('cold-in','arrive')]:
        d=root/name;d.mkdir(exist_ok=True)
        argv=[str(exe),str(rom),'--no-save','--config',str(d/'config.ini'),'--frames','3300','--input',str(route),'--coop','on','--protoman','on','--coop-view','split','--weapon-cycle','on','--wide','on','--slowdown-reduction','on','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--present-out',str(d/'present.png'),'--present-every','30','--wav-out',str(d/'audio.wav'),'--save-state',f'2549:{d}/depart.cycstate','--save-state',f'2699:{d}/hidden.cycstate','--save-state',f'3029:{d}/arrive.cycstate','--save-state',f'3299:{d}/end.cycstate']
        for who,hp,ammo in [(0,0x94,0x8D),(1,0x91,0x91)]:
            for a,val in [(0xB0,hp),(0xB1,ammo)]:argv+=['--mm4-coop-poke',f'2420:{who}:{a:X}:{val:X}']
        if load:argv+=['--load-state',str(root/'native'/(load+'.cycstate'))]
        argv+=['--no-flicker','on']
        if name=='interp':argv+=['--interp-only']
        with (d/'run.log').open('w') as log:q=subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0,timeout=180)
        assert q.returncode==0,(name,q.returncode)
        print(seat,name,flush=True)
    n=root/'native';i=root/'interp';rows=[json.loads(x) for x in (n/'frames.jsonl').read_text().splitlines()];by={x['frame']:x['coop'] for x in rows};last=rows[-1]['coop']
    label=f'seat{seat}-';first=next(x['frame'] for x in rows if x['coop']['withdrawals'][seat])
    checks[label+'90-native-frames']=first==2540 and by[2539]['transport'][3+seat]==89
    checks[label+'native-departure']=by[2550]['transport'][1+seat]==1 and by[2550]['players'][seat][4]==8 and by[2699]['transport'][1+seat]==2
    checks[label+'survivor-promoted']=last['anchor']==(seat^1) and last['updates'][seat^1]==last['world']
    checks[label+'last-player-protected']=last['withdrawals']==([1,0] if not seat else [0,1]) and all(x['coop']['transport'][0]!=(3) for x in rows)
    checks[label+'held-select-stays-out']=by[2899]['transport'][0]==(1<<seat) and by[2899]['rejoins'][seat]==0
    checks[label+'withdrawn-start-ignored']=all(x['stage']==0 and x['coop']['ready'] and not x['coop']['menu'] for x in rows if 2860<=x['frame']<=2900)
    checks[label+'returned-inventory-works']=rows[-1]['stage']==16 and last['menu']==1 and last['pauser']==seat
    checks[label+'native-return']=by[3030]['transport'][1+seat]==3 and by[3030]['players'][seat][4]==10 and last['rejoins'][seat]==1 and last['transport'][:3]==[0,0,0]
    beam=[x['coop']['players'][seat][1] for x in rows if x['coop']['transport'][1+seat]==3]
    checks[label+'return-descends-to-checked-floor']=len(beam)>25 and beam[0]<16 and all(0<=y<=180 for y in beam) and all(a<=b for a,b in zip(beam,beam[1:]))
    checks[label+'returned-controls']=by[3221]['players'][seat][0]-by[3199]['players'][seat][0]>=16
    checks[label+'personal-inventory']=all(personal(n/'end.cycstate',who,a)==expected for who in [0,1] for a,expected in [(0xB0,0x94 if not who else 0x91),(0xB1,0x8D if not who else 0x91),(0xA0,int(who==seat))])
    checks[label+'no-life-spent']=all(x['coop']['lives']==2 for x in rows if x['coop']['ready'])
    checks[label+'native-scope-returns']=all(x['coop']['failed']==0 and x['wide']['failed']==0 for x in rows)
    checks[label+'backend-exact']=all((n/f).read_bytes()==(i/f).read_bytes() for f in ['run.hash','audio.wav','present.png','end.cycstate'])
    full=qa_route.load_hashes(n/'run.hash')
    for name,cut in [('cold-out',2549),('cold-in',3029)]:
        c=root/name;tail=qa_route.load_hashes(c/'run.hash')
        checks[label+name+'-hash']=len(tail)==3300-cut-1 and tail=={f:h for f,h in full.items() if f>cut}
        checks[label+name+'-state-picture']=all((n/f).read_bytes()==(c/f).read_bytes() for f in ['end.cycstate','present.png',f'present_{cut+1:05d}.png'])
        with wave.open(str(n/'audio.wav')) as a,wave.open(str(c/'audio.wav')) as b:pcm=a.readframes(a.getnframes());cold=b.readframes(b.getnframes())
        checks[label+name+'-pcm']=bool(cold) and pcm.endswith(cold)
    cases.append({'seat':seat,'first_departure_frame':first,'last':last})
result={'passed':all(checks.values()),'checks':checks,'cases':cases,'exe':hashlib.sha256(exe.read_bytes()).hexdigest(),'scope':__doc__}
(out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True);raise SystemExit(not result['passed'])
