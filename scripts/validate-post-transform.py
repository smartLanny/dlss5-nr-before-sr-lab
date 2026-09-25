"""Fail-closed provenance for the exact audited post-adapter revisions."""
from __future__ import annotations
import argparse
import hashlib
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OLD = '0542d143584a8eb56364a54c2ad8daae46fcc9c3'
ADAPTER = '035040cc833053f938afbcdb99521a48551ee5e1'
D10_ADAPTER = 'f1093ac98eb2069a85194908e93aa7765139441f'
D11_ADAPTER = 'dd9606498e5283d859999a234db1302f2da87adb'
D14_ADAPTER = 'd8f1bac7e50333c523e88414e6c93376c22891c2'
AREA = '00e7eecadbb08278fe664c23297d7ca8068776b5'
PATCH_PATH = 'scripts/patch-post-compat.py'
AREA_PATH = 'src/interop/nr_post_area.hlsl'

def git(*args: str) -> bytes:
    return subprocess.check_output(['git', '-C', str(ROOT), *args])

def blob_id(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()

def shader_transform(data: bytes) -> bytes:
    start = b'    # Existing shader and root signature are reused.'
    end = b"    text=once(text,'NgxResult ProcessEvaluate(',"
    if data.count(start) != 1 or data.count(end) != 1:
        raise ValueError('Post shader transformation markers changed')
    a, b = data.index(start), data.index(end)
    if b <= a:
        raise ValueError('Invalid post shader transformation range')
    return data[a:b]

def verify_contents(base_pin: str, candidate_pin: str, base: bytes, candidate: bytes,
                    base_area: bytes, candidate_area: bytes) -> None:
    if base_pin != candidate_pin and (base_pin, candidate_pin) not in {
            (OLD, ADAPTER), (OLD, D10_ADAPTER), (ADAPTER, D10_ADAPTER),
            (OLD,D11_ADAPTER),(ADAPTER,D11_ADAPTER),(D10_ADAPTER,D11_ADAPTER),
            (OLD,D14_ADAPTER),(ADAPTER,D14_ADAPTER),(D10_ADAPTER,D14_ADAPTER),
            (D11_ADAPTER,D14_ADAPTER)}:
        raise ValueError('Unreviewed post transformation pair')
    if blob_id(base) != base_pin or blob_id(candidate) != candidate_pin:
        raise ValueError('Post transformation bytes do not match their pins')
    # D11 redirects exactly two raw NR fetch forms through the fused accessor.
    # D14's shader transform is byte-identical to D11; its changes are Motion
    # binding validation, diagnostic anchors and the resolved SRV writer guard.
    # Normalize only these exact pinned adapters, on either comparison side.
    def normalized(pin: str, data: bytes) -> bytes:
        if pin in {D11_ADAPTER, D14_ADAPTER}:
            return data.replace(b'LoadFinalNeuralStoredV2(int2(tid.xy))',b'NeuralColor.Load(int3(tid.xy, 0))').replace(b'LoadFinalNeuralStoredV2(int2(coord))',b'NeuralColor.Load(int3(coord, 0))')
        return data
    if shader_transform(normalized(base_pin, base)) != shader_transform(normalized(candidate_pin, candidate)):
        raise ValueError('Post shader transformation changed')
    if blob_id(base_area) != AREA or blob_id(candidate_area) != AREA:
        raise ValueError('Post area helper changed')

def verify(base_commit: str, candidate_commit: str, base_pin: str, candidate_pin: str) -> None:
    for value in (base_commit, candidate_commit, base_pin, candidate_pin):
        if not re.fullmatch(r'[0-9a-f]{40}', value):
            raise ValueError('Require exact Git identity')
    for commit, pin in ((base_commit, base_pin), (candidate_commit, candidate_pin)):
        if git('rev-parse', f'{commit}:{PATCH_PATH}').decode().strip() != pin:
            raise ValueError('Manifest post pin does not belong to its source commit')
    base, candidate = git('show', f'{base_commit}:{PATCH_PATH}'), git('show', f'{candidate_commit}:{PATCH_PATH}')
    base_area, candidate_area = git('show', f'{base_commit}:{AREA_PATH}'), git('show', f'{candidate_commit}:{AREA_PATH}')
    verify_contents(base_pin, candidate_pin, base, candidate, base_area, candidate_area)

def negative_controls() -> None:
    base, candidate, area = git('show', OLD), git('show', ADAPTER), git('show', AREA)
    verify_contents(OLD, OLD, base, base, area, area)
    verify_contents(OLD, ADAPTER, base, candidate, area, area)
    d10=git('show',D10_ADAPTER)
    verify_contents(OLD,D10_ADAPTER,base,d10,area,area)
    verify_contents(ADAPTER,D10_ADAPTER,candidate,d10,area,area)
    d11=git('show',D11_ADAPTER)
    verify_contents(OLD,D11_ADAPTER,base,d11,area,area)
    verify_contents(D10_ADAPTER,D11_ADAPTER,d10,d11,area,area)
    d14=git('show',D14_ADAPTER)
    for pin, content in ((OLD,base),(ADAPTER,candidate),(D10_ADAPTER,d10),
                         (D11_ADAPTER,d11),(D14_ADAPTER,d14)):
        verify_contents(pin,D14_ADAPTER,content,d14,area,area)
    if shader_transform(d11) != shader_transform(d14):
        raise AssertionError('D14 host-only adapter changes modified shader transformation')
    cases = [
        (ADAPTER, OLD, candidate, base, area, area),
        (OLD, '0' * 40, base, candidate, area, area),
        (OLD, ADAPTER, base, candidate + b'\n# changed', area, area),
        (OLD, ADAPTER, base, candidate, area, area + b'\n// changed'),
        (D14_ADAPTER, D11_ADAPTER, d14, d11, area, area),
        (OLD, D14_ADAPTER, base, d14 + b'\n# changed', area, area),
    ]
    for case in cases:
        try:
            verify_contents(*case)
        except ValueError:
            continue
        raise AssertionError('Unsafe post transformation was admitted')
    if shader_transform(base) != shader_transform(candidate):
        raise AssertionError('Reviewed route-only changes modified shader transformation')
    print('Post transform provenance: exact D1/0.4.7/D10/D11/D14 adapter pairs accepted; unchanged shader transform and 6 unsafe cases rejected.')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-commit')
    parser.add_argument('--candidate-commit')
    parser.add_argument('--baseline-pin')
    parser.add_argument('--candidate-pin')
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    if args.negative_control:
        negative_controls()
    else:
        verify(args.baseline_commit, args.candidate_commit, args.baseline_pin, args.candidate_pin)
        print('Post transformation provenance verified; shader transform and area helper unchanged.')
