#!/usr/bin/env python3
"""Compile/test the shared #208 scalar implementation and offline mask reader.

No runtime hooks, model execution or game access. SM5 is a compile-only check.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True, help='clang++/g++ compatible compiler')
    parser.add_argument('--out-dir', type=Path, default=ROOT / 'build/skin-protection')
    parser.add_argument('--skip-sm5', action='store_true', help='For non-Windows builds only')
    args = parser.parse_args()
    out = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    records = []

    def run(command, expected=0):
        result = subprocess.run([str(v) for v in command], capture_output=True, text=True)
        records.append({'command': [str(v) for v in command], 'exit_code': result.returncode,
                        'stdout': result.stdout, 'stderr': result.stderr})
        with (out/'command-history.jsonl').open('a', encoding='utf-8') as history:
            history.write(json.dumps(records[-1], ensure_ascii=False) + '\n')
        if result.returncode != expected:
            print(result.stdout, end='')
            print(result.stderr, end='')
            raise RuntimeError(f'Expected exit {expected}, got {result.returncode}: {command[0]}')
        return result.stdout

    def compile_cpp(source, name, include):
        binary = out / (name + '.exe')
        command = [args.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-static', '-I', include, source, '-o', binary]
        if source.name == 'skin_protection_test.cpp' and not args.skip_sm5:
            command.append('-ld3dcompiler')
        run(command)
        return binary

    header = ROOT / 'src/nr_skin_protection.h'
    source = ROOT / 'tests/skin_protection_test.cpp'
    test = compile_cpp(source, 'skin-protection-test', ROOT / 'src')
    arguments = [] if args.skip_sm5 else ['--sm5', header]
    print(run([test, *arguments]), end='')
    # The same production tests must detect coupling the two independent knobs.
    negative = out / 'negative-coupled'
    negative.mkdir(exist_ok=True)
    original = header.read_text(encoding='utf-8')
    needle = 'float colourWeight = mask.confidence * request.preserveColour;'
    assert original.count(needle) == 1
    (negative / header.name).write_text(original.replace(needle,
        'float colourWeight = mask.confidence * request.preserveLuma;'), encoding='utf-8')
    broken = compile_cpp(source, 'skin-protection-negative', negative)
    result = run([broken], expected=1)
    assert 'colour-only' in records[-1]['stderr']
    print('PASS negative control: luma/colour coupling rejected')
    fixture = compile_cpp(ROOT / 'tools/skin_protection_fixture.cpp', 'skin-protection-fixture', ROOT / 'src')
    samples = run([fixture, '--samples'])
    (out / 'synthetic-colour-selection.csv').write_text(samples, encoding='utf-8')
    # Exercise real binary PPM parsing with one warm seed, blue and grey.
    # This is authored synthetic data; it is not a real-image validation claim.
    ppm = out / 'synthetic-reference.ppm'
    pgm = out / 'synthetic-mask.pgm'
    ppm.write_bytes(b'P6\n3 1\n255\n' + bytes([194,125,92,26,77,204,128,128,128]))
    if pgm.exists():
        # Delete only the exact generated fixture in this output directory.
        pgm.unlink()
    print(run([fixture, '--original-srgb709-ppm', ppm, '--mask-pgm', pgm]), end='')
    data = pgm.read_bytes()
    assert data[:11] == b'P5\n3 1\n255\n' and data[11] > 128 and data[12:] == bytes([0,0])
    run([fixture, '--original-srgb709-ppm', ppm, '--mask-pgm', pgm], expected=2)
    malformed = out / 'truncated.ppm'
    malformed.write_bytes(b'P6\n3 1\n255\n' + bytes([1,2]))
    run([fixture, '--original-srgb709-ppm', malformed, '--mask-pgm', out/'rejected-mask.pgm'], expected=2)
    paths = [header, source, ROOT/'tools/skin_protection_fixture.cpp', Path(__file__).resolve()]
    report = {'scope': 'shared production scalar CPU + compile-only SM5 + synthetic offline mask',
              'warp': 'not run', 'runtime': 'not integrated', 'real_images': 'not evaluated',
              'game': 'not evaluated', 'sources': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
              'commands': records}
    (out/'validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS offline confidence data, output protection and truncated-input rejection')
    print(f'Validation record: {out / "validation.json"}')


if __name__ == '__main__':
    main()
