"""Pure byte fixtures exercise packaging validation, not runnable Windows code."""
from pathlib import Path
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('ota045',ROOT/'scripts/package-045.py')
ota=importlib.util.module_from_spec(spec);spec.loader.exec_module(ota)
from nr_ota_pe import companion_check, REQUIRED
SHA='0123456789abcdef0123456789abcdef01234567'

def addon():
 b=bytearray(1024);b[:2]=b'MZ';struct.pack_into('<I',b,0x3c,128);b[128:132]=b'PE\0\0'
 struct.pack_into('<H',b,132,0x8664);struct.pack_into('<H',b,152,0x20b)
 text=ota.VERSION.encode()+b'\0'+SHA.encode()+b'\0'+'0.4.5 beta'.encode('utf-16le')
 b[256:256+len(text)]=text
 return bytes(b)

def companion():
 b=bytearray(2048);b[:2]=b'MZ';struct.pack_into('<I',b,0x3c,128);b[128:132]=b'PE\0\0'
 struct.pack_into('<HH',b,132,0x8664,1);struct.pack_into('<HH',b,148,240,0x2000)
 o=152;struct.pack_into('<H',b,o,0x20b);struct.pack_into('<I',b,o+108,16)
 struct.pack_into('<II',b,o+112,0x1000,0x300)
 p=o+240;b[p:p+8]=b'.text\0\0\0';struct.pack_into('<IIII',b,p+8,0x600,0x1000,0x600,0x200);struct.pack_into('<I',b,p+36,0x60000020)
 t=0x200;struct.pack_into('<IIIIII',b,t+16,1,4,4,0x1040,0x1050,0x1060)
 cursor=0x280
 for i,name in enumerate(sorted(REQUIRED)):
  struct.pack_into('<I',b,0x240+4*i,0x1400+i)
  struct.pack_into('<I',b,0x250+4*i,0x1000+cursor-0x200)
  struct.pack_into('<H',b,0x260+2*i,i)
  value=name.encode()+b'\0';b[cursor:cursor+len(value)]=value;cursor+=len(value)
 return bytes(b)

def payload(lang='Chinese'):
 return {ota.ADDONS[lang]:addon(),'nrchain_nvngx.dll':companion(),'Instructions.txt':b'fixture',**{name:b'owned-collector-fixture' for name in ota.FEEDBACK}}

def manifest(files,lang):
 return {'schema':'nr-branch-ota-v1','version':ota.VERSION,'language':lang,'sourceCommit':SHA,'sourceDirty':False,'api':'D3D12-x64','includesDx11':False,
   'files':[{'name':n,'sha256':ota.digest(v),'bytes':len(v)} for n,v in files.items()]}

class Ota045(unittest.TestCase):
 def verify(self,files=None,modify=None,lang='Chinese'):
  data=payload(lang) if files is None else files;m=manifest(data,lang)
  if modify:modify(m)
  with tempfile.TemporaryDirectory() as td:
   path=Path(td)/f'DLSS5-{ota.VERSION}-{lang}-OTA.zip'
   with zipfile.ZipFile(path,'w') as z:
    for n,v in data.items():z.writestr(n,v)
    z.writestr('ota-manifest.json',json.dumps(m))
   return ota.verify(path,SHA,lang)
 def test_two_languages(self):
  for language in ota.ADDONS:self.assertEqual(self.verify(lang=language)['language'],language)
 def test_missing_companion(self):
  files=payload();del files['nrchain_nvngx.dll']
  with self.assertRaises(ValueError):self.verify(files)
 def test_extra_nvidia_runtime(self):
  files=payload();files['nvngx_dlssnr.dll']=b'not-permitted'
  with self.assertRaises(ValueError):self.verify(files)
 def test_extra_ini(self):
  files=payload();files['nr_before_sr.ini']=b'not-permitted'
  with self.assertRaises(ValueError):self.verify(files)
 def test_mismatched_source(self):
  with self.assertRaises(ValueError):self.verify(modify=lambda m:m.update(sourceCommit='f'*40))
 def test_dirty_lane_language(self):
  for delta in [{'sourceDirty':True},{'includesDx11':True},{'api':'D3D11'},{'language':'English'}]:
   with self.assertRaises(ValueError):self.verify(modify=lambda m,d=delta:m.update(d))
 def test_corrupt_payload_hash(self):
  with self.assertRaises(ValueError):self.verify(modify=lambda m:m['files'][1].update(sha256='0'*64))
 def test_old_addon_identity(self):
  files=payload();files[ota.ADDONS['Chinese']]=addon().replace(b'beta0.4.5',b'beta0.4.4')
  with self.assertRaises(ValueError):self.verify(files)
 def test_companion_positive(self):companion_check(companion())
 def test_non_dll_or_wrong_architecture(self):
  for offset,value in [(132,0x14c),(150,0)]:
   b=bytearray(companion());struct.pack_into('<H',b,offset,value)
   with self.assertRaises(ValueError):companion_check(b)
 def test_incomplete_export(self):
  b=companion().replace(b'NRChain_Create',b'NRChain_Broken')
  with self.assertRaises(ValueError):companion_check(b)
 def test_forwarded_function(self):
  b=bytearray(companion());struct.pack_into('<I',b,0x240,0x1080)
  with self.assertRaises(ValueError):companion_check(b)
 def test_out_of_bounds_exports(self):
  for offset,value in [(152+112,0xffffff00),(0x200+32,0x1800),(0x200+24,0xffffffff)]:
   b=bytearray(companion());struct.pack_into('<I',b,offset,value)
   with self.assertRaises(ValueError):companion_check(b)
 def test_companion_checker_is_in_package_path(self):
  files=payload();files['nrchain_nvngx.dll']=b'MZ-invalid'
  with self.assertRaises(ValueError):self.verify(files)

if __name__=='__main__':unittest.main()
