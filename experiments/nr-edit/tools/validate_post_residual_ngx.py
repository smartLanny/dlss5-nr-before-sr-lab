"""Offline actual NR pixels -> post transport -> real private SR -> compose.

The admitted capture package is explicitly transcoded SDR. Depth/motion are
artificial static-plane fixtures, never captured native guides. No game hook.
All binary inputs, outputs and receipts belong in private evidence directories.
"""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import prepare_post_capture as prepare
import stage_edit_integration as integration

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = PACKAGE.parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from temporal_test_objects import objects_for


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['core', 'compiler', 'reshade', 'nrchain', 'sr-runtime', 'output']:
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--pair-proof', type=Path, action='append', default=[])
    args = parser.parse_args()
    core = args.core.resolve(strict=True)
    out = args.output.resolve()
    if out.exists():
        parser.error('fresh output required; preserve previous evidence')
    source_manifest = core.parent/'source-manifest.json'
    upstream = json.loads(source_manifest.read_text(encoding='utf-8'))['source_commit']
    out.mkdir(parents=True)
    receipt = dict(schema='actual-post-residual-sr-v1', upstream_commit=upstream,
                   source_manifest_sha256=sha(source_manifest), result='running', commands=[], cases={},
                   scope='offline actual captured NR pixels, explicit SDR transcode and artificial stationary guides; no live NR+SR chain, Core route, game, native motion or quality claim')
    sources = [PACKAGE/p for p in integration.OVERLAY]
    sources += [PACKAGE/'tests'/n for n in ['post_residual_ngx_host.cpp', 'private_sr_ngx_host.cpp']]
    sources += [Path(__file__), PACKAGE/'tools/prepare_post_capture.py', PACKAGE/'tools/stage_edit_integration.py',
                PACKAGE/'tools/stage_private_image.py', ROOT/'scripts/nr_replay.py', ROOT/'scripts/temporal_test_objects.py']
    initial = {str(path): sha(path) for path in sources}
    receipt['source_sha256'] = initial

    def save():
        (out/'validation.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')

    def run(command, name, cwd=out, timeout=240):
        command = list(map(str, command))
        row = dict(name=name, command=command, cwd=str(cwd), exit_code=None)
        receipt['commands'].append(row)
        save()
        try:
            result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
            log = result.stdout
            row['exit_code'] = result.returncode
        except subprocess.TimeoutExpired as error:
            log = (error.stdout or b'') + b'\nTIMEOUT; process killed; no passing inference claim\n'
            row['exit_code'] = -1
        path = cwd/(name+'.log')
        path.write_bytes(log)
        row.update(log=str(path), sha256=sha(path))
        save()
        print(name+': '+str(row['exit_code'])+'\n'+log.decode('utf-8', 'replace')[-1800:], flush=True)
        return row['exit_code']

    try:
        frozen_tools = out/'frozen-tools'
        frozen_tools.mkdir()
        for source in sources:
            if source.suffix == '.py':
                shutil.copy2(source, frozen_tools/source.name)
        staged = out/'core'
        integration.stage(core, staged, upstream)
        receipt['integration_manifest_sha256'] = sha(staged/'edit-integration-manifest.json')
        build = staged/'build'
        build.mkdir()
        for name in ['nr_codec_precompiled.h', 'face-backend.json']:
            shutil.copy2(core/'build'/name, build/name)
        shader_compiler = build/'compile-edit-shader.exe'
        if run([args.compiler, '-std=c++20', '-O2', '-static', '-municode', staged/'tools/compile_edit_shader.cpp',
                '-ld3dcompiler', '-o', shader_compiler], 'compile-shader-compiler'):
            raise RuntimeError('shader compiler build failed')
        if run([sys.executable, staged/'tools/compile_edit_artifacts.py', '--compiler', shader_compiler,
                '--source', staged/'shaders', '--output', build], 'compile-eight-shaders'):
            raise RuntimeError('shader build failed')
        for name in ['post_residual_ngx_host.cpp', 'private_sr_ngx_host.cpp']:
            shutil.copy2(PACKAGE/'tests'/name, out/name)
        objects = [core/'build/obj'/n for n in ['buffer.o', 'hook.o', 'trampoline.o', 'hde64.o',
                   'nr_dual_spatial_d3d12.o', 'nr_face_detector.o', 'nr_face_worker.o']]
        objects += objects_for(core/'src/nr_before_sr.cpp')
        receipt['linked_objects'] = {str(path): sha(path) for path in objects}
        flags = ['-std=c++20', '-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64', '-DNR_FACE_BACKEND=1',
                 '-DNR_CODEC_PRECOMPILED=1', '-DNR_PRIVATE_SR_CORE="'+(staged/'src/nr_before_sr.cpp').as_posix()+'"',
                 '-ffunction-sections', '-fdata-sections', '-I'+str(build), '-I'+str(staged/'src'),
                 '-I'+str(staged/'third_party/minhook/include'), '-I'+str(args.reshade/'include'),
                 '-I'+str(args.reshade/'deps/imgui'), '-static', '-municode', '-Wl,--gc-sections']
        implementation = [staged/'src'/n for n in ['nr_edit_dx12.cpp', 'nr_deferred_dx12.cpp', 'nr_rr_dx12.cpp', 'nr_post_residual_dx12.cpp']]
        binary = out/'post-residual-ngx.exe'
        if run([args.compiler, *flags, out/'post_residual_ngx_host.cpp', *implementation, *objects, '-o', binary,
                '-lkernel32', '-luser32', '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler',
                '-lshell32', '-lbcrypt'], 'compile-host'):
            raise RuntimeError('actual SR host build failed')
        cases = []
        for name, edit, kind in [('zero', 0, 1), ('tiny-positive', 1/1024, 2), ('tiny-negative', -1/2048, 3)]:
            folder = out/name
            folder.mkdir()
            base = struct.pack('<4e', .625, .625, .625, 1)*(640*384)
            candidate = struct.pack('<4e', .625+edit, .625+edit, .625+edit, 1)*(640*384)
            proof = dict(schema='post-synthetic-control-v1', kind=name, geometry='artificial static plane',
                         package=prepare.write_pair(folder/'pair.bin', 640, 384, 160, 96, base, candidate, kind))
            (folder/'pair-proof.json').write_text(json.dumps(proof, indent=2), encoding='utf-8')
            cases.append((name, folder))
        for i, proof_path in enumerate(args.pair_proof):
            proof_path = proof_path.resolve(strict=True)
            proof = json.loads(proof_path.read_text())
            prepare.require(proof.get('schema') == 'post-nr-offline-pair-v1' and proof.get('result') == 'prepared-offline-transcoded-pair', 'unadmitted NR pair proof')
            prepare.require(proof.get('geometry', {}).get('captured_native_guides') is False, 'native guide claim refused')
            pair = proof_path.parent/'pair.bin'
            prepare.require(sha(pair) == proof['package']['sha256'], 'NR package hash mismatch')
            capture = Path(proof['capture'])
            prepare.require(sha(capture) == proof['capture_sha256'], 'original capture changed')
            folder = out/('nr-'+str(i+1))
            folder.mkdir()
            validation = Path(proof['validation'])
            prepare.require(sha(validation) == proof['validation_sha256'], 'producer receipt changed')
            regenerated = prepare.prepare(capture, validation, folder, True)
            prepare.require(regenerated['result'] == 'prepared-offline-transcoded-pair' and
                            regenerated['package']['sha256'] == proof['package']['sha256'], 'independent source audit/transcode mismatch')
            for src in [pair, proof_path, capture, capture.with_suffix('.json')]:
                shutil.copy2(src, folder/src.name)
            shutil.copy2(validation, folder/'capture-producer-validation.json')
            cases.append((folder.name, folder))
        for name, folder in cases:
            for src, dest in [(binary, binary.name), (args.nrchain, 'nrchain_nvngx.dll'), (args.sr_runtime, 'nvngx_dlss.dll')]:
                shutil.copy2(src, folder/dest)
            code = run([folder/binary.name, folder/'nrchain_nvngx.dll', folder/'pair.bin'], 'run', folder, 90)
            row = dict(exit_code=code, result='passed' if code == 0 else 'numerical-refusal' if code == 4 else 'failed')
            if (folder/'metrics.json').is_file():
                row['metrics'] = json.loads((folder/'metrics.json').read_text())
            row['files'] = {p.name: dict(sha256=sha(p), bytes=p.stat().st_size) for p in folder.iterdir() if p.is_file()}
            receipt['cases'][name] = row
            save()
        receipt['sources_unchanged'] = all(sha(Path(path)) == digest for path, digest in initial.items())
        prepare.require(receipt['sources_unchanged'], 'experiment sources changed during validation')
        failed = any(row['exit_code'] not in (0, 4) for row in receipt['cases'].values())
        refused = any(row['exit_code'] == 4 for row in receipt['cases'].values())
        receipt['result'] = 'failed' if failed else 'completed-with-numerical-refusals' if refused else 'actual-output-checks-passed'
        save()
        return 1 if failed else 4 if refused else 0
    except Exception as error:
        receipt.update(result='failed', failure=str(error))
        save()
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
