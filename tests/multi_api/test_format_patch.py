"""Transformation unit tests; minimal source fixtures are NOT a full core build."""
import importlib.util
import os
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('format_patch', ROOT/'scripts/patch-format-access.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

# Only anchors copied from the inspected 0.4.5-fix1 source. Irrelevant bodies
# omitted intentionally. Tests verify edits/ordering, not NGX integration.
FIXTURE='''#include "MinHook.h"
struct NrSlot {
    DXGI_FORMAT source_format = DXGI_FORMAT_UNKNOWN;
};
enum class PostSrStorageContract { Unsupported, Fp16 };
enum class PreSrStorageContract {
    Unsupported, Fp16,
    R8EncodingUnknown,
};
PreSrStorageContract ClassifyPreSrStorage(int format, int flags) {
    if (format == 10) return PreSrStorageContract::Fp16;
    return PreSrStorageContract::Unsupported;
}
bool IsUsablePreSrStorage(int contract) {
    return contract == PreSrStorageContract::Fp16 || other;
}
bool SupportsPreSrStorage() {
    support.Format = desc.Format;
    return true;
}
bool CreateSlot() {
    slot.source_format = source_desc.Format;
    const bool source_format_supported = IsNativeSceneColorFormat(slot.source_format);
    srv.Format = slot.source_format;
    return true;
}
bool RebindSlotExternal() {
    if (post_sr) { return true; }
    const bool pre_source_supported =
        IsNativeSceneColorFormat(external_desc.Format);
    AllocateResult();
    slot.source_format = external_desc.Format;
    srv.Format = slot.source_format;
    return true;
}
'''
class Patches(unittest.TestCase):
    def test_current_native_source_is_verified_and_staged_once(self):
        source=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        result=m.patch_core(source)
        self.assertIn('#define NR_NATIVE_FORMAT_STAGED 1',result)
        self.assertEqual(result.replace('\n#define NR_NATIVE_FORMAT_STAGED 1',''),source)
        with self.assertRaises(ValueError): m.patch_core(result)
        for anchor in ('#include "nr_native_format_runtime.inl"',
                       'const auto color_view = NrResolveColorViewFromList(',
                       'const auto output_view = NrResolveColorViewFromList(',
                       'DXGI_FORMAT result_srv_format = DXGI_FORMAT_UNKNOWN;'):
            with self.assertRaises(ValueError): m.patch_core(source.replace(anchor,'/* missing seam */'))
    def test_complete_minimal_edit(self):
        result=m.patch_core(FIXTURE)
        self.assertEqual(result.count('srv.Format = slot.source_srv_format;'),2)
        self.assertIn('support.Format = NrSceneReadFormat(desc.Format);',result)
        self.assertIn('slot.source_format = source_desc.Format;',result)
        self.assertIn('slot.source_format = external_desc.Format;',result)
        self.assertIn('PreSrStorageContract::Fp32',result)
        self.assertLess(result.index('if (source_srv_format == DXGI_FORMAT_UNKNOWN) return false;'), result.index('AllocateResult();'))
    def test_repeated_application_rejected(self):
        with self.assertRaises(ValueError): m.patch_core(m.patch_core(FIXTURE))
    def test_changed_anchor_rejected(self):
        for anchor in ('#include "MinHook.h"','struct NrSlot {','    support.Format = desc.Format;', '    slot.source_format = external_desc.Format;'):
            with self.assertRaises(ValueError):m.patch_core(FIXTURE.replace(anchor, '/*changed*/'))
    def test_braces_comments_and_strings(self):
        s='void fn() { /* } */ const char* t="}"; // }\n if (true) { ok(); } }\nnext'
        a,b=m.block(s,'void fn()');self.assertEqual(s[b:], '\nnext')
    def test_raw_string_rejected(self):
        with self.assertRaises(ValueError):m.block('void fn() { auto s=R"({})"; }','void fn()')
    def test_no_unsafe_cast_or_runtime_latch(self):
        data=(ROOT/'src/interop/nr_format_access_d3d12.inl').read_text()
        for bad in ('VirtualProtect','CreateShaderResourceView','failed = true','SetDescriptorHeaps'):
            self.assertNotIn(bad,data)
    @unittest.skipUnless(os.environ.get('MULTIAPI_CORE_SOURCE'), 'requires real full core file; not available in host fixture test')
    def test_actual_core_input(self):
        import hashlib
        source=Path(os.environ['MULTIAPI_CORE_SOURCE']).read_bytes()
        digest=hashlib.sha1(b'blob '+str(len(source)).encode()+b'\0'+source).hexdigest()
        self.assertEqual(digest,'06018f0d4aa7d6df035c9aec0ecefe63fb69a1de')
        text=source.decode('utf-8')
        result=m.patch_core(text)
        # Production shader remains byte-for-byte unchanged.
        def shader(s):return s.split('const char kCodecShader[] = R"HLSL(',1)[1].split(')HLSL";',1)[0]
        self.assertEqual(shader(result), shader(text))
        self.assertEqual(result.count('srv.Format = slot.source_srv_format;'),2)
if __name__=='__main__':unittest.main()
