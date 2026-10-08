"""Root-only bounded 3D short gate. Preparation and imports never start a device."""
from pathlib import Path
import argparse,json,shutil,subprocess,sys,traceback
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent;OUTPUT=ROOT.parent/'minimal-scene-two-rr-v1-r1'
sys.path.insert(0,str(Path(r'<PRIVATE_PATH>
from runner_common import need,sha,save,verify_manifest,preflight,TUPLE,RUNTIME,seal_output
from runner_support import process_guard,launch,RetainedProcessTimeout
from two_rr_scene_receipt import inspect,NR_SHA
def signature(path):
 literal=str(path).replace("'","''");script="$p='"+literal+"';$s=Get-AuthenticodeSignature -LiteralPath $p;$f=Get-Item -LiteralPath $p;[pscustomobject]@{status=$s.Status.ToString();fileVersion=$f.VersionInfo.FileVersion;bytes=$f.Length}|ConvertTo-Json -Compress"
 query=shutil.which('pwsh');need(bool(query),'existing Authenticode query tool')
 result=subprocess.run([query,'-NoProfile','-Command',script],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
 need(result.returncode==0,'actual runtime signature query');row=json.loads(result.stdout)
 need(row['status']=='NotSigned' and row['bytes']==165844992 and sha(path)==NR_SHA,'exact prior actual NotSigned NR tuple');return {**row,'sha256':NR_SHA}
def run():
 parser=argparse.ArgumentParser();parser.add_argument('--root-exclusive-gpu-go',action='store_true');args=parser.parse_args()
 need(args.root_exclusive_gpu_go,'explicit root exclusive GPU GO required before any device/output')
 own=verify_manifest(ROOT);sourceguard=preflight();need(not OUTPUT.exists(),'fresh exact output only, never overwrite or retry')
 need(shutil.disk_usage(ROOT.parent).free>4*1024**3,'bounded three-frame readback/runtime space')
 source_signature=signature(RUNTIME/'nvngx_dlssnr.dll');OUTPUT.mkdir()
 state={'schema':'bounded-same-source-3D-twoRR-v1','status':'running','attempts':0,'retry':False,'maximum_processes':1,'warm':2,'observe':1,'total_frames':3,'maximum_parent_Create_Eval':[1,3],'maximum_child_Create_Eval':[1,3],'maximum_NR_Init_Create_Eval':[1,1,3],'maximum_precise_recordings':5,'expected_scene_producer_dispatches':4,'quality_performance_game':'unknown'}
 save(OUTPUT/'PREFLIGHT.json',{'own_packet':own,'frozen_sourceguard':sourceguard,'NR_source_signature':source_signature})
 save(OUTPUT/'validation.json',state)
 try:
  directory=OUTPUT/'two-rr-approx';directory.mkdir()
  for name in ['scene-two-rr.exe','scene.cso','capture-decode.cso','ratio-before-shift.cso']:
   shutil.copy2(ROOT/'host'/name,directory/name);need(sha(directory/name)==sha(ROOT/'host'/name),'actual immutable host/shader copy')
  fixed={**TUPLE,'nvngx_dlssnr.dll':NR_SHA}
  for name,digest in fixed.items():shutil.copy2(RUNTIME/name,directory/name);need(sha(directory/name)==digest,'exact copied native/NR tuple')
  actual_signature=signature(directory/'nvngx_dlssnr.dll');save(directory/'actual-NR-signature.json',actual_signature)
  (directory/'runtime.sha256').write_text(''.join(digest+' '+name+'\n' for name,digest in fixed.items()),encoding='ascii')
  save(directory/'ACTUAL_COPIES.json',{p.name:{'sha256':sha(p),'bytes':p.stat().st_size} for p in directory.iterdir() if p.is_file()})
  process_guard(directory/'before-processes.json')
  command=[str(directory/'scene-two-rr.exe'),'--mode','functional','--route','RR','--nr','on','--warm','2','--frames','1','--out',str(directory),'--root-exclusive-gpu-go']
  state['attempts']=1;save(OUTPUT/'validation.json',state)
  process=launch(command,directory,directory/'host.log',directory/'process.json',180)
  receipt=inspect(process,directory,None);save(OUTPUT/'receipt.json',receipt)
  save(OUTPUT/'COMPOSITION_COVERAGE.json',{'frames':receipt['composition_diagnostics'],'observed':receipt['composition_diagnostics'][2],'scope':'CPU accepted domain vs actual changed bits; no additional UAV write mask'})
  state.update({'status':'passed-bounded-same-source-3D-twoRR-effective-edit-functional-only','actual_parent_Create_Eval':[1,3],'actual_child_Create_Eval':[1,3],'actual_NR_Init_Create_Eval':[1,1,3],'actual_precise_recordings':5,'actual_scene_producer_dispatches':4,'rawProducerAlphaU16':0x3c00,'rawGuides':'explicitly-approximate','observed_nonzero_predicted_and_actual_RGB_edits':True,'game_route_enabled':False})
  save(OUTPUT/'validation.json',state);seal_output(OUTPUT);print(json.dumps({'status':state['status'],'manifest_sha256':sha(OUTPUT/'MANIFEST.json')}))
 except RetainedProcessTimeout as error:
  state.update({'status':'STOP-timeout-process-retained-unsealed','reason':str(error),'retained_PID':error.record['PID'],'retry':False,'no_kill':True,'manifest_not_sealed_while_child_can_write':True})
  save(OUTPUT/'validation.json',state);raise
 except Exception as error:
  state.update({'status':'STOP-first-fixed-contract-or-process-failure','reason':str(error),'traceback':traceback.format_exc(),'retry':False})
  save(OUTPUT/'validation.json',state);seal_output(OUTPUT);raise
if __name__=='__main__':run()
