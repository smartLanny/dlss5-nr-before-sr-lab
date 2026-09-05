from __future__ import annotations
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ota',ROOT/'scripts/package-044-test1.py')
ota=importlib.util.module_from_spec(spec);spec.loader.exec_module(ota)
SHA='a'*40

class BranchOtaTests(unittest.TestCase):
    def payload(self):
        image=bytearray(256);image[:2]=b'MZ';struct.pack_into('<I',image,0x3c,128)
        image[128:132]=b'PE\0\0';struct.pack_into('<H',image,132,0x8664);struct.pack_into('<H',image,152,0x20b)
        image+=ota.VERSION.encode()+SHA.encode()+'0.4.4-test1 beta'.encode('utf-16le')
        files={n:b'echo fixture\n' for n in ota.FEEDBACK}
        files[ota.ADDONS['Chinese']]=bytes(image);files['Instructions.txt']=b'Preserve existing configuration'
        return files
    def write(self,folder,files,**overrides):
        m={'schema':'nr-branch-ota-v1','version':ota.VERSION,'language':'Chinese','sourceCommit':SHA,
           'sourceDirty':False,'api':'D3D12-x64','includesDx11':False,
           'files':[{'name':n,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()} for n,b in files.items()]}
        m.update(overrides);path=Path(folder)/'test.zip'
        with zipfile.ZipFile(path,'w') as z:
            for n,b in files.items():z.writestr(n,b)
            z.writestr('ota-manifest.json',json.dumps(m))
        return path
    def test_valid_owned_addon_only(self):
        with tempfile.TemporaryDirectory() as d:
            r=ota.verify(self.write(d,self.payload()),SHA,'Chinese')
            self.assertEqual(r['sourceCommit'],SHA)
    def test_wrong_source(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):ota.verify(self.write(d,self.payload(),sourceCommit='b'*40),SHA,'Chinese')
    def test_integer_zero_is_not_clean_boolean(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):ota.verify(self.write(d,self.payload(),sourceDirty=0),SHA,'Chinese')
    def test_configuration_is_not_an_ota_payload(self):
        f=self.payload();f['nr_before_sr.ini']=b'[NRBeforeSR]\nEnabled=1\n'
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):ota.verify(self.write(d,f),SHA,'Chinese')
    def test_no_vendor_runtime(self):
        f=self.payload();f['nvngx_dlssnr.dll']=b'not allowed'
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):ota.verify(self.write(d,f),SHA,'Chinese')
    def test_wrong_architecture(self):
        f=self.payload();b=bytearray(f[ota.ADDONS['Chinese']]);struct.pack_into('<H',b,132,0x14c);f[ota.ADDONS['Chinese']]=bytes(b)
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):ota.verify(self.write(d,f),SHA,'Chinese')
    def test_missing_compiled_source(self):
        f=self.payload();f[ota.ADDONS['Chinese']]=f[ota.ADDONS['Chinese']].replace(SHA.encode(),b'b'*40)
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):ota.verify(self.write(d,f),SHA,'Chinese')
    def test_wrong_lane(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):ota.verify(self.write(d,self.payload(),includesDx11=True),SHA,'Chinese')
    def test_tampered_checksum(self):
        with tempfile.TemporaryDirectory() as d:
            f=self.payload();entries=[{'name':n,'bytes':len(b),'sha256':'0'*64} for n,b in f.items()]
            with self.assertRaises(ValueError):ota.verify(self.write(d,f,files=entries),SHA,'Chinese')

if __name__=='__main__':unittest.main()
