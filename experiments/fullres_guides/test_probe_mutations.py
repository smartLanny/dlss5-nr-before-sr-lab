"""Deliberate failures in helpers actually called by Session and the addon."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    source = (root / 'probe_state.hpp').read_text(encoding='utf-8')
    cases = [
        ('stale-generation', 'produced.epoch == view.epoch', 'true', 'foreign/reset/future preview rejected'),
        ('removed-fence', 'observed == UINT64_MAX || target == UINT64_MAX', 'target == UINT64_MAX', 'device loss not an endless wait'),
        ('lost-timeout', 'now - submitted_at >= timeout_ms', 'now - submitted_at > timeout_ms', 'deadline latches failure'),
        ('ignored-consent', 'enabled && confirmed && !unavailable', '(confirmed || !confirmed) && enabled && !unavailable', 'explicit consent gate'),
    ]
    with tempfile.TemporaryDirectory(prefix='nr-probe-regression-') as tmp:
        work = Path(tmp)
        for file in ('policy.hpp', 'test_probe_state.cpp'):
            (work / file).write_bytes((root / file).read_bytes())
        for name, old, new, diagnostic in cases:
            if source.count(old) != 1:
                raise RuntimeError(f'{name}: ambiguous mutation anchor')
            (work / 'probe_state.hpp').write_text(source.replace(old, new), encoding='utf-8')
            exe = work / (name + '.exe')
            subprocess.run([args.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            str(work / 'test_probe_state.cpp'), '-o', str(exe)], check=True,
                           capture_output=True, text=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            if run.returncode != 1 or f'FAIL: {diagnostic}' not in run.stderr:
                raise RuntimeError(f'{name}: wrong failure: {run}')
            print(f'PASS negative control {name}: exact assertion detected')
    print('4 intentional probe defects detected; Windows/Shader/GPU not executed')


if __name__ == '__main__':
    main()
