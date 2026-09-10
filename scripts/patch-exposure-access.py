#!/usr/bin/env python3
"""Patch exposure access only inside the canonical source staging transaction."""
from pathlib import Path
import importlib.util
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('exposure_source_parser', ROOT/'scripts/patch-format-access.py')
base=importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
once, edit = base.once, base.edit_block

def patch_exposure_core(text: str) -> str:
    """Use the same reviewed exposure interpretation at admission and every writer.

    Invoked by the canonical generator after the SR/RR post transformation.
    No global fault latch, ownership, synchronization or colour budget changes.
    """
    text=once(text,'bool IsExposureTextureFormat(',
        '#include "interop/nr_exposure_access_d3d12.inl"\n\nbool IsExposureTextureFormat(')
    text=edit(text,'bool IsExposureTextureFormat(',lambda s:
        s[:s.index('{')]+'''{
    return NrExposureReadFormat(format) != DXGI_FORMAT_UNKNOWN;
}''')
    def admission(s):
        start=s.index('    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};')
        stop=s.index('    // Formats outside the DirectSR exposure-scale family',start)
        original=s[start:stop]
        if original.count('IsExposureTextureFormat(desc.Format)')!=1 or original.count('ID3D12Device_CheckFeatureSupport(')!=1:
            raise ValueError('Exposure admission anchors changed; review device contract')
        s=s[:start]+'''    const auto access = NrResolveExposureAccess(device, desc);
    const bool supported = access.allowed();
'''+s[stop:]
        return once(s,'                static_cast<unsigned int>(desc.Flags), count);',
            '                static_cast<unsigned int>(desc.Flags), count);\n'
            '            Log("Exposure view=%u reason=%s capabilityResult=0x%08X; "\n'
            '                "collect F8 TXT and upload BUG feedback.",\n'
            '                static_cast<unsigned int>(access.view),\n'
            '                NrExposureAccessReasonName(access.reason),\n'
            '                static_cast<unsigned int>(access.query_result));')
    text=edit(text,'bool SupportsExposureTextureContract(',admission)
    def writer(s):
        return once(s,'? ID3D12Resource_GetDesc(exposure).Format',
            '? NrExposureReadFormat(ID3D12Resource_GetDesc(exposure).Format)')
    text=edit(text,'void WriteExposureSrv(',writer)
    def create(s):
        s=once(s,'    slot.key = external;',
            '    // Validate before retaining or publishing any slot dependency.\n'
            '    if (exposure && !NrResolveExposureAccess(device, ID3D12Resource_GetDesc(exposure)).allowed())\n'
            '        return false;\n'
            '    slot.key = external;')
        if 'CODEC_SHARED_DESCRIPTOR_INITIALIZATION_V1' in s:
            return once(s,'    WriteCodecDescriptorSet(device, slot, slot.heap);',
                          '    WriteCodecDescriptorSet(device, slot, slot.heap);')
        return writer(s)
    text=edit(text,'bool CreateSlot(',create)
    return edit(text,'bool RebindSlotExternal(',lambda s:once(s,
        '    if (!CanMutateSlotLocked(slot)) return false;',
        '    if (!CanMutateSlotLocked(slot)) return false;\n'
        '    // Rejection must not replace references or partially rewrite a heap.\n'
        '    if (exposure && !NrResolveExposureAccess(device, ID3D12Resource_GetDesc(exposure)).allowed())\n'
        '        return false;'))


def patch_exposure_descriptors(text: str) -> str:
    # Same descriptor set is rebuilt for snapshots/F7. Do not revert to TYPELESS.
    return once(text,
        'slot.exposure_source?ID3D12Resource_GetDesc(slot.exposure_source).Format:DXGI_FORMAT_R32_FLOAT',
        'slot.exposure_source?NrExposureReadFormat(ID3D12Resource_GetDesc(slot.exposure_source).Format):DXGI_FORMAT_R32_FLOAT')


def apply(stage: Path) -> None:
    core = stage/'core/src/nr_before_sr.cpp'
    descriptors = stage/'core/src/nr_codec_descriptors.inl'
    helper = stage/'core/src/interop/nr_exposure_access_d3d12.inl'
    for path in (core, descriptors, helper):
        if not path.is_file() or path.is_symlink():
            raise ValueError('Missing or symlinked staged exposure input: ' + path.name)
    # Validate both transformations before writing either file. The enclosing
    # canonical transaction hashes and publishes this entire candidate.
    patched_core = patch_exposure_core(core.read_text(encoding='utf-8'))
    patched_descriptors = patch_exposure_descriptors(descriptors.read_text(encoding='utf-8'))
    core.write_text(patched_core, encoding='utf-8', newline='\n')
    descriptors.write_text(patched_descriptors, encoding='utf-8', newline='\n')
