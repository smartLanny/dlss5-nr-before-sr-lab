"""Private collision check and observed process launcher; never terminates a child."""
from pathlib import Path
import ctypes,datetime,hashlib,json,os,subprocess,time
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
class RetainedProcessTimeout(RuntimeError):
 def __init__(self,record):super().__init__('bounded process timeout; child retained without termination or retry');self.record=record
def process_guard(path):
 # Same bounded known-process policy as scene_suite, plus this exact new EXE.
 pattern=r'^(HTGame|Sample|NRDSample|RTXPT|PathTracing|raster_capture|scene-two-rr|scene-bench|raw-nr-phase|dual-grid|nr_rr_identity|(?:.*-)?clang(?:\+\+|-cl)?|cl|ninja|ffmpeg)(\.exe)?$'
 script=r"@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '"+pattern+r"' } | Select-Object Name,ProcessId,ExecutablePath) | ConvertTo-Json -Compress"
 result=subprocess.run(['powershell','-NoProfile','-Command',script],capture_output=True,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
 if result.returncode:raise RuntimeError('cannot check actual known GPU/compile collisions')
 active=json.loads(result.stdout or '[]');active=active if isinstance(active,list) else [active]
 save(path,{'utc':utc(),'active':active,'known_name_guard_only':True,'source':'actual Win32_Process','names_regex':pattern})
 if active:raise RuntimeError('known game/model/capture/compiler process still active; no process started or terminated')
def modules(process):
 if os.name!='nt':return []
 psapi=ctypes.WinDLL('psapi',use_last_error=True)
 psapi.EnumProcessModulesEx.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_ulong]
 psapi.GetModuleFileNameExW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_ulong]
 handles=(ctypes.c_void_p*1024)();count=ctypes.c_ulong();handle=ctypes.c_void_p(int(process._handle))
 if not psapi.EnumProcessModulesEx(handle,handles,ctypes.sizeof(handles),ctypes.byref(count),3):return []
 found=[]
 for module in handles[:min(count.value//ctypes.sizeof(ctypes.c_void_p),1024)]:
  buffer=ctypes.create_unicode_buffer(32768)
  if psapi.GetModuleFileNameExW(handle,module,buffer,len(buffer)):
   p=Path(buffer.value)
   if p.name.lower() in {'d3dcompiler_47.dll','d3d12.dll','dxgi.dll','nrchain_nvngx.dll','nvngx_dlss.dll','nvngx_dlssd.dll','nvngx_dlssnr.dll','_nvngx.dll'}:found.append(str(p))
 return found
def launch(command,cwd,log,receipt,timeout_seconds):
 observed={};ticks=0
 with log.open('wb') as stream:
  child=subprocess.Popen(command,cwd=cwd,stdout=stream,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
  record={'command':command,'cwd':str(cwd),'PID':child.pid,'started_UTC':utc(),'exit':None,'log':str(log),'retry':False,'no_kill':True}
  save(receipt,record);deadline=time.monotonic()+timeout_seconds
  while child.poll() is None:
   for path in modules(child):observed.setdefault(path,utc())
   ticks+=1
   if time.monotonic()>deadline:
    record.update({'status':'timeout-process-retained-no-kill','process_retained':True,'ended_UTC':None,'module_snapshot_polls':ticks,'log_sha256_at_timeout':sha(log),'modules':[{'path':p,'first_observed_UTC':t,'sha256':sha(p)} for p,t in sorted(observed.items())]})
    save(receipt,record);raise RetainedProcessTimeout(record)
   time.sleep(.001)
  record.update({'exit':child.returncode,'ended_UTC':utc(),'log_sha256':sha(log),'module_snapshot_polls':ticks,'modules':[{'path':p,'first_observed_UTC':t,'sha256':sha(p)} for p,t in sorted(observed.items())]})
  save(receipt,record)
 if record['exit']!=0:raise RuntimeError('actual process exit nonzero; stopped without retry')
 return record
