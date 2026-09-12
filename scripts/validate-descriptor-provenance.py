#!/usr/bin/env python3
"""Test bounded production descriptor provenance, then real WARP native clears.

The WARP case includes the complete staged Core. Optional development overlays
are explicit component tests; their source/manifest and all copied inputs are
hashed separately and are never reported as an unchanged frozen candidate.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OVERLAYS = (
    'nr_native_clear_request.h', 'nr_native_clear_request_runtime.inl',
    'nr_descriptor_provenance.h', 'nr_descriptor_provenance_runtime.inl',
    'nr_descriptor_provenance_hooks.inl', 'nr_descriptor_provenance_commands.inl',
    'nr_native_format_runtime.inl', 'nr_native_format_hooks.inl',
    'nr_command_state.inl', 'nr_post_boundary.h', 'nr_post_boundary_runtime.inl',
    'nr_post_boundary_native.inl', 'nr_feedback_capture.inl',
)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source', type=Path, help='Canonical staged core/src/nr_before_sr.cpp.')
    parser.add_argument('--reshade', type=Path)
    parser.add_argument('--overlay-root', type=Path, help='Explicit nonfrozen component overlay for development only.')
    parser.add_argument('--cpu-only', action='store_true')
    parser.add_argument('--throughput-only', action='store_true', help='Run only bounded CPU copy throughput/retention observation, no GPU.')
    parser.add_argument('--compile-only', action='store_true')
    args = parser.parse_args()
    if not (args.cpu_only or args.throughput_only) and (not args.source or not args.reshade):
        parser.error('--source and --reshade are required for the full production WARP fixture')
    out = args.output.resolve()
    if out.exists():
        raise SystemExit('Preserve earlier descriptor evidence; choose a fresh output directory.')
    out.mkdir(parents=True)
    compiler = args.compiler.resolve(strict=True)
    record = {'schema': 1, 'scope': 'production descriptor provenance and native Clear CPU request semantics',
              'component_overlay': bool(args.overlay_root), 'compiler_sha256': sha(compiler),
              'cpu_exit': None, 'warp_exit': None, 'gpu_executed': False,
              'nvidia_nr': False, 'game_verified': False, 'admission_changed': False}
    snapshot = out/'core/src'
    if args.source:
        source = args.source.resolve(strict=True)
        shutil.copytree(source.parent, snapshot)
        record['original_source'] = str(source)
        record['original_source_sha256'] = sha(source)
        manifest = source.parents[2]/'source-manifest.json'
        if not manifest.exists():
            raise SystemExit('Staged source manifest is required for the full translation-unit test.')
        shutil.copyfile(manifest, out/'parent-source-manifest.json')
        record['original_stage_manifest_sha256'] = sha(manifest)
        if args.overlay_root:
            overlay = args.overlay_root.resolve(strict=True)
            record['overlay_files_sha256'] = {}
            for name in OVERLAYS:
                origin = overlay/'src'/name
                shutil.copyfile(origin, snapshot/name)
                record['overlay_files_sha256']['src/'+name] = sha(origin)
            main_source = snapshot/'nr_before_sr.cpp'
            content = main_source.read_text(encoding='utf-8-sig')
            anchor = '#include "nr_native_format.h"'
            insertion = '#include "nr_descriptor_provenance.h"\n#define NR_HAS_DESCRIPTOR_PROVENANCE 1'
            if insertion not in content:
                if content.count(anchor) != 1:
                    raise SystemExit('Native-format include anchor changed; overlay refused.')
                content = content.replace(anchor, anchor+'\n'+insertion, 1)
                main_source.write_text(content, encoding='utf-8', newline='\n')
            record['overlay_main_edit'] = 'only descriptor header include and feature macro; staged adapters retained'
    else:
        snapshot.mkdir(parents=True)
        for name in ('nr_descriptor_provenance.h', 'nr_post_boundary.h'):
            shutil.copyfile(ROOT/'src'/name, snapshot/name)
    record['source_inputs_sha256'] = {str(path.relative_to(snapshot)): sha(path)
        for path in sorted(snapshot.rglob('*')) if path.is_file()}
    fixture = ROOT/'tests'/('descriptor_provenance_throughput.cpp' if args.throughput_only else 'descriptor_provenance.cpp')
    shutil.copyfile(fixture, out/'descriptor-cpu.cpp')
    record['cpu_fixture_sha256'] = sha(fixture)
    record['bounded_cpu_throughput_only'] = args.throughput_only
    # Compile the exact production call-selection accessor against value-only
    # globals; an overwritten ring slot must never masquerade as another call.
    adapter = snapshot/'nr_descriptor_provenance_runtime.inl'
    if not adapter.exists():
        adapter = ROOT/'src/nr_descriptor_provenance_runtime.inl'
    adapter_text = adapter.read_text(encoding='utf-8-sig')
    first = adapter_text.index('nr_descriptor::NativeUse SnapshotNrDescriptorUse(')
    last = adapter_text.index('\nstd::uint64_t NrDescriptorResourceToken(', first)
    (out/'descriptor-use-snapshot.inl').write_text(
        'std::mutex g_nr_descriptor_use_mutex;\n'
        'nr_descriptor::NativeUse g_nr_descriptor_uses[64];\n'
        'std::uint64_t g_nr_descriptor_next_call=0;\n'+adapter_text[first:last]+'\n', encoding='utf-8')
    record['cpu_snapshot_adapter_sha256'] = sha(out/'descriptor-use-snapshot.inl')
    record['cpu_snapshot_production_source_sha256'] = sha(adapter)

    def run(command, name, timeout=180):
        result = subprocess.run([str(item) for item in command], cwd=out, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=timeout)
        (out/(name+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode:
            print((result.stdout+result.stderr)[-16000:])
        elif not name.startswith('compile'):
            print(result.stdout.strip())
        return result.returncode

    def save():
        (out/'result.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')

    record['admission_changed'] = (snapshot/'nr_native_clear_request.h').is_file()
    record['native_clear_request_is_gpu_proof'] = False
    cpu = out/'descriptor-cpu.exe'
    code = run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-static', '-pthread',
                '-I'+str(snapshot.parent), out/'descriptor-cpu.cpp', '-o', cpu], 'compile-cpu')
    record['cpu_compile_exit'] = code
    if code:
        save();raise SystemExit(code)
    record['cpu_binary_sha256'] = sha(cpu)
    if not args.compile_only:
        code = record['cpu_exit'] = run([cpu], 'cpu', 30)
        if code:
            save();raise SystemExit(code)
    if args.cpu_only or args.throughput_only:
        save();return

    queue = ROOT/'tests/queue_retirement_harness.cpp'
    queue_text = queue.read_text(encoding='utf-8')
    anchor = 'void OneShot() {'
    if queue_text.count(anchor) != 1:
        raise SystemExit('Shared WARP fixture boundary changed.')
    helpers = queue_text.split(anchor, 1)[0]+'\n} // namespace queue_test\n'
    fixture = ROOT/'tests/descriptor_provenance_warp.inl'
    device_adapter = ROOT/'tests/native_observer_device.inl'
    shutil.copyfile(device_adapter, out/device_adapter.name)
    record['device_adapter_sha256'] = sha(device_adapter)
    (out/'fixture.inl').write_text(helpers+fixture.read_text(encoding='utf-8'), encoding='utf-8')
    record['warp_fixture_sha256'] = sha(fixture)
    record['queue_helpers_sha256'] = sha(queue)
    wrapper = out/'descriptor-warp.cpp'
    wrapper.write_text(f'#include "{(snapshot/"nr_before_sr.cpp").as_posix()}"\n#include "fixture.inl"\n'
        'int main(){std::setvbuf(stdout,nullptr,_IONBF,0);descriptor_warp_test::Run();return 0;}\n', encoding='utf-8')
    common = ['-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64', '-ffunction-sections', '-fdata-sections',
              '-I'+str(ROOT/'third_party/minhook/include'), '-I'+str(args.reshade.resolve()/'include'),
              '-I'+str(args.reshade.resolve()/'deps/imgui')]
    c_compiler = compiler.with_name(compiler.name.replace('clang++', 'clang'))
    objects = []
    for name in ('buffer.c', 'hook.c', 'trampoline.c', 'hde/hde64.c'):
        source_file = ROOT/'third_party/minhook/src'/name
        target = out/(Path(name).stem+'.o')
        code = run([c_compiler, *common, '-std=c17', '-c', source_file, '-o', target], 'compile-'+target.stem)
        if code:
            save();raise SystemExit(code)
        objects.append(target)
    dual_source = snapshot / 'nr_dual_spatial_d3d12.cpp'
    if dual_source.is_file(): objects.append(str(dual_source))
    binary = out/'descriptor-warp.exe'
    code = run([compiler, *common, '-std=c++20', '-static', '-Wl,--gc-sections', wrapper, *objects, '-o', binary,
                '-lkernel32', '-luser32', '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid',
                '-ld3dcompiler', '-lshell32'], 'compile-warp')
    record['warp_compile_exit'] = code
    if code:
        save();raise SystemExit(code)
    record['warp_binary_sha256'] = sha(binary)
    record['full_production_translation_unit'] = True
    if not args.compile_only:
        record['gpu_executed'] = True
        code = record['warp_exit'] = run([binary], 'warp', 90)
    save()
    if code:
        raise SystemExit(code)
    print('PASS descriptor component compile only' if args.compile_only else
          'PASS descriptor provenance CPU + real WARP; observations only, no NGX/game claim')

if __name__ == '__main__':
    main()
