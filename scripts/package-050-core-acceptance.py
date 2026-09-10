#!/usr/bin/env python3
"""Package the verified 0.5 D-line and 0.4.7 compatibility integration, without carrier.

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
VERSION = 'beta0.5-dline11'

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

def package(build: Path, output: Path, face_runtime_root: Path | None = None,
            face_model: Path | None = None, face_model_license: Path | None = None) -> dict:
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
            or manifest['integration'].get('source_commit') != commit
            or manifest['integration'].get('version') != VERSION):
        raise ValueError('Exact-source focused acceptance evidence required; no capability promotion.')
    required = {'dual policy admission/history and independent matched-residual numeric oracle',
                'dual spatial/fused residual and generic production WARP; synthetic NR; opaque failure retained',
                'canonical pinned full-source staging', 'validate-work-config.py',
                'face tracker exact lifetime WARP and optional native CPU worker',
                'independent face C1 and original endpoints production WARP',
                'independent face production C1 negative control',
                'engine region production adapter and snapshot WARP',
                'fine region composition production WARP',
                'full-input replay actual WARP capture and independent reader; isolated model lab compile',
                'legacy stage capture lifecycle and shared diagnostic lease',
                'frozen generated D4 directional reference source',
                'directional light production scalar and independent negative controls',
                'validate-color-policy.py', 'validate-bug-sweep.py',
                'validate-strength-migration.py', 'validate-f8-controller.py', 'validate-core-feedback.py',
                'validate-optional-layer-probe.py',
                'validate-face-capture-geometry.py', 'validate-config-policy.py', 'tests/test_settings_notices_ui.py',
                'face diagnostic group8 production GPU ring and invalid group9 WARP',
                'tests/test_face_selection_ui.py', 'tests/test_ui_refresh.py', 'tests/multi_api/test_rr_post_diagnostics.py',
                'tests/test_imgui_abi.py', 'tests/test_picture_reset.py', 'tests/test_light_style.py',
                'tests/test_rr_depth_device_identity.py',
                'four-region production scalar and three negative controls',
                'legacy-post production scalar and four negative controls',
                'post transformation exact adapter pair and negative controls',
                'generated legacy-post light fixture compiled without GPU dispatch',
                'legacy-post light controls production WARP; synthetic Neural only',
                'four-region light controls production WARP; synthetic Neural only',
                'bounded RR projection CPU copy',
                'validate-command-creation.py', 'validate-first-recording.py',
                'validate-reset-notification-gap.py', 'validate-core-feedback.py',
                'first-recording old-policy CPU negative control',
                'first-recording real Create Capture Restore native API',
                'first-recording old-policy native API negative control', 'tests/test_rr_depth_projection.py',
                'tests/test_compat_route.py', 'tests/test_lighting_edge_weights.py', 'tests/test_picture_reset.py',
                'native command creation and lifecycle; full state and negative restore WARP',
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
        raise ValueError('Expected two dline11 language cores and only the owned bridge.')
    binaries = {name: checked_file(binary_root, name, entry['sha256']) for name, entry in artifacts.items()}
    for name, data in binaries.items():
        if len(data) != artifacts[name]['bytes']:
            raise ValueError(f'Binary length mismatch: {name}')
    notice = (ROOT / 'THIRD_PARTY_NOTICES.txt').read_bytes() + b'\n\nMinHook license\n' + (ROOT / 'third_party/minhook/LICENSE.txt').read_bytes()
    guide = (ROOT / 'docs/testing/DLINE11_CORE_ACCEPTANCE.zh-CN.md').read_bytes()
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
            'single_nr': True, 'experimental_algorithms_default_off': True,
            'light_style': 'D pre/post controls and three candidate presets; neutral by default',
            'compatibility_source': '6a0ad7684683993328a7a98a9bea83ac76b01a64',
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
            'abc_new_changes_merged': False,
            'optional_layer_probe': 'metadata observer retained; region adapter separately validates and snapshots supported RR inputs',
            'model_baseline_comparison': 'manual seven-control reset with generation-bound undo; not a face-fix claim',
            'face_diagnostic': 'nine transient model-control groups; selected ROI and complete PNGs; exact picture restore; no face algorithm change',
            'source_verified': True, 'compile_verified': True, 'warp_verified': True,
            'nvidia_feature18_verified': False, 'game_runtime_verified': False,
            'five_percent_performance_gate': 'unmeasured', 'carrier_included': False,
            'ini_included': False, 'vendor_runtime_included': False,
            'binaries': {name: artifacts[name] for name in (addon, 'nrchain_nvngx.dll')},
        }
        entries = {addon: binaries[addon], 'nrchain_nvngx.dll': binaries['nrchain_nvngx.dll'],
                   'README.zh-CN.md': guide, 'LICENSES.txt': notice,
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
    args = parser.parse_args()
    print(json.dumps(package(args.build, args.output, args.face_runtime_root, args.face_model, args.face_model_license), ensure_ascii=False, indent=2))
