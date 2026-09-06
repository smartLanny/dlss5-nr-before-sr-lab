"""Create only audited packaging blobs on a fixed product source; never publish or move refs."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import urllib.request

BASE='12009d0448fb8bba8c699020382bf5bb6b84dec4'
REPO='smartLanny/dlss5-nr-before-sr-lab'
HERE=Path(__file__).resolve().parents[1]

def main():
 with tempfile.TemporaryDirectory(prefix='nr045-package-') as temp:
  root=Path(temp)/'tree'
  subprocess.run(['git','worktree','add','--detach',str(root),BASE],cwd=HERE,check=True)
  def git(*a,raw=False):return subprocess.check_output(['git',*a],cwd=root,text=not raw)
  def read(p):return (root/p).read_text(encoding='utf-8-sig')
  def write(p,v):
   f=root/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(v,encoding='utf-8',newline='\n')
  def swap(s,a,b):
   if s.count(a)!=1:raise RuntimeError(f'Expected one anchor: {a}')
   return s.replace(a,b)
  for p in ['scripts/nr_ota_pe.py','scripts/publish-045.py','tests/test_045_ota.py']:
   write(p,(HERE/p).read_text())
  p='scripts/package-045.py';s=read(p)
  s=swap(s,'import zipfile','import zipfile\nfrom nr_ota_pe import companion_check')
  s=swap(s,'        pe_check(z.read(ADDONS[language]),sha)','        pe_check(z.read(ADDONS[language]),sha)\n        companion_check(z.read("nrchain_nvngx.dll"))')
  s=swap(s,'        pe_check(data[addon],sha)','        pe_check(data[addon],sha)\n        companion_check(data["nrchain_nvngx.dll"])')
  s=swap(s,"'preserve':['nr_before_sr.ini','ReShade.ini','ReShadePreset.ini','nvngx_dlssnr.dll']", "'preserve':['nr_before_sr.ini','ReShade.ini','ReShadePreset.ini','nvngx_dlssnr.dll','nvngx_dlss.dll','nvngx_dlssg.dll','nvngx_dlssd.dll']")
  write(p,s)
  p='.github/workflows/beta042-validate.yml';s=read(p)
  s=swap(s,'          python3 tests/test_f8_parser.py','          python3 tests/test_f8_parser.py\n          python3 -m unittest discover -s tests -p test_045_ota.py -v')
  write(p,s)
  subprocess.run(['python3','-m','unittest','discover','-s','tests','-p','test_045_ota.py','-v'],cwd=root,check=True)
  subprocess.run(['python3','scripts/validate-beta04-structure.py'],cwd=root,check=True)
  git('add','.')
  paths=git('diff','--cached','--name-only',BASE).splitlines()
  entries=[]
  for path in paths:
   if path.startswith('bug-inbox/'):raise RuntimeError('Do not change archived evidence')
   data=git('show',':'+path,raw=True) # preserve Git attribute normalization
   req=urllib.request.Request('https://api.github.com/repos/'+REPO+'/git/blobs',data=json.dumps({'encoding':'utf-8','content':data.decode('utf-8')}).encode(),method='POST',headers={'Authorization':'Bearer '+os.environ['GH_TOKEN'],'Content-Type':'application/json'})
   with urllib.request.urlopen(req,timeout=60) as response:sha=json.load(response)['sha']
   assert sha==hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
   entries.append({'path':path,'mode':git('ls-files','-s','--',path).split()[0],'type':'blob','sha':sha})
  print('REVIEWED_TREE_ENTRIES '+json.dumps({'base':BASE,'entries':entries}))
  subprocess.run(['git','worktree','remove','--force',str(root)],cwd=HERE,check=True)
if __name__=='__main__':main()
