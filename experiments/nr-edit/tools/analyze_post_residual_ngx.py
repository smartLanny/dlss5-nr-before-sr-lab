"""Audit stored actual SR output without clipping, modifying or rerunning it."""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rgb(raw):
    return [v for i, (v,) in enumerate(struct.iter_unpack('<e', raw)) if i % 4 != 3]


def statistics(values):
    finite = [v for v in values if math.isfinite(v)]
    return dict(minimum=min(finite), maximum=max(finite), nonfinite=len(values)-len(finite),
                below_zero=sum(v < 0 for v in finite), above_one=sum(v > 1 for v in finite))


def spatial_controls(folder, base, actual, nw, nh, width, height):
    """Same recorded low-grid edit; replace only the model's reconstruction.

    First retain the FP32 residual in spatial interpolation; second also store
    the interpolated carrier in FP16 as real SR does. CPU double intermediates
    and nearest-even FP16 storage: this is an offline reference, not a GPU run.
    """
    carriers = list(struct.iter_unpack('<4e', (folder/'carrier.fp16').read_bytes()))
    tails = list(struct.iter_unpack('<4f', (folder/'tail.fp32').read_bytes()))
    if len(carriers) != nw*nh or len(tails) != nw*nh or any(t[3] < .5 for t in tails):
        raise ValueError('unreviewed invalid low-grid support')
    half = lambda v: struct.unpack('<e', struct.pack('<e', v))[0]
    phases = []
    for full, work in [(width, nw), (height, nh)]:
        axis = []
        for coordinate in range(full):
            whole, rest = divmod((2*coordinate+1)*work+full, 2*full)
            origin = whole-1
            axis.append((max(0, min(work-1, origin)), max(0, min(work-1, origin+1)), rest/(2*full)))
        phases.append(axis)
    smooth, stored = [], []
    for y, (y0, y1, fy) in enumerate(phases[1]):
        for x, (x0, x1, fx) in enumerate(phases[0]):
            taps = [(y0*nw+x0, (1-fx)*(1-fy)), (y0*nw+x1, fx*(1-fy)),
                    (y1*nw+x0, (1-fx)*fy), (y1*nw+x1, fx*fy)]
            support = any(weight > 0 and tails[index][3] >= 1.5 for index, weight in taps)
            for c in range(3):
                carrier = sum(carriers[index][c]*weight for index, weight in taps)
                tail = sum(tails[index][c]*weight for index, weight in taps)
                b = base[(y*width+x)*3+c]
                smooth.append(half(b+((2*carrier-1)*4 if support else 0)+tail))
                stored.append(half(b+((2*half(carrier)-1)*4 if support else 0)+tail))
    controls = {}
    for name, values in [('spatial_fp32_residual', smooth), ('spatial_fp16_carrier', stored)]:
        def outside(v): return not 0 <= v <= 1
        controls[name] = dict(output=statistics(values),
            actual_outside_and_reference_inside=sum(outside(a) and not outside(b) for a,b in zip(actual,values)),
            actual_inside_and_reference_outside=sum(not outside(a) and outside(b) for a,b in zip(actual,values)),
            both_outside=sum(outside(a) and outside(b) for a,b in zip(actual,values)),
            actual_minus_reference_abs_mean=sum(abs(a-b) for a,b in zip(actual,values))/len(values),
            actual_minus_reference_abs_max=max(abs(a-b) for a,b in zip(actual,values)))
    return controls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('fresh output required')
    source = json.loads(args.validation.read_text())
    report = dict(schema='actual-post-residual-storage-audit-v1', validation_sha256=sha(args.validation),
                  analysis_sha256=sha(Path(__file__)), carrier_range=4, cases={},
                  scope='stored RGB and artificial-guide audit; no clipping, temporal quality or game claim',
                  spatial_reference='same stored low-grid carrier/tail, rational half-pixel bilinear, range 4; CPU double arithmetic and nearest-even FP16 final store; not a GPU/model evaluation')
    for name, recorded in source['cases'].items():
        folder = args.validation.parent/name
        for file, identity in recorded['files'].items():
            if sha(folder/file) != identity['sha256']:
                raise ValueError('recorded file changed: '+str(folder/file))
        package = (folder/'pair.bin').read_bytes()
        nw, nh, width, height = recorded['metrics']['shape']
        length = width*height*8
        base, candidate = rgb(package[64:64+length]), rgb(package[64+length:])
        output = rgb((folder/'final.fp16').read_bytes())
        sr = rgb((folder/'sr-output.fp16').read_bytes())
        carrier = rgb((folder/'carrier.fp16').read_bytes())
        depth = [v for (v,) in struct.iter_unpack('<f', (folder/'artificial-depth.fp32').read_bytes())]
        motion = [v for (v,) in struct.iter_unpack('<f', (folder/'artificial-motion.fp32').read_bytes())]
        if len(depth) != nw*nh or any(v != .5 for v in depth) or len(motion) != nw*nh*2 or any(v != 0 for v in motion):
            raise ValueError('stored artificial geometry mismatch')
        residual = [z-b for b, z in zip(base, candidate)]
        transport = [z-b for b, z in zip(base, output)]
        row = dict(base=statistics(base), candidate=statistics(candidate), output=statistics(output),
                   sr_output=statistics(sr), carrier=statistics(carrier), artificial_guides_exact=True,
                   original_residual_abs_mean=sum(map(abs, residual))/len(residual),
                   transported_residual_abs_mean=sum(map(abs, transport))/len(transport),
                   change_from_candidate_abs_mean=sum(abs(a-b) for a,b in zip(output,candidate))/len(output),
                   sr_max_offset_from_neutral=max(abs(v-.5) for v in sr),
                   output_clipped_count=0, result=recorded['result'])
        if row['output']['below_zero']+row['output']['above_one'] != recorded['metrics']['outside_sdr_channels']:
            raise ValueError('independent range count mismatch')
        row['spatial_controls'] = spatial_controls(folder, base, output, nw, nh, width, height)
        report['cases'][name] = row
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
