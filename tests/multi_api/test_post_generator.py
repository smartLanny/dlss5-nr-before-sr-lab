"""Dev2 wrapper composition/final manifest tests; not a full core/Windows build."""
import hashlib
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('dev2_wrapper',ROOT/'scripts/prepare-compat-dev2.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def hashes(root):
    return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file() and p!=root/'source-manifest.json'}

def old_finalizer(core,stage,commit):
    (stage/'source-manifest.json').write_text(json.dumps({'integration':{'source_commit':commit}}))

class Generator(unittest.TestCase):
    def parent(self,ancestor=True):
        legacy=SimpleNamespace(patch_core=lambda text:'prior-core:'+text,
                               patch_ui=lambda text:'prior-ui:'+text)
        configured=SimpleNamespace(legacy=legacy)
        return SimpleNamespace(is_ancestor=lambda root,ref:ancestor,
                configure=lambda root:configured,finalize=old_finalizer,file_hashes=hashes)

    def test_missing_real_parent_refuses_before_modifying_callbacks(self):
        parent=self.parent(False); original=parent.configure
        with self.assertRaisesRegex(ValueError,'ancestry'):
            m.configure(parent,ROOT)
        self.assertIs(parent.configure,original)

    def test_callback_order_and_final_hashes(self):
        parent=self.parent()
        stub=SimpleNamespace(patch_core=lambda text:'post-core:'+text,
                             patch_ui=lambda text:'post-ui:'+text,
                             patch_descriptors=lambda text:text.replace('physical','typed'))
        with patch.object(m,'load',return_value=stub):m.configure(parent,ROOT)
        merged=parent.configure(ROOT)
        self.assertEqual(merged.legacy.patch_core('input'),'post-core:prior-core:input')
        self.assertEqual(merged.legacy.patch_ui('input'),'post-ui:prior-ui:input')
        self.assertEqual(parent.VERSION,m.VERSION)
        with tempfile.TemporaryDirectory() as tmp:
            stage=Path(tmp)
            (stage/'core/src/interop').mkdir(parents=True)
            (stage/'core/config').mkdir(parents=True)
            (stage/'core/src/nr_codec_descriptors.inl').write_text('physical')
            (stage/'core/src/interop/nr_deployment_names.h').write_text('dlss5-native-carrier-multiapi-dev1.addon64')
            (stage/'core/src/nr_before_sr.rc').write_text('0.4.5 DX11 exp3 beta')
            (stage/'core/config/nr_before_sr.ini').write_text('[NRBeforeSR]\n')
            parent.finalize(ROOT,stage,'a'*40)
            data=json.loads((stage/'source-manifest.json').read_text())
            self.assertEqual(data['files'],hashes(stage))
            self.assertEqual(data['integration']['version'],m.VERSION)
            self.assertEqual(data['integration']['source_commit'],'a'*40)
            self.assertIn('not converted',data['integration']['rr_linear_depth'])
            self.assertEqual((stage/'core/src/nr_codec_descriptors.inl').read_text(),'typed')
            self.assertIn('dev2', (stage/'core/src/interop/nr_deployment_names.h').read_text())
            self.assertEqual((stage/'core/src/nr_before_sr.rc').read_text(),'0.4.5 multiapi dev2 beta')
            self.assertIn('PostWorkPercent=100',(stage/'core/config/nr_before_sr.ini').read_text())

    def test_pe_identity_drift_refuses_before_manifest_claim(self):
        parent=self.parent()
        stub=SimpleNamespace(patch_descriptors=lambda text:text)
        with patch.object(m,'load',return_value=stub):m.configure(parent,ROOT)
        with tempfile.TemporaryDirectory() as tmp:
            stage=Path(tmp);(stage/'core/src/interop').mkdir(parents=True)
            (stage/'core/src/nr_codec_descriptors.inl').write_text('descriptor')
            (stage/'core/src/interop/nr_deployment_names.h').write_text('dlss5-native-carrier-multiapi-dev1.addon64')
            (stage/'core/src/nr_before_sr.rc').write_text('different version')
            with self.assertRaisesRegex(ValueError,'PE description'):
                parent.finalize(ROOT,stage,'a'*40)
            data=json.loads((stage/'source-manifest.json').read_text())
            self.assertNotIn('version',data['integration'])
            # The parent's prepare transaction owns and discards this directory.
            # This unit test does not claim to execute that full source pipeline.

if __name__=='__main__':unittest.main()
