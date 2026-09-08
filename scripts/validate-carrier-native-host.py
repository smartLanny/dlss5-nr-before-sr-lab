#!/usr/bin/env python3
"""Validate real detours in the exact generated carrier with a controlled DLL.

Uses WARP D3D11 devices and controlled ReShade callback identities, not NVIDIA
inference, real ReShade dispatch, private bridge frames, or a game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wrapper(header, name, bases):
    methods = []
    for base in bases:
        match = re.search(r'struct(?: __declspec\(novtable\))? ' + base + r'\b', header)
        if not match:
            raise ValueError('Missing pinned API type: ' + base)
        body = header[match.start():header.index('\n\t};', match.start())]
        methods.extend(re.findall(r'virtual\s+([^;]+?)\s*=\s*0;', body))
    output = 'class ' + name + ' : public reshade::api::' + bases[-1] + ' {\npublic:\n'
    output += 'std::uint64_t native = 0;\n'
    output += 'reshade::api::device_api api = reshade::api::device_api::d3d11;\n'
    output += 'reshade::api::device *owner = nullptr;\n'
    for method in methods:
        code = 'std::abort();'
        if re.search(r'\bget_native\(', method):
            code = 'return native;'
        elif re.search(r'\bget_api\(', method):
            code = 'return api;'
        elif re.search(r'\bget_device\(', method):
            code = 'return owner;'
        output += method + ' override { ' + code + ' }\n'
    return output + '};\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--math', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, out = args.source.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((source / 'source-manifest.json').read_text(encoding='utf-8'))
    for path, digest in manifest['files'].items():
        if sha(source / path) != digest:
            raise ValueError('Staged source mismatch: ' + path)
    carrier = source / 'carrier/src'
    minhook = source / 'carrier/third_party/minhook'
    cxx = str(args.toolchain.resolve() / 'bin/x86_64-w64-mingw32-clang++.exe')
    cc = str(args.toolchain.resolve() / 'bin/x86_64-w64-mingw32-clang.exe')

    def run(command):
        result = subprocess.run([str(x) for x in command], cwd=out, text=True,
                                capture_output=True, timeout=180)
        if result.stdout:
            print(result.stdout, end='')
        if result.stderr:
            print(result.stderr, end='')
        if result.returncode:
            raise RuntimeError('Native host command failed: ' + str(result.returncode))

    objects = []
    for name in ('buffer', 'hook', 'trampoline', 'hde/hde64'):
        obj = out / (Path(name).name + '.o')
        run([cc, '-std=c11', '-O2', '-I' + str(minhook / 'include'), '-c',
             minhook / ('src/' + name + '.c'), '-o', obj])
        objects.append(obj)
    fixture = out / 'carrier-native-fixture.dll'
    run([cxx, '-std=c++17', '-O2', '-shared', '-static', '-I' + str(ROOT),
         ROOT / 'tests/native_bridge/carrier_native_fixture.cpp', '-o', fixture])
    header = (carrier / 'reshade/reshade_api_device.hpp').read_text(encoding='utf-8')
    wrappers = 'using namespace reshade::api;\n' + wrapper(header, 'CarrierTestDevice', ('api_object', 'device'))
    wrappers += wrapper(header, 'CarrierTestSwapchain', ('api_object', 'device_object', 'swapchain'))
    host = (ROOT / 'tests/native_bridge/carrier_native_host.cpp').read_text(encoding='utf-8')
    host = host.replace('// GENERATED_CARRIER_INCLUDE', '#include "' +
                        (carrier / 'dlss5-bridge.cpp').as_posix() + '"')
    host = host.replace('// GENERATED_API_WRAPPERS', wrappers)
    cpp, exe = out / 'carrier-native-host.cpp', out / 'carrier-native-host.exe'
    cpp.write_text(host, encoding='utf-8')
    (out / 'dlss5-bridge.cfg').write_text('source=off\nstage=0\nprobe=0\nvk_mirror=0\n', encoding='utf-8')
    run([cxx, '-std=c++17', '-O2', '-fms-extensions', '-static', '-municode',
         '-I' + str(minhook / 'include'), '-I' + str(args.math.resolve() / 'Inc'),
         '-I' + str(carrier / 'reshade'), cpp, *objects, '-o', exe,
         '-luser32', '-ladvapi32', '-lbcrypt', '-lversion', '-ldxgi', '-ld3d11',
         '-ld3d12', '-ld3dcompiler', '-ldxguid'])
    run([exe, fixture])
    evidence = dict(source_commit=manifest['source_commit'],
                    generated_carrier_sha256=sha(carrier / 'dlss5-bridge.cpp'),
                    generated_bridge_sha256=sha(carrier / 'bridge.inc'),
                    host_sha256=sha(exe), fixture_sha256=sha(fixture),
                    real_detours=True, d3d11_device='WARP',
                    reshade_callback_identities='test wrappers',
                    nvidia_inference=False, private_bridge_frame=False, game_verified=False)
    (out / 'native-host-validation.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
