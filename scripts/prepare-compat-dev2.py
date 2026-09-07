#!/usr/bin/env python3
"""Compatibility alias for the single canonical multi-API staging pipeline.

The recovered upload used nested callback wrappers. The canonical generator
now includes those transformations exactly once, including descriptor, UI,
source identity and manifest updates. This alias never maintains another core.
"""
from __future__ import annotations
import argparse
import importlib.util
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = 'f367feac9da5ebf26b0f472f94c4440565664c8d'
VERSION = 'beta0.4.5-multiapi-dev2'

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f'Cannot import {path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def configure(parent, core):
    # Kept for callers of the recovered script; deliberately no callback wrap.
    if not parent.is_ancestor(core, BASE):
        raise ValueError('Missing actual #133 ancestry')
    if parent.VERSION != VERSION:
        raise ValueError('Canonical pipeline identity does not match dev2')
    return parent

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--core', type=Path, default=ROOT)
    ap.add_argument('--upstream', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    core = args.core.resolve()
    parent = configure(load('canonical_transaction', core/'scripts/prepare-multi-api-beta.py'), core)
    parent.prepare(core, args.upstream.resolve(), args.output.resolve())
    print(f'{VERSION}: canonical generated source; not game verified')

if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
