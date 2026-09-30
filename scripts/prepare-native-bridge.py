#!/usr/bin/env python3
"""Compatibility entry for the direct committed-source exporter.

Historical transformations are archived; this entry stages the current Core.
"""
from pathlib import Path
import importlib.util
ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('direct_core_source', ROOT / 'scripts/prepare-core-source.py')
assert _spec and _spec.loader
_direct = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_direct)
git = _direct.git
file_hashes = _direct.file_hashes
prepare = _direct.prepare
main = _direct.main
if __name__ == '__main__':
    main()
