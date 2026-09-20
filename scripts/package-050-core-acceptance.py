#!/usr/bin/env python3
"""Package the verified beta0.5-dline15 D-line and compatibility integration, without carrier.

This is NOT the multi-API/Manager OTA packager. It never changes games or INI,
includes no runtime/model/private log, and will not overwrite existing output.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = 'beta0.5-dline15'

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding='utf-8-sig'))

def checked_file(root: Path, name: str, expected: str) -> bytes:
    candidate = root / name
    path = candidate.resolve(strict=True)
    if not path.is_relative_to(root.resolve()) or candidate.is_symlink() or not path.is_file():
        raise ValueError(f'Unsafe manifest file: {name}')
    data = path.read_bytes()
    if len(expected) != 64 or sha(data) != expected.lower():
        raise ValueError(f'Hash mismatch: {name}')
    return data

def git(*args: str) -> str:
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()

def checked_activation(path: Path | None, source_manifest: str, core_source: str,
                       bridge: str, host_source: str) -> dict:
    if path is None:
        raise ValueError('Same-source real ReShade cold activation evidence required before packaging.')
    value = load(path)
    if (value.get('schema') != 'real-reshade-no-native-dlss-activation-v1'
            or value.get('result') != 0 or value.get('compile_only') is not False
            or value.get('host_unchanged') is not True
            or value.get('host_sources_unchanged') is not True
            or value.get('production_sources_unchanged') is not True
            or value.get('source_manifest_sha256') != source_manifest
            or value.get('core_source_sha256') != core_source
            or value.get('production_bridge_sha256') != bridge
            or value.get('host_source_sha256') != host_source):
        raise ValueError('Real ReShade cold activation evidence does not match the frozen production source.')
    hosts={name.replace('\\','/'):digest for name,digest in value.get('host_sources_at_compile',{}).items()}
    if not {'tests/compat_nodlss_ngx_host.cpp','tests/compat_present_ngx_host.cpp'}.issubset(hosts):
        raise ValueError('Real ReShade activation host dependency identity is incomplete.')
    if hosts['tests/compat_nodlss_ngx_host.cpp'] != host_source:
        raise ValueError('Real ReShade activation entry identity differs.')
    for name,digest in hosts.items():checked_file(ROOT,name,digest)
    commands = {item['name']: item for item in value.get('commands', [])}
    if not {'compile-no-dlss-host', 'run-no-dlss-1', 'run-no-dlss-4'}.issubset(commands):
        raise ValueError('Actual ReShade one/four-layer cold runs are required.')
    for name, item in commands.items():
        if item.get('exit') != 0:
            raise ValueError('A real ReShade cold run failed.')
        log = checked_file(path.parent, Path(item['log']).name, item['sha256'])
        if name.startswith('run-no-dlss-') and b'PASS NO_DLSS_ACTIVATION' not in log:
            raise ValueError('Missing actual ReShade cold runtime completion.')
    for passes in (1, 4):
        name = f'no-dlss-p{passes}.rgba'
        info = value.get('outputs', {}).get(name, {})
        if info.get('bytes') != 640 * 360 * 4:
            raise ValueError('Missing actual ReShade cold readback output.')
        checked_file(path.parent, name, info['sha256'])
    # Keep private command paths, logs and captures out of distributable metadata.
    return {key: value[key] for key in ('schema', 'source_manifest_sha256',
        'core_source_sha256', 'host_source_sha256', 'production_bridge_sha256', 'reshade_sha256', 'scope')}


def checked_gpu_report(path: Path | None, kind: str, source_manifest: str,
                       core_source: str, bridge: str) -> dict:
    if path is None:
        raise ValueError(f'Same-source independent {kind} GPU evidence required.')
    value = load(path)
    expected_schema = {'present': 'real-ngx-present-host-v1','native-fg': 'real-ngx-native-fg-host-v1',
                       'mfg': 'real-ngx-mfg-all-indexed-outputs-v1'}[kind]
    reported_bridge = value.get('production_bridge_sha256', value.get('artifacts', {}).get('nrchain_nvngx.dll'))
    if (value.get('schema') != expected_schema or value.get('result') != 0
            or value.get('compile_only') is not False
            or value.get('production_sources_unchanged') is not True
            or value.get('source_manifest_sha256') != source_manifest
            or value.get('core_source_sha256') != core_source or reported_bridge != bridge):
        raise ValueError(f'Independent {kind} evidence differs from frozen production source.')
    commands = {item['name']: item for item in value.get('commands', [])}
    for item in commands.values():
        if item.get('exit', item.get('exit_code')) != 0:
            raise ValueError(f'An independent {kind} command failed.')
        checked_file(path.parent, Path(item['log']).name, item['sha256'])
    summary = {key: value[key] for key in ('schema', 'source_manifest_sha256', 'core_source_sha256', 'scope')}
    summary['production_bridge_sha256'] = bridge
    summary['receipt_sha256'] = sha(path.read_bytes())
    if kind == 'present':
        if (value.get('case') != 'all' or value.get('cold_bootstrap') is not True
                or value.get('present_capture_cases_checked') != 5
                or value.get('host_sources_unchanged') is not True
                or value.get('host_source_sha256') != sha((ROOT/'tests/compat_present_ngx_host.cpp').read_bytes())):
            raise ValueError('Cold Present full five-case capture validation required.')
        required={'compile-host','run-1-100','run-2-100','run-3-100','run-4-100','run-2-50'}
        if not required.issubset(commands):raise ValueError('Missing actual Present run records.')
        captures=value.get('present_nrf_captures',{});metadata=value.get('present_nrf_metadata',{})
        if len(captures) != 12 or len(metadata) != 12:raise ValueError('Incomplete Present layer capture set.')
        for name,info in captures.items():
            data=checked_file(path.parent,name,info['sha256'])
            if len(data) != info['bytes'] or len(data) <= 6080:raise ValueError('Incomplete Present NRF payload.')
        for name,info in metadata.items():
            data=checked_file(path.parent,name,info['sha256']);content=json.loads(data.decode('utf-8-sig'))
            if content != info['content'] or content.get('schema') != 'present-nr-observed-frame-v1' or content.get('complete') is not True:
                raise ValueError('Incomplete actual Present layer metadata.')
        summary.update(scenarios=5,nrf_files=12,
            image_validation={Path(name).name:info for name,info in captures.items()})
    elif kind == 'native-fg':
        required = {'compile-bootstrap','compile-host','run-1-100','run-2-100','run-3-100',
            'run-4-100','run-2-50','quadrant-nr0-fg0','quadrant-nr1-fg0','quadrant-nr0-fg1',
            'hudless-1-1','hudless-1-2','hudless-4-1','hudless-4-2','fault-control-1','fault-control-2'}
        if not required.issubset(commands) or value.get('host_sources_unchanged') is not True:
            raise ValueError('Complete native FG/NR/HUD/failure-control scenarios are required.')
        hosts={name.replace('\\','/'):digest for name,digest in value.get('host_sources_at_compile',{}).items()}
        if not {'tests/fg_consumer_ngx_host.cpp','tests/fg_consumer_hud_fixture.inl',
                'tests/fg_consumer_capture_fixture.inl','tests/compat_native_fg_bootstrap.cpp',
                'tests/compat_present_ngx_host.cpp'}.issubset(hosts):
            raise ValueError('Native FG host dependency identity is incomplete.')
        for name, digest in hosts.items():
            checked_file(ROOT, name, digest)
        if value.get('host_source_sha256') != sha((ROOT/'tests/fg_consumer_ngx_host.cpp').read_bytes()):
            raise ValueError('Native FG host differs from the committed host.')
        comparison = value.get('four_quadrant_pixel_comparison', {})
        for name in ('ConsumerBackbuffer','OutputReal','OutputInterpolated'):
            if comparison.get(name+'_NRoff_vs_NRon_pixels',0) <= 0:
                raise ValueError('Missing native FG NR-on/off actual pixel comparison.')
        if comparison.get('NRoff_consumer_FGoff_vs_FGon_pixels') != 0:
            raise ValueError('NR-off native FG input changed unexpectedly.')
        if value.get('nrf_requests_checked',0) < 9 or len(value.get('nrf_captures',{})) < 22:
            raise ValueError('Native FG layer capture validation is incomplete.')
        for name, info in value['nrf_captures'].items():
            data = checked_file(path.parent,name,info['sha256'])
            if len(data) != info['bytes']:raise ValueError('FG NRF size differs.')
        for field in ('captures','png_previews'):
            if not value.get(field):raise ValueError('Missing native FG image validation.')
            for name,info in value[field].items():
                data=checked_file(path.parent,name,info['sha256'] if isinstance(info,dict) else info)
                if isinstance(info,dict) and len(data) != info['bytes']:raise ValueError('Native FG image size differs.')
        summary.update(scenarios=14,capture_requests=value['nrf_requests_checked'],
            nrf_files=len(value['nrf_captures']),rgba_files=len(value['captures']),png_files=len(value['png_previews']),
            pixel_comparisons=comparison,
            image_validation={field:{Path(name).name:info for name,info in value[field].items()}
                for field in ('captures','png_previews','nrf_captures')})
    else:
        maximum = value.get('runtime_max_generated',0)
        if not 2 <= maximum <= 16 or value.get('all_reported_modes_completed') is not True or value.get('source_unchanged') is not True:
            raise ValueError('All reported native MFG indexed modes must complete.')
        for name,digest in value.get('source_hashes',{}).items():
            checked_file(ROOT,'tests/'+Path(name).name,digest)
        if not {'fg_consumer_ngx_host.cpp','compat_native_mfg_bootstrap.cpp','fg_multiframe_ngx_host.cpp',
                'fg_consumer_hud_fixture.inl','fg_consumer_capture_fixture.inl','compat_present_ngx_host.cpp'}.issubset(
                    {Path(name).name for name in value.get('source_hashes',{})}):
            raise ValueError('MFG transitive host identity is incomplete.')
        cases = value.get('cases',[])
        expected={(count,1,1) for count in range(1,maximum+1)}|{(maximum,4,1),(maximum,1,0)}
        if {(case['generated'],case['passes'],case['nr']) for case in cases} != expected:
            raise ValueError('Missing MFG count, full NR or NR-off case.')
        required = {'compile-mfg-bootstrap','compile-mfg-host'} | {
            f'run-generated-{count}-nr{nr}-passes{passes}' for count,passes,nr in expected}
        if not required.issubset(commands):
            raise ValueError('MFG case rows lack actual successful run records.')
        compact=[]
        for case in cases:
            count=case['generated']; captures=case.get('captures',{})
            if case.get('exit') != 0 or case.get('outputs') != 24*count or len(captures) != 24*count:
                raise ValueError('Incomplete indexed MFG outputs.')
            eligible=case.get('eligible_by_index',{})
            if set(eligible) != {str(i) for i in range(1,count+1)} or min(eligible.values()) < 4:
                raise ValueError('MFG interpolation eligibility missing for an output index.')
            folder=path.parent/f"generated-{count}-nr{case['nr']}-passes{case['passes']}"/'mfg'
            for name,info in captures.items():
                if info.get('bytes') != 640*360*4 or (not info.get('disabled') and info.get('pixels_overwritten_from_prefill',0)<640*360//2):
                    raise ValueError('Invalid MFG indexed GPU readback.')
                checked_file(folder,name,info['sha256'])
            item={key:case[key] for key in ('generated','multiplier','passes','nr','outputs','eligible_by_index')}
            item['image_validation']={Path(name).name:info for name,info in captures.items()}
            compact.append(item)
        summary.update(runtime_max_generated=maximum,max_multiplier=maximum+1,cases=compact,
            indexed_outputs=sum(case['outputs'] for case in cases))
    return summary


def package(build: Path, output: Path, face_runtime_root: Path | None = None,
            face_model: Path | None = None, face_model_license: Path | None = None,
            activation_evidence: Path | None = None, native_fg_evidence: Path | None = None,
            mfg_evidence: Path | None = None, present_evidence: Path | None = None) -> dict:
    if output.exists():
        raise ValueError('Output already exists; preserve all previous packages.')
    build = build.resolve(strict=True)
    stage = build / 'source'
    result = load(build / 'validation.json')
    manifest_bytes = checked_file(stage, 'source-manifest.json', result['source_manifest_sha256'])
    manifest = json.loads(manifest_bytes.decode('utf-8-sig'))
    commit = result['source_commit']
    if (result.get('schema') != 'nr050-focused-acceptance-v1' or result.get('success') is not True
            or result.get('warp_verified') is not True or result.get('carrier_compiled') is not False
            or result.get('post_light_style_warp_verified') is not True
            or result.get('directional_light_style_warp_verified') is not True
            or result.get('directional_baseline_source_sha256') != 'c681b208ef320b1b9fbd497e3c46485bf3836296a084cd4a741a2b179f1c8208'
            or result.get('directional_baseline_manifest_sha256') != '212d863167d4b55b5cc613528a2df68600243c73d07195c9136a6fe8b43c18c6'
            or result.get('nvidia_feature18_verified') is not False
            or result.get('game_runtime_verified') is not False
            or result.get('five_percent_performance_gate') != 'unmeasured'
            or manifest.get('source_commit') != commit
            or manifest.get('development_snapshot') is True
            or manifest.get('source_dirty') is True
            or manifest['integration'].get('source_commit') != commit
            or manifest['integration'].get('version') != VERSION):
        raise ValueError('Exact-source focused acceptance evidence required; no capability promotion.')
    required = {
        'production cold NGX bootstrap warm reuse bounded retry and compiled negative controls CPU',
        'full NR two to four pass plan and partial failure policy CPU',
        'full NR production wrapper serial inputs ownership and opaque failure CPU',
        'source-frame ownership current-copy and submitted-queue ordering CPU',
        'processing route sticky suffix reselection and bounded source retry CPU',
        'UI compatibility receipt explicit observation origin and no native Window borrowing CPU',
        'full NR two to four same-size spatial layers and exact retired readback WARP; synthetic model',
        'Present conversion and initialization production SM5 entries compiled',
        'Present fullscreen blit and exact joined FG HUD shader production SM5 entries compiled',
        'Present SDR HDR conversion RT copyback and source alpha WARP; synthetic edit',
        'FG HUD SDR scRGB PQ alpha coverage protected pixels and identity WARP; synthetic neural',
        'owned source guide transport and resource fence reuse WARP; no NGX or FG inference',
        'FG capture one to four layer readback and exact submitted ledger WARP; handoff substitute',
        'tests/test_compat_capture_ui.py',
        'NR2 observation window, preserved scales, per-layer model parameters and bilingual UI contracts',
        'actual RR timing carrier original-call preservation and no Feature1 recovery borrowing CPU',
        'actual ReShade dual device identity; legacy refusal and cross-adapter/budget negative controls; no NGX',
        'actual queued immutable bindings, bounded capacity, cross-queue/device negatives and recovery; no NVIDIA NGX',
        'all staged production SM5 entries compiled',
        'bounded RR projection CPU copy',
        'bounded UI activity alternating/stall/scope/reopen CPU gate',
        'canonical pinned full-source staging',
        'combined F8 capacity UTF8 privacy and end-verdict negative controls',
        'combined F8 current unknown store cannot inherit historical match',
        'combined low-grid WARP fixture; constructed neural planes, no NVIDIA NR',
        'descriptor-alias-runtime-cpu',
        'descriptor-provenance-cpu',
        'directional light production scalar and independent negative controls',
        'dual policy admission/history and independent matched-residual numeric oracle',
        'dual spatial/fused residual and generic production WARP; synthetic NR; opaque failure retained',
        'engine region production adapter and snapshot WARP',
        'exact submitted fence event wait and scoped CPU timing',
        'face diagnostic group8 production GPU ring and invalid group9 WARP',
        'face tracker exact lifetime WARP and optional native CPU worker',
        'final F8 typed timing numbers privacy disk output and actual summary CPU',
        'fine region composition production WARP',
        'first-recording old-policy CPU negative control',
        'first-recording old-policy native API negative control',
        'first-recording real Create Capture Restore native API',
        'four-region light controls production WARP; synthetic Neural only',
        'four-region production scalar and three negative controls',
        'frozen generated D4 directional reference source',
        'full assembled Core timing probe phases reserved tails and negative controls CPU',
        'full staged bilingual Core and nrchain compile/link',
        'full-input replay actual WARP capture and independent reader; isolated model lab compile',
        'generated legacy-post light fixture compiled without GPU dispatch',
        'independent face C1 and original endpoints production WARP',
        'independent face production C1 negative control',
        'legacy stage capture lifecycle and shared diagnostic lease',
        'legacy-post light controls production WARP; synthetic Neural only',
        'legacy-post production scalar and four negative controls',
        'manifest source hashes and actual numeric PE identities',
        'native command creation and lifecycle; full state and negative restore WARP',
        'post transformation exact adapter pair and negative controls',
        'process-local automatic GPU sampling policy and concurrent phase snapshots CPU',
        'production PRE POST actual storage quantization and wrong-target WARP',
        'production PRE POST rotating motion slots exact fence and descriptor readback WARP',
        'production PRE disabled jitter and POST resolved zero binding CPU',
        'production RR depth cs_5_0 and private R32 WARP readback',
        'production RR notifications cannot retain old blocks or borrow Feature1 recovery CPU',
        'production UNORM interval coverage mismatch and late-phase collection CPU',
        'production UNORM legal interval endpoints complete coverage and slot reuse WARP',
        'production bounded submitted-retirement and output binding WARP',
        'production bulk-copy destination scope and old-global negative control CPU',
        'production codec FLOAT exposure finite extreme and composition regression WARP',
        'production descriptor provenance native uses RTV targets and Execute positions WARP',
        'production jitter migration Win32 persistence fresh processes and negative controls CPU',
        'production layered NGX lifecycle and ownership with actual Win32 locks',
        'production motion slot selection and old-first-match negative control CPU',
        'production native Clear CPU format declaration and fresh-call rejection WARP',
        'production native Clear CPU request and exact fresh-call guard CPU',
        'production native boundary detours and immutable scoped window CPU',
        'production one-shot queue retirement WARP',
        'production ordinary paused sampling equivalence and late-readback phase WARP',
        'production output-state-only barriers without diagnostic dispatch or readback WARP',
        'production oversized native copies preserve unrelated Output CPU evidence WARP',
        'production paused current-phase read-only boundary and historical-store isolation CPU',
        'production post-store readback with unused-target negative control WARP',
        'production read-only post-boundary callbacks and identity isolation CPU',
        'production recovery snapshot permits fresh proof after historical evidence loss CPU',
        'production same-process semantic recovery with real descriptor submit and readback WARP',
        'production typed R8 exposure versus R32 normalized control WARP',
        'production typed native color admission descriptors and storage WARP',
        'recommended independent negative control',
        'route-compatibility-runtime-cpu',
        'test_picture_integration.py',
        'test_post_patch.py',
        'tests/multi_api/test_rr_post_diagnostics.py',
        'tests/test_compat_route.py',
        'tests/test_face_selection_ui.py',
        'tests/test_imgui_abi.py',
        'tests/test_light_style.py',
        'tests/test_lighting_edge_weights.py',
        'tests/test_picture_reset.py',
        'tests/test_rr_depth_device_identity.py',
        'tests/test_rr_depth_projection.py',
        'tests/test_rr_current_projection.py',
        'tests/test_runtime_timing_summary.py',
        'tests/test_settings_notices_ui.py',
        'tests/test_ui_refresh.py',
        'ui-activity-receipt-runtime-cpu',
        'validate-bug-sweep.py',
        'validate-color-edit-field.py',
        'validate-color-policy.py',
        'validate-command-creation.py',
        'validate-compatibility-verdict-feedback.py',
        'validate-compatibility-verdict.py',
        'validate-config-policy.py',
        'validate-core-feedback.py',
        'validate-edit-field.py',
        'validate-f8-controller.py',
        'validate-face-capture-geometry.py',
        'validate-feedback-combined.py',
        'validate-first-recording.py',
        'validate-native-format-observer.py',
        'validate-optional-layer-probe.py',
        'validate-recommended-picture.py',
        'validate-reset-notification-gap.py',
        'validate-resource-recovery.py',
        'validate-resource-semantics.py',
        'validate-retirement-return.py',
        'validate-strength-migration.py',
        'validate-work-config.py',
    }
    if not required.issubset(result.get('checks', [])):
        raise ValueError('Focused required gate missing.')
    checked_file(build, 'validation.log', result['log_sha256'])
    for name, digest in manifest['files'].items():
        checked_file(stage, name, digest)
    if git('status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Packaging source must be committed and clean.')
    packaging_commit = git('rev-parse', 'HEAD')
    git('merge-base', '--is-ancestor', commit, packaging_commit)
    for changed in git('diff', '--name-only', commit, packaging_commit).splitlines():
        if not (changed.startswith('docs/') or changed in {
                'scripts/package-050-core-acceptance.py',
                'tests/multi_api/test_050_core_package.py'}):
            raise ValueError(f'Build inputs changed since validation: {changed}')
    binary_root = stage / 'core' / 'build'
    artifacts = result['artifacts']
    addons = sorted(n for n in artifacts if n.endswith('.addon64') and VERSION in n)
    if len(addons) != 2 or set(artifacts) != set(addons + ['nrchain_nvngx.dll']):
        raise ValueError('Expected two beta0.5-dline15 language cores and only the owned bridge.')
    binaries = {name: checked_file(binary_root, name, entry['sha256']) for name, entry in artifacts.items()}
    for name, data in binaries.items():
        if len(data) != artifacts[name]['bytes']:
            raise ValueError(f'Binary length mismatch: {name}')
    activation = checked_activation(activation_evidence, result['source_manifest_sha256'],
        manifest['files']['core/src/nr_before_sr.cpp'], artifacts['nrchain_nvngx.dll']['sha256'],
        sha((ROOT / 'tests/compat_nodlss_ngx_host.cpp').read_bytes()))
    gpu_evidence = {kind:checked_gpu_report(path,kind,result['source_manifest_sha256'],
        manifest['files']['core/src/nr_before_sr.cpp'],artifacts['nrchain_nvngx.dll']['sha256'])
        for kind,path in (('present',present_evidence),('native-fg',native_fg_evidence),('mfg',mfg_evidence))}
    ngx_build = json.loads(checked_file(binary_root, 'nrchain-build.json', result['nrchain_build_receipt_sha256']).decode('utf-8-sig'))
    if (ngx_build.get('schema') != 'nrchain-official-ngx-build-v1' or ngx_build.get('crt') != 'static /MT'
            or ngx_build.get('binary_sha256') != artifacts['nrchain_nvngx.dll']['sha256']):
        raise ValueError('Official SDK production bridge provenance mismatch.')
    ngx_license = checked_file(binary_root, 'NVIDIA-NGX-LICENSE.txt', 'dc2778a3283427285984cdb5b3f7f03eae7d8a06057f672f2999c5ee7fd4f67d')
    ngx_license = ngx_license.decode('windows-1252').encode('utf-8')
    notice = (ROOT / 'THIRD_PARTY_NOTICES.txt').read_bytes() + b'\n\nMinHook license\n' + (ROOT / 'third_party/minhook/LICENSE.txt').read_bytes() + b'\n\nNVIDIA RTX SDKs license (official loader object code)\n' + ngx_license
    guides = {language: (ROOT / f'docs/testing/DLINE15_CORE_ACCEPTANCE.{language}.md').read_bytes()
              for language in ('zh-CN', 'en')}
    face_assets = {}
    receipt = json.loads(checked_file(binary_root, 'face-backend.json', result['face_backend_receipt_sha256']))
    if receipt['enabled'] != result['face_backend_enabled']:
        raise ValueError('Face build receipt differs from validation.')
    if receipt['enabled']:
        if not all((face_runtime_root, face_model, face_model_license)):
            raise ValueError('Enabled experimental backend requires all pinned assets and licenses.')
        asset_pins = {
            'lib/onnxruntime.dll': 'dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea',
            'lib/onnxruntime_providers_shared.dll': 'a2b3a50956aa75a9879c8472bc7df4f7a8072bcd2db19a1b7d988e7688f293ef',
            'LICENSE': 'c250d6278f0b47a6439fb7592b08b58a55eb9f535aa49a1db63211c3f982b674',
            'ThirdPartyNotices.txt': 'e00f828e0a33de591a355ae6606d2625f5758da7d2c844db7821c9dd3e3647b6',
        }
        for name, digest in asset_pins.items():
            face_assets['nr_face/' + Path(name).name] = checked_file(face_runtime_root, name, digest)
        face_assets['nr_face/yunet-dynamic.onnx'] = checked_file(face_model.parent, face_model.name,
            'b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1')
        face_assets['nr_face/YUNET-LICENSE'] = checked_file(face_model_license.parent, face_model_license.name,
            'c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958')
    output.mkdir(parents=True, exist_ok=False)
    packages = []
    for addon in addons:
        language = 'en' if addon.endswith('.en.addon64') else 'zh-CN'
        provenance = {
            'schema': 'nr050-core-only-acceptance-v1', 'version': VERSION,
            'source_commit': commit, 'packaging_commit': packaging_commit, 'language': language,
            'source_manifest_sha256': sha(manifest_bytes),
            'validation_sha256': sha((build / 'validation.json').read_bytes()),
            'scope': 'D3D12 Core-only manual acceptance; not Manager/multi-API OTA',
            'single_nr_default': True, 'max_nr_passes': 4, 'dual_nr_default': False,
            'full_size_reference_default': False, 'full_size_reference_passes': [2, 3, 4],
            'processing_starts': ['Before', 'After', 'Present'],
            'processing_fallback': 'selected suffix; sticky until explicit reselection or process restart',
            'native_sr_forwarding': 'exactly once per intercepted native SR call; no invented SR on no-DLSS route',
            'legacy_native_sr_retry_disabled': True,
            'dual_nr_scales': 'manual rational 25%-100%; whole-scene matched residual',
            'generic_residual_default': False, 'generic_residual_domain': 'normalized D model storage; final D guards retained', 'experimental_algorithms_default_off': True,
            'light_style': 'D pre/post controls and three candidate presets; neutral by default',
            'compatibility_source': 'd7a6def43a58ea6c4b26c553547429a8ef88b735',
            'rr_depth_adapter': '0.4.7 standard positive view-Z adapter and verified device identity; integration awaits owner game test',
            'post_light_style_warp_verified': True, 'post_light_style_game_verified': False,
            'skin_protection_runtime': False,
            'face_isolation_runtime': receipt['enabled'],
            'face_isolation_default': False,
            'fine_regions_runtime': True,
            'fine_regions_default': False,
            'reflection_scope': 'model edits on surfaces with valid RR specular albedo and hit-distance guides',
            'low_grid_post_composition': True,
            'face_assets_included': bool(face_assets),
            'extra_strength_ui_range': [1, 4],
            'saved_extra_policy_version': 1,
            'jitter_policy_version': 1,
            'jitter_migration': 'once off, then respect user re-enable; native SR unchanged',
            'abc_new_changes_merged': True,
            'optional_layer_probe': 'metadata observer retained; region adapter separately validates and snapshots supported RR inputs',
            'model_baseline_comparison': 'manual seven-control reset with generation-bound undo; not a face-fix claim',
            'face_diagnostic': 'nine transient model-control groups; selected ROI and complete PNGs; exact picture restore; no face algorithm change',
            'source_verified': True, 'compile_verified': True, 'warp_verified': True,
            'nvidia_feature18_verified': True, 'game_runtime_verified': False, 'physical_present_verified': False,
            'verification': {'focused_cpu_warp_runner': {'verified':True,'nvidia_feature18_tested':False},
                'independent_real_nvidia': {'activation':activation,**gpu_evidence},
                'game_runtime_verified':False,'physical_present_verified':False,'whole_frame_performance':'unmeasured'},
            'five_percent_performance_gate': 'unmeasured', 'carrier_included': False,
            'ini_included': False, 'vendor_runtime_included': False,
            'official_ngx_sdk_loader_embedded': True, 'ngx_sdk_build': ngx_build,
            'real_reshade_cold_activation_verified': True, 'activation_evidence': activation,
            'binaries': {name: artifacts[name] for name in (addon, 'nrchain_nvngx.dll')},
        }
        entries = {addon: binaries[addon], 'nrchain_nvngx.dll': binaries['nrchain_nvngx.dll'],
'README.md': guides[language], 'LICENSES.txt': notice,
                   'GPU-VALIDATION.json': json.dumps(provenance['verification'],ensure_ascii=False,indent=2).encode('utf-8'),
                   'DLINE15_GAME_ACCEPTANCE.zh-CN.md': (ROOT / 'docs/testing/DLINE15_GAME_ACCEPTANCE.zh-CN.md').read_bytes(),
                   'DLINE15_BUILD_VALIDATION_20260912.md': (ROOT / 'docs/testing/DLINE15_BUILD_VALIDATION_20260912.md').read_bytes(),
                   'build-info.json': json.dumps(provenance, ensure_ascii=False, indent=2).encode('utf-8')}
        entries.update(face_assets)
        entries['SHA256.json'] = json.dumps({n: sha(v) for n, v in entries.items()}, ensure_ascii=False, indent=2).encode('utf-8')
        target = output / f'DLSS5-{VERSION}-{language}-D3D12-Core-Acceptance.zip'
        with zipfile.ZipFile(target, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
        with zipfile.ZipFile(target) as archive:
            if len(archive.namelist()) != len(entries) or archive.testzip() is not None:
                raise ValueError('ZIP verification failed.')
            for name, data in entries.items():
                if archive.read(name) != data:
                    raise ValueError(f'ZIP changed bytes: {name}')
        packages.append({'file': target.name, 'bytes': target.stat().st_size, 'sha256': sha(target.read_bytes())})
    index = {'version': VERSION, 'source_commit': commit, 'packaging_commit': packaging_commit, 'packages': packages}
    (output / 'packages.json').write_text(json.dumps(index, indent=2), encoding='utf-8')
    return index

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--face-runtime-root', type=Path)
    parser.add_argument('--face-model', type=Path)
    parser.add_argument('--face-model-license', type=Path)
    parser.add_argument('--activation-evidence', type=Path, required=True)
    parser.add_argument('--native-fg-evidence', type=Path, required=True)
    parser.add_argument('--mfg-evidence', type=Path, required=True)
    parser.add_argument('--present-evidence', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.build, args.output, args.face_runtime_root, args.face_model, args.face_model_license,
        args.activation_evidence, args.native_fg_evidence, args.mfg_evidence, args.present_evidence), ensure_ascii=False, indent=2))
