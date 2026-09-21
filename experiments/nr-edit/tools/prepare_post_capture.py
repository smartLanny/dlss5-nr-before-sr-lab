"""Audit one actual Present NRF; explicitly opt in to an offline SDR transcode.

Captured placeholder guides are NEVER promoted to native SR guides. The opt-in
uses a separate artificial stationary plane (hardware depth .5, zero motion).
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import math
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'scripts'))
import nr_replay

PACKAGE_HEADER = struct.Struct('<8s6I3Q2I')
assert PACKAGE_HEADER.size == 64


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def write_pair(path, width, height, nw, nh, base, candidate, kind=0, frame=1):
    require(len(base) == len(candidate) == width*height*8, 'FP16 pair payload size')
    stream = int(sha(base+candidate)[:16], 16) or 1
    header = PACKAGE_HEADER.pack(b'NRPOST1\0', 1, width, height, nw, nh, kind,
                                 stream, 1, frame, 1, 0)
    path.write_bytes(header+base+candidate)
    return dict(file=path.name, sha256=sha(path.read_bytes()),
                base_sha256=sha(base), candidate_sha256=sha(candidate),
                source=dict(stream=stream, epoch=1, sequence=frame),
                source_scope='offline replay identity; not a native game timeline')


def transcode(raw):
    values = [p[0] for p in struct.iter_unpack('<e', raw)]
    nonfinite = sum(not math.isfinite(v) for v in values)
    outside = sum(math.isfinite(v) and not 0 <= v <= 1 for v in values)
    finite = [v for v in values if math.isfinite(v)]
    stats = dict(nonfinite_count=nonfinite, out_of_range_count=outside, clipped_count=0,
                 input_sha256=sha(raw), input_min=min(finite) if finite else None,
                 input_max=max(finite) if finite else None)
    if nonfinite or outside:
        return None, stats
    out = bytearray(len(raw))
    error = 0.0
    for i, value in enumerate(values):
        encoded = value if i % 4 == 3 else 12.92*value if value <= .0031308 else 1.055*value**(1/2.4)-.055
        struct.pack_into('<e', out, i*2, encoded)
        error = max(error, abs(struct.unpack_from('<e', out, i*2)[0]-encoded))
    stats.update(output_sha256=sha(out), max_fp16_store_error=error)
    return bytes(out), stats


def prepare(capture, validation, out, opt_in):
    receipt = json.loads(validation.read_text(encoding='utf-8-sig'))
    require(receipt.get('result') == 0 and not receipt.get('compile_only'), 'actual model validation did not pass')
    require(receipt.get('schema') == 'real-ngx-present-host-v1', 'unreviewed capture producer')
    row = receipt.get('present_nrf_captures', {}).get(capture.name)
    require(row and row['sha256'] == sha(capture.read_bytes()), 'capture hash missing or mismatched')
    metadata_path = capture.with_suffix('.json')
    metadata_raw = metadata_path.read_bytes()
    metadata = json.loads(metadata_raw)
    meta_row = receipt.get('present_nrf_metadata', {}).get(metadata_path.name)
    require(meta_row and meta_row['sha256'] == sha(metadata_raw), 'metadata hash missing or mismatched')
    require(metadata.get('schema') == 'present-nr-observed-frame-v1' and metadata.get('complete') is True,
            'capture was not a completed observed frame')
    match = re.fullmatch(r'capture-p([1-5])-s([0-9]+)-layer([1-5])\.nrf', capture.name)
    require(match is not None and match[1] == match[3], 'select the actual final requested NR layer')
    require(metadata['captured_auxiliary_layer'] == int(match[3]), 'layer identity mismatch')
    core = Path(receipt['core'])
    require(sha((core/'src/nr_before_sr.cpp').read_bytes()) == receipt['core_source_sha256'], 'producer Core changed')
    for name in ['nr_present_capture.inl', 'nr_compat_present_models.inl']:
        require(sha((core/'src'/name).read_bytes()) == receipt['present_source'][name], 'producer module changed: '+name)
    runtime = validation.parent/'nvngx_dlssnr.dll'
    require(sha(runtime.read_bytes()) == receipt['artifacts']['nvngx_dlssnr.dll'], 'signed NR runtime receipt mismatch')
    report = dict(schema='post-nr-offline-pair-v1', capture=str(capture), capture_sha256=row['sha256'],
                  metadata_sha256=sha(metadata_raw), validation=str(validation), validation_sha256=sha(validation.read_bytes()),
                  producer_core_sha256=receipt['core_source_sha256'], nr_runtime_sha256=sha(runtime.read_bytes()),
                  raw_capture_admission='refused: LinearSdr and color-only placeholder guides',
                  native_guides='absent; captured depth/motion are not admitted',
                  scope='offline numerical/storage study from actual NR pixels; no Core/game/temporal-motion validation')
    with nr_replay.Frame(capture) as f:
        h = f.header
        require(h.completion == 1 and h.frame > 0 and h.evaluation > 0 and h.instance > 0,
                'missing actual frame/evaluation identity')
        require(h.instance == metadata['auxiliary_instance'] and h.frame == metadata['parent_evaluation'] and
                h.sequence == (metadata['request'] << 8) | (int(match[3])-1), 'same-frame metadata mismatch')
        require(h.color_mode == 1 and metadata['model_domain'] == 1, 'only explicitly declared LinearSdr capture may transcode')
        scalars = nr_replay.scalars(h)
        for name, expected in [('DLSS.Feature.Create.Flags', 0), ('DLSS.Pre.Exposure', 1), ('DLSS.Exposure.Scale', 1), ('DLSSNR.Reset', 1)]:
            require(scalars.get(name, {}).get('present') and scalars[name]['value'] == expected, 'SDR scalar proof missing: '+name)
        width, height, nw, nh = h.source_width, h.source_height, h.work_width, h.work_height
        require(nw <= width and nh <= height and nw*4 >= width and nh*4 >= height, 'actual shape fails strict 25%-100% transport contract')
        raw_planes = {}
        for role in ['source', 'composite', 'proxy', 'neural']:
            p = h.planes[nr_replay.ROLES.index(role)]
            require(p.storage_format == p.view_format == 10 and p.encoding == 2 and p.channels == 4 and p.x == p.y == 0,
                    'not complete typed FP16 '+role)
            if role in ['source', 'composite']:
                require((p.width, p.height) == (width, height), 'B/Z full-grid mismatch')
            raw_planes[role] = bytes(f.mapping[ctypes.sizeof(h)+p.offset:ctypes.sizeof(h)+p.offset+p.bytes])
        require(raw_planes['proxy'] != raw_planes['neural'], 'real captured NR output is not distinguishable from input')
        report.update(full_extent=[width, height], work_extent=[nw, nh], captured_frame=h.frame,
                      captured_instance=h.instance, captured_sequence=h.sequence,
                      captured_source_commit=bytes(h.source_commit).decode('ascii'),
                      plane_hashes={role: sha(raw) for role, raw in raw_planes.items()})
        base, bs = transcode(raw_planes['source'])
        candidate, zs = transcode(raw_planes['composite'])
        report['transcode'] = dict(operation='IEC sRGB encode RGB, retain alpha, nearest-even FP16 store; no clipping', base=bs, candidate=zs)
        report['result'] = 'refused'
        if base is None or candidate is None:
            report['reason'] = 'nonfinite or out-of-range input; no saturation permitted'
        elif not opt_in:
            report['reason'] = 'explicit --offline-static-planar is required; raw capture is not admissible'
        else:
            report['geometry'] = dict(kind='artificial stationary plane', depth=.5, depth_encoding='hardware non-inverted',
                                      motion=[0, 0], motion_units='working-grid pixels', jitter=[0, 0], reset=True,
                                      captured_native_guides=False, moving_sequence=False)
            report['package'] = write_pair(out/'pair.bin', width, height, nw, nh, base, candidate, frame=h.frame)
            report['result'] = 'prepared-offline-transcoded-pair'
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--validation', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--offline-static-planar', action='store_true')
    a = p.parse_args()
    out = a.output.resolve()
    require(not out.exists(), 'fresh output required')
    out.mkdir(parents=True)
    try:
        report = prepare(a.capture.resolve(strict=True), a.validation.resolve(strict=True), out, a.offline_static_planar)
    except Exception as error:
        report = dict(schema='post-nr-offline-pair-v1', result='refused', reason=str(error))
    (out/'pair-proof.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['result'] == 'prepared-offline-transcoded-pair' else 2


if __name__ == '__main__':
    raise SystemExit(main())
