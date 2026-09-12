#!/usr/bin/env python3
"""Keep the pinned carrier's NGX interface in native Microsoft x64 slot order.

Applied only to generated source inside the integration staging transaction.
No vendor runtime, SDK library, detour or feature identity is changed.
"""
from pathlib import Path
import shutil

HEADER = 'nr_ngx_parameter_abi.h'
ORIGINAL = '''struct NVSDK_NGX_Parameter
{
    virtual void Set(const char *InName, unsigned long long InValue) = 0;
    virtual void Set(const char *InName, float InValue) = 0;
    virtual void Set(const char *InName, double InValue) = 0;
    virtual void Set(const char *InName, unsigned int InValue) = 0;
    virtual void Set(const char *InName, int InValue) = 0;
    virtual void Set(const char *InName, ID3D11Resource *InValue) = 0;
    virtual void Set(const char *InName, ID3D12Resource *InValue) = 0;
    virtual void Set(const char *InName, void *InValue) = 0;

    virtual NVSDK_NGX_Result Get(const char *InName, unsigned long long *OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, float *OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, double *OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, unsigned int *OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, int *OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, ID3D11Resource **OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, ID3D12Resource **OutValue) const = 0;
    virtual NVSDK_NGX_Result Get(const char *InName, void **OutValue) const = 0;

    virtual void Reset() = 0;
};'''

def patch(text: str) -> str:
    if text.count(ORIGINAL) != 1 or '#include "' + HEADER + '"' in text:
        raise ValueError('Pinned carrier NGX parameter ABI declaration changed')
    return text.replace(ORIGINAL, '#include "' + HEADER + '"', 1)

def apply(stage: Path) -> None:
    source = stage / 'core/src/interop' / HEADER
    carrier = stage / 'carrier/src/dlss5-bridge.cpp'
    patched = patch(carrier.read_text(encoding='utf-8'))
    if not source.is_file() or source.is_symlink():
        raise ValueError('Reviewed NGX parameter ABI header absent from archived core')
    shutil.copyfile(source, carrier.parent / HEADER)
    carrier.write_text(patched, encoding='utf-8', newline='\n')
