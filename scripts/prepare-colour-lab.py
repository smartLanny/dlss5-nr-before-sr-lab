#!/usr/bin/env python3
"""Generate a colour1/M1 build, preserving all uniform3 pin and safety checks.

Example: python scripts/prepare-colour-lab.py --upstream <v1.4.12-checkout> --output <new-directory>
The generated core/scripts/build.ps1 is the normal Windows build entry.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = '58ce637c1fbb03d0e7d93b90d26b3e4bce7b03df'
INPUTS = ('scripts/prepare-colour-lab.py', 'scripts/patch-colour-lab.py',
          'src/nr_colour_lab_policy.h', 'src/nr_colour_lab_ui.inl')

def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f'Cannot load {path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def prepare(core: Path, upstream: Path, output: Path) -> None:
    base = load('colour_lab_uniform3', core / 'scripts/prepare-multi-api-beta.py')
    if not base.is_ancestor(core, BASE):
        raise ValueError('The reviewed D21 uniform3 base is not in this checkout history')
    # Do not execute a dirty/untracked overlay under a clean source identity.
    inputs = {}
    for path in INPUTS:
        data = base.git(core, 'show', 'HEAD:' + path)
        if (core / path).is_symlink() or (core / path).read_bytes() != data:
            raise ValueError(f'Uncommitted overlay input: {path}')
        inputs[path] = data
    patch = load('colour_lab_patch', core / 'scripts/patch-colour-lab.py')
    prior = base.finalize
    def finalize(core_root: Path, stage: Path, source: str) -> None:
        prior(core_root, stage, source)
        patch.apply(stage, inputs)
        mf = stage / 'source-manifest.json'
        manifest = json.loads(mf.read_text(encoding='utf-8'))
        manifest['colour_lab'] = {
            'version': patch.VERSION, 'base_commit': BASE, 'source_commit': source,
            'milestone': 'M1-colour-admission', 'default_mode': 1,
            'modes': {'0': 'legacy-configured', '1': 'colour-priority-experimental',
                      '2': 'conservative-hdr'},
            'rgb_restore_implemented': False,
            'shader_math_changed': False, 'model_history_changed': False,
            'runtime_verified': False,
            'overlay_sha256': {path: hashlib.sha256(data).hexdigest() for path, data in inputs.items()},
        }
        # Keep inherited integration identity as provenance, and expose the
        # selected product identity explicitly rather than pretending uniform3
        # and colour1 are the same binary. PE numeric version is inherited.
        manifest['files'] = base.file_hashes(stage)
        mf.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if json.loads(mf.read_text(encoding='utf-8'))['files'] != base.file_hashes(stage):
            raise ValueError('Colour-lab manifest mismatch')
    base.finalize = finalize
    base.prepare(core, upstream, output)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT)
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.core.resolve(), args.upstream.resolve(), args.output.resolve())
    print('Staged beta0.5-dline21-colour1: M1 source only; not a game validation.')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
