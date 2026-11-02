"""Actual same-source seven-plane 3D producer and five real recordings."""
from pathlib import Path
import hashlib,json,re,struct
import numpy as np
ROLES={'raw-color-rgba16f':('<f2',4),'depth-r32f':('<f4',1),'motion-rg32f':('<f4',2),'normal-rgba16f':('<f2',4),'diffuse-rgba16f':('<f2',4),'specular-rgba16f':('<f2',4),'roughness-r32f':('<f4',1)}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(v,msg):
 if not v:raise RuntimeError(msg)
def inspect_scene(text,directory,root,nr_retired):
 need(sha(directory/'scene.cso')==sha(root/'host/scene.cso')=='0f505924f3b875d97923a8c631025e6afa31836fbe070feadc7e29ad1bebccda','exact unchanged finite scene shader actually copied')
 need(sha(root/'host/scene.hlsl')=='0c5b6fbf9360ac0025ed1ba01978efda686bcf425e4ba801fb6508f8474e4d54','scene HLSL renderer unchanged')
 rows=list(re.finditer(r'^SCENE_RECORD frame=(\d+) nativeToken=(\d+) reset=(\d+) constants=([0-9a-f]{64}) resources=7 sameCommandList=1 inputBytes=await-exact-fence$',text,re.M))
 need([int(r.group(1)) for r in rows]==[0,0,1,2],'four actual producer dispatches: parentCreate initialization then three frame dispatches')
 schedule=(root/'host/phase_schedule.inl').read_text().split('phase_J[32]={',1)[1].split('};',1)[0]
 for i,row in enumerate(rows):
  frame=int(row.group(1));values=re.findall(r'\{([^}]+)\}',schedule)[frame].split(',');sx,sy=[float(v.rstrip('f')) for v in values[:2]]
  constants=struct.pack('<4I8f',1280,720,frame,1,sx,sy,1,np.float32(1280/720),0,1,-4,.375)
  need(hashlib.sha256(constants).hexdigest()==row.group(4),'actual 48-byte scene constants match same-source frame/camera/projection/sample/exposure')
  if i:
   pin=re.search(r'^LEASE stage=approx-child-eval frame='+str(frame)+r' nrToken=(\d+) nativeToken='+row.group(2)+r' nativeReset='+row.group(3)+r' device=(\d+) queue=(\d+) resources=10 matrices=owned$',text,re.M)
   need(pin and row.start()<pin.start() and pin.group(1)==nr_retired[frame].group(2),'actual producer and child/NR/final RR belong to this same native recording/reset')
 inputs={}
 for m in re.finditer(r'^SCENE_BYTES frame=(\d+) role=([^ ]+) file=([^ ]+) sha256=([0-9a-f]{64}) bytes=(\d+) min=([^ ]+) max=([^ ]+) finite=(\d+) nonzero=(\d+)$',text,re.M):
  frame=int(m.group(1));role=m.group(2);need(frame in range(3) and role in ROLES,'only three same-source seven-plane frame receipts')
  need(m.group(3)=='scene-'+role+'-'+str(frame)+'.bin','exact 3D source filename/role/frame')
  p=directory/m.group(3);dtype,channels=ROLES[role];size=1280*720*np.dtype(dtype).itemsize*channels
  need(p.stat().st_size==int(m.group(5))==size and sha(p)==m.group(4),'actual complete producer bytes/hash')
  values=np.fromfile(p,dtype=dtype).reshape(720,1280,channels);need(np.all(np.isfinite(values)),'every actual guide/raw component finite')
  if role=='raw-color-rgba16f':need(np.all(values.view('<u2')[:,:,3]==0x3c00),'fixed actual 3D producer alpha1, not flat alpha.75')
  if role=='depth-r32f':need(np.all((values>0)&(values<=1)),'hardware reversed depth range')
  if role=='motion-rg32f':need(np.all(values==0),'actual stationary physical motion excludes jitter')
  if role=='normal-rgba16f':need(np.all(np.abs(np.sum(values[:,:,:3].astype(np.float32)**2,axis=2)-1)<.01),'actual world normals unit length')
  if role=='diffuse-rgba16f':need(np.all((values[:,:,:3]>=0)&(values[:,:,:3]<=1)),'actual diffuse material range')
  if role=='specular-rgba16f':need(np.all(values[:,:,:3]==0),'actual scene no specular lobe')
  if role=='roughness-r32f':need(np.all(values==1),'actual diffuse-only roughness')
  if role in ('raw-color-rgba16f','depth-r32f','diffuse-rgba16f'):need(float(values[:,:,:min(channels,3)].max())>float(values[:,:,:min(channels,3)].min()),'actual spatially varying 3D signal')
  need((frame,role) not in inputs and nr_retired[frame].start()<m.start(),'exact retired frame precedes producer readback save, no duplicates')
  inputs[frame,role]={'frame':frame,'role':role,'sha256':m.group(4),'bytes':size,'all_finite':True}
 need(set(inputs)=={(i,r) for i in range(3) for r in ROLES},'all21 actual same-frame producer attachments')
 creates=list(re.finditer(r'^RETIRED stage=(parent-create|approx-child-create) nrToken=(\d+) nativeToken=(\d+) queue=(\d+) fence=(\S+) value=(\d+) completed=(\d+)$',text,re.M))
 need([m.group(1) for m in creates]==['parent-create','approx-child-create'],'two separate actual Create recordings')
 real=[{'stage':m.group(1),'token':int(m.group(2)),'native':int(m.group(3)),'queue':int(m.group(4)),'fence':m.group(5),'value':int(m.group(6)),'completed':int(m.group(7))} for m in creates]
 real +=[{'stage':'child-NR-composition-parent','frame':int(m.group(1)),'token':int(m.group(2)),'native':int(m.group(3)),'queue':int(m.group(4)),'fence':m.group(5),'value':int(m.group(6)),'completed':int(m.group(7))} for m in nr_retired]
 need(len(real)==5 and len({r['token'] for r in real})==5 and len({(r['queue'],r['fence']) for r in real})==1 and [r['value'] for r in real]==list(range(1,6)) and all(r['completed']>=r['value']>0 for r in real),'five exact real recording/queue/fence/Reset retirements')
 result=json.loads((directory/'result.json').read_text())
 need(result['schema']=='dlss5-scene-twoRR-functional-v1' and result['route']=='research-clean-RR-NR-composite-native-RR' and result['warm']==2 and result['frames']==1 and result['totalFrames']==3,'new bounded actual result schema')
 need(result['counts']=={'nativeCreate':1,'nativeEval':3,'nrCreate':1,'nrEval':3,'actualCombines':1,'childCreate':1,'childEval':3,'sceneProducerDispatches':4},'actual model and producer counts in result')
 need(result['retirement']=={'exactFenceAndSuccessfulReset':True,'nativeReleased':True,'nrContextDestroyed':True,'childReleased':True,'recordings':5},'all actual owners released only after exact retirements')
 need(result['rawGuideContract']=='approximate-after-child-clean' and result['rawProducerAlphaU16']==0x3c00 and result['compositionEditAlpha']==1,'explicit independent 3D alpha/guide domain')
 need(sha(directory/'frame-0000.rgba16f')==sha(directory/'native-rr-output-rgba16f-2.bin'),'observed O adoption copy equals actual retired frame2 output')
 return inputs
