#!/usr/bin/env python3
"""Independently validate actual N-sized log/valid fields and production A/B.

Without --capture, check the production wiring/math contract only. This is not
NVIDIA inference or game/performance evidence.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def wiring():
    s=(ROOT/'src/nr_before_sr.cpp').read_text(encoding='utf-8-sig')
    assert 'bool low_grid_edit_field = false;' in s
    assert 'a.low_grid_edit_field == b.low_grid_edit_field &&' in s
    assert 'L"LowGridEditField", 0, g_ini_path' in s
    assert 'WriteIniInt(L"LowGridEditField", config.low_grid_edit_field ? 1 : 0)' in s
    build=s.split('void BuildEditFieldCS(',1)[1].split('float2 SampleEditFieldV5',1)[0]
    assert 'IsFiniteModelStoredV2' in build and '1.0 / 4096.0' in build
    for forbidden in ['OriginalColor','EdgeGuard','LightingLock','DetailStability','TransferStrength']:
        assert forbidden not in build,('full-res protection moved into N pass',forbidden)
    sample=s.split('float2 SampleEditFieldV5(',1)[1].split('float ModelLumaRatioV2',1)[0]
    assert all(f'{v}.y != 1.0' in sample for v in 'abcd')
    assert 'return float2(0.0,0.0)' in sample
    assert 'slot.edit_field_allocation_failed || !CanMutateSlotLocked(slot)' in s
    assert 'g_edit_field_allocated_bytes -= slot.edit_field_bytes;' in s
    assert 'g_config.color_strength <= 0.000001f && slot.edit_field_recorded' in s
    assert 'context->successful_evaluations_since_reset >= g_config.warmup_after_create' in s
    body=s.split('if (edit_field_ready) {',1)[1].split('DispatchCodec(command_list, *slot, g_codec.combine_v2',1)[0]
    assert body.index('g_codec.build_edit_field')<body.index('UavBarrier(command_list,slot->edit_field)')<body.index('slot->edit_field_recorded = true')
    # Public budgets cover actual allocation sizes (including alignment), not
    # merely estimated width*height, and reject rather than wrap at extremes.
    assert 'allocation.SizeInBytes > kEditFieldTotalBudget - g_edit_field_allocated_bytes' in s
    print('PASS EditField production opt-in, strict validity, full-res guards and lifetime/budget wiring')


def verify(root):
    m=json.loads((root/'edit-field-contract.json').read_text())
    assert m['schema']=='nr-edit-field-fixture-v1' and m['cases_total']==64
    assert m['field_format']=='R32G32_FLOAT' and m['color_strength']==0
    assert not m['nvidia_inference'] and not m['game_capture']
    hashes={};total=0;different=0;field_samples=0
    def read(name,components,extent):
        nonlocal total
        p=root/name;raw=p.read_bytes();total+=len(raw)
        assert len(raw)==math.prod(extent)*components*4,(name,'extent')
        hashes[name]=hashlib.sha256(raw).hexdigest()
        return list(struct.iter_unpack('<'+'f'*components,raw))
    def decode(v,flags):
        if flags&1:return v
        return v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
    y=lambda v:.2126*v[0]+.7152*v[1]+.0722*v[2]
    for c in m['cases']:
        prefix=f"case-{c['id']}-"
        p={n:read(prefix+n+'.rgba32f',4,extent) for n,extent in
           [('original',m['source_texture']),('proxy',c['N']),('neural',c['N']),
            ('a',m['output_texture']),('b',m['output_texture'])]}
        field=read(prefix+'edit.rg32f',2,c['N'])
        for pv,nv,(edit,valid) in zip(p['proxy'],p['neural'],field):
            finite=all(math.isfinite(v) and abs(v)<=65504 for v in (*pv[:3],*nv[:3]))
            if not finite:
                assert (edit,valid)==(0,0),(c['id'],'invalid field encoding')
            else:
                pl=max(y([decode(v,c['codec_flags']) for v in pv[:3]]),m['epsilon'])
                nl=max(y([decode(v,c['codec_flags']) for v in nv[:3]]),m['epsilon'])
                expected=max(-8,min(8,math.log2(nl/pl)))
                assert valid==1 and math.isclose(edit,expected,rel_tol=3e-5,abs_tol=2e-6),(c['id'],'raw N log oracle',edit,expected)
            field_samples+=1
        delta=0
        for yy in range(13):
            for xx in range(13):
                a,b=(p[n][(yy+1)*17+xx+2] for n in ('a','b'))
                o=p['original'][(yy+2)*18+xx+3]
                assert all(math.isfinite(v) for v in (*a,*b)) and a[3]==b[3]==o[3]
                if c['strength']==0 or c['fixture']==1:assert b==o,(c['id'],'identity')
                delta+=sum(abs(a[i]-b[i]) for i in range(3))/3
        assert math.isclose(delta/169,c['a_b_rgb_mae'],rel_tol=2e-6,abs_tol=2e-8),(c['id'],'metric')
        if c['N']==[13,13]:assert delta/169<2e-6,(c['id'],'same grid reference')
        different+=delta>1e-5
    assert total==m['plane_bytes']<=m['byte_budget']==4194304
    assert different>0,'Interpolation order difference was not demonstrated'
    return m,hashes,field_samples,different


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture',type=Path)
    ap.add_argument('--source',type=Path)
    a=ap.parse_args();wiring()
    if not a.capture:return
    assert a.source,'Generated runtime source is required'
    m,hashes,samples,different=verify(a.capture)
    manifest_path=a.source.parents[2]/'source-manifest.json'
    manifest=json.loads(manifest_path.read_text());source_sha=hashlib.sha256(a.source.read_bytes()).hexdigest()
    assert source_sha==manifest['files']['core/src/nr_before_sr.cpp']
    report={'source_commit':manifest['source_commit'],'source_sha256':source_sha,
            'manifest_sha256':hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
            'fixture_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
            'fixture_dirty':bool(subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain','--untracked-files=no'],text=True).strip()),
            'contract_sha256':hashlib.sha256((a.capture/'edit-field-contract.json').read_bytes()).hexdigest(),
            'field_samples':samples,'different_cases':different,'plane_bytes':m['plane_bytes'],
            'planes':hashes,'nvidia_verified':False,'game_verified':False}
    (a.capture/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS 64 actual EditField A/B pairs, {samples} independently recomputed N values, {different} differences')


if __name__=='__main__':main()
