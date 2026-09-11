#!/usr/bin/env python3
"""Patch the staged 0.4.5-fix1 core; no game files, COM spoofing, or GPU work.

The caller must establish source provenance first. Each edit is unique and
fails before any write if its expected current-core anchor has changed.
"""
from __future__ import annotations
import argparse
from pathlib import Path


def once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f"Expected one source anchor, got {count}: {old[:100]!r}")
    return text.replace(old, new, 1)


def block(text: str, signature: str) -> tuple[int, int]:
    """Find a complete C++ function/struct, ignoring comments and quoted text.

    The targeted definitions contain no raw strings. Reject rather than parse
    a new raw-string construct incorrectly. Return the brace-inclusive span.
    """
    if text.count(signature) != 1:
        raise ValueError(f"Non-unique definition: {signature}")
    start = text.index(signature)
    i = text.index("{", start)
    depth = 0
    state = "code"
    while i < len(text):
        c = text[i]
        n = text[i:i+2]
        if state == "line":
            if c == "\n": state = "code"
        elif state == "comment":
            if n == "*/": state = "code"; i += 1
        elif state in ('"', "'"):
            if c == "\\": i += 1
            elif c == state: state = "code"
        else:
            if n == "//": state = "line"; i += 1
            elif n == "/*": state = "comment"; i += 1
            elif n == 'R"': raise ValueError("Raw string in patched definition")
            elif c in ('"', "'"): state = c
            elif c == "{": depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0: return start, i + 1
        i += 1
    raise ValueError(f"Unterminated definition: {signature}")


def edit_block(text: str, signature: str, edit) -> str:
    a, b = block(text, signature)
    return text[:a] + edit(text[a:b]) + text[b:]


def patch_core(text: str) -> str:
    if '#define NR_HAS_NATIVE_FORMAT_OBSERVER 1' in text:
        # The current source owns the complete adapter and native view path.
        # Validate its production seams instead of applying the historical
        # FLOAT-only substitutions twice. Repeated staging is still rejected.
        required = (
            '#include "interop/nr_format_access.h"',
            '#include "interop/nr_format_access_d3d12.inl"',
            '#include "nr_native_format_runtime.inl"',
            '#include "nr_native_format_hooks.inl"',
            'DXGI_FORMAT source_srv_format = DXGI_FORMAT_UNKNOWN;',
            'DXGI_FORMAT result_srv_format = DXGI_FORMAT_UNKNOWN;',
            'const auto color_view = NrResolveColorViewFromList(',
            'const auto output_view = NrResolveColorViewFromList(',
            'const auto current_view = NrResolveColorView(',
        )
        for anchor in required:
            if text.count(anchor) != 1:
                raise ValueError(f'Native format integration seam changed: {anchor}')
        for signature in ('bool CreateSlot(', 'bool RebindSlotExternal('):
            a, b = block(text, signature)
            definition = text[a:b]
            if definition.count('const auto external_view = NrResolveColorView(') != 1:
                raise ValueError(f'Native format admission missing: {signature}')
            if 'srv.Format = slot.source_format;' in definition:
                raise ValueError(f'Physical storage used as SRV: {signature}')
        if '#define NR_NATIVE_FORMAT_STAGED 1' in text:
            raise ValueError('Native format source already staged')
        return once(text, '#define NR_HAS_NATIVE_FORMAT_OBSERVER 1',
                    '#define NR_HAS_NATIVE_FORMAT_OBSERVER 1\n#define NR_NATIVE_FORMAT_STAGED 1')
    # Preserve all shader math, mode selection, NGX/Reset tracking and lifetime
    # code. This deliberately overlaps none of PR #132's producer/collector files.
    text = once(text, '#include "MinHook.h"',
        '#include "MinHook.h"\n#include "interop/nr_format_access.h"')
    text = once(text, 'enum class PostSrStorageContract {',
        '#include "interop/nr_format_access_d3d12.inl"\n\nenum class PostSrStorageContract {')
    text = edit_block(text, 'enum class PreSrStorageContract {', lambda s:
        once(s, '    R8EncodingUnknown,', '    R8EncodingUnknown,\n    Fp32, // typed/capability checked; same existing canonical codec'))
    text = edit_block(text, 'PreSrStorageContract ClassifyPreSrStorage(', lambda s:
        once(s, '{\n', '{\n    format = NrSceneReadFormat(format);\n'
             '    if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)\n'
             '        return PreSrStorageContract::Fp32;\n'))
    text = edit_block(text, 'bool IsUsablePreSrStorage(', lambda s:
        once(s, 'return contract == PreSrStorageContract::Fp16 ||',
             'return contract == PreSrStorageContract::Fp32 ||\n'
             '           contract == PreSrStorageContract::Fp16 ||'))
    text = edit_block(text, 'bool SupportsPreSrStorage(', lambda s:
        once(s, '    support.Format = desc.Format;',
             '    support.Format = NrSceneReadFormat(desc.Format);'))
    text = edit_block(text, 'struct NrSlot {', lambda s:
        once(s, '    DXGI_FORMAT source_format = DXGI_FORMAT_UNKNOWN;',
             '    DXGI_FORMAT source_format = DXGI_FORMAT_UNKNOWN; // physical storage\n'
             '    DXGI_FORMAT source_srv_format = DXGI_FORMAT_UNKNOWN; // actual typed read view'))

    def create_slot(s: str) -> str:
        s = once(s, '    slot.source_format = source_desc.Format;',
            '    slot.source_format = source_desc.Format;\n'
            '    slot.source_srv_format = NrResolveSceneSrv(device, source_desc);\n'
            '    if (slot.source_srv_format == DXGI_FORMAT_UNKNOWN) {\n'
            '        Log("SceneColor typed SRV capability unavailable: resourceFormat=%u; "\n'
            '            "native SR retained. Collect F8 for BUG feedback.",\n'
            '            static_cast<unsigned int>(source_desc.Format));\n'
            '        DestroySlot(slot);\n'
            '        return false;\n'
            '    }\n'
            '    if (slot.source_format != slot.source_srv_format)\n'
            '        Log("SceneColor typed-view path: physical=%u view=%u; "\n'
            '            "FLOAT interpretation -> canonical FP16. This is not a semantic or game validation.",\n'
            '            static_cast<unsigned int>(slot.source_format),\n'
            '            static_cast<unsigned int>(slot.source_srv_format));')
        s = once(s, 'IsNativeSceneColorFormat(slot.source_format)',
                       'NrIsFloatSceneStorage(slot.source_format)')
        return once(s, '    srv.Format = slot.source_format;',
                       '    srv.Format = slot.source_srv_format;')
    text = edit_block(text, 'bool CreateSlot(', create_slot)

    def rebind(s: str) -> str:
        s = once(s, 'IsNativeSceneColorFormat(external_desc.Format)',
                       'NrIsFloatSceneStorage(external_desc.Format)')
        # This is after the post-SR branch returns, and before replacement
        # allocation or external AddRef/publication. Rejection cannot half-rebind.
        s = once(s, '    const bool pre_source_supported =',
            '    const DXGI_FORMAT source_srv_format = NrResolveSceneSrv(device, external_desc);\n'
            '    if (source_srv_format == DXGI_FORMAT_UNKNOWN) return false;\n\n'
            '    const bool pre_source_supported =')
        s = once(s, '    slot.source_format = external_desc.Format;',
            '    slot.source_format = external_desc.Format;\n'
            '    slot.source_srv_format = source_srv_format;')
        return once(s, '    srv.Format = slot.source_format;',
                       '    srv.Format = slot.source_srv_format;')
    return edit_block(text, 'bool RebindSlotExternal(', rebind)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    if args.output.exists(): raise SystemExit('Output exists; refusing overwrite')
    result = patch_core(args.input.read_text(encoding='utf-8'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Write only after every transformation has validated.
    with args.output.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(result)

if __name__ == '__main__': main()
