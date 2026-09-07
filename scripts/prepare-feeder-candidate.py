#!/usr/bin/env python3
"""Archive the optional Feeder producer and reviewed integration adapters.

This stages real producer source. It never bundles NVIDIA runtimes/static SDK
libraries, modifies a dependency checkout, or enables Vulkan's native mirror.
The production entry requires committed core adapters. --development labels an
uncommitted local compile experiment and must not be used for a distributable.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FEEDER = '26c002d5156d178c2db438327194077c9ad94418'
SDK = 'a291cc7d2cc642a51566f3dfd5376f635cd1b284'
SDK_PARAMS = '014d9f4adec600ea3677947b7f98258da9f9c7fc'
VULKAN = 'ee2ec5fd83dafce291024683b50dc89219333076'
ADAPTERS = ('nr_feeder_driver.inl', 'nr_ngx_parameter_abi.h')

def git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

def dependency(root: Path, expected: str) -> None:
    if git(root, 'rev-parse', 'HEAD').decode().strip() != expected:
        raise ValueError(f'Wrong pinned dependency revision: {root}')
    if git(root, 'status', '--porcelain', '--untracked-files=no').strip():
        raise ValueError(f'Dirty pinned dependency: {root}')

def archive(root: Path, dest: Path, *paths: str) -> None:
    data = git(root, 'archive', '--format=tar', 'HEAD', *paths)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        # These are immutable dependency Git trees, not uploaded archives. Still
        # reject links/traversal and extract only ordinary files/directories.
        for member in tar.getmembers():
            if not (member.isfile() or member.isdir()) or '..' in Path(member.name).parts:
                raise ValueError('Unexpected archive member')
        tar.extractall(dest, filter='data')

def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError('Pinned producer source anchor changed: ' + old[:100])
    return text.replace(old, new, 1)

def patch_sdk(text: str) -> str:
    start = text.find('typedef struct NVSDK_NGX_Parameter\n{')
    end = text.find('} NVSDK_NGX_Parameter;', start)
    if start < 0 or end < 0:
        raise ValueError('Pinned SDK parameter ABI declaration changed')
    return text[:start] + '#include "nr_ngx_parameter_abi.h"' + text[end + len('} NVSDK_NGX_Parameter;'):]

def patch_producer(text: str) -> str:
    # Keep original declarations and all actual SDK helpers, but resolve genuine
    # calls through the driver instead of linking an incompatible C++ CRT.
    return once(text, '#define FEED_VERSION "0.14.0-beta.4"',
        'static void Log(const char*, ...);\n#include "nr_feeder_driver.inl"\n\n'
        '#define FEED_VERSION "0.14.0-core-mc1-dev"')

def prepare(core: Path, feeder: Path, sdk: Path, vulkan: Path, output: Path,
            development: bool = False) -> None:
    if output.exists():
        raise ValueError('Output already exists; previous build candidates are never overwritten')
    for root, expected in ((feeder, FEEDER), (sdk, SDK), (vulkan, VULKAN)):
        dependency(root, expected)
    if git(sdk, 'rev-parse', 'HEAD:include/nvsdk_ngx_params.h').decode().strip() != SDK_PARAMS:
        raise ValueError('SDK parameter header changed')
    source = git(core, 'rev-parse', 'HEAD').decode().strip()
    dirty = bool(git(core, 'status', '--porcelain', '--untracked-files=no').strip())
    adapters = {}
    for name in ADAPTERS:
        relative = 'src/interop/' + name
        data = (core / relative).read_bytes()
        if not development and git(core, 'show', 'HEAD:' + relative) != data:
            raise ValueError('Commit reviewed adapter source first: ' + relative)
        adapters[name] = data
    if dirty and not development:
        raise ValueError('Commit reviewed core source before production staging')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nr-feeder-', dir=output.parent) as temp:
        stage = Path(temp) / 'candidate'
        archive(feeder, stage)
        archive(sdk, stage / 'sdk', 'include', 'LICENSE.txt')
        archive(vulkan, stage / 'vulkan', 'include', 'LICENSE.md')
        target = stage / 'src/dlss5-feed.cpp'
        target.write_text(patch_producer(target.read_text(encoding='utf-8-sig')), encoding='utf-8', newline='\n')
        hook = stage / 'src/feed_vk_hook.h'
        text = hook.read_text(encoding='utf-8')
        text = once(text, 'void *target = GetProcAddress(lib, "vkCreateDevice");',
            'void *target = reinterpret_cast<void *>(GetProcAddress(lib, "vkCreateDevice"));')
        text = once(text, 'if (void *ptarget = GetProcAddress(lib, "vkQueuePresentKHR"))',
            'if (void *ptarget = reinterpret_cast<void *>(GetProcAddress(lib, "vkQueuePresentKHR")))')
        hook.write_text(text, encoding='utf-8', newline='\n')
        params = stage / 'sdk/include/nvsdk_ngx_params.h'
        params.write_text(patch_sdk(params.read_text(encoding='utf-8')), encoding='utf-8', newline='\n')
        (stage / 'src/nr_feeder_driver.inl').write_bytes(adapters['nr_feeder_driver.inl'])
        (stage / 'sdk/include/nr_ngx_parameter_abi.h').write_bytes(adapters['nr_ngx_parameter_abi.h'])
        manifest = {
            'source_commit': source, 'development': development, 'tracked_dirty': dirty,
            'feeder_commit': FEEDER, 'sdk_headers_commit': SDK, 'vulkan_headers_commit': VULKAN,
            'sdk_static_library_linked': False, 'nvidia_runtime_bundled': False,
            'runtime_verified': False, 'files': {
                p.relative_to(stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(stage.rglob('*')) if p.is_file()
            }
        }
        (stage / 'nr-feeder-source.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        stage.rename(output)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT)
    for name in ('feeder', 'sdk', 'vulkan', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--development', action='store_true')
    args = parser.parse_args()
    prepare(args.core.resolve(), args.feeder.resolve(), args.sdk.resolve(), args.vulkan.resolve(),
            args.output.resolve(), args.development)
    print('Staged actual optional Feeder producer; compile/runtime validation separate')

if __name__ == '__main__':
    main()
