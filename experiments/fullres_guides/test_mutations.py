"""Compile deliberate defects in the real policy; require the intended assertion.
This does not compile any Windows, shader, SDK, or addon implementation.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    source = (root / 'policy.hpp').read_text(encoding='utf-8')
    cases = [
        ('native-priority', '(!s.enabled || !s.force_synthetic_diagnostic) && native_admissible',
         'false && native_admissible', 'native while experiment off'),
        ('removed-device', 'observed != std::numeric_limits<std::uint64_t>::max()',
         'true', 'device removed is not completion'),
        ('unsubmitted-history', '!submitted || !plan(f, backend).accepted',
         '(submitted && false) || !plan(f, backend).accepted', 'unsubmitted capture not accepted'),
        ('wrong-default', 'Backend fallback = Backend::amd;',
         'Backend fallback = Backend::nvidia;', 'off/AMD defaults'),
    ]
    with tempfile.TemporaryDirectory(prefix='nr-fullres-policy-') as temp:
        work = Path(temp)
        (work / 'test.cpp').write_bytes((root / 'test_policy.cpp').read_bytes())
        for name, old, new, failure in cases:
            if source.count(old) != 1:
                raise RuntimeError(f'{name}: source anchor no longer unique')
            (work / 'policy.hpp').write_text(source.replace(old, new), encoding='utf-8')
            exe = work / (name + '.exe')
            subprocess.run([args.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-pedantic', str(work / 'test.cpp'), '-o', str(exe)],
                           check=True, capture_output=True, text=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            if run.returncode != 1 or f'FAIL: {failure}' not in run.stderr:
                raise RuntimeError(f'{name}: wrong negative-control outcome: {run}')
            print(f'PASS negative control {name}: intended policy assertion detected')
    print('4 deliberate policy defects detected; no GPU or game used')


if __name__ == '__main__':
    main()
