"""Explicit root GO: one new v3 J history, effective-edit gate, pinned v1 controls."""
from pathlib import Path
import argparse,json,shutil,subprocess,sys,traceback
import numpy as np
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent;BASE=ROOT.parent;OUTPUT=BASE/'two-rr-approx-v3-r1'
V1=BASE/'two-rr-approx-v1-r1';V1_REVIEW=BASE/'two-rr-approx-v1-r1-cpu-review'
V1_SHA='97475bee24b6cf47828b0fc2f88e8074f868b6f6eba9f66c625d1468a5fe492f'
V1_REVIEW_SHA='32630ebbb699c0ec938144913529095554202c8b910461ac96c0b1483971348e'
sys.path.insert(0,str(BASE/'raw-nr-execution-v2-r1'))
from runner_common import sha,save,need,verify_manifest,preflight,RASTER,RUNTIME,TUPLE,process_guard,launch,seal_output
from two_rr_receipt import inspect as inspect_two,NR_SHA
def signature(path):
 literal=str(path).replace("'","''");code="$p='"+literal+"';$s=Get-AuthenticodeSignature -LiteralPath $p;$f=Get-Item -LiteralPath $p;[pscustomobject]@{path=$p;status=$s.Status.ToString();fileVersion=$f.VersionInfo.FileVersion;bytes=$f.Length}|ConvertTo-Json -Compress"
 query=shutil.which('pwsh');need(bool(query),'existing Authenticode query tool')
 result=subprocess.run([query,'-NoProfile','-Command',code],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
 need(result.returncode==0,'actual Authenticode query');data=json.loads(result.stdout)
 need(data['status']=='NotSigned' and data['bytes']==165844992 and sha(path)==NR_SHA,'exact prior actual NotSigned a2d16 NR source/copy')
 data['sha256']=NR_SHA;return data
def run():
 parser=argparse.ArgumentParser();parser.add_argument('--root-two-rr-go',action='store_true');args=parser.parse_args()
 need(args.root_two_rr_go,'explicit new root GPU GO required; preparation alone never starts a device')
 own=verify_manifest(ROOT);inherited=preflight();old=verify_manifest(BASE/'raw-nr-one-rr-host-v1')
 need(old['sha256']==json.loads((ROOT/'PACKET_IDENTITY.json').read_text())['inherited_oneRR_manifest_sha256'],'same frozen functional oneRR packet')
 raster=verify_manifest(RASTER,'38a52409a5ffb4a59fdbcc1feeeeece743a09abafb2cc105b1ead8223a0d5245')
 v1=verify_manifest(V1,V1_SHA);v1_review=verify_manifest(V1_REVIEW,V1_REVIEW_SHA)
 prior=json.loads((V1/'runs.json').read_text());need(set(prior['histories'])=={'baseline','variant'},'only the two completed exact v1 controls may be inherited')
 review=json.loads((V1_REVIEW/'SUMMARY.json').read_text());need(review['completed_histories']==['baseline','variant'] and review['exact_retired_recordings_actual']==68 and review['twoRR_frame_recording_submitted']==False,'exact independent v1 review distinguishes failed unsubmitted arm')
 recomputed=json.loads((V1_REVIEW/'RECOMPUTED_EXISTING_HISTORIES.json').read_text());need(all(prior['histories'][name]['receipt']==recomputed[name] for name in ['baseline','variant']),'inherited original actual receipts equal independently recomputed CPU receipts')
 need(not OUTPUT.exists(),'fresh exact output only, no overwrite/retry')
 need(shutil.disk_usage(BASE).free>8*1024**3,'one bounded new history and runtime-copy space')
 source_signature=signature(RUNTIME/'nvngx_dlssnr.dll');OUTPUT.mkdir()
 save(OUTPUT/'PREFLIGHT.json',{'own':own,'inherited':inherited,'frozen_functional_oneRR':old,'raster':raster,'NR_signature':source_signature,'v1_actual':v1,'v1_independent_review':v1_review})
 state={'status':'running','history_order':['two-rr-approx'],'inherited_control_histories':['baseline','variant'],'attempts':0,'retry':False,'GPU_authorization':'explicit-root-two-rr-go','maximum_processes':1,'maximum_frames_per_process':32}
 runs={'schema':'one-new-v3-J-twoRR-with-explicit-pinned-v1-controls','source_manifest':own,'input_dataset_sha256':sha(RASTER/'dataset.json'),'histories':{name:prior['histories'][name] for name in ['baseline','variant']},'inherited_controls':{'histories':['baseline','variant'],'actual':v1,'independent_review':v1_review,'actual_EXE_sha256':'1158ef0e3adf42e827715b7c909553ca5d7511d40e77edc1ba8b822c160a9716','binary_identity_differs_from_v3':True,'old_results_unchanged':True},'quality_performance_game':'unknown'}
 save(OUTPUT/'validation.json',state);save(OUTPUT/'runs.json',runs);dataset=json.loads((RASTER/'dataset.json').read_text())
 try:
  for mode in state['history_order']:
   directory=OUTPUT/mode;directory.mkdir();fixed={**TUPLE,**({'nvngx_dlssnr.dll':NR_SHA} if mode!='baseline' else {})}
   for name in ['raw-nr-phase.exe','capture-decode.cso','ratio-before-shift.cso']:
    shutil.copy2(ROOT/'host'/name,directory/name);need(sha(directory/name)==sha(ROOT/'host'/name),'actual immutable host/shader copy')
   for name,digest in fixed.items():shutil.copy2(RUNTIME/name,directory/name);need(sha(directory/name)==digest,'exact actual copied native/NR tuple')
   actual_signature=signature(directory/'nvngx_dlssnr.dll') if mode!='baseline' else None
   if actual_signature:save(directory/'actual-NR-signature.json',actual_signature)
   pin=directory/'runtime.sha256';pin.write_text(''.join(digest+' '+name+'\n' for name,digest in fixed.items()),encoding='ascii')
   save(directory/'ACTUAL_COPIES.json',{p.name:{'sha256':sha(p),'bytes':p.stat().st_size} for p in directory.iterdir() if p.is_file()})
   process_guard(directory/'before-processes.json')
   command=[str(directory/'raw-nr-phase.exe'),mode,str(directory/'nrchain_nvngx.dll'),str(pin),'--root-two-rr-go']
   state['attempts']+=1;state['active_history']=mode;save(OUTPUT/'validation.json',state)
   process=launch(command,directory,directory/'host.log',directory/'process.json',600)
   receipt=inspect_two(process,directory,dataset)
   composition=receipt['composition_diagnostics']
   save(OUTPUT/'COMPOSITION_COVERAGE.json',{'schema':'v3-actual-vs-oracle-effective-edit-coverage','frames':composition,'warmup_totals':{key:sum(row['composition'][key] for row in composition[:16]) for key in ['accepted','identity','refused']},'observed_totals':{key:sum(row['composition'][key] for row in composition[16:]) for key in ['accepted','identity','refused']},'observed_predicted_RGB_changed':sum(row['oracle_predicted_RGB_changed'] for row in composition[16:]),'observed_actual_RGB_changed':sum(row['actual_RGB_changed'] for row in composition[16:]),'every_observed_frame_nonzero_predicted_and_actual_RGB_edits':receipt['every_observed_frame_nonzero_predicted_and_actual_RGB_edits'],'scope':'accepted counts are CPU oracle domain; actual changed bits and legal value/unchanged exclusion checks are independently reported; no new GPU write-mask instrumentation'})
   runs['histories'][mode]={'process':process,'receipt':receipt,'actual_NR_signature':actual_signature};save(OUTPUT/'runs.json',runs)
  comparison=['baseline','variant','two-rr-approx'];receipts={name:runs['histories'][name]['receipt'] for name in comparison}
  need(all(receipts[name]['bound_raw_contract_sha256']==receipts['baseline']['bound_raw_contract_sha256'] for name in comparison),'new v3 and explicitly inherited v1 normalized raw input/guide/jitter/scalar/matrix contracts match')
  # NonColor SHA includes actual resource/CPU addresses and is compared only
  # within each process around the Color transaction. Cross-process comparison
  # uses the normalized bound_raw_contract_sha256 above.
  diagnostics=[]
  for i in range(32):
   arrays={name:np.fromfile(receipts[name]['one_RR_outputs'][i]['file'],dtype='<f2').reshape(1080,1920,4)[:,:,:3].astype(np.float64) for name in comparison}
   comparisons={name:{'mean_abs_RGB_difference':float(np.abs(arrays[name]-arrays['baseline']).mean()),'max_abs_RGB_difference':float(np.abs(arrays[name]-arrays['baseline']).max())} for name in ['variant','two-rr-approx']}
   diagnostics.append({'frame':i,'observe':i>=16,'relative_to_baseline':comparisons,'quality_gate':None})
  save(OUTPUT/'OUTPUT_DIAGNOSTICS.json',{'scope':'diagnostic only, not quality/performance/game acceptance','frames':diagnostics})
  state.update({'status':'passed-bounded-twoRR-v3-effective-edit-with-pinned-v1-controls-functional-only','new_histories':1,'inherited_control_histories':['baseline','variant'],'new_native_parent_Create_Eval':[1,32],'new_private_child_Create_Eval':[1,32],'new_NR_Init_Create_Eval':[1,1,32],'new_exact_model_recordings':34,'inherited_control_exact_model_recordings':66,'comparison_set_exact_model_recordings':100,'every_observed_frame_nonzero_predicted_and_actual_RGB_edits':True,'raw_alpha_edit_domain_u16':0x3a00,'raw_guides':'explicitly-approximate','game_route_enabled':False,'quality_performance_game':'unknown'})
  save(OUTPUT/'validation.json',state);seal_output(OUTPUT);print(json.dumps({'status':state['status'],'manifest_sha256':sha(OUTPUT/'MANIFEST.json')}))
 except Exception as error:
  state.update({'status':'STOP-first-fixed-contract-or-process-failure','reason':str(error),'traceback':traceback.format_exc(),'retry':False,'quality_performance_game':'unknown'})
  save(OUTPUT/'validation.json',state);save(OUTPUT/'runs.json',runs);seal_output(OUTPUT);raise
if __name__=='__main__':run()
