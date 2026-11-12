"""Native-width crowd, wide composition, intentional hiding and current-state QA.

The native type50/frame88 crowd is an explicit renderer prerequisite. It does
not establish authored spawning, normal combat or stage coverage.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess,wave
from PIL import Image
import qa_route

ROOT=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__)
for key in ['exe','rom','out']:p.add_argument('--'+key,required=True)
v=p.parse_args();out=Path(v.out).resolve();out.mkdir(parents=True,exist_ok=True)
exe=Path(v.exe).resolve();rom=Path(v.rom).resolve()
_,lines=qa_route.expand(ROOT/'tests/routes/coop-gallery.route');lines=[x for x in lines if int(x.split()[0])<2400]
for base in range(0x300,0x5B9,24):lines.append(f'2400 POKE {base+8:04X}-{base+23:04X} 00')
for base,val in [(0x300,0x50),(0x348,0),(0x378,128),(0x390,0),(0x408,0xC0),(0x450,1),(0x498,3),(0x528,0xD0),(0x540,0),(0x558,0x88),(0x588,5),(0x5A0,0xA7)]:lines.append(f'2400 HOLD {base+8:04X}-{base+23:04X} {val:02X}')
for slot in range(8,24):lines.append(f'2400 HOLD {0x330+slot:04X} {8+(slot-8)*14:02X}')
env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
cases=[]
for name in ['off','native','interp','cold','wide','mask','hidden']:
    d=out/name;d.mkdir(exist_ok=True);route=lines[:]
    mods=d/'mods';shutil.copytree(exe.parent/'mods/packages',mods/'packages',dirs_exist_ok=True)
    if name=='mask':route+=['2450 HOLD 00FE 08'] # actual native NMI's PPUMASK shadow
    if name=='hidden':route+=['2450 HOLD 0530-053F D4'] # native DC66 intentional visibility bit
    route.sort(key=lambda x:int(x.split()[0]));(d/'input.txt').write_text('\n'.join(route)+'\n')
    args=[str(exe),str(rom),'--no-save','--mods-root',str(mods),'--config',str(d/'config.ini'),'--frames','2500','--input',str(d/'input.txt'),'--coop','off','--protoman','off','--wide','on' if name=='wide' else 'off','--aspect','fit','--present-size','1280x720','--weapon-cycle','off','--slowdown-reduction','on','--mm4-controls-log',str(d/'frames.jsonl'),'--hash-out',str(d/'run.hash'),'--screenshot',str(d/'hardware.png'),'--present-out',str(d/'present.png'),'--present-every','50','--wav-out',str(d/'audio.wav'),'--save-state',f'2449:{d}/pressure.cycstate','--save-state',f'2499:{d}/end.cycstate']
    # Other cases intentionally use the fresh catalog's default-on feature.
    if name=='off':args+=['--no-flicker','off']
    if name=='interp':args+=['--interp-only']
    if name in ['cold','mask','hidden']:args+=['--load-state',str(out/'native/pressure.cycstate')]
    with (d/'run.log').open('w') as log:q=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0,timeout=180)
    assert q.returncode==0,(name,(d/'run.log').read_text(errors='replace')[-1200:])
    rows=[json.loads(x) for x in (d/'frames.jsonl').read_text().splitlines()]
    cases.append({'name':name,'last':rows[-1],'argv':args});print(name,rows[-1]['wide'],flush=True)

def cpu_ram(path):
    b=path.read_bytes();at=32
    while at<len(b):
        tag,n=struct.unpack_from('<4sI',b,at)
        if tag==b'HW  ':return b[at+8:at+8+2071]
        at+=8+n
    raise AssertionError('native CPU state missing')

off=out/'off';n=out/'native';i=out/'interp';c=out/'cold';w=out/'wide'
checks={
    'fresh-default-on':cases[1]['last']['wide']['noflicker']==1,
    'independent-of-wide':cases[1]['last']['wide']['on']==0 and cases[1]['last']['wide']['width']==256,
    'beyond-64-native-parts':cases[1]['last']['wide']['parts']>64,
    'cpu-ram-hardware-audio-unchanged':cpu_ram(off/'end.cycstate')==cpu_ram(n/'end.cycstate') and all((off/f).read_bytes()==(n/f).read_bytes() for f in ['hardware.png','audio.wav']),
    'missing-parts-now-visible':(off/'present.png').read_bytes()!=(n/'present.png').read_bytes(),
    'backend-exact':all((n/f).read_bytes()==(i/f).read_bytes() for f in ['run.hash','end.cycstate','present.png','audio.wav']),
    'cold-state-picture':all((n/f).read_bytes()==(c/f).read_bytes() for f in ['end.cycstate','present.png','present_02450.png']),
    'wide-426':Image.open(w/'present.png').size==(426,240) and cases[4]['last']['wide']['parts']>64,
    'intentional-ppu-hiding':(out/'mask/present.png').read_bytes()==(out/'mask/hardware.png').read_bytes(),
    'intentional-native-animation-hiding':cases[6]['last']['wide']['parts']<16 and (out/'hidden/present.png').read_bytes()==(out/'hidden/hardware.png').read_bytes(),
    'zero-failed-native-calls':all(x['last']['wide']['failed']==0 for x in cases),
}
with Image.open(off/'present.png') as a,Image.open(n/'present.png') as b,Image.open(w/'present.png') as wide:
    checks['native-left-mask']=a.crop((0,0,8,240)).tobytes()==b.crop((0,0,8,240)).tobytes()
    checks['wide-native-region']=b.tobytes()==wide.crop((0,0,256,240)).tobytes()
full=qa_route.load_hashes(n/'run.hash');tail=qa_route.load_hashes(c/'run.hash');checks['cold-50-frames']=len(tail)==50 and tail=={f:h for f,h in full.items() if f>2449}
with wave.open(str(n/'audio.wav')) as a,wave.open(str(c/'audio.wav')) as b:pcm=a.readframes(a.getnframes());cold=b.readframes(b.getnframes())
checks['cold-pcm']=bool(cold) and pcm.endswith(cold)
result={'passed':all(checks.values()),'checks':checks,'cases':cases,'exe':hashlib.sha256(exe.read_bytes()).hexdigest(),'scope':__doc__}
(out/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'passed':result['passed'],'checks':checks,'exe':result['exe']}),flush=True);raise SystemExit(not result['passed'])
