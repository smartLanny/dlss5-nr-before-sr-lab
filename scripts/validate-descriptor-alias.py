#!/usr/bin/env python3
"""Run production native/proxy observation through a full-TU synthetic COM seam.

This gate creates no D3D device and never calls NGX. The source must be a staged
Core with a manifest. Development overlays are explicit and separately hashed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OVERLAYS = (
    'nr_native_clear_request.h', 'nr_native_clear_request_runtime.inl', 'nr_feedback_capture.inl',
    'nr_descriptor_provenance.h', 'nr_descriptor_provenance_runtime.inl',
    'nr_descriptor_provenance_hooks.inl', 'nr_descriptor_provenance_commands.inl',
    'nr_native_format_runtime.inl', 'nr_native_format_hooks.inl',
    'nr_post_boundary_native.inl', 'nr_post_boundary_runtime.inl',
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(directory):
    return {str(p.relative_to(directory)): sha(p) for p in sorted(directory.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--reshade', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--overlay-root', type=Path)
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    compiler = args.compiler.resolve(strict=True)
    reshade = args.reshade.resolve(strict=True)
    manifest = source.parents[2]/'source-manifest.json'
    if not manifest.is_file():
        parser.error('A staged source manifest is required.')
    manifest_bytes = manifest.read_bytes()
    manifest_sha256 = hashlib.sha256(manifest_bytes).hexdigest()
    source_manifest = json.loads(manifest_bytes.decode('utf-8-sig'))
    source_commit = source_manifest.get('source_commit', '')
    if not re.fullmatch('[0-9a-f]{40}', source_commit):
        parser.error('Source manifest must identify a full source commit.')
    source_root = source.parents[1]
    minhook = source_root/'third_party/minhook'
    if not minhook.is_dir():
        parser.error('Staged Core must contain its manifest-bound MinHook sources.')
    compiled_inputs = {}
    for directory in (source.parent, minhook):
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                key = 'core/'+path.relative_to(source_root).as_posix()
                actual = sha(path)
                if source_manifest.get('files', {}).get(key) != actual:
                    parser.error('Staged input does not match source manifest: '+key)
                compiled_inputs[key] = actual
    expected_inputs = {key: value for key, value in source_manifest.get('files', {}).items()
                       if key.startswith(('core/src/', 'core/third_party/minhook/'))}
    if compiled_inputs != expected_inputs:
        parser.error('Manifest-bound Core or MinHook inputs are missing.')
    out = args.output.resolve()
    if out.exists():
        parser.error('Preserve earlier evidence; choose a fresh output directory.')
    out.mkdir(parents=True)
    snapshot = out/'core/src'
    before = inventory(source.parent)
    shutil.copytree(source.parent, snapshot)
    minhook_snapshot = snapshot.parent/'third_party/minhook'
    shutil.copytree(minhook, minhook_snapshot)
    if inventory(source.parent) != before or inventory(snapshot) != before:
        raise SystemExit('Source changed while snapshotting.')
    for directory in (snapshot, minhook_snapshot):
        for path in sorted(directory.rglob('*')):
            if path.is_file() and compiled_inputs.get('core/'+path.relative_to(snapshot.parent).as_posix()) != sha(path):
                raise SystemExit('A manifest-bound input changed while snapshotting.')
    (out/'parent-source-manifest.json').write_bytes(manifest_bytes)
    record = {'schema': 1, 'tag': 'descriptor-alias-runtime-cpu',
              'scope': 'full production TU; synthetic COM forwarding; CPU only',
              'component_overlay': bool(args.overlay_root),
              'compiler_sha256': sha(compiler), 'original_source_sha256': sha(source),
              'source_commit': source_commit, 'source_manifest_verified': True,
              'manifest_bound_inputs_sha256': compiled_inputs,
              'original_stage_manifest_sha256': manifest_sha256,
              'original_source_inputs_sha256': before,
              'gpu_executed': False, 'd3d_devices': 0, 'nvidia_nr': False, 'game_verified': False}
    if args.overlay_root:
        overlay = args.overlay_root.resolve(strict=True)
        record['overlay_files_sha256'] = {}
        for name in OVERLAYS:
            origin = overlay/'src'/name
            expected = sha(origin)
            shutil.copyfile(origin, snapshot/name)
            if sha(origin) != expected or sha(snapshot/name) != expected:
                raise SystemExit('Overlay changed while snapshotting.')
            record['overlay_files_sha256']['src/'+name] = expected
    record['source_inputs_sha256'] = inventory(snapshot)
    record['minhook_inputs_sha256'] = inventory(minhook_snapshot)
    record['reshade_inputs_sha256'] = {
        prefix+'/'+key: value for prefix in ('include', 'deps/imgui')
        for key, value in inventory(reshade/prefix).items()
    }
    fixture = ROOT/'tests/descriptor_alias_cpu.inl'
    shutil.copyfile(fixture, out/'fixture.inl')
    record['fixture_sha256'] = sha(fixture)
    helper = ROOT/'tests/native_observer_device.inl'
    shutil.copyfile(helper, out/helper.name)
    record['device_adapter_sha256'] = sha(helper)
    wrapper = out/'descriptor-alias.cpp'
    wrapper.write_text('#include "core/src/nr_before_sr.cpp"\n#include "fixture.inl"\n'
                       'int main(int argc,char**){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);'
                       'std::setvbuf(stdout,nullptr,_IONBF,0);if(argc>1)descriptor_alias_test::RunLowOverhead();'
                       'else descriptor_alias_test::Run();return 0;}\n', encoding='utf-8')
    record['runner_sha256'] = sha(Path(__file__))
    record['commands'] = []

    def save():
        (out/'result.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')

    def run(command, name, timeout=180):
        command = [str(v) for v in command]
        record['commands'].append(command)
        result = subprocess.run(command, cwd=out, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=timeout)
        (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        record[name+'_exit'] = result.returncode
        save()
        if result.returncode:
            print((result.stdout+result.stderr)[-16000:])
            raise SystemExit(result.returncode)
        if name == 'cpu':
            print(result.stdout)

    common = ['-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64',
              '-ffunction-sections', '-fdata-sections',
              '-I'+str(minhook_snapshot/'include'),
              '-I'+str(reshade/'include'), '-I'+str(reshade/'deps/imgui')]
    c_compiler = compiler.with_name(compiler.name.replace('clang++', 'clang'))
    objects = []
    for name in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        target = out/(Path(name).stem+'.o')
        run([c_compiler, *common, '-std=c17', '-c', minhook_snapshot/'src'/name, '-o', target],
            'compile-'+target.stem)
        objects.append(target)
    # D13+ full Core references its out-of-line spatial implementation even
    # when this fixture does not enable NR2. Compile the same frozen source.
    dual_source = snapshot / 'nr_dual_spatial_d3d12.cpp'
    if dual_source.is_file(): objects.append(str(dual_source))
    binary = out/'descriptor-alias.exe'
    run([compiler, *common, '-std=c++20', '-static', '-Wl,--gc-sections', wrapper, *objects, '-o', binary,
         '-lkernel32', '-luser32', '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid',
         '-ld3dcompiler', '-lshell32'], 'compile-cpu')
    record['binary_sha256'] = sha(binary)
    record['full_production_translation_unit'] = True
    run([binary], 'cpu', 30)
    run([binary, 'low-overhead'], 'cpu-low-overhead', 30)
    if args.negative_controls:
        record['negative_controls'] = []
        mutations = (
            ('restore-global-copy-invalidation', 'nr_descriptor_provenance_runtime.inl',
             'if(destination_count!=source_count || destination_count>NrDescriptorCopy::kMaximum) {\n        unknown_destinations();return;',
             'if(destination_count!=source_count || destination_count>NrDescriptorCopy::kMaximum) {\n        g_nr_descriptors.Invalidate();return;'),
            ('restore-gpu-pair-dependency', 'nr_native_clear_request.h',
             'if(!before.cpu || !after.cpu || !same(before.cpu,after.cpu) ||',
             'if(!before || !after || !before.cpu || !after.cpu || !same(before.cpu,after.cpu) ||'),
            ('ignore-current-call', 'nr_native_clear_request.h',
             'current.call==fresh.call && fresh.call>prepared.call', 'true'),
            ('ignore-clear-revision', 'nr_native_clear_request.h',
             'a.stamp.revision==b.stamp.revision', 'true'),
            ('ignore-current-sync-conflict', 'nr_native_clear_request_runtime.inl',
             '(!synchronous.Resolved() || synchronous.Resolved()==format)', 'true'),
            ('skip-first-command-guard', 'nr_before_sr.cpp',
             'if(substitution.prepared_clear_request.call &&', 'if(false &&'),
        )
        for name, filename, old, new in mutations:
            target = snapshot/filename
            original = target.read_bytes()
            text = original.decode('utf-8')
            if text.count(old) != 1:
                raise SystemExit('Negative-control source anchor changed: '+name)
            try:
                target.write_text(text.replace(old, new, 1), encoding='utf-8', newline='\n')
                negative_binary = out/(name+'.exe')
                command = [compiler, *common, '-std=c++20', '-static', '-Wl,--gc-sections', wrapper,
                           *objects, '-o', negative_binary, '-lkernel32', '-luser32', '-ld3d12', '-ldxgi',
                           '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32']
                run(command, 'compile-'+name)
                result = subprocess.run([str(negative_binary)], cwd=out, capture_output=True, text=True,
                                        encoding='utf-8', errors='replace', timeout=30)
                log = out/(name+'.log')
                log.write_text(result.stdout+result.stderr, encoding='utf-8')
                item = {'name': name, 'file': filename, 'original_sha256': hashlib.sha256(original).hexdigest(),
                        'mutation_sha256': sha(target), 'binary_sha256': sha(negative_binary),
                        'command': [str(negative_binary)], 'exit': result.returncode, 'log_sha256': sha(log),
                        'caught': result.returncode != 0}
                record['negative_controls'].append(item)
                save()
                if not item['caught']:
                    raise SystemExit('Negative control unexpectedly passed: '+name)
                if name == 'restore-global-copy-invalidation' and 'COPY_SCOPE_UNRELATED_ORACLE' not in result.stdout+result.stderr:
                    raise SystemExit('Global-copy negative control failed outside its unrelated-view oracle.')
                print('PASS caught negative control: '+name)
            finally:
                target.write_bytes(original)
    record['source_unchanged_after_run'] = inventory(snapshot) == record['source_inputs_sha256']
    record['original_source_unchanged_after_run'] = inventory(source.parent) == before
    record['manifest_unchanged_after_run'] = sha(manifest) == manifest_sha256
    record['minhook_unchanged_after_run'] = inventory(minhook_snapshot) == record['minhook_inputs_sha256'] == inventory(minhook)
    record['reshade_unchanged_after_run'] = record['reshade_inputs_sha256'] == {
        prefix+'/'+key: value for prefix in ('include', 'deps/imgui')
        for key, value in inventory(reshade/prefix).items()
    }
    save()
    if not all(record[key] for key in ('source_unchanged_after_run', 'original_source_unchanged_after_run', 'manifest_unchanged_after_run',
                                     'minhook_unchanged_after_run', 'reshade_unchanged_after_run')):
        raise SystemExit('Source changed during the gate.')
    print('PASS descriptor-alias-runtime-cpu; no D3D device, GPU, NGX or game claim')


if __name__ == '__main__':
    main()
