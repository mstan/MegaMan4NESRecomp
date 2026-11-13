"""Check native Bright blackout HUDs, room geometry and current-state replay."""
from pathlib import Path
import argparse,os,json,subprocess,shutil,wave,hashlib
from PIL import Image
p=argparse.ArgumentParser(description=__doc__)
for key in ('exe','rom','out'):p.add_argument('--'+key,type=Path,required=True)
v=p.parse_args();o=v.out.resolve();o.mkdir(parents=True,exist_ok=False)
exe=v.exe.resolve();rom=v.rom.resolve();packages=Path(__file__).resolve().parent.parent/'mods/preloaded/packages'
profile='format_version = 1\n'
for package,feature in [('coop','coop'),('character.protoman','protoman'),('enhancement.weapons','weapons'),('enhancement.widescreen','widescreen'),('enhancement.noflicker','noflicker'),('enhancement.slowdown','slowdown')]:
 profile+=f'\n[[feature]]\npackage_id = "mega-man-4-usa.{package}"\nid = "{feature}"\nenabled = true\n'
lines=['300 START','305 -','600 START','605 -','900 START','905 -','1200 A','1201 -','1600 RIGHT+B','1612 RIGHT+A+B','1630 RIGHT','1650 -','1700 DOWN+A','1701 DOWN','1725 -','1750 B','1870 -','1900 START','1901 -','2000 START','2001 -','2200 -'];last=None
for frame in range(2500,7001):
 value='+'.join(['RIGHT']+(['A'] if frame%90<18 else [])+(['B'] if frame%18<2 else []))
 if value!=last:lines.extend([f'{frame} {value}',f'{frame} 2:{value}']);last=value
route=o/'native.input';route.write_text('\n'.join(lines)+'\n')
env=dict(os.environ,NESRECOMP_NO_LAUNCHER='1',SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='dummy',SDL_GAMECONTROLLER_IGNORE_DEVICES='0x054c/0x0ce6');checks={};observed={}
for name in ('native','interpreter','cold','instant'):
 d=o/name;d.mkdir();shutil.copytree(packages,d/'mods/packages');(d/'mods/state.toml').write_text(profile);a=[str(exe),str(rom),'--no-save','--mods-root',str(d/'mods'),'--config',str(d/'config.ini'),'--frames','4001' if name=='instant' else '7001','--input',str(route),'--coop','on','--coop-view','shared','--present-size','1280x720','--present-out',str(d/'scene.png'),'--hash-out',str(d/'hash.txt'),'--wav-out',str(d/'audio.wav'),'--mm4-controls-log',str(d/'frames.jsonl'),'--save-state',f'4000:{d}/middle.cycstate','--save-state',f'6999:{d}/end.cycstate','--frame-log',str(d/'ram.frames'),'--frame-log-frames','4000:4000']
 if name=='native':a+=['--present-every','100']
 if name=='interpreter':a+=['--interp-only']
 if name in ('cold','instant'):a+=['--load-state',str(o/'native/middle.cycstate')]
 with(d/'log.txt').open('w') as log:p=subprocess.run(a,cwd=exe.parent,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
 assert p.returncode==0,(name,(d/'log.txt').read_text()[-1400:]);print(name,'completed',flush=True)
n=o/'native';i=o/'interpreter';c=o/'cold';rows={x['frame']:x for x in map(json.loads,(n/'frames.jsonl').read_text().splitlines())}
checks['room-retained-through-native-spike-death']=rows[7001]['wide']['width']==426 and rows[7001]['wide']['room_valid']==1 and rows[7001]['wide']['parts']==0
checks['no-native-call-failures']=all(x['wide']['failed']==x['coop']['failed']==0 for x in rows.values())
checks['continuous-walking-blackout-room-geometry']=all(rows[f]['wide']['room_valid']==1 and rows[f]['wide']['width']==426 for f in range(2400,3101))
for f in ('hash.txt','scene.png','end.cycstate','middle.cycstate','audio.wav','ram.frames'):checks['Native-I-'+f]=(n/f).read_bytes()==(i/f).read_bytes()
for f in ('scene.png','end.cycstate'):checks['cold-'+f]=(n/f).read_bytes()==(c/f).read_bytes()
checks['cold-hash']=[s for s in(n/'hash.txt').read_text().splitlines() if s[:1].isdigit() and int(s.split()[0])>4000]==(c/'hash.txt').read_text().splitlines()
def pcm(p):
 with wave.open(str(p),'rb') as w:return w.readframes(w.getnframes())
cold=pcm(c/'audio.wav');checks['cold-PCM']=bool(cold) and pcm(n/'audio.wav').endswith(cold)
checks['instant-first-current-picture']=(o/'instant/scene.png').read_bytes()==(n/'scene_04000.png').read_bytes()
for f in (2400,2500,2700,2800,2900,3000):
 im=Image.open(n/f'scene_{f:05}.png').convert('RGB');hp=[p[2] for p in rows[f]['coop']['players']];counts=[sum(any(rgb) for rgb in im.crop((8,20+seat*76,16,76+seat*76)).getdata()) for seat in range(2)];observed[str(f)]=dict(hp=hp,HUD_colored_pixels=counts)
 checks[f'native-personal-meter-{f}']=all(counts[seat]==hp[seat]*6 for seat in range(2) if hp[seat]>0 and not rows[f]['coop']['dead'][seat])
result=dict(passed=all(checks.values()),checks=checks,observed=observed,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),scope='Original Bright reset route plus both actual move/jump/fire inputs through arrival, genuine blackout, native spike death and survivor camera handoff. Native/I, current cold/instant/state/PCM, and exact personal HP fill during arrival and four native blackout pictures. No completion flags or synthetic fade/HUD result flags.')
(o/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True);assert result['passed'],checks
