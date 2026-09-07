#!/usr/bin/env python3
"""Opt-in D3D12 provider recovery seam; never part of the release generator.

Reads a core translation unit and writes a new file. This does not label the
Feeder runtime tested or supported. Existing outputs are never overwritten.
"""
from pathlib import Path
import argparse


def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"provider recovery anchor changed: {old[:100]!r}")
    return text.replace(old, new, 1)


def patch_core(text: str) -> str:
    text = once(text, '#include "MinHook.h"',
                '#include "MinHook.h"\n#include "interop/nr_external_provider_feedback.h"')
    text = once(text,
        '    // A disabled add-on must be indistinguishable from an unloaded add-on on\n',
        '    if (nr_external_provider_feedback::owner_provider_id.load(\n'
        '            std::memory_order_acquire) != 0)\n'
        '        return original(command_list, handle, params, callback);\n'
        '    // A disabled add-on must be indistinguishable from an unloaded add-on on\n')
    # Claim uses g_chain_cs; this second check closes the native/Claim race.
    text = once(text,
        '    if (!g_config.enabled || zero_transfer_native ||\n        OtherNrOwnerActive()) {',
        '    if (!g_config.enabled || zero_transfer_native ||\n'
        '        nr_external_provider_feedback::owner_provider_id.load(\n'
        '            std::memory_order_acquire) != 0 || OtherNrOwnerActive()) {')
    text = once(text,
        '    if (g_lifecycle_active_command_lists.load(std::memory_order_acquire) == 0) {\n'
        '        original(queue, count, command_lists);',
        '    if (g_lifecycle_active_command_lists.load(std::memory_order_acquire) == 0 &&\n'
        '        !(g_enabled_fast.load(std::memory_order_acquire) &&\n'
        '          nr_external_provider_feedback::affinity_train.load(std::memory_order_acquire))) {\n'
        '        original(queue, count, command_lists);')
    # Reuse the actual Reset hook installer. The producer's private list is not
    # a ReShade object; no fake Reset or duplicate detour is introduced.
    start = text.index('bool EnsureNativeCommandListResetHook(\n')
    body = text.index('    if (native_command_list == nullptr) return false;', start)
    prefix = text[start:body]
    text = once(text, prefix,
        'bool EnsureNativeCommandListResetHookRaw(ID3D12GraphicsCommandList *native_command_list);\n'
        + prefix + '    return EnsureNativeCommandListResetHookRaw(native_command_list);\n}\n\n'
        'bool EnsureNativeCommandListResetHookRaw(ID3D12GraphicsCommandList *native_command_list) {\n')
    return text + '\n#include "interop/nr_external_provider_consumer.inl"\n'


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    patched = patch_core(args.source.read_text(encoding='utf-8'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8', newline='\n') as out:
        out.write(patched)


if __name__ == '__main__':
    main()
