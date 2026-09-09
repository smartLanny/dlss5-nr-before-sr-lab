#!/usr/bin/env python3
"""Stage a committed NVFP4 descendant of the exact dev14 owner baseline.

Run only after every writer has finished and the candidate is committed. The
historical dev14 generator and its pins remain unchanged. This entry validates
the old pins against dev14, checks the source/shader boundary (including one
exactly pinned diagnostic-gate change), then supplies a
separate set of exact HEAD pins to the same canonical transformation pipeline.
It creates source only; it neither copies private assets nor deploys games.
"""
from __future__ import annotations
import argparse
import difflib
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile

BASELINE = 'a51141f32531e7e48b533e749f0f6edaaa17bd1d'
BASELINE_VERSION = 'beta0.5-dev14'
EXPERIMENT_VERSION = 'beta0.5-dev14-nvfp4-dev1'
ROOT = Path(__file__).resolve().parents[1]
ALLOWED_EXISTING = frozenset((
    'src/nr_before_sr.cpp', 'src/nr_settings_ui.inl', 'src/nr_feedback_core.inl',
    'config/nr_before_sr.ini', 'scripts/build.ps1', 'src/nr_before_sr.rc'))
NEW_CORE_FILES = frozenset((
    'src/nr_gpu_timing.h', 'src/nr_gpu_timing.inl', 'src/nr_nvfp4_core.inl',
    'src/nr_nvfp4_failure.h'))
KERNEL_ABI = 'kernels/nvfp4/nvfp4_kernel_abi.h'
DIAGNOSTIC_GATE_ID = 'combine-v2-diagnostic-gate-v1'
# Hash the complete raw HLSL block, not an arbitrary permitted function region.
# These are the exact source and canonical-generated before/after pairs. The
# only change defers diagnostic-only calculations to the existing statistics
# sampling gate; it does not change the diagnostic bit's scheduling or NR math.
DIAGNOSTIC_GATE_HASH_PAIRS = {
    'source': (
        'a279dac6758ca320b61623d784b929125349359e4602da345d9f57446556b9dc',
        'dee549cbf543a74c5b013165a20e8c926395cc78e601c3520c087dd0da8ebacd'),
    'canonical_generated': (
        '7cf7fe7ef5eee765387adf2ed28d1c7abeab3a6951388b32d043f1b7544eec1a',
        'eb10b9548d0b8c49d2b048f5c93c4a135004a976ff7162e89cbe611e302adc55'),
}

def git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

def load(path: Path):
    spec = importlib.util.spec_from_file_location('nvfp4_canonical_dev14', path)
    if spec is None or spec.loader is None:
        raise ValueError('Cannot load the canonical dev14 generator')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def shader_blocks(value: bytes) -> list[bytes]:
    return re.findall(rb'R"HLSL\((.*?)\)HLSL"', value, re.S)

def require_same_shader(before: bytes, after: bytes, label: str) -> str:
    a, b = shader_blocks(before), shader_blocks(after)
    if not a or a != b:
        raise ValueError(f'Inherited HLSL changed: {label}')
    return hashlib.sha256(b''.join(a)).hexdigest()

def shader_contract(before: bytes, after: bytes, label: str, *, approved_scope=None) -> dict:
    a, b = shader_blocks(before), shader_blocks(after)
    if not a or len(a) != len(b):
        raise ValueError(f'Inherited HLSL block boundary changed: {label}')
    baseline_hash = hashlib.sha256(b''.join(a)).hexdigest()
    candidate_hash = hashlib.sha256(b''.join(b)).hexdigest()
    change, exact_diff = None, ''
    if a != b:
        allowed = DIAGNOSTIC_GATE_HASH_PAIRS.get(approved_scope)
        if len(a) != 1 or (baseline_hash, candidate_hash) != allowed:
            raise ValueError(f'Inherited HLSL changed outside the exact diagnostic-gate allowlist: {label}')
        change = DIAGNOSTIC_GATE_ID
        exact_diff = ''.join(difflib.unified_diff(
            a[0].decode('utf-8').splitlines(keepends=True),
            b[0].decode('utf-8').splitlines(keepends=True),
            fromfile=label + ':baseline-HLSL', tofile=label + ':candidate-HLSL'))
    else:
        require_same_shader(before, after, label)
    return {'baseline_sha256': baseline_hash, 'candidate_sha256': candidate_hash,
            'hash_scope': 'Concatenated raw R"HLSL" block bytes; no whitespace/newline normalization',
            'approved_change': change, 'exact_diff': exact_diff,
            'exact_diff_sha256': hashlib.sha256(exact_diff.encode('utf-8')).hexdigest() if change else None}

def source_boundary(core: Path, source: str, shader_contracts: dict | None = None) -> dict:
    git(core, 'merge-base', '--is-ancestor', BASELINE, source)
    baseline_paths = set(git(core, 'ls-tree', '-r', '--name-only', BASELINE,
                            '--', 'src', 'scripts', 'config').decode().splitlines())
    changes = git(core, 'diff', '--name-only', BASELINE, source,
                  '--', 'src', 'scripts', 'config').decode().splitlines()
    for path in changes:
        if path in baseline_paths and path not in ALLOWED_EXISTING:
            raise ValueError(f'Outside isolated Core integration boundary: {path}')
        if path.startswith('src/') and path not in baseline_paths:
            if path not in NEW_CORE_FILES and not path.startswith('src/nvfp4/'):
                raise ValueError(f'Unexpected new production module: {path}')
            if Path(path).suffix not in ('.h', '.cpp', '.inl') and Path(path).name != 'README.md':
                raise ValueError(f'Private/binary payload is not a source input: {path}')
            if shader_blocks(git(core, 'show', f'{source}:{path}')):
                raise ValueError(f'Unexpected new HLSL block: {path}')
    hashes = {}
    contracts = {} if shader_contracts is None else shader_contracts
    for path in sorted(p for p in baseline_paths if p.startswith('src/')):
        before = git(core, 'show', f'{BASELINE}:{path}')
        after = git(core, 'show', f'{source}:{path}')
        if shader_blocks(before) or shader_blocks(after):
            contract = shader_contract(before, after, path,
                approved_scope='source' if path == 'src/nr_before_sr.cpp' else None)
            contracts[path] = contract
            # Retain the historical baseline meaning of this compatibility map.
            hashes[path] = contract['baseline_sha256']
        elif Path(path).suffix == '.hlsl':
            if before != after:
                raise ValueError(f'Inherited HLSL source changed: {path}')
            hashes[path] = hashlib.sha256(before).hexdigest()
            contracts[path] = {'baseline_sha256': hashes[path], 'candidate_sha256': hashes[path],
                               'hash_scope': 'Entire inherited .hlsl file bytes',
                               'approved_change': None, 'exact_diff': '', 'exact_diff_sha256': None}
    if git(core, 'diff', '--name-only', BASELINE, source, '--', 'src/nrchain_nvngx.cpp').strip():
        raise ValueError('NRChain bridge ABI/source changed')
    return hashes

def prepare(core: Path, upstream: Path, output: Path) -> None:
    if output.exists():
        raise ValueError('Output already exists; preserve earlier evidence')
    if git(core, 'status', '--porcelain', '--untracked-files=no').strip():
        raise ValueError('Commit the complete candidate before exact-source staging')
    if git(core, 'ls-files', '--others', '--exclude-standard', '--', 'src', 'kernels', 'scripts').strip():
        raise ValueError('Untracked source exists; finish and commit all writers first')
    source = git(core, 'rev-parse', 'HEAD').decode().strip()
    if source == BASELINE:
        raise ValueError('The NVFP4 changes must be committed after dev14')
    source_shader_contracts = {}
    shader_hashes = source_boundary(core, source, source_shader_contracts)
    canonical = load(core / 'scripts/prepare-multi-api-beta.py')
    historical_pins = dict(canonical.INTEGRATION_CORE_PINS)
    for path, expected in historical_pins.items():
        if git(core, 'rev-parse', f'{BASELINE}:{path}').decode().strip() != expected:
            raise ValueError(f'Historical dev14 pin does not match baseline: {path}')
    # Every historical transformer must retain its exact dev14 bytes. Overrides
    # exist only in this process and are recorded separately in the new manifest.
    for path in git(core, 'ls-tree', '-r', '--name-only', BASELINE, '--', 'scripts').decode().splitlines():
        if path != 'scripts/build.ps1' and git(core, 'show', f'{BASELINE}:{path}') != git(core, 'show', f'{source}:{path}'):
            raise ValueError(f'Historical transformer/test script changed: {path}')
    new_sources = git(core, 'ls-tree', '-r', '--name-only', source, '--', 'src/nvfp4').decode().splitlines()
    new_sources += sorted(NEW_CORE_FILES)
    kernel_headers = [path for path in git(core, 'ls-tree', '-r', '--name-only', source,
        '--', 'kernels/nvfp4', 'kernels/nvfp4_expansion').decode().splitlines() if Path(path).suffix in ('.h', '.cuh')]
    if KERNEL_ABI not in kernel_headers:
        raise ValueError('Required public kernel ABI header is not committed')
    for path in new_sources + kernel_headers:
        git(core, 'cat-file', '-e', f'{source}:{path}')
    current_pins = {path: git(core, 'rev-parse', f'{source}:{path}').decode().strip()
                    for path in sorted(set(historical_pins) | set(new_sources))}
    canonical.INTEGRATION_CORE_PINS = current_pins
    canonical.VERSION = EXPERIMENT_VERSION
    canonical.DISPLAY_VERSION = '0.5-dev14 NVFP4 dev1 experiment'
    if git(upstream, 'rev-parse', 'HEAD').decode().strip() != canonical.UPSTREAM:
        raise ValueError('Upstream is not the pinned canonical bridge source')
    transformer = canonical.configure(core)
    baseline_generated = transformer.legacy.patch_core(
        git(core, 'show', f'{BASELINE}:src/nr_before_sr.cpp').decode('utf-8').replace(
            BASELINE_VERSION, EXPERIMENT_VERSION))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nr-nvfp4-stage-', dir=output.parent) as tmp:
        stage = Path(tmp) / 'candidate'
        transformer.prepare(core, upstream, stage)
        canonical.finalize(core, stage, source)
        transformer.legacy.archive_sources(core, kernel_headers, stage / 'core')
        generated_contract = shader_contract(baseline_generated.encode('utf-8'),
            (stage / 'core/src/nr_before_sr.cpp').read_bytes(), 'canonical generated dev14',
            approved_scope='canonical_generated')
        generated_hash = generated_contract['candidate_sha256']
        manifest_path = stage / 'source-manifest.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        manifest['nvfp4'] = {
            'experiment': EXPERIMENT_VERSION, 'experimental': True, 'baseline_commit': BASELINE,
            'source_commit': source, 'historical_dev14_pins': historical_pins,
            'candidate_source_pins': current_pins, 'inherited_shader_hashes': shader_hashes,
            'canonical_generated_shader_sha256': generated_hash,
            'source_shader_contracts': source_shader_contracts,
            'canonical_generated_shader_contract': generated_contract,
            'shader_change_allowlist': {
                'id': DIAGNOSTIC_GATE_ID,
                'exact_hash_pairs': {scope: {'baseline_sha256': pair[0], 'candidate_sha256': pair[1]}
                                     for scope, pair in DIAGNOSTIC_GATE_HASH_PAIRS.items()},
                'scope': 'Only the complete pinned CombineV2 diagnostic-gate HLSL change; all other HLSL and NRChain remain unchanged'},
            'shader_hash_field_meanings': {
                'inherited_shader_hashes': 'Historical dev14 baseline hashes; see source_shader_contracts for actual candidate hashes and exact accepted differences',
                'canonical_generated_shader_sha256': 'Actual validated generated candidate HLSL hash; equal to the baseline until an explicitly allowed change is applied'},
            'kernel_abi_source_blob': git(core, 'rev-parse', f'{source}:{KERNEL_ABI}').decode().strip(),
            'kernel_header_source_pins': {path: git(core, 'rev-parse', f'{source}:{path}').decode().strip()
                                         for path in kernel_headers},
            'default_precision': 'Off / original FP8', 'nrchain_changed': False,
            'private_assets_included': False, 'nvidia_verified': False,
            'game_verified': False, 'performance_verified': False,
        }
        manifest['files'] = canonical.file_hashes(stage)
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        for path in new_sources:
            if 'core/' + path not in manifest['files']:
                raise ValueError(f'New Core file missing from source manifest: {path}')
        for path in kernel_headers:
            if 'core/' + path not in manifest['files']:
                raise ValueError(f'Kernel ABI/source header missing from source manifest: {path}')
        if manifest['files'] != canonical.file_hashes(stage):
            raise ValueError('Generated source hashes changed')
        if source != git(core, 'rev-parse', 'HEAD').decode().strip() or git(core, 'status', '--porcelain', '--untracked-files=no').strip():
            raise ValueError('Source changed during staging')
        transformer.legacy.publish_stage(stage, output)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())
    print('Staged exact-source dev14 NVFP4 experiment; no runtime or performance claim')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
