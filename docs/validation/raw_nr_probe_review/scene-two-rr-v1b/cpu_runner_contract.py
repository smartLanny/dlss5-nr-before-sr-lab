"""Exercise actual private launcher timeout with a fake live child; no process/GPU."""
from pathlib import Path
import ast,json,re,sys
sys.dont_write_bytecode=True
import runner_support as support
ROOT=Path(__file__).resolve().parent;PROOF=ROOT/'cpu-no-kill-proof'
assert not PROOF.exists();PROOF.mkdir()
tree=ast.parse((ROOT/'runner_support.py').read_text());assert not any(isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr in ('kill','terminate','send_signal','wait') for n in ast.walk(tree))
pattern=next(n.value.value for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='pattern' for t in n.targets))
names=['scene-two-rr.exe','scene-bench.exe','x86_64-w64-mingw32-clang++.exe','clang.exe','clang-cl.exe','HTGame.exe','raw-nr-phase.exe','ffmpeg.exe']
assert all(re.search(pattern,name,re.I) for name in names)
class Child:
 pid=12345;returncode=None;termination_calls=0
 def poll(self):return None
 def kill(self):self.termination_calls+=1;raise AssertionError('must never terminate fake live child')
 def terminate(self):self.termination_calls+=1;raise AssertionError('must never terminate fake live child')
 def wait(self):raise AssertionError('must not wait for fake still-live child')
child=Child();original_popen=support.subprocess.Popen;original_modules=support.modules
support.subprocess.Popen=lambda *a,**k:child;support.modules=lambda p:[]
try:
 try:support.launch(['CPU-FAKE-NOT-EXECUTED'],PROOF,PROOF/'host.log',PROOF/'process.json',-1)
 except support.RetainedProcessTimeout as error:
  record=error.record;assert record['PID']==12345 and record['exit'] is None and record['process_retained'] and record['no_kill'] and not record['retry'] and child.termination_calls==0
 else:raise AssertionError('actual timeout branch must return retained-process exception')
finally:support.subprocess.Popen=original_popen;support.modules=original_modules
entry=ast.parse((ROOT/'run_scene_two_rr.py').read_text())
imports=[n for n in ast.walk(entry) if isinstance(n,ast.ImportFrom) and n.module=='runner_common']
assert imports and not any(x.name in ('launch','process_guard') for n in imports for x in n.names)
report={'CPU':True,'GPU':False,'NGX':False,'actual_processes_created':0,'fake_child_still_live_after_actual_timeout_branch':True,'termination_calls':0,'timeout_process_receipt_saved':True,'actual_launch_support_has_no_kill_terminate_send_signal_wait_calls':True,'old_unsafe_launch_and_guard_not_imported':True,'known_process_regex_actual_source_cases':names,'all_known_cases_detected':True,'timeout_outer_runner_policy':'STOP retained PID; do not seal mutable output while child can still write','scope':'actual support function with fake process; not GPU runtime/timeout acceptance'}
(ROOT/'CPU_RUNNER_CONTRACT.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
