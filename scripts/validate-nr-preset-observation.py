"""Bounded real Feature18 observation: unspecified vs upstream's explicit 0.

The frozen production Core is unchanged. The existing host sets the one known
key on its actual Create parameter block only when explicitly requested here.
No unknown preset range, UI option, inference mock, or game installation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('core', 'compiler', 'reshade', 'nr-runtime', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    if out.exists():
        p.error('fresh output required; earlier observations are preserved')
    out.mkdir(parents=True)
    command = [sys.executable, str(ROOT / 'scripts/validate-compat-present-ngx.py'),
               '--core', str(a.core), '--compiler', str(a.compiler),
               '--reshade', str(a.reshade), '--nr-runtime', str(a.nr_runtime),
               '--output', str(out / 'host'), '--case', 'single', '--cold-bootstrap',
               '--compile-only']
    result = subprocess.run(command, capture_output=True)
    (out / 'compile.log').write_bytes(result.stdout + result.stderr)
    if result.returncode:
        return result.returncode
    exe = out / 'host/compat-present-ngx.exe'
    observations = []
    for label, explicit in [('unset-a', False), ('zero-a', True),
                            ('unset-b', False), ('zero-b', True)]:
        case = out / label
        case.mkdir()
        cmd = [str(exe), str(case), '1', '100'] + (['--preset-zero'] if explicit else [])
        start = time.monotonic()
        r = subprocess.run(cmd, capture_output=True, timeout=240)
        (case / 'run.log').write_bytes(r.stdout + r.stderr)
        if r.returncode:
            return r.returncode
        text = r.stdout.decode('utf-8', 'replace')
        expected = 'MODEL_PRESET=' + ('explicit-0' if explicit else 'unspecified')
        if expected not in text:
            raise RuntimeError('Host did not report the requested test contract')
        planes = {name: case / ('plane-p1-s100-layer1-role%d.bin' % role)
                  for name, role in [('P', 1), ('N', 2), ('E', 6)]}
        observations.append({'case': label, 'explicit_preset': 0 if explicit else None,
                             'process_seconds_including_startup_capture': time.monotonic() - start,
                             'planes': {name: {'sha256': sha(path), 'bytes': path.stat().st_size}
                                        for name, path in planes.items()}})
    same_input = len({o['planes']['P']['sha256'] for o in observations}) == 1
    repeatable = all(observations[i]['planes']['N'] == observations[i+2]['planes']['N']
                     for i in range(2))
    equal_output = observations[0]['planes']['N'] == observations[1]['planes']['N']
    receipt = {'schema': 'bounded-preset-observation-v1',
               'source_manifest_sha256': sha(a.core.parent / 'source-manifest.json'),
               'host_sha256': sha(ROOT / 'tests/compat_present_ngx_host.cpp'),
               'runtime_sha256': sha(a.nr_runtime),
               'known_value': {'value': 0, 'source_blob': '51906c6572cf49df803475d15a06fa161e5fcb2a',
                               'url': 'https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/8802b2b4/OptiScaler/Config.h'},
               'observations': observations, 'identical_actual_input': same_input,
               'repeatable_actual_output': repeatable, 'equal_between_variants': equal_output,
               'interpretation': ('no difference observed in this fixed condition' if same_input and repeatable and equal_output
                                  else 'difference or repeatability confound requires analysis'),
               'scope': 'actual signed Feature18 on synthetic SDR Present input; one layer, fixed parameters, placeholder guides and forced Reset; no game, texture superiority, temporal-history, GPU time or memory benefit claim'}
    (out / 'observation.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(receipt['interpretation'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
