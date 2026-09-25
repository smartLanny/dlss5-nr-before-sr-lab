#!/usr/bin/env python3
"""Compile full staged Core and run CPU-only boundary callbacks/native detours.

The native case uses real MinHook trampolines and a synthetic COM object. It
does not create a D3D device, submit GPU work, load NGX, or launch a game.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reshade', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--sdk-header', type=Path, help='Optional independent Microsoft/DirectX SDK d3d12.h for ABI compile checks.')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out.exists():
        raise ValueError('Preserve prior evidence; choose a fresh build directory.')
    out.mkdir(parents=True)
    compiler = args.compiler.resolve(strict=True)
    c_compiler = compiler.with_name(compiler.name.replace('clang++', 'clang'))
    source = args.source.resolve(strict=True)
    fixture = ROOT/'tests/queue_retirement_harness.cpp'
    wrapper = out/'boundary-core.cpp'
    wrapper.write_text(f'#include "{source.as_posix()}"\n#include "{fixture.as_posix()}"\n', encoding='utf-8')
    common = ['-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64',
              '-ffunction-sections', '-fdata-sections',
              '-I'+str(ROOT/'third_party/minhook/include'),
              '-I'+str(args.reshade.resolve()/'include'),
              '-I'+str(args.reshade.resolve()/'deps/imgui')]
    def run(command: list[str], name: str, timeout: int = 180) -> None:
        result = subprocess.run(command, cwd=out, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=timeout)
        (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode:
            print((result.stdout+result.stderr)[-12000:])
            raise RuntimeError(f'{name} failed: {result.returncode}')
        if not name.startswith('compile'):
            print(result.stdout.strip())
    abi = out/'boundary-abi.cpp'
    abi.write_text('''#define CINTERFACE
#include <windows.h>
#include <cstddef>
#include <d3d12.h>
static_assert(offsetof(ID3D12GraphicsCommandListVtbl,ResourceBarrier)==26*sizeof(void*));
static_assert(offsetof(ID3D12GraphicsCommandList7Vtbl,Barrier)==80*sizeof(void*));
static_assert(sizeof(D3D12_BARRIER_GROUP)==16);
static_assert(sizeof(D3D12_BARRIER_SUBRESOURCE_RANGE)==24);
static_assert(sizeof(D3D12_TEXTURE_BARRIER)==64);
static_assert(offsetof(D3D12_TEXTURE_BARRIER,Subresources)==32);
static_assert(offsetof(D3D12_TEXTURE_BARRIER,Flags)==56);
int main(){}
''', encoding='utf-8')
    run([str(compiler), '-std=c++20', '-fsyntax-only', str(abi)], 'compile-compiler-sdk-abi')
    if args.sdk_header:
        sdk = args.sdk_header.resolve(strict=True)
        run([str(compiler), '-std=c++20', '-fms-extensions', '-fsyntax-only', '-I'+str(sdk.parent), str(abi)], 'compile-independent-sdk-abi')
    objects=[]
    # The staged Core keeps the dual spatial backend in a separate production
    # translation unit.  Link the exact sibling source used by this wrapper.
    spatial_source=source.parent/'nr_dual_spatial_d3d12.cpp'
    if spatial_source.is_file():
        spatial_object=out/'nr_dual_spatial_d3d12.o'
        run([str(compiler),*common,'-std=c++20','-c',str(spatial_source),'-o',str(spatial_object)],
            'compile-nr-dual-spatial')
        objects.append(str(spatial_object))
    for rel in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        obj=out/(Path(rel).stem+'.o')
        run([str(c_compiler), *common, '-std=c17', '-c', str(ROOT/'third_party/minhook/src'/rel), '-o', str(obj)], 'compile-'+obj.stem)
        objects.append(str(obj))
    binary=out/'boundary.exe'
    run([str(compiler), *common, '-std=c++20', '-static', '-Wl,--gc-sections',
         str(wrapper), *objects, '-o', str(binary), '-lkernel32', '-luser32',
         '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32'], 'compile-core')
    for case in ('boundary-callbacks', 'boundary-native', 'boundary-readonly'):
        run([str(binary), case], case, 30)
    sha=lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    evidence={'source':str(source), 'source_sha256':sha(source),
              'fixture_sha256':sha(fixture), 'native_fixture_sha256':sha(ROOT/'tests/post_boundary_native.inl'),
              'readonly_fixture_sha256':sha(ROOT/'tests/post_boundary_readonly.inl'),
              'compiler_sha256':sha(compiler), 'cases':['boundary-callbacks','boundary-native','boundary-readonly'],
              'full_production_translation_unit':True, 'native_minhook':True,
              'synthetic_com':True, 'warp':False, 'nvidia_nr':False, 'game':False}
    if args.sdk_header:
        evidence['independent_sdk_header_sha256']=sha(args.sdk_header.resolve())
    (out/'result.json').write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8')
    print('PASS bounded output-boundary CPU/native-hook evidence; no GPU or game claim')

if __name__ == '__main__':
    main()
