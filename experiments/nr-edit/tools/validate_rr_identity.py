"""Validate the isolated B2 implementation with real WARP and exact mutations.

This never loads NVIDIA, an NR runtime, a game or the product entry. The native
callback and envelope in this fixture are controlled; Core adapter is separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'src/nr_rr_identity_dx12.cpp'
    host = ROOT / 'tests/rr_identity_gpu.cpp'
    original = source.read_text(encoding='utf-8')
    variants = [
        ('positive', None, None, 0, 'assertions passed'),
        ('copy-removed', 'list->CopyResource(c->copy,f.color.resource);',
         '// precise negative: no identity copy', 41,
         'entire physical copy exact including alpha/outside nonzero subrect'),
        ('restore-removed', 'p.write(p.object,"Color",use_typed,use_typed?typed.value:untyped.value);',
         '// precise negative: no parameter restore', 41, 'identity exactly once'),
        ('double-native', '++out.native_calls;out.result=native(user,list,p);\n    unsigned observed=0;',
         '++out.native_calls;out.result=native(user,list,p);\n'
         '++out.native_calls;out.result=native(user,list,p);\n    unsigned observed=0;',
         41, 'identity exactly once'),
        ('quarantine-off-bypass', 'if(c&&c->poisoned){out.reason=Reason::OpaqueFailure;out.result=fatal;return out;}',
         '// precise negative: disabled path escapes quarantine', 41,
         'switching off cannot escape quarantine'),
    ]
    report = {'scope': 'isolated real D3D12 WARP; controlled native copy/envelope/lease adapter',
              'ngx_inference': False, 'game': False, 'production_adapter': False,
              'sources': {str(x.relative_to(ROOT)): digest(x) for x in
                          [source, host, ROOT / 'src/nr_rr_identity_dx12.hpp']},
              'compiler': {'path': str(a.compiler), 'sha256': digest(a.compiler)},
              'runs': [], 'passed': False}
    for name, needle, replacement, expected, diagnostic in variants:
        directory = a.output / name
        directory.mkdir(exist_ok=True)
        text = original
        if needle is not None:
            if text.count(needle) != 1:
                raise RuntimeError(f'{name}: expected exactly one mutation anchor')
            text = text.replace(needle, replacement)
        generated = directory / source.name
        generated.write_text(text, encoding='utf-8')
        exe = directory / 'rr-identity.exe'
        compile_cmd = [str(a.compiler), '-std=c++20', '-O2', '-static', '-Wall',
                       '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(host),
                       str(generated), '-o', str(exe), '-ld3d12', '-ldxgi', '-ldxguid']
        row = {'name': name, 'expected_exit': expected, 'diagnostic': diagnostic,
               'mutated_source_sha256': digest(generated), 'steps': []}
        for stage, cmd in [('compile', compile_cmd), ('run', [str(exe)])]:
            start = time.monotonic()
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            log = r.stdout + r.stderr
            (directory / f'{stage}.log').write_text(log, encoding='utf-8')
            row['steps'].append({'stage': stage, 'command': cmd, 'exit_code': r.returncode,
                                 'seconds': time.monotonic() - start,
                                 'log_sha256': digest(directory / f'{stage}.log')})
            if stage == 'compile' and r.returncode:
                break
        row['passed'] = (len(row['steps']) == 2 and row['steps'][0]['exit_code'] == 0
                         and row['steps'][1]['exit_code'] == expected and diagnostic in log)
        report['runs'].append(row)
        if not row['passed']:
            break
    report['passed'] = len(report['runs']) == len(variants) and all(r['passed'] for r in report['runs'])
    receipt = a.output / 'validation.json'
    receipt.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'passed': report['passed'], 'receipt': str(receipt)}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
