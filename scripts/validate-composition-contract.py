"""Independently verify bounded raw N planes and production pre/post metrics."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess


def verify(root, m=None):
    if m is None:
        m = json.loads((root / 'contract.json').read_text(encoding='utf-8'))
    assert m['schema'] in ('nr-composition-fixture-v1','nr-composition-fixture-v2')
    candidate = m['schema'] == 'nr-composition-fixture-v2'
    if candidate:
        assert m['new_algorithm'] and m['candidate'] == 'independent-chroma-v4' and m['candidate_flag'] == 4096
    assert m['synthetic'] and not m['nvidia_inference'] and not m['game_capture']
    assert m['cases_total'] == len(m['cases']) == 72
    hashes, total, different = {}, 0, 0
    for c in m['cases']:
        planes = {}
        layout = [('original',m['source_texture']),('proxy',c['N']),
                  ('neural',c['N']),('pre',m['output_texture']),('post',m['output_texture'])]
        if candidate:
            layout.append(('candidate',m['output_texture']))
        for name, extent in layout:
            path = root / f"case-{c['id']}-{name}.rgba32f"
            raw = path.read_bytes()
            assert len(raw) == math.prod(extent)*16, (path.name, 'actual N extent')
            total += len(raw)
            hashes[path.name] = hashlib.sha256(raw).hexdigest()
            planes[name] = list(struct.iter_unpack('<4f', raw))
        sums = [0.,0.,0.,0.,0.]
        for y in range(13):
            for x in range(13):
                a,b = (planes[n][(y+1)*17+x+2] for n in ('pre','post'))
                o = planes['original'][(y+2)*18+x+3]
                assert all(math.isfinite(v) for v in (*a,*b)) and a[3] == b[3] == o[3]
                luma=lambda p: .2126*p[0]+.7152*p[1]+.0722*p[2]
                sums[0] += sum(abs(a[i]-b[i]) for i in range(3))/3
                sums[1] += abs(luma(a)-luma(o)); sums[2] += abs(luma(b)-luma(o))
                if candidate:
                    v = planes['candidate'][(y+1)*17+x+2]
                    assert all(math.isfinite(t) for t in v) and v[3] == o[3]
                    assert c['strength'] != 0 or v == o, (c['id'],'candidate zero identity')
                    sums[3] += abs(luma(v)-luma(o))
                    sums[4] += sum(abs(v[i]-a[i]) for i in range(3))/3
        for measured, key in zip(sums, ('pre_post_rgb_mae','pre_luma_edit_mae','post_luma_edit_mae')):
            assert math.isclose(measured/169,c[key],rel_tol=2e-6,abs_tol=2e-8), (c['id'],key)
        if candidate:
            for measured,key in zip(sums[3:],('candidate_luma_edit_mae','candidate_pre_rgb_mae')):
                assert math.isclose(measured/169,c[key],rel_tol=2e-6,abs_tol=2e-8), (c['id'],key)
        different += sums[0] > 1e-6
    assert total == m['plane_bytes'] <= m['byte_budget'] == 8388608
    assert different == m['different_outputs'] and different > 0
    assert len(list(root.glob('*.rgba32f'))) == 72*(6 if candidate else 5)
    return hashes


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture',type=Path,required=True)
    ap.add_argument('--source',type=Path,required=True)
    ap.add_argument('--negative-control', choices=('raw-extent','metric'))
    a=ap.parse_args()
    if a.negative_control:
        m=json.loads((a.capture/'contract.json').read_text(encoding='utf-8'))
        if a.negative_control == 'raw-extent':
            m['cases'][0]['N'][0] += 1
            expected=('case-0-proxy.rgba32f','actual N extent')
        else:
            m['cases'][1]['pre_luma_edit_mae'] += .125
            expected=(1,'pre_luma_edit_mae')
        try:
            verify(a.capture,m)
        except AssertionError as error:
            assert error.args == (expected,), ('wrong negative-control failure',error.args)
            print('PASS exact composition negative control:',a.negative_control)
            return
        raise AssertionError('negative control was not detected')
    hashes=verify(a.capture)
    source=a.source.read_bytes()
    assert b'LoadPostArea' in source and b'LayerTone' in source
    manifest_path=a.source.parents[2]/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    digest=hashlib.sha256(source).hexdigest()
    assert digest == manifest['files']['core/src/nr_before_sr.cpp']
    report={'schema':'nr-composition-validation-v1','source_commit':manifest['source_commit'],
            'source_sha256':digest,'source_manifest_sha256':hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
            'contract_sha256':hashlib.sha256((a.capture/'contract.json').read_bytes()).hexdigest(),
            'planes':hashes,'warp_fixture_only':True,'nvidia_verified':False,'game_verified':False}
    fixture_root=Path(__file__).resolve().parents[1]
    report['fixture_commit']=subprocess.check_output(['git','-C',str(fixture_root),'rev-parse','HEAD'],text=True).strip()
    report['fixture_dirty']=bool(subprocess.check_output(['git','-C',str(fixture_root),'status','--porcelain','--untracked-files=no'],text=True).strip())
    report['fixture_files']={name:hashlib.sha256((fixture_root/name).read_bytes()).hexdigest() for name in (
        'scripts/validate-codec-gpu.ps1','scripts/validate-composition-contract.py',
        'tests/codec_gpu_harness.cpp.in','tests/composition_contract_gpu.inl')}
    (a.capture/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS 72 production pre/post pairs: actual N planes, alpha, dimensions, independent metrics, source manifest and SHA256.')


if __name__ == '__main__':
    main()
