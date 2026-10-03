"""Actual Core private SR with native Feature1/13 and optional real Feature18 chain."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,time
from temporal_test_objects import objects_for
from private_sr_cost_report import parse_metrics, summarize_cost
from temporal_cost_report import summarize_temporal_cost
ROOT=Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def verify_product_capture(receipt_path,requested,directory):
    """Verify pixels produced by actual Core Flight; no timing or game claim."""
    def need(ok,message):
        if not ok:raise ValueError('Product capture receipt: '+message)
    receipt=json.loads(receipt_path.read_text(encoding='utf-8'))
    need(receipt['schema']=='actual-core-product-capture-v1' and receipt['requestedFrames']==requested,'host identity/request')
    need(receipt['producer']=='actual-Core-Capture-Finish-Flight-worker' and receipt['nativeFeature']==receipt['privateFeature']==1 and receipt['game'] is False,'actual Feature1 producer')
    need(receipt['workerState']==4 and all(receipt[k]==0 for k in ('leases','residentBytes','users','activeRuns')),'CPU worker jobs drained after owner retirement')
    need(0<receipt['attempts']<=64 and 0<receipt['sessions']<=8 and 0<receipt['residentCapBytes']<=8<<30 and receipt['diskReservedBytes']<=8<<30,'process budgets')
    need(receipt['timingExcluded'] is True and receipt['performanceSample']=='capture_period_not_performance_sample','capture timing excluded')
    folder=Path(receipt['folder']).resolve(strict=True)
    need(folder.is_relative_to(directory.resolve()) and folder.is_dir(),'output folder contained in explicit directory')
    capture=json.loads((folder/'capture.json').read_text())
    need(capture['producer']=='product-Capture-Finish-exact-Flight' and capture['defaultEnabled'] is False and capture['timingExcluded'] is True,'product capture manifest')
    need(capture['maxAttempts']==64 and capture['maxSessions']==8 and capture['residentCapBytes']==capture['diskCapBytes']==8<<30 and capture['physicalReserveBytes']==4<<30,'capture limits')
    summary_path=folder/('session-%02u.json'%receipt['session']);summary=json.loads(summary_path.read_text())
    need(summary['requestedFrames']==summary['seenFrames']==requested and summary['frameMask']==(1<<requested)-1 and summary['continuousPixelsComplete'] is True and summary['anyFailedFrame'] is False and summary['status']=='complete','complete continuous session, without failed frames')
    frame_paths=sorted(folder.glob('attempt-*-frame.json'));need(len(frame_paths)<=64,'bounded frame metadata')
    need(len(list(folder.glob('session-*.json')))<=8,'bounded session metadata')
    selected=[];hashes={str(receipt_path):sha(receipt_path),'capture.json':sha(folder/'capture.json'),summary_path.name:sha(summary_path)}
    for path in frame_paths:
        frame=json.loads(path.read_text());hashes[path.name]=sha(path)
        if frame['session']==receipt['session']:selected.append(frame)
    selected.sort(key=lambda frame:frame['sessionIndex'])
    need(len(selected)==requested and [f['sessionIndex'] for f in selected]==list(range(1,requested+1)),'exact session frame indices')
    first=selected[0];source=first['source'];need(source==summary['sourceFirst'] and all(source),'source identity')
    roles=('native-depth','native-motion','native-base','nr-candidate','carrier','tail','private-reconstruction','final','normalized-depth','normalized-motion')
    formats=(41,16,10,10,10,2,10,10,41,16);sizes={41:4,16:8,10:8,2:16}
    for index,frame in enumerate(selected):
        need(frame['schema']=='dlss5-private-sr-planes-v1' and frame['requestedFrames']==requested and frame['sessionFirstSource']==source,'frame schema/session source')
        need(frame['source']==[source[0],source[1],source[2]+index] and (index==0 or frame['previousCapturedSource']==selected[index-1]['source']),'continuous same-stream source sequence')
        need(frame['identity']==first['identity'] and frame['identity']['nativeFeature']==1,'constant owner/configuration/encoding/native identity')
        need(frame['pixelsValid'] is True and frame['savedPlaneMask']==1023 and frame['failure']=='none' and frame['fullPrivateNGXInputs'] is True,'all ten full private inputs/outputs saved')
        need(frame['reset']['actualBind'] is True and frame['reset']['privateResult']==1,'actual successful private Feature1 bind')
        need(all(isinstance(frame['reset'][k],bool) for k in ('nativeKnown','native','sourceGap','normalizedGuideRequest','actualBindReset')),'separate native and actual reset metadata')
        proof=frame['completion'];recording=frame['recording']
        need(proof['submitted'] is True and proof['recordingReleased'] is True and proof['queue']==recording['queue'] and all(recording.values()) and proof['fence']>0 and 0<proof['target']<=proof['observed']<(1<<64)-1,'exact recording release plus same queue completed fence')
        need(frame['timingExcluded'] is True and frame['performanceSample']=='capture_period_not_performance_sample' and frame['supportPolicy']=='per-channel-stored-carrier-neutral','capture period/support policy')
        need(frame['derivations']['spatialReference']=='not-captured' and frame['nativeNGXReplayComplete'] is False,'truthful spatial/native replay scope')
        budget=frame['budgets'];need(0<budget['residentBytes']<=budget['frameCap']==512<<20 and budget['residentBytes']<=budget['sessionResidentCap']<=receipt['residentCapBytes'] and budget['processResidentCap']==budget['processDiskCap']==8<<30,'actual committed allocation bounds')
        planes=frame['planes'];need(tuple(p['role'] for p in planes)==roles,'full plane roles')
        guides=frame['guides'];need(all(guides[k]==first['guides'][k] for k in ('render','work','output','phase','rasterConvention','nativeMVScaleRenderPx','normalizerMVScaleWorkPx')),'constant guide contract')
        need(guides['actualBindJitterWorkPx']==[0,0] and guides['actualBindMVScaleWorkPx']==[1,1] and frame['exposure']['actualPrivateBindPreExposure']==frame['exposure']['actualPrivateBindExposureScale']==1 and frame['exposure']['actualPrivateBindExposureTexture'] is None,'actual neutral private bind metadata')
        for role,fmt,plane in zip(roles,formats,planes):
            need(plane['copied'] is True and plane['saved'] is True and plane['resource']>0 and plane['resourceFormat']==plane['viewFormat']==plane['copyFormat']==fmt,'actual native format/copy '+role)
            width,height=plane['extent'];x,y,w,h=plane['subrect'];extent=guides['render'] if role in roles[:2] else guides['output'] if role in roles[2:4] or role in ('private-reconstruction','final') else guides['work']
            need([width,height]==extent and [x,y,w,h]==[0,0,*extent] and plane['mip']==plane['plane']==0,'full physical plane extent/subrect '+role)
            need(plane['rows']==height and plane['rowBytes']==width*sizes[fmt] and plane['fileBytes']==height*plane['rowBytes'] and plane['gpuRowPitch']>=plane['rowBytes'] and plane['fileLayout']=='tight-native-rows-little-endian','tight native rows '+role)
            file=(folder/plane['file']).resolve(strict=True);need(file.is_relative_to(folder) and file.is_file() and file.stat().st_size==plane['fileBytes'],'exact plane bytes '+role)
            hashes[file.name]=sha(file)
    disk=sum(f.stat().st_size for f in folder.iterdir() if f.is_file());need(disk<=8<<30,'actual folder disk bound')
    return {'status':'passed','receipt':receipt,'summary':summary,'actual_disk_bytes':disk,'continuous_frames':requested,'full_planes':requested*10,'files_sha256':hashes,'performance':'excluded','game_acceptance':'unjudged','quality':'unjudged','native_ngx_replay_complete':False,'spatial_reference':'not-captured'}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','compiler','reshade','sr-runtime','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--reshade-runtime',type=Path,help='Optional actual ReShade d3d12.dll; isolated proxy/raw device compatibility test, no Core addon')
    p.add_argument('--nr-runtime',type=Path)
    p.add_argument('--rr-runtime',type=Path)
    p.add_argument('--layers',type=int,choices=range(6),default=0)
    p.add_argument('--percent',type=int,choices=(25,50,100),default=50)
    p.add_argument('--native-rr',action='store_true')
    p.add_argument('--private-sr-off',action='store_true',help='Isolated native/ordinary NR control; performs no private Feature1 work')
    p.add_argument('--spatial-mode',type=int,choices=(0,1,2),default=0,help='Post-final B/Z spatial experiment: 0 off, 1 guided four-tap area delta, 2 local 3x3 B-guided mean')
    p.add_argument('--temporal',choices=('off','amd-of','amd-of-low','native-mv-refusal','amd-of-low-later','amd-of-later'),default='off',
                   help='Actual PostRr Core temporal route; private SR must be off')
    p.add_argument('--odd',action='store_true',help='Add one pixel to each native render/output dimension; N uses floor percent')
    p.add_argument('--compile-only',action='store_true')
    p.add_argument('--frames',type=int,help='Final Z frames, 2..256; default 6, or enough for explicit capture mode')
    p.add_argument('--capture-frames',type=int,choices=(16,32),default=0,help='Explicit actual Core full-plane capture; all timing samples excluded')
    p.add_argument('--capture-dir',type=Path,help='Explicit evidence directory; defaults to OUTPUT/product-capture in capture mode')
    a=p.parse_args()
    if a.frames is None:a.frames=max(32,a.capture_frames+1) if a.capture_frames else 6
    if not 2<=a.frames<=256:p.error('--frames must be 2..256')
    if a.capture_frames and (a.private_sr_off or a.native_rr or a.temporal!='off' or a.spatial_mode or a.frames<=a.capture_frames):
        p.error('--capture-frames requires private Feature1, temporal/spatial off, and --frames > capture frames')
    if a.capture_dir and not a.capture_frames:p.error('--capture-dir requires --capture-frames')
    if a.layers and not a.nr_runtime:p.error('--layers requires explicit --nr-runtime')
    if a.native_rr and not a.rr_runtime:p.error('--native-rr requires explicit --rr-runtime')
    if a.temporal!='off' and (not a.native_rr or not a.layers or not a.private_sr_off):
        p.error('--temporal requires --native-rr, --layers >= 1, and --private-sr-off')
    if a.spatial_mode and (not a.private_sr_off or a.temporal!='off'):
        p.error('--spatial-mode requires --private-sr-off and --temporal off')
    if a.temporal in ('amd-of-low-later','amd-of-later') and a.layers<2:
        p.error('later-layer temporal requires at least two real NR layers')
    temporal_case={'off':0,'amd-of':1,'amd-of-low':2,'native-mv-refusal':3,'amd-of-low-later':4,'amd-of-later':5}[a.temporal]
    core=a.core.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    child_env=os.environ.copy()
    for key in ('DLSS5_PRIVATE_SR_EVIDENCE_FRAMES','DLSS5_PRIVATE_SR_EVIDENCE_DIR'):child_env.pop(key,None)
    capture_dir=None
    if a.capture_frames:
        capture_dir=(a.capture_dir or out/'product-capture').resolve();capture_dir.mkdir(parents=True,exist_ok=True)
        child_env.update(DLSS5_PRIVATE_SR_EVIDENCE_FRAMES=str(a.capture_frames),DLSS5_PRIVATE_SR_EVIDENCE_DIR=str(capture_dir))
    source=core/'src/nr_before_sr.cpp';manifest_path=core.parent/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    for rel,digest in manifest['files'].items():
        f=(manifest_path.parent/rel).resolve()
        if not f.is_relative_to(manifest_path.parent.resolve()) or sha(f)!=digest:raise ValueError('Generated source identity mismatch: '+rel)
    original=ROOT/'experiments/nr-edit/tests/private_sr_ngx_host.cpp'
    host=original.read_text(encoding='utf-8').split('void NeutralAndTinyComposition(',1)[0]+'\n}\n'
    # Preserve the existing host helpers verbatim, excluding its independent
    # experiment Run/main; the new fixture calls the actual product entry points.
    fixture=out/'private_sr_product_real.cpp';shutil.copy2(ROOT/'tests/private_sr_product_real.cpp',fixture)
    wrapper=out/'host.cpp';wrapper.write_text('#define NR_PRIVATE_SR_CORE "'+source.as_posix()+'"\n'+host+
        '#include "'+fixture.as_posix()+'"\n',encoding='utf-8')
    build=core/'build';objects=[build/'obj'/n for n in ('buffer.o','hook.o','trampoline.o','hde64.o','nr_dual_spatial_d3d12.o','nr_face_detector.o','nr_face_worker.o','nr_aux_shader_runtime.o')]+objects_for(source)
    report={'schema':'actual-product-private-sr-v2','result':'running','gpu_execution':'not-run','compile_only':a.compile_only,'private_sr_enabled':not a.private_sr_off,'source_commit':manifest['source_commit'],
        'generated_source':str(source),'generated_source_sha256':sha(source),'manifest_path':str(manifest_path),'manifest_sha256':sha(manifest_path),'fixture_sha256':sha(fixture),
        'host_helpers_sha256':sha(original),'wrapper_sha256':sha(wrapper),'compiler_sha256':sha(a.compiler),'commands':[],
        'linked_objects':{str(o):sha(o) for o in objects},'game':False,'feature18':bool(a.layers),'nr_layers':a.layers,'frames':a.frames,'percent':a.percent,'native_feature':13 if a.native_rr else 1,
        'native_render_extent':[x+int(a.odd) for x in ([1280,720] if a.native_rr else [320,192])],
        'native_output_extent':[x+int(a.odd) for x in ([1920,1080] if a.native_rr else [640,384])],
        'temporal_case':a.temporal,'spatial_mode':a.spatial_mode,'capture_frames':a.capture_frames,'capture_directory':str(capture_dir) if capture_dir else None,
        'timing':'CPU recording includes native SR; GPU timestamps isolate guides, complete NR-chain work (including codec and final Z copy), and private SR; none is a complete game frame. Synthetic native frames plus optional real 1-5 layer Feature18; not a game benchmark.'}
    def save(): (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    def run(name,args,timeout):
        start=time.monotonic();r=subprocess.run(list(map(str,args)),cwd=out,env=child_env,capture_output=True,timeout=timeout)
        (out/(name+'.log')).write_bytes(r.stdout+r.stderr);report['commands'].append({'name':name,'args':list(map(str,args)),'exit':r.returncode,'seconds':time.monotonic()-start})
        save();print((r.stdout+r.stderr).decode('utf-8','replace')[-9000:],flush=True);return r.returncode
    for s,n in ((build/'nrchain_nvngx.dll','nrchain_nvngx.dll'),(a.sr_runtime,'nvngx_dlss.dll')):shutil.copy2(s,out/n)
    for runtime,name in [(a.nr_runtime,'nvngx_dlssnr.dll'),(a.rr_runtime,'nvngx_dlssd.dll')]:
        if runtime:shutil.copy2(runtime,out/name)
    if a.reshade_runtime:
        shutil.copy2(a.reshade_runtime.resolve(strict=True),out/'d3d12.dll')
    report['reshade_proxy_mode']=bool(a.reshade_runtime)
    report['object_domains']=({'init':'proxy device','native_parent':'proxy command list','core_resources':'raw device','private_sr':'actual product live original alias; raw Core recording and resources','feature18':'proxy command list','submission_and_reset':'raw list with exact Core receipts'} if a.reshade_runtime else {'all':'native raw D3D12'})
    report['core_addon_loaded']=False
    report['runtime_sha256']={f.name:sha(f) for f in out.glob('*.dll')};save()
    report['runtime_sources']={name:{'path':str(path.resolve(strict=True)),'sha256':sha(path)} for name,path in [('bridge',build/'nrchain_nvngx.dll'),('sr',a.sr_runtime),('nr',a.nr_runtime),('rr',a.rr_runtime)] if path}
    if a.capture_frames:report['timing']='capture_period_not_performance_sample; all host and production timing excluded'
    flags=['-std=c++20','-O1','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN64','-DNR_FACE_BACKEND=1','-DNR_CODEC_PRECOMPILED=1','-DNR_AUX_PRECOMPILED=1','-DNR_AUX_TIMING=1',
        '-ffunction-sections','-fdata-sections','-static','-municode','-Wl,--gc-sections','-I'+str(build),'-I'+str(core/'third_party/minhook/include'),'-I'+str(a.reshade/'include'),'-I'+str(a.reshade/'deps/imgui')]
    if a.reshade_runtime:flags.append('-DNR_TEST_RESHADE_RUNTIME=1')
    try:
        result=run('compile',[a.compiler,*flags,wrapper,*objects,'-o',out/'product-real.exe','-lkernel32','-luser32','-ld3d12','-ldxgi','-ldxguid','-lole32','-luuid','-ld3dcompiler','-lshell32','-lbcrypt'],240)
        if not result and not a.compile_only:
            report['gpu_execution']='running';save()
            result=run('real-feature1',[out/'product-real.exe',out/'nrchain_nvngx.dll',a.layers,a.percent,int(a.native_rr),a.frames,int(a.odd),int(not a.private_sr_off),temporal_case,a.spatial_mode,a.capture_frames],240)
            report['gpu_execution']='passed' if not result else 'failed'
        report['result']='passed' if not result else 'failed'
        if (out/'real-feature1.log').is_file():
            log=(out/'real-feature1.log').read_text(encoding='utf-8',errors='replace')
            report['metrics']=parse_metrics(log)
            if a.capture_frames:
                report['cost']=report['temporal_cost']={'status':'capture_excluded','reason':'capture_period_not_performance_sample; complete capture run is excluded'}
                if not result:report['product_capture']=verify_product_capture(out/'actual-core-product-capture.json',a.capture_frames,capture_dir)
            else:
                report['cost']=summarize_cost(report['metrics'],a.frames,a.layers,not a.private_sr_off) if not result else {'status':'insufficient','reason':'host execution failed; partial timings are not a cost result'}
                report['temporal_cost']=summarize_temporal_cost(report['metrics'],a.frames,a.temporal) if not result else {'status':'insufficient','reason':'host execution failed'}
        else: report['cost']={'status':'not_run','reason':'compile-only or host was not started'}
        if 'temporal_cost' not in report:report['temporal_cost']={'status':'not_run','reason':'compile-only or host was not started'}
        report['captures_sha256']={f.name:sha(f) for pattern in ('*.fp16','*.fp32') for f in out.glob(pattern)}
        capture_manifest=out/'actual-core-same-frame.json'
        if capture_manifest.is_file():
            report['same_frame']=json.loads(capture_manifest.read_text())
            report['same_frame_manifest_sha256']=sha(capture_manifest)
        report['cost_sample_exclusion']='Entire explicit capture run excluded; capture_period_not_performance_sample' if a.capture_frames else 'Exclude reset/creation, the first eight private inference outputs (and matching ordinary output indices), incomplete outputs, final diagnostic capture, and malformed rows; require at least 16 remaining complete outputs.'
        report['binary_sha256']=sha(out/'product-real.exe') if (out/'product-real.exe').is_file() else None
        save();return result
    except Exception as e:
        report['result']='failed';report['error']=str(e)
        if report['gpu_execution']=='running':report['gpu_execution']='failed'
        save();raise
if __name__=='__main__':raise SystemExit(main())
