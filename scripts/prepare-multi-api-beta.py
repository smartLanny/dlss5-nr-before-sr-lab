#!/usr/bin/env python3
"""Stage the merged 0.4.5 compatibility candidate without changing the checkout.

Requires #131, #132 and the completed #134 merge. Generated-source availability
is not game verification. MFG work is deferred for the current owner test batch.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = 'beta0.4.5-multiapi-dev1'
UPSTREAM = '28aed4099b0fe1c207b20b5fee5364c0773c25c2'
PR131 = 'f88adc33d04803982d98336a3d26e7081bb66f9f'
PR132 = '2ab31a17388fb9cfe3c6394397df4cb555b7a172'
PR134 = '145689aee077fa51c54d0b1dba2d79bc38633b2c'
STATE_132 = '5e067f23051cc313a668e91332e82eef9a5bbcf1'
REQUIRED_ANCESTORS = (PR131, PR132, PR134)
INTEGRATION_CORE_PINS = {
    'src/nr_command_state.inl': STATE_132,
    'src/nr_settings_ui.inl': '085742e4e02bd434bb491ce23a9a1df3f370b2ec',
    'src/nr_color_policy.inl': 'e52bbc78b15647e8947de88afdbccf6ac7591361',
    'src/nr_color_amplification.h': '1d386e71884c31093de3cb66f7273f8f43d74d90',
    'src/nr_precision_ui.h': 'a0ea17f649ee372bf1089e3ad4438414c5b87335',
    'scripts/build.ps1': 'd5426edca3eefd52e88786bb7892284e2bf5f2d5',
}
BRIDGE_BLOBS = {
    'src/bridge.inc': '244e3ae373b33b40763dc36e8ce967e152cbb505',
    'src/dlss5-bridge.cpp': 'f1337f31cfe3b42a950cb3df2fc83d2190ec5506',
    'src/vkmirror.inc': '8f21f435262b627b8445c215bc975e96205d6312',
}

def git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f'Cannot load source adapter {path.name}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def is_ancestor(root: Path, commit: str) -> bool:
    return subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor',
        commit, 'HEAD'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0

def configure(core: Path):
    for ancestor in REQUIRED_ANCESTORS:
        if not is_ancestor(core, ancestor):
            raise ValueError(f'Missing integration ancestor {ancestor}; merge, do not copy its files')
    # Resolve all reviewed overrides before exposing a configured transformer.
    # The library retains its original pins as a historical standalone fixture.
    for path, expected in INTEGRATION_CORE_PINS.items():
        actual = git(core, 'rev-parse', 'HEAD:' + path).decode().strip()
        if actual != expected:
            raise ValueError(f'Integrated source changed: {path}; review and repin, do not restore old UI')
    m = load('bridge_045_prerequisite', core / 'scripts/prepare-native-bridge-045.py')
    fmt = load('format_patch', core / 'scripts/patch-format-access.py')
    m.EXPERIMENT = m.legacy.EXPERIMENT = VERSION
    m.legacy.UPSTREAM = UPSTREAM
    m.legacy.UPSTREAM_BLOBS = dict(BRIDGE_BLOBS)
    m.legacy.CORE_BLOBS.update(INTEGRATION_CORE_PINS)
    prior_patch = m.patch_core
    def patch_core(text: str) -> str:
        return fmt.patch_core(prior_patch(text))
    m.legacy.patch_core = patch_core
    return m

def file_hashes(root: Path) -> dict[str, str]:
    result = {}
    for p in sorted(root.rglob('*')):
        if p.is_symlink():
            raise ValueError('Generated sources must not contain symlinks')
        if p.is_file() and p != root / 'source-manifest.json':
            result[p.relative_to(root).as_posix()] = hashlib.sha256(p.read_bytes()).hexdigest()
    return result

def finalize(core: Path, stage: Path, source: str) -> None:
    names = stage / 'core/src/interop/nr_deployment_names.h'
    text = names.read_text(encoding='utf-8')
    old = 'return Equal(name, L"dlss5-native-carrier-045-exp3.addon64");'
    if text.count(old) != 1:
        raise ValueError('Companion identity anchor changed')
    names.write_text(text.replace(old,
        'return Equal(name, L"dlss5-native-carrier-multiapi-dev1.addon64");'),
        encoding='utf-8', newline='\n')
    deployment = stage / 'core/src/interop/nr_dx11_deployment.inl'
    text = deployment.read_text(encoding='utf-8')
    old = ('return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire) ||\n'
           '        !nr_dx11_deployment::Read().complete;')
    if text.count(old) != 1:
        raise ValueError('Deployment policy anchor changed')
    deployment.write_text(text.replace(old,
        'return nr_dx11_deployment::duplicate_carriers.load(std::memory_order_acquire);'),
        encoding='utf-8', newline='\n')
    mf = stage / 'source-manifest.json'
    manifest = json.loads(mf.read_text(encoding='utf-8'))
    manifest['integration'] = {
        'version': VERSION, 'source_commit': source,
        # Retain the original dual-parent field for existing artifact readers.
        'parents_in_history': [PR131, PR132],
        'required_ancestors': list(REQUIRED_ANCESTORS),
        'picture_pr134_ancestor': PR134,
        'picture_source_pins': INTEGRATION_CORE_PINS,
        'bridge_release': 'v1.4.12', 'bridge_commit': UPSTREAM,
        'reset_source_blob': STATE_132, 'feedback': 'F8 single TXT; F7 optional paired archive',
        'format_path': 'typed FP16/FP32 SceneColor read -> existing FP16 codec',
        'optional_inventory_blocks_on_failure': False,
        'vulkan_native': 'disabled: submit synchronization not implemented',
        'synthetic_legacy': 'not integrated', 'rr_post': 'not implemented',
        'fg_control': 'not implemented', 'getdesc_spoof': 'not implemented',
        'mfg_scope': 'deferred by owner; not a gate for this compatibility development round',
        'game_validation': 'owner will test final batch; old BG3 result retained separately',
    }
    manifest['files'] = file_hashes(stage)
    mf.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    if json.loads(mf.read_text(encoding='utf-8'))['files'] != file_hashes(stage):
        raise ValueError('Generated manifest mismatch')

def prepare(core: Path, upstream: Path, output: Path) -> None:
    if output.exists():
        raise ValueError('Output already exists')
    if git(core, 'status', '--porcelain', '--untracked-files=no').strip():
        raise ValueError('Commit reviewed source changes first')
    if git(upstream, 'rev-parse', 'HEAD').decode().strip() != UPSTREAM:
        raise ValueError('Upstream is not the pinned v1.4.12 commit')
    source = git(core, 'rev-parse', 'HEAD').decode().strip()
    m = configure(core)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nr-integration-', dir=output.parent) as tmp:
        stage = Path(tmp) / 'candidate'
        m.prepare(core, upstream, stage)
        finalize(core, stage, source)
        if git(core, 'rev-parse', 'HEAD').decode().strip() != source or \
                git(core, 'status', '--porcelain', '--untracked-files=no').strip():
            raise ValueError('Source changed during generation')
        if output.exists():
            raise ValueError('Output appeared during generation')
        stage.rename(output)

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--core', type=Path, default=ROOT)
    ap.add_argument('--upstream', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())
    print(f'Staged {VERSION}: source only, not game-verified')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
