"""Actual ReShade GetDevice regression; WARP allocations, no game or NGX.

Requires a hardware adapter for the cross-device refusal control. A missing
adapter fails this gate; it is never reported as successful coverage.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--reshade-dll', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    compiler = args.compiler.resolve(strict=True)
    dll = args.reshade_dll.resolve(strict=True)
    if sha(dll) != PIN:
        raise ValueError('Expected the pinned ReShade 6.8 integration runtime.')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    adapter = (ROOT/'src/nr_rr_depth_adapter.inl').read_text(encoding='utf-8')
    identity = adapter[adapter.index('constexpr GUID kRrDepthUnwrappedObject'):adapter.index('\nbool ResolveRrLinearDepth(')]
    template = (ROOT/'tests/dual_device_host.cpp.in').read_text(encoding='utf-8')
    (out/'probe.cpp').write_text(template.replace('// @CORE_DEVICE_IDENTITY@', identity), encoding='utf-8')
    shutil.copyfile(dll, out/'dxgi.dll')
    (out/'ReShade.ini').write_text('[GENERAL]\nNoEffectCache=1\n', encoding='utf-8')
    command = [str(compiler), '-std=c++20', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT/'src'), str(out/'probe.cpp'), str(ROOT/'src/nr_dual_spatial_d3d12.cpp'),
               '-ld3d12', '-ldxgi', '-ldxguid', '-ld3dcompiler', '-o', str(out/'probe.exe')]
    with (out/'compile.log').open('wb') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    # CREATE_NO_WINDOW prevents a console flash; this host never creates a UI.
    with (out/'probe.log').open('wb') as log:
        subprocess.run([str(out/'probe.exe')], cwd=out, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
    inputs = ['src/nr_rr_depth_adapter.inl', 'src/nr_dual_spatial_d3d12.cpp',
              'src/nr_dual_spatial_d3d12.hpp', 'tests/dual_device_host.cpp.in']
    receipt = {'success': True, 'reshade_sha256': PIN, 'compiler_sha256': sha(compiler),
               'source_commit': subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'], text=True).strip(),
               'inputs': {name: sha(ROOT/name) for name in inputs},
               'log_sha256': sha(out/'probe.log'), 'game_verified': False, 'ngx_calls': 0}
    (out/'validation.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print((out/'probe.log').read_text(encoding='utf-8'))

if __name__ == '__main__':
    main()
