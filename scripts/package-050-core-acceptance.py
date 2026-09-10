#!/usr/bin/env python3
"""Package a focused, verified 0.4.7beta-corefix.5 D3D12 Core acceptance build, without carrier.

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
VERSION = '0.4.7beta-corefix.5'

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

def package(build: Path, output: Path) -> dict:
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
            or result.get('nvidia_feature18_verified') is not False
            or result.get('game_runtime_verified') is not False
            or result.get('five_percent_performance_gate') != 'unmeasured'
            or manifest.get('source_commit') != commit
            or manifest['integration'].get('source_commit') != commit
            or manifest['integration'].get('version') != VERSION):
        raise ValueError('Exact-source focused acceptance evidence required; no capability promotion.')
    required = {'canonical pinned full-source staging', 'validate-work-config.py',
                'validate-resource-semantics.py', 'validate-resource-recovery.py',
                'validate-native-format-observer.py', 'validate-compatibility-verdict.py',
                'validate-compatibility-verdict-feedback.py', 'validate-feedback-combined.py',
                'combined F8 capacity UTF8 privacy and end-verdict negative controls',
                'combined F8 current unknown store cannot inherit historical match',
                'production recovery snapshot permits fresh proof after historical evidence loss CPU',
                'production RR notifications cannot retain old blocks or borrow Feature1 recovery CPU',
                'production layered NGX lifecycle and ownership with actual Win32 locks',
                'production native boundary detours and immutable scoped window CPU',
                'production typed native color admission descriptors and storage WARP',
                'production PRE POST actual storage quantization and wrong-target WARP',
                'production UNORM interval coverage mismatch and late-phase collection CPU',
                'production UNORM legal interval endpoints complete coverage and slot reuse WARP',
                'production same-process semantic recovery with real descriptor submit and readback WARP',
                'production codec FLOAT exposure finite extreme and composition regression WARP',
                'production typed R8 exposure versus R32 normalized control WARP',
        'bounded UI activity alternating/stall/scope/reopen CPU gate',
                'ui-activity-receipt-runtime-cpu',
                'descriptor-alias-runtime-cpu',
                'production paused current-phase read-only boundary and historical-store isolation CPU',
                'validate-color-policy.py', 'validate-bug-sweep.py',
                'validate-strength-migration.py', 'validate-f8-controller.py',
                'tests/test_ui_refresh.py', 'tests/multi_api/test_rr_post_diagnostics.py',
                'tests/test_imgui_abi.py', 'tests/test_rr_depth_device_identity.py',
                'bounded RR projection CPU copy',
                'validate-command-creation.py', 'validate-first-recording.py',
                'validate-reset-notification-gap.py', 'validate-core-feedback.py',
                'first-recording old-policy CPU negative control',
                'first-recording real Create Capture Restore native API',
                'first-recording old-policy native API negative control', 'tests/test_rr_depth_projection.py',
                'tests/test_compat_route.py', 'tests/test_lighting_edge_weights.py', 'tests/test_picture_reset.py',
                'native command creation and lifecycle; full state and negative restore WARP',
                'production one-shot queue retirement WARP',
                'production bounded submitted-retirement and output binding WARP',
                'production post-store readback with unused-target negative control WARP',
                'process-local automatic GPU sampling policy and concurrent phase snapshots CPU',
                'production ordinary paused sampling equivalence and late-readback phase WARP',
                'tests/test_runtime_timing_summary.py',
                'production jitter migration Win32 persistence fresh processes and negative controls CPU',
                'production PRE disabled jitter and POST resolved zero binding CPU',
                'exact submitted fence event wait and scoped CPU timing',
                'actual RR timing carrier original-call preservation and no Feature1 recovery borrowing CPU',
                'full assembled Core timing probe phases reserved tails and negative controls CPU',
                'final F8 typed timing numbers privacy disk output and actual summary CPU',
                'production output-state-only barriers without diagnostic dispatch or readback WARP',
                'route-compatibility-runtime-cpu', 'descriptor-provenance-cpu',
                'production descriptor provenance native uses RTV targets and Execute positions WARP',
                'production read-only post-boundary callbacks and identity isolation CPU',
                'production RR depth cs_5_0 and private R32 WARP readback',
                'validate-recommended-picture.py', 'recommended independent negative control',
                'test_post_patch.py', 'test_picture_integration.py',
                'validate-edit-field.py', 'validate-color-edit-field.py',
                'all staged production SM5 entries compiled',
                'full staged bilingual Core and nrchain compile/link',
                'combined low-grid WARP fixture; constructed neural planes, no NVIDIA NR',
                'manifest source hashes and actual numeric PE identities'}
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
        raise ValueError('Expected two 0.4.7beta-corefix.5 language cores and only the owned bridge.')
    binaries = {name: checked_file(binary_root, name, entry['sha256']) for name, entry in artifacts.items()}
    for name, data in binaries.items():
        if len(data) != artifacts[name]['bytes']:
            raise ValueError(f'Binary length mismatch: {name}')
    notice = (ROOT / 'THIRD_PARTY_NOTICES.txt').read_bytes() + b'\n\nMinHook license\n' + (ROOT / 'third_party/minhook/LICENSE.txt').read_bytes()
    guide = (ROOT / 'docs/testing/CORE_047BETA_COREFIX5_ACCEPTANCE.zh-CN.md').read_bytes()
    english_guide = (ROOT / 'docs/testing/CORE_047BETA_COREFIX5_ACCEPTANCE.en.md').read_bytes()
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
            'single_nr': True, 'experimental_algorithms_default_off': True,
            'source_verified': True, 'compile_verified': True, 'warp_verified': True,
            'nvidia_feature18_verified': False, 'game_runtime_verified': False,
            'five_percent_performance_gate': 'unmeasured', 'carrier_included': False,
            'ini_included': False, 'vendor_runtime_included': False,
            'binaries': {name: artifacts[name] for name in (addon, 'nrchain_nvngx.dll')},
        }
        entries = {addon: binaries[addon], 'nrchain_nvngx.dll': binaries['nrchain_nvngx.dll'],
                   'README.zh-CN.md': guide, 'README.en.md': english_guide, 'LICENSES.txt': notice,
                   'build-info.json': json.dumps(provenance, ensure_ascii=False, indent=2).encode('utf-8')}
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
    args = parser.parse_args()
    print(json.dumps(package(args.build, args.output), ensure_ascii=False, indent=2))
