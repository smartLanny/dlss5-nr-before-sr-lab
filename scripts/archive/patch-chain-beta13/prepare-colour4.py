#!/usr/bin/env python3
"""Stage colour4 from the reviewed colour3 line, with inherited pin/transaction gates.

This is source generation, not Windows compilation or game validation. The old
colour-lab entry remains available for a colour3 baseline; no existing output
or game installation is overwritten.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
COLOUR3 = '3ae7249c1d7c482ffe9051244a4e12a99d508386'
SOURCE_GATES = '6126c9068ae04f72008d453fb21fccc8738eef25'
EXTRA_INPUTS = ('scripts/prepare-colour4.py', 'scripts/build-colour4.ps1',
                'tests/test_colour_stage.py', 'tests/run_colour_stage_math.py',
                'tests/colour_stage_math_test.cpp')


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f'Cannot load {path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare(core: Path, upstream: Path, output: Path) -> None:
    base = load('colour4_uniform', core/'scripts/prepare-multi-api-beta.py')
    previous = load('colour4_previous', core/'scripts/prepare-colour-lab.py')
    stage_patch = load('colour4_stage_patch', core/'scripts/patch-colour-stage.py')
    for commit in (previous.BASE, COLOUR3, SOURCE_GATES):
        if not base.is_ancestor(core, commit):
            raise ValueError('Missing reviewed colour3/source-gate ancestor: '+commit)
    inputs = {}
    for path in previous.INPUTS + stage_patch.INPUTS + EXTRA_INPUTS:
        data = base.git(core, 'show', 'HEAD:'+path)
        local = core/path
        if local.is_symlink() or local.read_bytes() != data:
            raise ValueError('Dirty/untracked colour4 input: '+path)
        inputs[path] = data
    m1 = load('colour4_m1', core/'scripts/patch-colour-lab.py')
    m2 = load('colour4_m2', core/'scripts/patch-colour-restore.py')
    prior = base.finalize
    def finalize(core_root: Path, stage: Path, source: str) -> None:
        prior(core_root, stage, source)
        m1.apply(stage, inputs)
        m2.apply(stage, inputs)
        # Check the actual inherited export BEFORE colour4. Do not weaken the
        # colour3 checks or relabel a partial/source-only fixture as an export.
        subprocess.run([sys.executable, str(core/'tests/test_colour_restore.py'),
                        '--repo', str(core), '--generated', str(stage/'core'), '-v'], check=True)
        stage_patch.apply(stage, inputs, m2)
        subprocess.run([sys.executable, str(core/'tests/test_colour_stage.py'),
                        '--generated', str(stage/'core'), '-v'], check=True)
        mf = stage/'source-manifest.json'
        manifest = json.loads(mf.read_text(encoding='utf-8'))
        manifest['colour_lab'] = {
            'version': stage_patch.VERSION, 'source_commit': source,
            'base_commit': COLOUR3, 'source_gates_commit': SOURCE_GATES,
            'milestone': 'M3c-early-raw-reference-and-neutral-consumer',
            'default_mode': 1, 'default_restore_mode': 1, 'default_input_curve': 0,
            'default_scaled_proxy_mode': 0, 'neutral_probe_default': False,
            'neutral_probe_persisted': False, 'neutral_probe_flag': 33554432,
            'rgb_restore_implemented': True, 'raw_reference_before_aesthetics': True,
            'raw_reference_retains_master_readiness_gamut_ratio_finite': True,
            'neutral_probe_changes_model_output_or_history': False,
            'captures_keep_actual_neural_output': True,
            'capture_native_sr_output': False,
            'v2_capture_proxy_neural_are_resampled_to_composition_grid': True,
            'extra_nr_invocations': 0, 'extra_gpu_passes': 0,
            'restore_selection_resets_history': False,
            'input_curve_selection_resets_history': True,
            'default_model_input_changed': False, 'runtime_verified': False,
            'overlay_sha256': {p: hashlib.sha256(b).hexdigest() for p, b in inputs.items()},
        }
        manifest['files'] = base.file_hashes(stage)
        mf.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if json.loads(mf.read_text(encoding='utf-8'))['files'] != base.file_hashes(stage):
            raise ValueError('Colour4 staged-file manifest mismatch')
    base.finalize = finalize
    base.prepare(core, upstream, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
    print('Staged beta0.5-dline21-colour4. Windows/GPU/game verification not implied.')
