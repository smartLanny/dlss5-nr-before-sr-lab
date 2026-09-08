#!/usr/bin/env python3
"""Stage a committed NVFP4 descendant of the exact dev14 owner baseline.

Run only after every writer has finished and the candidate is committed. The
historical dev14 generator and its pins remain unchanged. This entry validates
the old pins against dev14, checks the source/shader boundary, then supplies a
separate set of exact HEAD pins to the same canonical transformation pipeline.
It creates source only; it neither copies private assets nor deploys games.
"""
from __future__ import annotations
import argparse
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

def source_boundary(core: Path, source: str) -> dict:
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
            if Path(path).suffix not in ('.h', '.cpp', '.inl'):
                raise ValueError(f'Private/binary payload is not a source input: {path}')
    hashes = {}
    for path in sorted(p for p in baseline_paths if p.startswith('src/')):
        before = git(core, 'show', f'{BASELINE}:{path}')
        if shader_blocks(before):
            hashes[path] = require_same_shader(before, git(core, 'show', f'{source}:{path}'), path)
        elif Path(path).suffix == '.hlsl':
            after = git(core, 'show', f'{source}:{path}')
            if before != after:
                raise ValueError(f'Inherited HLSL source changed: {path}')
            hashes[path] = hashlib.sha256(before).hexdigest()
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
    shader_hashes = source_boundary(core, source)
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
        '--', 'kernels/nvfp4').decode().splitlines() if Path(path).suffix in ('.h', '.cuh')]
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
        generated_hash = require_same_shader(baseline_generated.encode('utf-8'),
            (stage / 'core/src/nr_before_sr.cpp').read_bytes(), 'canonical generated dev14')
        manifest_path = stage / 'source-manifest.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        manifest['nvfp4'] = {
            'experiment': EXPERIMENT_VERSION, 'experimental': True, 'baseline_commit': BASELINE,
            'source_commit': source, 'historical_dev14_pins': historical_pins,
            'candidate_source_pins': current_pins, 'inherited_shader_hashes': shader_hashes,
            'canonical_generated_shader_sha256': generated_hash,
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
