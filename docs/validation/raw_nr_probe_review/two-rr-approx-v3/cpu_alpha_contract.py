"""Source-derived alpha counterexample; CPU only, never creates D3D/NGX objects."""
from pathlib import Path
import hashlib,importlib.util,json,re,sys
import numpy as np
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent
OLD=ROOT.parent/'two-rr-approx-host-v2'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def run():
 old=load('sealed_v2_ratio',OLD/'ratio_oracle.py');new=load('prepared_v3_ratio',ROOT/'ratio_oracle.py')
 before=(OLD/'host/ratio-before-shift.hlsl').read_text();after=(ROOT/'host/ratio-before-shift.hlsl').read_text()
 pattern=r' if\(raw\.a!=([0-9.]+)\) return;[^\n]*'
 old_match=re.search(pattern,before);new_match=re.search(pattern,after)
 assert old_match and new_match and before.replace(old_match.group(0),new_match.group(0))==after,'single alpha predicate/comment shader change only'
 old_domain=float(old_match.group(1));new_domain=float(new_match.group(1));assert old_domain==1 and new_domain==.75
 tests=[]
 def check(name,condition,**facts):
  assert condition,name;tests.append({'name':name,'pass':True,**facts})
 raw=np.ones((1,1,4),dtype='<f2');raw[0,0,3]=.75;clean=np.ones_like(raw);edited=clean.copy();edited[:,:,:3]=2
 a,ac,am=old.compose(raw,clean,edited,(0,0));b,bc,bm=new.compose(raw,clean,edited,(0,0))
 check('actual-old-alpha-predicate-rejects-proven-0x3a00',float(raw[0,0,3])!=old_domain and ac['accepted']==0 and a.tobytes()==raw.tobytes(),old_shader_threshold=old_domain,raw_alpha_u16=int(raw.view('<u2')[0,0,3]))
 check('actual-new-alpha-predicate-allows-proven-0x3a00',float(raw[0,0,3])==new_domain and bc['accepted']==1 and np.all(b[0,0,:3]==2) and b.view('<u2')[0,0,3]==0x3a00,new_shader_threshold=new_domain,predicted_RGB=[float(x) for x in b[0,0,:3]],alpha_u16=int(b.view('<u2')[0,0,3]))
 for name,bits in [('one',0x3c00),('unknown-half',0x3800),('positive-zero',0x0000),('negative-zero',0x8000),('NaN-payload',0x7e77),('signaling-NaN-payload',0x7d35),('positive-infinity',0x7c00),('negative-infinity',0xfc00)]:
  raw.view('<u2')[0,0,3]=bits
  b,bc,bm=new.compose(raw,clean,edited,(0,0));check('unknown-alpha-'+name+'-refuses-full-raw-bits',bc['accepted']==0 and b.tobytes()==raw.tobytes(),raw_alpha_u16=bits)
 raw.view('<u2')[0,0]=[0x7e35,0x8000,0xbc00,0x7e77]
 b,bc,bm=new.compose(raw,clean,clean,(0,0));check('identity-does-not-write-arbitrary-raw-bits',bc['identity']==1 and b.tobytes()==raw.tobytes(),raw_u16=[int(x) for x in raw.view('<u2')[0,0]])
 raw.view('<u2')[0,0]=[0x7e35,0x8000,0x3c00,0x3a00]
 b,bc,bm=new.compose(raw,clean,edited,(0,0));check('known-alpha-rejected-RGB-NaN-preserves-payload-and-zero',bc['accepted']==0 and b.tobytes()==raw.tobytes())
 check('0.75-half-float-half-exact',int(np.float16(np.float32(np.float16(.75))).view('<u2'))==0x3a00)
 report={'schema':'source-derived-alpha-domain-CPU-v3','CPU':True,'GPU':False,'NGX':False,'old_shader_sha256':sha(OLD/'host/ratio-before-shift.hlsl'),'new_shader_sha256':sha(ROOT/'host/ratio-before-shift.hlsl'),'old_oracle_sha256':sha(OLD/'ratio_oracle.py'),'new_oracle_sha256':sha(ROOT/'ratio_oracle.py'),'actual_shader_single_predicate_change_proven':True,'old_allowed_alpha':old_domain,'new_allowed_alpha':new_domain,'new_allowed_alpha_u16':0x3a00,'tests':tests,'limitation':'predicate extracted from actual HLSL, shader compile plus CPU mathematical oracle; not GPU equivalence or NGX validation'}
 (ROOT/'CPU_ALPHA_CONTRACT.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':print(json.dumps(run(),indent=2))
