"""Real distant Bright exits: guest right3-to4, then other player's up6-to7.

Native entry, body positioning, separate resources and invulnerability are
prerequisites. No exit, scene, death or completion result is supplied.
"""
from pathlib import Path
import argparse,csv,hashlib,json,os,subprocess,wave
import qa_route
from qa_coop_targets import memory,personal

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','out'):parser.add_argument('--'+key,required=True)
    v=parser.parse_args();out=Path(v.out);out.mkdir(parents=True,exist_ok=True)
    _,lines=qa_route.expand(Path(__file__).resolve().parent.parent/'tests/routes/coop-boss.route')
    lines=[x for x in lines if int(x.split()[0])<2390 and 'HOLD' not in x and '001F' not in x]
    lines+=['2400 2:RIGHT','2550 2:-','2690 X1:R','2692 X1:-','2710 UP','2900 -',
            '2905 X1:L','2907 X1:-','2910 B','2911 -','2920 2:B','2921 2:-']
    route=out/'input.txt';route.write_text('\n'.join(lines)+'\n')
    checks={};env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    snapshots=(2399,2418,2429,2449,2699,2729,2759,2919,2929)
    for name,cut in [('native',None),('interp',None),('cold-drain',2418),('cold-first',2429),('cold-second',2759)]:
        d=out/name;d.mkdir(exist_ok=True)
        args=[v.exe,v.rom,'--no-save','--config',str(d/'config.ini'),'--frames','2930','--input',str(route),
              '--coop','on','--coop-view','split','--protoman','on','--weapon-cycle','on',
              '--wide','on','--no-flicker','on','--slowdown-reduction','on',
              '--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),
              '--present-out',str(d/'present.png'),'--present-every','1','--wav-out',str(d/'audio.wav'),
              '--world-audit',str(d/'world.csv')]
        if cut is None:args+=['--mm4-qa-entry','0,0,0,0,0,0']
        for seat in (0,1):
            fields=[(0x348,0 if not seat else 3),(0x330,64 if not seat else 224),
                    (0x378,180 if not seat else 116),(0x390,0),(0x30,0),(0x3D8,0),(0x3F0,0),
                    (0xB0,0x94 if not seat else 0x91),(0xB1,0x8D if not seat else 0x91),(0xA0,0),(0x3C,120)]
            future=[(0x348,6 if not seat else 4),(0x330,216 if not seat else 64),
                    (0x378,40 if not seat else 148),(0x390,0),(0x30,0),(0x3D8,0),(0x3F0,0),(0x3C,120)]
            for frame,pokes in [(2390,fields),(2650,[(0x3C,120)]),(2700,future)]:
                if cut is not None and frame<=cut:continue
                for address,value in pokes:args+=['--mm4-coop-poke',f'{frame}:{seat}:{address:X}:{value:X}']
        for f in snapshots:
            if cut is None or f>cut:args+=['--save-state',f'{f}:{d}/{f}.cycstate']
        if name=='interp':args+=['--interp-only']
        if cut is not None:args+=['--load-state',str(out/'native'/f'{cut}.cycstate')]
        with (d/'run.log').open('w')as log:
            q=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0,timeout=180)
        assert q.returncode==0,(name,q.returncode)
        print(name,flush=True)
        n=out/'native'
        if name=='interp':checks['backend-exact']=all((n/f).read_bytes()==(d/f).read_bytes()for f in ('run.hash','2929.cycstate','present.png','audio.wav'))
        if cut is not None:
            full=qa_route.load_hashes(n/'run.hash');tail=qa_route.load_hashes(d/'run.hash')
            checks[name+'-hash']=len(tail)==2929-cut and tail=={f:h for f,h in full.items()if f>cut}
            checks[name+'-picture-state']=all((n/f).read_bytes()==(d/f).read_bytes()for f in (f'present_{cut+1:05d}.png','2929.cycstate','present.png'))
            with wave.open(str(n/'audio.wav'))as x,wave.open(str(d/'audio.wav'))as y:pcm=x.readframes(x.getnframes());cold=y.readframes(y.getnframes())
            checks[name+'-pcm']=bool(cold)and pcm.endswith(cold)
    n=out/'native';rows={x['frame']:x['coop']for x in map(json.loads,(n/'frames.jsonl').read_text().splitlines())}
    checks['both-real-source-loaders']=rows[2429]['source']==[1,3,0,0,1] and rows[2759]['source']==[1,6,0,1,1]
    checks['drain-phase']=rows[2418]['source'][0]==3 or rows[2419]['source'][0]==3
    checks['horizontal-destination']=memory(n/'2699.cycstate')[0xF9]==4 and rows[2699]['anchor']==1
    checks['vertical-destination']=memory(n/'2929.cycstate')[0xF9]==7 and rows[2929]['source']==[0,6,0,1,1]
    checks['separate-equipment-through-second-loader']=personal(n/'2759.cycstate',0,0xA0)==1 and personal(n/'2759.cycstate',1,0xA0)==0
    # Snapshots are after the named frame; the frame log labels the following
    # frame's boundary. The original firing countdown has advanced to7 here.
    checks['returned-native-shots']=personal(n/'2919.cycstate',0,0x32)==7 and personal(n/'2929.cycstate',1,0x32)==7
    checks['personal-resources']=all(personal(n/'2929.cycstate',s,a)==value for s in (0,1)for a,value in [(0xB0,0x94 if not s else 0x91),(0xB1,0x8D if not s else 0x91),(0xA0,0)])
    checks['no-failure-death-or-life-loss']=all(x['failed']==0 and x['dead']==[0,0] and x['lives']==2 for x in rows.values()if x['ready'])
    audit=list(csv.DictReader((n/'world.csv').open()));script=[x for x in audit if 768<int(x['camera'])<1024]
    checks['native-scroll-background']=bool(script)and all(int(x['mismatched'])==int(x['native_edge_attribute_pixels'])for x in script)
    checks['settled-background']=any(x['camera']=='1024'and x['mismatched']=='0'for x in audit)
    result={'passed':all(checks.values()),'checks':checks,'exe':hashlib.sha256(Path(v.exe).read_bytes()).hexdigest(),'scope':__doc__}
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
    return not result['passed']

if __name__=='__main__':raise SystemExit(main())
