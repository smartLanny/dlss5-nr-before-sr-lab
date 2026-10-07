#!/usr/bin/env python3
"""Compile an isolated Scene Feature13 R->R probe; GPU requires --execute-prepared."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import re
import shutil
import subprocess
import time
from temporal_test_objects import objects_for

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_CORE = '0660260f52e84eb729f70a83325646f5e11df9a0'
PINNED = {
    'nrchain_nvngx.dll': '1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2',
    'nvngx_dlss.dll': '22c4660249da5b352cde0ce4d95178c3761223dd6ac096d0b88e5c03d7a5cdd1',
    'nvngx_dlssd.dll': '50eb21e654a1fd12403e000f5ac9c2488bf9149930c028e73196f3f1d47d10a9',
}
CONTROLS = {
    'missing-capability': (42, 'STOP stage=capability key=SuperSamplingDenoising.Available reason=RR-unavailable-or-unproven'),
    'missing-callback': (42, 'STOP stage=optimal key=DLSSDOptimalSettingsCallback reason=missing-callback'),
    'wrong-optimal': (42, 'STOP stage=optimal key=OutWidth/OutHeight reason=R-to-R-not-proven'),
    'parent-generation': (42, 'STOP stage=capability-query key=parent-generation/domain reason=stale-parent-or-init-domain'),
    'recording-pin': (41, 'FAIL SCENE_LEASE_BEFORE_OPAQUE'),
    'envelope-restore': (41, 'FAIL PRODUCTION_ENVELOPE_RESTORED'),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(out, report):
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def run(out, report, name, command, timeout, cwd=None):
    args = list(map(str, command))
    start = time.monotonic()
    try:
        result = subprocess.run(args, cwd=cwd or out, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        (out / (name + '.log')).write_bytes((error.stdout or b'') + (error.stderr or b''))
        report['commands'].append({'name': name, 'args': args, 'exit': None, 'timeout': timeout})
        save(out, report)
        raise
    (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
    report['commands'].append({'name': name, 'args': args, 'exit': result.returncode,
                               'seconds': time.monotonic() - start})
    save(out, report)
    print((result.stdout + result.stderr).decode('utf-8', 'replace')[-10000:], flush=True)
    return result


def output_statistics(case):
    import numpy as np
    rows = []
    previous = None
    for index in range(16, 32):
        path = case / f'output-{index}.fp16'
        if path.stat().st_size != 1280 * 720 * 8:
            raise ValueError('Incomplete Scene clean output: ' + str(path))
        pixels = np.fromfile(path, dtype='<f2').reshape(720, 1280, 4)
        if not np.isfinite(pixels).all():
            raise ValueError('Nonfinite Scene clean pixels')
        channels = []
        for channel, name in enumerate(('R', 'G', 'B', 'A')):
            plane = pixels[:, :, channel].astype(np.float32)
            channels.append({'channel': name, 'finite': int(np.isfinite(plane).sum()),
                             'nonzero': int(np.count_nonzero(plane)), 'minimum': float(plane.min()),
                             'maximum': float(plane.max()), 'spatial_range': float(np.ptp(plane))})
        if any(c['nonzero'] == 0 or c['spatial_range'] == 0 for c in channels[:3]):
            raise ValueError('Trivial or spatially constant Scene clean RGB')
        changed = None if previous is None else int(np.count_nonzero(pixels.view('<u2') != previous))
        rows.append({'frame': index, 'sha256': sha(path), 'channels': channels,
                     'changed_rgba_half_components_from_previous': changed})
        previous = pixels.view('<u2').copy()
    return {'frames': rows, 'all_finite': True, 'nonzero_spatially_varying_rgb': True,
            'input_response_observed': any(row['changed_rgba_half_components_from_previous'] for row in rows[1:]),
            'claim': 'Controlled clean output validity only; no phase, NR benefit, or game-quality oracle.'}


def execute_control(prepared, control):
    baseline = json.loads((prepared / 'validation.json').read_text(encoding='utf-8'))
    for path, digest in baseline['immutable_sha256'].items():
        if sha(path) != digest:
            raise ValueError('Prepared control input changed: ' + path)
    out = prepared / ('negative-' + control); out.mkdir(exist_ok=False)
    report = {'schema': 'scene-rr-explicit-negative-v1', 'result': 'running', 'control': control,
              'prepared_receipt_sha256': sha(prepared / 'validation.json'),
              'source_commit': baseline['source_commit'], 'immutable_sha256': baseline['immutable_sha256'],
              'commands': [], 'scope': 'Test-only missing getter/callback/result or expected-generation input; lease/Restore fix removal. Actual parent Create/observer/lease/fence remain production. No production flags/atomics changed.'}
    save(out, report)
    try:
        for name in (*PINNED, 'scene-rr-probe.exe'):
            shutil.copy2(prepared / name, out / name)
        result = run(out, report, 'negative', [out / 'scene-rr-probe.exe', control,
                                             out / 'nrchain_nvngx.dll'], 300)
        log = (result.stdout + result.stderr).decode('utf-8', 'replace')
        expected_exit, expected_message = CONTROLS[control]
        report.update({'host_exit': result.returncode, 'expected_exit': expected_exit,
                       'expected_message': expected_message, 'injection_receipt': re.findall(r'INJECTION .*', log),
                       'private_create_attempts': len(re.findall(r'SCENE_CALL_BEGIN kind=Create ', log)),
                       'private_eval_attempts': len(re.findall(r'SCENE_CALL_BEGIN kind=Eval ', log))})
        passed = result.returncode == expected_exit and expected_message in log and bool(report['injection_receipt'])
        if control not in ('envelope-restore',):
            passed = passed and report['private_create_attempts'] == 0 and report['private_eval_attempts'] == 0
        else:
            passed = passed and report['private_create_attempts'] == 1 and report['private_eval_attempts'] == 0
        report['result'] = 'negative-reproduced' if passed else 'negative-not-reproduced'
        save(out, report); return 0 if passed else 1
    except Exception as error:
        report['result'] = 'failed'; report['error'] = str(error); save(out, report); raise


def execute(out):
    report = json.loads((out / 'validation.json').read_text(encoding='utf-8'))
    if report['result'] != 'compiled-not-run':
        raise ValueError('Receipt is not an unstarted compiled probe')
    for path, digest in report['immutable_sha256'].items():
        if sha(path) != digest:
            raise ValueError('Prepared input/object/runtime/binary changed: ' + path)
    report['result'] = 'running'; save(out, report)
    try:
        for mode in ('off', 'probe'):
            case = out / mode; case.mkdir(exist_ok=False)
            for name in (*PINNED, 'scene-rr-probe.exe'):
                shutil.copy2(out / name, case / name)
            result = run(out, report, mode, [case / 'scene-rr-probe.exe', mode,
                                          case / 'nrchain_nvngx.dll'], 300, cwd=case)
            log = (result.stdout + result.stderr).decode('utf-8', 'replace')
            row = {'mode': mode, 'exit': result.returncode,
                   'passed': result.returncode == 0 and 'PASS SceneRR ' + mode + ':' in log}
            row['stop'] = [dict(zip(('stage', 'key', 'reason', 'result_hex'), values)) for values in
                           re.findall(r'STOP stage=(\S+) key=(\S+) reason=(\S+) result=([0-9a-f]{8})', log)]
            row['raw_getters'] = [dict(zip(('type', 'key', 'result_hex', 'value'), values)) for values in
                                  re.findall(r'GET type=(\S+) key=(\S+) result=([0-9a-f]{8}) (?:value|present)=(\S+)', log)]
            row['ranges'] = [dict(zip(('key', 'value', 'origin'), values)) for values in
                             re.findall(r'RANGE key=(\S+) value=(\d+) origin=(\S+)', log)]
            row['parent_guards'] = re.findall(r'PARENT_GUARD .*', log)
            row['leases'] = re.findall(r'LEASE .*', log)
            row['retired'] = re.findall(r'RETIRED .*', log)
            row['resource_states'] = re.findall(r'RESOURCE_STATE .*', log)
            row['state_before'] = re.findall(r'STATE_BEFORE .*', log)
            row['binding_resources'] = re.findall(r'BIND_RESOURCE .*', log)
            row['binding_floats'] = re.findall(r'BIND_FLOAT .*', log)
            row['callback_owner'] = re.findall(r'CALLBACK_OWNER .*', log)
            row['actual_module_path'] = next(iter(re.findall(r'NATIVE_MODULE (.*)', log)), None)
            for kind in ('INPUT', 'OUTPUT', 'CONTRACT'):
                row[kind.lower() + '_sha256'] = dict(re.findall(kind + r'_SHA256 index=(\d+) value=([0-9a-f]{64})', log))
            row['guide_sha256'] = dict(re.findall(r'GUIDE_SHA256 role=(\S+) value=([0-9a-f]{64})', log))
            row['evaluations'] = [dict(zip(('index', 'call', 'result_hex'), values)) for values in
                                  re.findall(r'SCENE_EVAL index=(\d+) call=(\d+) feature=13 result=([0-9a-f]{8})', log)]
            row['frames'] = re.findall(r'FRAME .*', log)
            report['nvidia_ngx'] |= 'BOOTSTRAP calls=1' in log
            report['cases'].append(row); save(out, report)
            if not row['passed']:
                report['result'] = 'blocked-capability' if result.returncode == 42 and any(
                    item['stage'] in ('capability', 'optimal') for item in row['stop']) else 'failed'
                save(out, report)
                return result.returncode or 1
            if mode == 'off':
                if 'bootstrap=0 capability=0 allocation=0 create=0 eval=0 copy=0 barrier=0 timestamp=0 GPU=0' not in log:
                    raise ValueError('Off did not exit before all GPU/init/allocation work')
                continue
            report['nvidia_ngx'] = True
            if any(len(row[key]) != 32 for key in ('input_sha256', 'contract_sha256', 'output_sha256', 'frames', 'evaluations')):
                raise ValueError('Incomplete continuous 32-frame Scene receipt')
            if len(row['leases']) != 34 or len(row['retired']) != 34:
                raise ValueError('Missing parent/child Create or per-Eval production retirement proof')
            if [int(v['index']) for v in row['evaluations']] != list(range(32)) or any(
                    int(v['call']) != i + 1 or v['result_hex'] != '00000001' for i, v in enumerate(row['evaluations'])):
                raise ValueError('Scene Eval sequence/result mismatch')
            if re.findall(r'BOOTSTRAP calls=1 result=([0-9a-f]{8})', log) != ['00000001'] or \
               re.findall(r'PARENT_CREATE calls=1 feature=13 result=([0-9a-f]{8})', log) != ['00000001'] or \
               re.findall(r'SCENE_CREATE index=1 feature=13 result=([0-9a-f]{8})', log) != ['00000001']:
                raise ValueError('Not exactly one init/native parent/private child Create')
            row['outputs'] = output_statistics(case)
            row['output_captures_sha256'] = {p.name: sha(p) for p in sorted(case.glob('output-*.fp16'))}
            if len(row['output_captures_sha256']) != 16:
                raise ValueError('Missing complete 16 observed clean planes')
        for path, digest in report['immutable_sha256'].items():
            if sha(path) != digest:
                raise ValueError('Prepared input changed during probe: ' + path)
        report['result'] = 'passed-controlled-scene-backend'; save(out, report); return 0
    except Exception as error:
        report['result'] = 'failed'; report['error'] = str(error); save(out, report); raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-prepared', type=Path)
    parser.add_argument('--control', choices=CONTROLS,
                        help='Explicit separate negative process/receipt; requires --execute-prepared, never runs automatically.')
    for name in ('core', 'compiler', 'reshade', 'runtime-root', 'b1', 'output'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--expected-core-commit', default=EXPECTED_CORE)
    args = parser.parse_args()
    if args.execute_prepared:
        prepared = args.execute_prepared.resolve(strict=True)
        return execute_control(prepared, args.control) if args.control else execute(prepared)
    if args.control:
        parser.error('--control requires --execute-prepared')
    if any(getattr(args, name.replace('-', '_')) is None for name in ('core', 'compiler', 'reshade', 'runtime-root', 'b1', 'output')):
        parser.error('All build input paths are required; building never executes GPU work')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    report = {'schema': 'scene-rr-isolated-backend-v1', 'result': 'preparing', 'commands': [], 'cases': [],
              'immutable_sha256': {}, 'nvidia_ngx': False, 'second_init': False, 'second_submission_ledger': False,
              'cold_off_zero_gpu_scope': True, 'pending_off_maintenance_verified': False,
              'available_explicit_negative_controls_not_run': list(CONTROLS),
              'product_entry_connected': False, 'game': False, 'NR': False, 'phase_verified': False,
              'contract': {'R': [1280, 720], 'N': [1280, 720], 'scene_output': [1280, 720],
                           'parent_create_geometry': [[1280, 720], [1920, 1080]], 'parent_eval_calls': 0,
                           'child_feature': 13, 'perf_quality': 5, 'flags': 75, 'rr_dlaa_preset': 0,
                           'denoise_mode': 1, 'roughness_mode': 0, 'hw_depth': 1, 'output_subrects': 0,
                           'warmup': 16, 'observe': 16, 'FG': False, 'jitter_pixels': [0, 0],
                           'pre_exposure': .75, 'exposure_scale': 2., 'auto_exposure': True,
                           'controlled_inputs': 'Same deterministic HDR/material/reversed-infinite HW depth/matrices/static primary+reflection MV as B2; every optional resource explicitly absent.',
                           'lifetime': 'Actual Core immutable packet/RecordNrUseLocked/exact submission fence/real Reset. Shared CPU owner retains matrix arrays. No second ledger.'},
              'scope': 'Independent generated scene backend capability/Create/Eval/retirement only. Old Feature1 adapter/runtime unchanged. No NR, rephase, native parent Evaluate, game, performance, or quality-benefit claim.'}
    save(out, report)
    try:
        core = args.core.resolve(strict=True); build = core / 'build'; source = core / 'src/nr_before_sr.cpp'
        compiler = args.compiler.resolve(strict=True); manifest_path = core.parent / 'source-manifest.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
        if manifest['source_commit'] != args.expected_core_commit:
            raise ValueError('Unexpected accurate Core source identity')
        for name, digest in manifest['files'].items():
            path = (manifest_path.parent / name).resolve(strict=True)
            if not path.is_relative_to(manifest_path.parent.resolve()) or sha(path) != digest:
                raise ValueError('Accurate source manifest mismatch: ' + name)
        rsp = build / 'obj/source-defines.rsp'; defines = rsp.read_text(encoding='utf-8-sig')
        if args.expected_core_commit not in defines or '-DNR_SOURCE_DIRTY=0' not in defines:
            raise ValueError('Actual source-defines identity/clean receipt mismatch')
        queue = ROOT / 'tests/queue_retirement_harness.cpp'; descriptor = ROOT / 'tests/descriptor_provenance_warp.inl'
        base = ROOT / 'tests/rr_identity_core_host.cpp.in'; native = ROOT / 'tests/rr_identity_native_host.cpp.in'
        fixture = ROOT / 'tests/scene_rr_probe_host.cpp.in'; module = ROOT / 'experiments/nr-edit/src/nr_rr_identity_dx12.cpp'
        qt = queue.read_text(encoding='utf-8-sig'); dt = descriptor.read_text(encoding='utf-8-sig')
        if qt.count('void OneShot() {') != 1 or dt.count('template<class Function> NgxResult Native(') != 1:
            raise ValueError('Actual existing helper boundaries changed')
        original_helpers = qt.split('void OneShot() {', 1)[0] + '\n}\n' + dt.split('template<class Function> NgxResult Native(', 1)[0] + '\n}\n'
        warp = '        Hr(IDXGIFactory4_EnumWarpAdapter(factory, IID_IDXGIAdapter, reinterpret_cast<void **>(&adapter)), "WARP");'
        hardware = '''        for(unsigned index=0;!adapter;++index){IDXGIAdapter1* value=nullptr;
            if(IDXGIFactory4_EnumAdapters1(factory,index,&value)==DXGI_ERROR_NOT_FOUND)break;
            DXGI_ADAPTER_DESC1 description{};Hr(IDXGIAdapter1_GetDesc1(value,&description),"actual adapter description");
            if(description.VendorId==0x10de&&!(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){
                adapter=reinterpret_cast<IDXGIAdapter*>(value);
                std::printf("ADAPTER vendor=%04x device=%04x luid=%08lx:%08lx index=%u\\n",description.VendorId,description.DeviceId,
                    (unsigned long)description.AdapterLuid.HighPart,(unsigned long)description.AdapterLuid.LowPart,index);
            }else ReleaseCom(value);
        }Check(adapter!=nullptr,"actual NVIDIA hardware adapter required");'''
        if original_helpers.count(warp) != 1:
            raise ValueError('Exact WARP helper adapter-selection anchor changed')
        helpers = original_helpers.replace(warp, hardware).replace('D3D_FEATURE_LEVEL_11_0', 'D3D_FEATURE_LEVEL_12_0').replace('WaitForSingleObject(event, 5000)', 'WaitForSingleObject(event, 20000)')
        (out / 'helpers.inl').write_text(helpers, encoding='utf-8')
        (out / 'helpers-adaptation.diff').write_text(''.join(difflib.unified_diff(original_helpers.splitlines(True), helpers.splitlines(True), fromfile='existing-warp-helpers', tofile='controlled-nvidia-helpers')), encoding='utf-8')
        b1 = json.loads(args.b1.read_text(encoding='utf-8'))
        resources = sorted({k['value'] for r in b1['roles'] if r['kind'] == 'resource' for k in r['ngx_keys']})
        subrects = sorted({k['value'] for r in b1['roles'] for k in r.get('subrect_base_keys', [])})
        (out / 'rr-native-keys.inl').write_text('static const char* rr_native_resource_keys[]={' + ','.join(map(json.dumps, resources)) + '};\nstatic const char* rr_native_subrect_keys[]={' + ','.join(map(json.dumps, subrects)) + '};\n', encoding='utf-8')
        # Both translation units use the *actual staged* ABI. No old public ABI
        # is copied or declared equivalent when combined #338 changes it.
        staged = {name: core / 'src/private_sr' / name for name in ('nr_edit_dx12.hpp', 'nr_private_sr_runtime.hpp')}
        for path in staged.values(): path.resolve(strict=True)
        api_text = module.with_suffix('.hpp').read_text(encoding='utf-8-sig')
        if api_text.count('#include "nr_edit_dx12.hpp"') != 1:
            raise ValueError('B2 public include boundary changed')
        (out / 'nr_rr_identity_dx12.hpp').write_text(api_text.replace('#include "nr_edit_dx12.hpp"', '#include "' + staged['nr_edit_dx12.hpp'].as_posix() + '"'), encoding='utf-8')
        # Relocate byte-identical helper source, so its quoted public-header
        # include resolves to the generated actual-staged-ABI header above.
        helper_source = out / 'nr_rr_identity_dx12.cpp'
        helper_source.write_bytes(module.read_bytes())
        wrapper = out / 'scene-rr-probe.cpp'
        wrapper.write_text(f'#include "{source.as_posix()}"\n#include "helpers.inl"\n#include "{base.as_posix()}"\n#include "{native.as_posix()}"\n#include "{fixture.as_posix()}"\n'
                           'int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3)return 2;std::wstring value=argv[1];scene_rr_probe_test::Run(std::string(value.begin(),value.end()),argv[2]);return 0;}\n', encoding='utf-8')
        objects = [build / 'obj' / name for name in ('buffer.o', 'hook.o', 'trampoline.o', 'hde64.o', 'nr_dual_spatial_d3d12.o', 'nr_face_detector.o', 'nr_face_worker.o', 'nr_aux_shader_runtime.o')] + objects_for(source)
        inputs = [queue, descriptor, base, native, fixture, module, module.with_suffix('.hpp'), ROOT / 'tests/native_observer_device.inl',
                  Path(__file__).resolve(), ROOT / 'scripts/temporal_test_objects.py', args.b1.resolve(), manifest_path, rsp, compiler, *staged.values(), *objects]
        runtimes = {}
        for name, digest in PINNED.items():
            path = (args.runtime_root / name).resolve(strict=True)
            if sha(path) != digest: raise ValueError('Pinned runtime hash mismatch: ' + name)
            shutil.copy2(path, out / name); inputs.extend([path, out / name])
            quoted = "'" + str(path).replace("'", "''") + "'"
            command = f'$p={quoted}; $s=Get-AuthenticodeSignature -LiteralPath $p; $v=(Get-Item -LiteralPath $p).VersionInfo; [pscustomobject]@{{Status=$s.Status.ToString();Signer=$s.SignerCertificate.Subject;FileVersion=$v.FileVersion;ProductVersion=$v.ProductVersion}} | ConvertTo-Json -Compress'
            receipt = subprocess.run(['pwsh', '-NoProfile', '-Command', command], capture_output=True, check=True)
            signature = json.loads(receipt.stdout.decode('utf-8-sig'))
            runtimes[name] = {'path': str(path), 'sha256': digest, **signature}
            if name in ('nvngx_dlss.dll', 'nvngx_dlssd.dll') and (signature['Status'] != 'Valid' or 'NVIDIA' not in signature['Signer']):
                raise ValueError('Pinned NVIDIA signature invalid: ' + name)
        report.update({'source_commit': manifest['source_commit'], 'actual_source_defines': defines, 'source_sha256': sha(source),
                       'manifest_sha256': sha(manifest_path), 'compiler_sha256': sha(compiler), 'runtime': runtimes,
                       'inputs_sha256': {str(path): sha(path) for path in inputs}, 'linked_objects_sha256': {str(path): sha(path) for path in objects},
                       'actual_staged_abi_sha256': {str(path): sha(path) for path in staged.values()},
                       'local_abi_sha256_not_used': {str(ROOT / 'experiments/nr-edit/src' / name): sha(ROOT / 'experiments/nr-edit/src' / name) for name in staged},
                       'abi_policy': 'Actual staged nr_edit/private_sr headers in both TUs, explicit include adaptation. This is a new Scene probe compile, not promotion of any B2 result or old ABI equivalence.'})
        flags = ['-std=c++20', '-O1', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64', '-DNR_FACE_BACKEND=1', '-DNR_CODEC_PRECOMPILED=1', '-DNR_AUX_PRECOMPILED=1', '-DNR_AUX_TIMING=1', '-ffunction-sections', '-fdata-sections',
                 '-I' + str(build), '-I' + str(source.parent), '-I' + str(out), '-I' + str(ROOT / 'experiments/nr-edit/src'), '-I' + str(ROOT / 'tests'),
                 '-I' + str(core / 'third_party/minhook/include'), '-I' + str(args.reshade.resolve() / 'include'), '-I' + str(args.reshade.resolve() / 'deps/imgui'), '@' + str(rsp)]
        save(out, report)
        identity = out / 'nr_rr_identity_dx12.o'
        result = run(out, report, 'compile-helper-module', [compiler, *flags, '-c', helper_source, '-o', identity], 120)
        if result.returncode: raise RuntimeError('Helper module failed to compile against actual staged ABI')
        binary = out / 'scene-rr-probe.exe'
        result = run(out, report, 'compile-complete-core-probe', [compiler, *flags, '-static', '-municode', '-Wl,--gc-sections', wrapper, identity, *objects, '-o', binary,
                     '-lkernel32', '-luser32', '-ld3d12', '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32', '-lbcrypt'], 300)
        if result.returncode: raise RuntimeError('Complete accurate Core Scene probe compile failed')
        inputs.extend([source, wrapper, helper_source, out / 'helpers.inl', out / 'helpers-adaptation.diff', out / 'rr-native-keys.inl', out / 'nr_rr_identity_dx12.hpp', identity, binary])
        report['immutable_sha256'] = {str(path): sha(path) for path in inputs}
        report['result'] = 'compiled-not-run'; save(out, report); return 0
    except Exception as error:
        report['result'] = 'failed'; report['error'] = str(error); save(out, report); raise


if __name__ == '__main__':
    raise SystemExit(main())
