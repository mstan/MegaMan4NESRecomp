"""Bounded native targeting/paired-refight regression; explicit existing hub/boss snapshots.

Fixtures place players and choose a visible native AI phase. Portal admission,
map loading and later ordinary controls remain native. No flag writes substitute
for the transition under test; the final check discloses prior-content setup.
"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,wave

def memory(path):
    b=Path(path).read_bytes();at=32
    while at<len(b):
        tag,n=struct.unpack_from('<4sI',b,at)
        if tag==b'HW  ':return b[at+31:at+8+2071]
        at+=8+n
    raise AssertionError('missing native RAM')

addresses=[base+slot for base in range(0x300,0x5B9,24) for slot in range(5)]+list(range(0x30,0x40))+[0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0xA0,0xAD]+list(range(0xB0,0xBE))+[a+i for i in range(8) for a in [0x610,0x630]]+[0x131,0x138,0x133,0x134]
index={a:i for i,a in enumerate(addresses)}

def personal(path,seat,address):
    b=Path(path).read_bytes();at=b.find(b'M4C8');assert at>=0
    if b[at+8]==seat:return memory(path)[address]
    return b[at+20+seat*386+index[address]]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['exe','rom','hub-state','boss-state','out']:p.add_argument('--'+key,required=True)
    p.add_argument('--return-state');p.add_argument('--final-state',help='legitimate completed-refights progress source; its dead guest is not revived')
    p.add_argument('--targets-only',action='store_true');p.add_argument('--warps-only',action='store_true')
    p.add_argument('--only-final',action='store_true',help='use hub-state from prior-return-portal-0@4799, with both players alive')
    v=p.parse_args();out=Path(v.out);out.mkdir(parents=True,exist_ok=True);checks={};runs=[]
    env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    def run(name,source,lines,end,pokes=(),interp=False,saves=()):
        d=out/name;d.mkdir(exist_ok=True);route=d/'input.txt';route.write_text('\n'.join(sorted(lines,key=lambda x:int(x.split()[0])))+'\n')
        # These retained native content snapshots have controls.on=0. Keep
        # that explicit CLI profile; catalog admission has separate coverage.
        argv=[v.exe,v.rom,'--no-save','--config',str(d/'config.ini'),'--frames',str(end),'--input',str(route),'--coop','on','--weapon-cycle','off','--wide','on','--aspect','fit','--present-size','1280x720','--load-state',str(source),'--mm4-controls-log',str(d/'state.jsonl'),'--hash-out',str(d/'frames.hash'),'--present-out',str(d/'present.png'),'--wav-out',str(d/'audio.wav'),'--save-state',str(end-1)+':'+str(d/'end.cycstate')]
        for f,seat,a,value in pokes:argv+=['--mm4-coop-poke',f'{f}:{seat}:{a:X}:{value:X}']
        for f in saves:argv+=['--save-state',str(f)+':'+str(d/f'at{f}.cycstate')]
        if interp:argv+=['--interp-only']
        with (d/'run.log').open('w') as log:r=subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS,timeout=120)
        runs.append(dict(name=name,exit=r.returncode,argv=argv));print(json.dumps(dict(name=name,exit=r.returncode)),flush=True)
        assert r.returncode==0,(d/'run.log').read_text(errors='replace')[-1800:]
        return d
    def compare(name,a,b):
        checks[name]=all((a/f).read_bytes()==(b/f).read_bytes() for f in ['frames.hash','end.cycstate','present.png','audio.wav'])
    if not v.warps_only and not v.only_final:
        m=memory(v.boss_state);slot=m[0x146];assert m[0x300+slot]==0x71,'use the first native Toad refight snapshot'
        f=3950;camera=m[0xF9]*256+m[0xFC]
        for name,near,zero in [('guest-nearest',1,False),('primary-nearest',0,False),('zero-hp-near-excluded',1,True),('guest-nearest-fire',1,False)]:
            selected=near^int(zero);pokes=[]
            for seat in [0,1]:
                x=camera+(176 if seat==near else 32)
                for a,value in [(0x330,x&255),(0x348,x>>8),(0x378,180),(0x390,0),(0x30,7 if zero and seat==near else 0),(0xB0,0x80 if zero and seat==near else 0x9C),(0x3C,60),(0x3D8,0),(0x3F0,0)]:pokes.append((f,seat,a,value))
            fields={0x330+slot:(camera+128)&255,0x348+slot:(camera+128)>>8,0x378+slot:180,0x558+slot:0,0x540+slot:0,0x588+slot:0,0x5A0+slot:0xA8,0x5B8+slot:0,0x4B0+slot:0,0x4C8+slot:0}
            action='B' if name.endswith('-fire') else 'RIGHT'
            lines=[f'{f} POKE {a:04X} {value:02X}' for a,value in fields.items()]+[f'{f} '+(action if not selected else '2:'+action)]
            d=run(name,v.boss_state,lines,f+8,pokes)
            end=memory(d/'end.cycstate')
            if name.endswith('-fire'):
                rows=[json.loads(line) for line in (d/'state.jsonl').read_text().splitlines()]
                checks[name+'-actual-native-pad-reaction']=end[0x4B0+slot]>0 and any(row['coop']['players'][1][3]>0 for row in rows)
            else:checks[name+'-actual-native-pad-reaction']=end[0x4C8+slot]>0 and (end[0x588+slot]!=0 or end[0x5A0+slot]!=0xA8)
            if name=='guest-nearest':
                i=run(name+'-interp',v.boss_state,lines,f+8,pokes,interp=True);compare(name+'-backend-exact',d,i)
        facing=[]
        for name,guest in [('enemy-native-primary-control',False),('enemy-near-guest',True)]:
            slot=8;pokes=[]
            for seat in [0,1]:
                x=camera+(176 if seat==int(guest) else 32)
                for a,value in [(0x330,x&255),(0x348,x>>8),(0x378,180),(0x390,0),(0x30,7 if not guest and seat else 0),(0xB0,0x80 if not guest and seat else 0x9C),(0x3C,60),(0x3D8,0),(0x3F0,0)]:pokes.append((f,seat,a,value))
            fields={base+slot:0 for base in range(0x300,0x5B9,24)}
            fields.update({0x300+slot:0x50,0x330+slot:(camera+128)&255,0x348+slot:(camera+128)>>8,0x378+slot:180,0x408+slot:0xC0,0x450+slot:1,0x498+slot:3,0x558+slot:0x88,0x588+slot:5,0x5A0+slot:0xA7})
            lines=[f'{f} POKE {a:04X} {value:02X}' for a,value in fields.items()]+[f'{f} -',f'{f} 2:-']
            d=run(name,v.boss_state,lines,f+8,pokes);m=memory(d/'end.cycstate')
            bullets=[s for s in range(5,24) if m[0x300+s]==0x51]
            facing.append(m[0x420+slot]&1)
            checks[name+'-native-actor-and-emitted-shot']=m[0x300+slot]==0x50 and bool(bullets) and all(m[0x420+s]&1==facing[-1] for s in bullets)
        checks['enemy-near-guest-native-projectile-oracle']=facing[0]==facing[1]==1
    if not v.targets_only:
        cases=[('entry',Path(v.hub_state),3501,3950,10,s) for s in [0,1]] if not v.only_final else [('final',Path(v.hub_state),4810,6050,12,1)]
        if v.return_state and not v.only_final:cases += [('return',Path(v.return_state),4410,4800,9,s) for s in [0,1]]
        if v.final_state and not v.only_final:
            assert v.return_state and memory(v.final_state)[0xAC]==255,'prior completed refights must have legitimate source evidence'
            b=Path(v.return_state).read_bytes();cp=b.find(b'M4C8');assert cp>=0 and all(not b[cp+20+s*386+385] for s in [0,1]),'use the both-living first-kill fixture'
            cases += [('prior-return',Path(v.return_state),4410,4800,9,0)]
        for kind,source,f,end_frame,destination,seat in cases:
            pokes=[];m=memory(source);camera=m[0xF9]*256+m[0xFC]
            if kind=='final':
                initial=source.read_bytes();cp=initial.find(b'M4C8')
                assert m[0xAC]==255 and cp>=0 and all(not initial[cp+20+s*386+385] for s in [0,1]),'final portal requires both living contexts and disclosed prior completion'
            if kind=='entry':x,y=camera+32,180
            else:
                pods=[slot for slot in range(5,24) if m[0x300+slot]==0xB3 and (kind!='final' or m[0x438+slot]==255)];assert len(pods)==1,pods
                pod=pods[0];x=m[0x330+pod]+256*m[0x348+pod];y=m[0x378+pod]-(16 if kind=='final' else 0)
            other=x+56 if x-camera<128 else x-56
            for who in [0,1]:
                px=x if who==seat else other
                for a,value in [(0x330,px&255),(0x348,px>>8),(0x378,y if who==seat else max(y,180)),(0x390,0),(0x30,0),(0x31,0),(0x3D8,0),(0x3F0,0),(0x3C,255),(0xA0,who),(0xB0,0x94 if not who else 0x8E)]+[(a,0x80+5+who*5) for a in range(0xB1,0xBE)]:pokes.append((f,who,a,value))
                for at in range(f+200,end_frame,200):pokes.append((at,who,0x3C,255))
            lines=[f'{f} UNHOLD 003C',f'{f+4} '+('DOWN' if not seat else '2:DOWN'),f'{f+14} '+('-' if not seat else '2:-')]
            if kind=='prior-return':lines.append(f'{f} POKE 00AC FF')
            label=f'{kind}-portal-{seat}';cut=f+(640 if kind=='final' else 14)
            d=run(label,source,lines,end_frame,pokes,saves=[cut]);end=d/'end.cycstate';m=memory(end)
            b=end.read_bytes();cp=b.find(b'M4C8');wp=b.find(b'M4WT')
            checks[label+'-both-native-destination']=m[0x2A]==destination and all(personal(end,s,0x348)==m[0xF9] and personal(end,s,0x30)<8 for s in [0,1])
            checks[label+'-personal-inventory']=all(personal(end,s,0xA0)==s and personal(end,s,0xB0)&31==[20,14][s] and all(personal(end,s,a)==0x80+5+s*5 for a in range(0xB1,0xBE)) for s in [0,1])
            rows=[json.loads(line) for line in (d/'state.jsonl').read_text().splitlines()]
            checks[label+'-actual-recipient']=any(row['state']==15 and row['coop']['anchor']==seat for row in rows) and wp>=0 and not b[wp+4] and struct.unpack_from('<Q',b,wp+8+seat*8)[0]==1
            if kind=='prior-return':
                checks['prior-completion-native-final-portal']=sum(m[0x300+s]==0xB3 and m[0x438+s]==255 for s in range(5,24))==1 and m[0xAC]==255 and all(not b[cp+20+s*386+385] for s in [0,1])
                cases.append(('final',end,end_frame+10,end_frame+1250,12,1))
            if kind=='final':checks['final-native-machine-admission']=m[0xAB]==8 and m[0x300+m[0x146]]==0xBC
            if kind=='prior-return' or (kind=='return' and not seat):continue
            i=run(label+'-interp',source,lines,end_frame,pokes,interp=True);compare(label+'-backend-exact',d,i)
            saved=d/f'at{cut}.cycstate';wb=saved.read_bytes();wa=wb.find(b'M4WT');checks[label+'-actual-pending-native-script-saved']=wa>=0 and wb[wa+4]==(2 if kind=='final' else 1)
            cold=run(label+'-cold',saved,lines,end_frame,pokes)
            checks[label+'-cold-state-picture']=all((d/n).read_bytes()==(cold/n).read_bytes() for n in ['end.cycstate','present.png'])
            warm_hash=(d/'frames.hash').read_text().splitlines();cold_hash=(cold/'frames.hash').read_text().splitlines()
            checks[label+'-cold-hashes']=cold_hash==[line for line in warm_hash if int(line.split()[0])>cut]
            with wave.open(str(d/'audio.wav'),'rb') as w:pcm=w.readframes(w.getnframes())
            with wave.open(str(cold/'audio.wav'),'rb') as w:tail=w.readframes(w.getnframes())
            checks[label+'-cold-pcm']=bool(tail) and pcm.endswith(tail)
    result=dict(checks=checks,runs=runs,exe_sha256=hashlib.sha256(Path(v.exe).read_bytes()).hexdigest(),prior_refight_completion_prerequisite=bool(v.final_state or v.only_final),scope=__doc__+' The optional final check sets prior-content AC=FF on a both-living first-kill fixture, then the original return builds the final hub. This does not claim the eight boss clears; no AB/final-stage/result flags are set.')
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(checks),flush=True)
    return int(not all(checks.values()))
if __name__=='__main__':raise SystemExit(main())
