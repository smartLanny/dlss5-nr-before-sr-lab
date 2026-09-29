"""Provenance-only negative controls; synthetic bytes are never executed/linked."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('optional_objects',ROOT/'scripts/temporal_test_objects.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

class OptionalObjects(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'core';self.src=self.root/'src/nr_before_sr.cpp'
        self.src.parent.mkdir(parents=True);self.src.write_text('#define NR_HAS_PRIVATE_RESIDUAL_SR 1\n')
        self.out=self.root/'build/obj/private-sr';self.out.mkdir(parents=True)
        self.receipt=self.out/'module.json'
        def payload(p):p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'provenance-only:'+p.name.encode());return p
        source=payload(self.root/'src/nr_private_sr_guides.cpp')
        generated=payload(self.out/'artifacts.cpp');header=payload(self.out/'bytecode/nr_edit_binaries.hpp')
        manifest=payload(self.out/'bytecode/edit-shader-manifest.json')
        names=['nr_edit_dx12','nr_post_residual_dx12','nr_post_private_sr_dx12','nr_private_sr_guides','artifacts']
        self.objects=[payload(self.out/(n+'.o')) for n in names]
        shaders={n:{'path':str(p),'sha256':sha(p)} for n in ['rr_guides','rr_accumulate','rr_compose',
                 'deferred_encode','deferred_compose','normalize','post_extract','post_compose']
                 for p in [payload(self.out/'bytecode'/(n+'.cso'))]}
        self.data={'schema':'private-sr-module-v2','passed':True,'root':str(self.root),
                   'source_sha256':{'src/nr_private_sr_guides.cpp':sha(source)},
                   'generated_source_sha256':{str(generated):sha(generated)},
                   'header':str(header),'header_sha256':sha(header),
                   'shader_manifest':str(manifest),'shader_manifest_sha256':sha(manifest),'shaders':shaders,
                   'objects':list(map(str,self.objects)),'object_sha256':{str(p):sha(p) for p in self.objects}}
        self.save()
    def save(self):self.receipt.write_text(json.dumps(self.data))
    def test_complete_without_temporal(self):self.assertEqual(helper.objects_for(self.src),self.objects)
    def test_absent_macro_keeps_legacy(self):
        self.src.write_text('// no consumer\n');self.receipt.unlink();self.assertEqual(helper.objects_for(self.src),[])
    def test_disabled_macro_keeps_legacy(self):
        self.src.write_text('#define NR_HAS_PRIVATE_RESIDUAL_SR 0\n');self.assertEqual(helper.objects_for(self.src),[])
    def test_missing_receipt(self):
        self.receipt.unlink()
        with self.assertRaises(FileNotFoundError):helper.objects_for(self.src)
    def test_failed_receipt(self):
        self.data['passed']=False;self.save()
        with self.assertRaises(ValueError):helper.objects_for(self.src)
    def test_wrong_root(self):
        self.data['root']=str(self.root.parent);self.save()
        with self.assertRaises(ValueError):helper.objects_for(self.src)
    def test_missing_fifth_object(self):
        self.data['objects'].pop();self.save()
        with self.assertRaises(ValueError):helper.objects_for(self.src)
    def test_missing_generated_source(self):
        self.data['generated_source_sha256']={};self.save()
        with self.assertRaises(ValueError):helper.objects_for(self.src)
    def test_all_provenance_tampering(self):
        files=[self.root/'src/nr_private_sr_guides.cpp',self.out/'artifacts.cpp',
               Path(self.data['header']),Path(self.data['shader_manifest']),*self.objects,
               *[Path(v['path']) for v in self.data['shaders'].values()]]
        for p in files:
            with self.subTest(file=p.name):
                old=p.read_bytes();p.write_bytes(old+b'changed')
                with self.assertRaises(ValueError):helper.objects_for(self.src)
                p.write_bytes(old)
    def test_legacy_temporal_no_main_tu(self):
        self.src.unlink();folder=self.root/'src/temporal';folder.mkdir();s=folder/'a.cpp';s.write_bytes(b'old')
        receipt=self.root/'build/obj/temporal/module.json';receipt.parent.mkdir(parents=True)
        obj=receipt.parent/'legacy.o';obj.write_bytes(b'synthetic')
        receipt.write_text(json.dumps({'passed':True,'source_sha256':{'a.cpp':sha(s)},'object_sha256':{str(obj):sha(obj)}}))
        self.assertEqual(helper.objects_for(self.src),[obj])

if __name__=='__main__':unittest.main()
