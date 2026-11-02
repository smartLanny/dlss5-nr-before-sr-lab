"""Run a frozen native bench in sequence; make images only after GPU runs finish."""
from __future__ import annotations
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import sys
import time
import zlib

def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()

def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

def png(path, pixels):
    import numpy as np
    a=np.ascontiguousarray(pixels, dtype=np.uint8)
    if a.ndim!=3 or a.shape[2]!=3: raise ValueError('RGB8 only')
    h,w,_=a.shape
    def chunk(tag,data):
        return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    data=b''.join(b'\0'+a[y].tobytes() for y in range(h))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+
                          chunk(b'IDAT',zlib.compress(data,6))+chunk(b'IEND',b''))

def display_frame(path, width, height, pre, scale):
    import numpy as np
    raw=np.fromfile(path, dtype='<f2')
    if raw.size!=width*height*4: raise ValueError(f'invalid packed frame: {path}')
    rgb=raw.reshape(height,width,4)[...,:3].astype(np.float32)
    if not np.isfinite(rgb).all(): raise ValueError(f'nonfinite RGB: {path}')
    # A common, fixed display transform. Raw captures are retained separately.
    x=np.maximum(rgb*(scale/pre),0)
    x=x/(1+x)
    x=np.where(x<=.0031308,12.92*x,1.055*np.power(x,1/2.4)-.055)
    return np.rint(np.clip(x,0,1)*255).astype(np.uint8)

def perf_summary(result):
    frames=result['timing']['frames']; warm=result['warm_frames']
    ids=[f['id'] for f in frames]
    if len(ids)!=warm+result['observed_frames'] or sorted(ids)!=list(range(len(ids))):
        raise ValueError('timing frames missing or duplicated')
    for f in frames:
        if f['completed'] < f['fence'] or f['fence']<=0 or f['end']<f['begin']:
            raise ValueError('timing lacks exact completed fence or valid timestamp order')
    values=[float(f['milliseconds']) for f in frames if f['id']>=warm]
    if not values or any(not (v>=0 and v<float('inf')) for v in values):
        raise ValueError('missing/invalid observed GPU timings')
    ordered=sorted(values)
    samples=result['memory_samples']
    if not any(s['phase']=='after_retire' for s in samples):
        raise ValueError('missing post-retirement memory sample')
    steady=[int(s['usage_bytes']) for s in samples if s['phase']=='observe']
    if not steady: raise ValueError('missing process memory observe samples')
    byphase={}
    for s in samples: byphase.setdefault(s['phase'],[]).append(int(s['usage_bytes']))
    return {'gpu_ms_median':statistics.median(values),
            'gpu_ms_p95':ordered[min(len(ordered)-1, int(.95*len(ordered)))],
            'serial_wall_ms_median':statistics.median(float(f.get('serial_wall_ms',0)) for f in frames if f['id']>=warm),
            'observed_frames':len(values),
            'process_vram_sampled_peak_bytes':max(int(s['usage_bytes']) for s in samples),
            'process_vram_stable_median_bytes':statistics.median(steady[-60:]),
            'memory_phases':{k:v[-1] for k,v in byphase.items()},
            'scope':result['timing']['scope']}

def normalize_result(raw):
    # Preserve the native result.json unchanged; only adapt keys in memory.
    result=dict(raw)
    result['nr_enabled']=raw.get('nr_enabled',raw.get('nrEnabled'))
    result['warm_frames']=raw.get('warm_frames',raw.get('warm'))
    result['observed_frames']=raw.get('observed_frames',raw.get('frames'))
    return result

def verify_packet(packet):
    native=packet['native_packet']; path=Path(native['path'])
    if digest(path)!=native['sha256']: raise ValueError('native source packet changed')
    sealed=json.loads(path.read_text(encoding='utf-8-sig'))
    for entry in sealed['files']:
        source=path.parent/entry['path']
        if source.stat().st_size!=entry['bytes'] or digest(source)!=entry['sha256']:
            raise ValueError('frozen native source/build/runtime differs: '+entry['path'])
    for entry in packet['tools']:
        source=Path(entry['path'])
        if digest(source)!=entry['sha256']: raise ValueError('bench tool differs from reviewed packet')
    encoders=subprocess.run([packet['ffmpeg'],'-hide_banner','-encoders'],capture_output=True,text=True)
    if encoders.returncode or 'libvpx-vp9' not in encoders.stdout:
        raise ValueError('CPU VP9 encoder unavailable; no native arm started')

def validate_arm(result, packet, kind, nr):
    enabled=nr=='on'
    expected={'nativeCreate':1,'nativeEval':240,'nrCreate':int(enabled),
              'nrEval':240 if enabled else 0,'actualCombines':238 if enabled else 0}
    if result['status']!='passed-'+kind or result['route']!=packet['route_contracts']['RR'] or result['nr_enabled']!=enabled:
        raise ValueError('native receipt does not match requested arm')
    identity=packet['identity']
    if any(result.get(k)!=v for k,v in identity.items()): raise ValueError('Core/input identity mismatch')
    if result['warm_frames']!=120 or result['observed_frames']!=120 or result['totalFrames']!=240 or result['counts']!=expected:
        raise ValueError('incomplete warmup, observations or actual model/combine counts')
    retired=result['retirement']
    if not retired['exactFenceAndSuccessfulReset'] or not retired['nativeReleased'] or retired['nrContextDestroyed']!=enabled:
        raise ValueError('actual model retirement/release failed')
    pins={e['name']:e['sha256'] for e in packet['runtime_files'] if e['name'].endswith('.dll')}
    if not enabled: pins.pop('nvngx_dlssnr.dll')
    if result['runtimes']!=pins: raise ValueError('actual loaded runtime/model identity mismatch')
    cap=result['capture']
    if cap['enabled']!=(kind=='capture'): raise ValueError('capture mode differs')
    if kind=='capture' and cap['files']!=[f'frame-{i:04d}.rgba16f' for i in range(120)]:
        raise ValueError('capture frame sequence missing or duplicated')
    perf_summary(result)

def media(result, run, ffmpeg):
    import numpy as np
    cap=result['capture']; width,height=cap['width'],cap['height']
    files=[run / p for p in cap['files']]
    if not files: raise ValueError('capture has no observed frames')
    pre,scale=cap['pre_exposure'],cap['exposure_scale']
    if pre<=0 or scale<=0: raise ValueError('invalid capture exposure')
    target=run/'preview.webm'
    cmd=[str(ffmpeg),'-hide_banner','-loglevel','error','-y','-f','rawvideo',
         '-pixel_format','rgb24','-video_size',f'{width}x{height}','-framerate',str(cap['simulation_fps']),
         '-i','pipe:0','-an','-c:v','libvpx-vp9','-deadline','good','-cpu-used','4','-crf','20','-b:v','0','-threads','2',
         '-pix_fmt','yuv420p',str(target)]
    p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        last=None
        for file in files:
            last=display_frame(file,width,height,pre,scale)
            p.stdin.write(last.tobytes())
        p.stdin.close(); error=p.stderr.read(); code=p.wait()
        if code: raise RuntimeError(error.decode(errors='replace'))
    finally:
        if p.stdin and not p.stdin.closed: p.stdin.close()
    png(run/'last-frame.png',last)
    # Fixed normalized regions, identical across all arms; no cherry picking.
    regions={'center':(.38,.25,.62,.65),'floor':(.25,.7,.75,.95),'shadow':(.05,.4,.35,.75)}
    for name,(x0,y0,x1,y1) in regions.items():
        crop=last[int(y0*height):int(y1*height),int(x0*width):int(x1*width)]
        png(run/f'crop-{name}.png',np.repeat(np.repeat(crop,2,0),2,1))
    return {'video':'preview.webm','image':'last-frame.png',
            'crops':[f'crop-{name}.png' for name in regions],
            'display_transform':'fixed: stored*exposureScale/preExposure, x/(1+x), sRGB',
            'interpolation':'crops: nearest x2', 'raw_retained':True}

def busy_processes():
    # Only known interfering applications. Never stop or kill any process.
    script=r"@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(HTGame|NRDSample|RTXPT|scene-bench|raw-nr-phase|dual-grid|nr_rr_identity|(?:.*-)?clang(?:\+\+)?|cl|ninja|ffmpeg)(\.exe)?$' } | Select-Object Name,ProcessId) | ConvertTo-Json -Compress"
    p=subprocess.run(['powershell','-NoProfile','-Command',script],capture_output=True,text=True)
    if p.returncode: raise RuntimeError('cannot check GPU/compile collisions')
    return p.stdout.strip() not in ('','[]')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--packet',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--kind',choices=('all','capture','perf'),default='all')
    ap.add_argument('--route',choices=('RR','SR'),default='RR')
    ap.add_argument('--execute',action='store_true')
    a=ap.parse_args(); packet=json.loads(a.packet.read_text(encoding='utf-8-sig'))
    verify_packet(packet)
    if a.route not in packet['supported_routes']: raise ValueError('route not admitted by frozen host')
    kinds=['capture','perf'] if a.kind=='all' else [a.kind]
    if any(k not in packet['supported_modes'] for k in kinds):
        raise ValueError('requested measurement mode is not implemented/admitted')
    exe=Path(packet['executable']['path'])
    if digest(exe)!=packet['executable']['sha256']: raise ValueError('host hash differs from reviewed packet')
    runtime=Path(packet['runtime_directory'])
    for entry in packet['runtime_files']:
        source=runtime/entry['name']
        if source.stat().st_size!=entry['bytes'] or digest(source)!=entry['sha256']:
            raise ValueError('runtime differs from reviewed packet')
    if not a.execute:
        print(json.dumps({'status':'prepared-not-run','route':a.route,'kind':a.kind}));return
    if busy_processes(): raise RuntimeError('游戏、其他测试或编译仍在运行；未启动本次对照。')
    a.out.mkdir(parents=True,exist_ok=False)
    suite={'schema':1,'started':time.time(),'packet_sha256':digest(a.packet),
           'route':a.route,'kind':a.kind,'runs':[],'status':'running'}
    save(a.out/'suite.json',suite)
    # Each arm starts in a fresh process with the same scene/seed/warmup.
    for kind in kinds:
        for nr in ('off','on'):
            if busy_processes(): raise RuntimeError('检测到其他测试或游戏，本次对照暂停；保留已有结果。')
            run=a.out/f'{kind}-nr-{nr}'; run.mkdir()
            for entry in packet['runtime_files']:
                shutil.copy2(runtime/entry['name'],run/entry['name'])
            shutil.copy2(exe,run/exe.name)
            cmd=[str((run/exe.name).resolve()),'--mode',kind,'--route',a.route,'--nr',nr,
                 '--frames','120','--warm','120','--out',str(run.resolve()),'--root-exclusive-gpu-go']
            with (run/'stdout.log').open('wb') as out,(run/'stderr.log').open('wb') as err:
                child=subprocess.Popen(cmd,cwd=run,stdout=out,stderr=err)
                # No forced timeout/kill: a failed retirement remains inspectable.
                code=child.wait()
            if code: raise RuntimeError(f'{kind}/NR {nr} failed ({code}); no retry')
            result=normalize_result(json.loads((run/'result.json').read_text()))
            validate_arm(result,packet,kind,nr)
            suite['runs'].append({'kind':kind,'nr':nr,'directory':run.name,
                                   'result_sha256':digest(run/'result.json')})
            save(a.out/'suite.json',suite)
    export_suite(suite,a.out,packet)

def export_suite(suite,output,packet):
    # All GPU work has finished before CPU image/video conversion.
    for entry in suite['runs']:
        run=output/entry['directory']; result=normalize_result(json.loads((run/'result.json').read_text()))
        if entry['kind']=='perf': entry['summary']=perf_summary(result)
        else: entry['media']=media(result,run,Path(packet['ffmpeg']))
    suite.update(status='pass',finished=time.time(),game_acceptance=False)
    save(output/'suite.json',suite)
    cards=[]
    for entry in suite['runs']:
        name=entry['directory']; label='NR 开启' if entry['nr']=='on' else 'NR 关闭'
        if entry['kind']=='capture':
            cards.append(f'<article><h2>{label}</h2><video controls loop src="{name}/preview.webm"></video>'+
                         ''.join(f'<img src="{name}/{c}" alt="相同区域放大图">' for c in entry['media']['crops'])+'</article>')
        else:
            s=entry['summary']; mib=1024**2
            phase_labels={'before_models':'模型创建前','before_model_create':'模型创建前','warm':'预热','observe':'稳定运行',
                          'after_retire':'完成退休','after_model_release':'释放模型后',
                          'after_pool_release':'释放场景后'}
            rows=''.join('<tr><td>'+html.escape(phase_labels.get(k,k))+f'</td><td>{v/mib:.1f} MiB</td></tr>'
                         for k,v in s['memory_phases'].items())
            cards.append(f'<article><h2>{label}：开销</h2><p>GPU 中位 {s["gpu_ms_median"]:.3f} ms；P95 {s["gpu_ms_p95"]:.3f} ms</p>'+
                         f'<p>串行宿主每帧中位 {s["serial_wall_ms_median"]:.3f} ms（含 CPU 校验、日志与等待，不作为游戏 FPS）</p>'+
                         f'<p>进程显存：采样峰值 {s["process_vram_sampled_peak_bytes"]/mib:.1f} MiB；稳定中位 {s["process_vram_stable_median_bytes"]/mib:.1f} MiB</p>'+
                         '<table>'+rows+'</table></article>')
    page='<!doctype html><meta charset="utf-8"><title>DLSS5 3D 对照</title><style>body{font:16px system-ui;max-width:1400px;margin:32px auto;background:#14171c;color:#eee}main{display:grid;grid-template-columns:1fr 1fr;gap:24px}article{background:#20252d;padding:20px;border-radius:12px}video,img{max-width:100%;display:block;margin:12px 0}p{line-height:1.6}</style><h1>DLSS5 3D 对照</h1><p>开发实验：低分 NR→原生 RR；产品默认路线仍为 RR→NR。</p><p>固定镜头、输入和预热；画质采集与开销测量分别运行。GPU 时间与进程显存仅代表此测试场景；显存峰值是采样峰值，不能当作游戏帧率或瞬时最高分配。</p><main>'+''.join(cards)+'</main>'
    (output/'report.html').write_text(page,encoding='utf-8')
    print(str(output/'report.html'))

if __name__=='__main__':
    try: main()
    except Exception as exc:
        print(str(exc),file=sys.stderr);raise SystemExit(1)
