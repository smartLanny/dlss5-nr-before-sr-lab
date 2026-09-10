"""Production patcher tests with explicit excerpts; complete-core coverage is separate."""
import importlib.util
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m
m=load('exposure_patch',ROOT/'scripts/patch-exposure-access.py')
SOURCE='''bool IsExposureTextureFormat(DXGI_FORMAT format) { return false; }
bool SupportsExposureTextureContract() {
    if (!hdr || !texture) return true;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    bool supported = IsExposureTextureFormat(desc.Format) && ID3D12Device_CheckFeatureSupport();
    // Formats outside the DirectSR exposure-scale family would require decoding.
    if (!supported) {
        Log("%u %llu",
                static_cast<unsigned int>(desc.Flags), count);
    }
    return supported;
}
void WriteExposureSrv() {
    srv.Format = exposure != nullptr
        ? ID3D12Resource_GetDesc(exposure).Format
        : DXGI_FORMAT_R32_FLOAT;
}
bool CreateSlot() {
    slot.key = external;
    ID3D12Resource_AddRef(exposure);
    exposure_srv.Format = exposure != nullptr
        ? ID3D12Resource_GetDesc(exposure).Format
        : DXGI_FORMAT_R32_FLOAT;
    return true;
}
bool RebindSlotExternal() {
    if (!CanMutateSlotLocked(slot)) return false;
    ID3D12Resource_AddRef(external);
    ReleaseCom(slot.result);
    RebindExposureSource(device, exposure, slot);
    return true;
}
// Untouched non-exposure functions and shader math.
void Other() { use(g_config.mode); }
'''
DESCRIPTORS='srv(4,slot.exposure_source,slot.exposure_source?ID3D12Resource_GetDesc(slot.exposure_source).Format:DXGI_FORMAT_R32_FLOAT);'
class ExposurePatch(unittest.TestCase):
    def test_admission_queries_resolved_view_and_logs_reason(self):
        out=m.patch_exposure_core(SOURCE)
        self.assertEqual(out.count('#include "interop/nr_exposure_access_d3d12.inl"'),1)
        self.assertIn('const bool supported = access.allowed();',out)
        self.assertIn('NrExposureAccessReasonName(access.reason)',out)
        self.assertIn('collect F8 TXT and upload BUG',out)
        self.assertIn('if (!hdr || !texture) return true;',out)
    def test_reject_before_partial_publication(self):
        out=m.patch_exposure_core(SOURCE)
        for signature, changed in [('bool CreateSlot(', '    slot.key = external;'),
                                   ('bool RebindSlotExternal(', '    ID3D12Resource_AddRef(external);')]:
            a,b=m.base.block(out,signature); body=out[a:b]
            self.assertLess(body.index('NrResolveExposureAccess'), body.index(changed))
        a,b=m.base.block(out,'bool RebindSlotExternal('); body=out[a:b]
        self.assertLess(body.index('CanMutateSlotLocked'),body.index('NrResolveExposureAccess'))
    def test_each_writer_preserves_the_same_float_view(self):
        out=m.patch_exposure_core(SOURCE)
        self.assertEqual(out.count('? NrExposureReadFormat(ID3D12Resource_GetDesc(exposure).Format)'),2)
        self.assertNotIn('? ID3D12Resource_GetDesc(exposure).Format',out)
        result=m.patch_exposure_descriptors(DESCRIPTORS)
        self.assertIn('NrExposureReadFormat(ID3D12Resource_GetDesc(slot.exposure_source).Format)',result)
    def test_unrelated_code_is_unchanged(self):
        out=m.patch_exposure_core(SOURCE)
        a,b=m.base.block(SOURCE,'void Other('); c,d=m.base.block(out,'void Other(')
        self.assertEqual(SOURCE[a:b],out[c:d])
    def test_duplicate_or_changed_source_is_rejected(self):
        with self.assertRaises(ValueError): m.patch_exposure_core(m.patch_exposure_core(SOURCE))
        for anchor in ['    slot.key = external;', '    if (!CanMutateSlotLocked(slot)) return false;',
                       'IsExposureTextureFormat(desc.Format)', 'ID3D12Device_CheckFeatureSupport()']:
            with self.subTest(anchor=anchor), self.assertRaises(ValueError):
                m.patch_exposure_core(SOURCE.replace(anchor,'// changed'))
        with self.assertRaises(ValueError): m.patch_exposure_descriptors(DESCRIPTORS*2)
    def fixture(self,path,descriptors=DESCRIPTORS):
        root=path/'core/src'; (root/'interop').mkdir(parents=True)
        (root/'nr_before_sr.cpp').write_text(SOURCE)
        (root/'nr_codec_descriptors.inl').write_text(descriptors)
        (root/'interop/nr_exposure_access_d3d12.inl').write_bytes((ROOT/'src/interop/nr_exposure_access_d3d12.inl').read_bytes())
        return root
    def test_transaction_rejects_missing_helper_or_writer_before_any_write(self):
        with tempfile.TemporaryDirectory() as d:
            root=self.fixture(Path(d),'// changed descriptor')
            with self.assertRaises(ValueError): m.apply(Path(d))
            self.assertEqual((root/'nr_before_sr.cpp').read_text(),SOURCE)
            (root/'interop/nr_exposure_access_d3d12.inl').unlink()
            with self.assertRaises(ValueError): m.apply(Path(d))
            self.assertEqual((root/'nr_before_sr.cpp').read_text(),SOURCE)
    def test_actual_staging_transaction_updates_both_files(self):
        with tempfile.TemporaryDirectory() as d:
            root=self.fixture(Path(d)); m.apply(Path(d))
            self.assertEqual((root/'nr_before_sr.cpp').read_text(),m.patch_exposure_core(SOURCE))
            self.assertEqual((root/'nr_codec_descriptors.inl').read_text(),m.patch_exposure_descriptors(DESCRIPTORS))
    def test_canonical_adapter_callback_order(self):
        canonical=load('exposure_canonical',ROOT/'scripts/prepare-multi-api-beta.py')
        calls=[]
        library=SimpleNamespace(patch_core=lambda s:s,prepare=lambda *args:calls.append('base'),
            legacy=SimpleNamespace(CORE_BLOBS={},patch_ui=lambda s:s))
        passthrough=SimpleNamespace(patch_core=lambda s:s,patch_ui=lambda s:s)
        abi=SimpleNamespace(apply=lambda stage:calls.append('abi'))
        exposure=SimpleNamespace(apply=lambda stage:calls.append('exposure'))
        completion=SimpleNamespace(apply=lambda stage:calls.append('completion'))
        def fake_git(root,*args):
            return canonical.INTEGRATION_CORE_PINS[args[1].removeprefix('HEAD:')].encode()
        with tempfile.TemporaryDirectory() as temporary, \
             patch.object(canonical,'is_ancestor',return_value=True), patch.object(canonical,'git',side_effect=fake_git), \
             patch.object(canonical,'load',side_effect=[library,passthrough,passthrough,abi,exposure,completion]):
            stage=Path(temporary)
            (stage/'core/scripts').mkdir(parents=True)
            configured=canonical.configure(ROOT)
            configured.prepare(ROOT,ROOT,stage)
            for name in ('compile-codec-artifact.cpp','prepare-codec-artifacts.py'):
                self.assertEqual((stage/'core/scripts'/name).read_bytes(),
                    canonical.INTEGRATION_CORE_PINS['scripts/'+name].encode())
        self.assertEqual(calls,['base','abi','exposure','completion'])
    @unittest.skipUnless(os.environ.get('MULTIAPI_CORE_SOURCE'),'full pinned core not mounted; excerpt tests are not a full-core build')
    def test_complete_core_read_from_environment(self):
        source=Path(os.environ['MULTIAPI_CORE_SOURCE']).read_text(encoding='utf-8')
        out=m.patch_exposure_core(source)
        for signature in ('Substitution PrepareBeforeSr(','bool FinishAfterSr(','void DispatchCodec('):
            a,b=m.base.block(source,signature); c,d=m.base.block(out,signature)
            self.assertEqual(source[a:b],out[c:d])
        writer=Path(os.environ['MULTIAPI_CORE_SOURCE']).parent/'nr_codec_descriptors.inl'
        self.assertIn('NrExposureReadFormat', m.patch_exposure_descriptors(writer.read_text()))
if __name__=='__main__': unittest.main()
