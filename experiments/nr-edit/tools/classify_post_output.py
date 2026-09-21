"""Classify five preserved actual outputs; never edit, clamp, or admit Core work.

Domain evidence, finite storage, bounded-SDR range and destination clipping are
independent facts. Excursion bins are descriptive, not acceptance thresholds.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct

import prepare_post_capture as prepare


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def binding(base, candidate, proof, declared_domain='SdrEncodedFp16'):
    """Classify provenance; pixel range alone can never prove a colour domain."""
    if declared_domain != 'SdrEncodedFp16':
        return 'declared-domain-mismatch'
    expected = proof['transcode']
    actual = [sha(base), sha(candidate)]
    if actual == [expected['base']['output_sha256'], expected['candidate']['output_sha256']]:
        return 'verified-explicit-srgb-transcode'
    raw = proof['plane_hashes']
    if any(a == raw[role] and a != expected[name]['output_sha256']
           for a, role, name in zip(actual, ['source', 'composite'], ['base', 'candidate'])):
        return 'proven-linear-bytes-in-encoded-contract'
    return 'unverified-payload-domain'


def summarize(raw, width, height):
    require(width > 0 and height > 0 and len(raw) == width*height*8, 'FP16 RGBA shape mismatch')
    counts = dict(in_unit_interval=0, finite_excursion=0, nan=0, positive_infinity=0, negative_infinity=0)
    bins = dict(up_to_1_over_1024=0, over_1_over_1024_up_to_1_over_255=0,
                over_1_over_255_up_to_1_over_64=0, over_1_over_64=0)
    below = above = affected = nonfinite_pixels = alpha_nonfinite = alpha_outside = 0
    lo = hi = None
    excursions = []
    max_under = max_over = 0.0
    bbox = None
    examples = []
    for index, pixel in enumerate(struct.iter_unpack('<4e', raw)):
        outside = invalid = False
        for channel, value in enumerate(pixel[:3]):
            if not math.isfinite(value):
                counts['nan' if math.isnan(value) else 'positive_infinity' if value > 0 else 'negative_infinity'] += 1
                invalid = True
                continue
            lo = value if lo is None else min(lo, value)
            hi = value if hi is None else max(hi, value)
            excess = -value if value < 0 else value-1 if value > 1 else 0
            if excess:
                counts['finite_excursion'] += 1
                below += value < 0
                above += value > 1
                max_under = max(max_under, -value)
                max_over = max(max_over, value-1)
                excursions.append(excess)
                key = ('up_to_1_over_1024' if excess <= 1/1024 else
                       'over_1_over_1024_up_to_1_over_255' if excess <= 1/255 else
                       'over_1_over_255_up_to_1_over_64' if excess <= 1/64 else 'over_1_over_64')
                bins[key] += 1
                outside = True
                if len(examples) < 8:
                    examples.append(dict(x=index % width, y=index//width, channel=channel, value=value))
            else:
                counts['in_unit_interval'] += 1
        if outside:
            affected += 1
            x, y = index % width, index//width
            bbox = [x, y, x, y] if bbox is None else [min(bbox[0], x), min(bbox[1], y), max(bbox[2], x), max(bbox[3], y)]
        nonfinite_pixels += invalid
        alpha_nonfinite += not math.isfinite(pixel[3])
        alpha_outside += math.isfinite(pixel[3]) and not 0 <= pixel[3] <= 1
    excursions.sort()
    total = width*height*3
    require(sum(counts.values()) == total and sum(bins.values()) == counts['finite_excursion'], 'classification partition failed')
    nonfinite = counts['nan']+counts['positive_infinity']+counts['negative_infinity']
    numeric = ('nonfinite-output' if nonfinite or alpha_nonfinite else
               'finite-fp16-excursion' if excursions else 'finite-fp16-unit-interval')
    return dict(classification=numeric, rgb_channels=total, pixels=width*height, counts=counts,
                minimum=lo, maximum=hi, below_zero=below, above_one=above,
                finite_fp16_contract=nonfinite == 0 and alpha_nonfinite == 0,
                bounded_sdr_rgb_contract=nonfinite == 0 and not excursions,
                affected_pixels=affected, affected_pixel_fraction=affected/(width*height),
                affected_channel_fraction=len(excursions)/total, nonfinite_pixels=nonfinite_pixels,
                alpha_nonfinite=alpha_nonfinite, alpha_outside_unit_interval=alpha_outside,
                maximum_undershoot=max_under, maximum_overshoot=max_over,
                excursion_bins=bins, excursion_units='stored encoded code values; bins are not approval thresholds',
                excursion_p50=excursions[(len(excursions)-1)//2] if excursions else 0,
                excursion_p95=excursions[math.ceil(len(excursions)*.95)-1] if excursions else 0,
                affected_bbox_inclusive=bbox, first_examples=examples,
                hypothetical_unorm=dict(finite_channels_requiring_clipping=len(excursions),
                    maximum_clipping_loss=max(max_under, max_over),
                    mean_clipping_loss_over_all_rgb=sum(excursions)/total,
                    nonfinite_not_treated_as_valid=nonfinite,
                    scope='range clipping only; no UNORM conversion, quantization, transfer function or GPU store executed'))


def verify_blob(raw, identity):
    require(len(raw) == identity['bytes'] and sha(raw) == identity['sha256'], 'evidence-identity-mismatch')
    return raw


def verify_file(path, identity):
    return verify_blob(path.read_bytes(), identity)


def negative_controls(final, width, height, base, candidate, proof, capture):
    """Mutate only private in-memory copies of each same sample; no model A/B."""
    rows = {}
    with prepare.nr_replay.Frame(capture) as frame:
        raw_pair = []
        for role in ['source', 'composite']:
            plane = frame.header.planes[prepare.nr_replay.ROLES.index(role)]
            start = prepare.ctypes.sizeof(frame.header)+plane.offset
            raw_pair.append(bytes(frame.mapping[start:start+plane.bytes]))
    result = binding(*raw_pair, proof)
    require(result == 'proven-linear-bytes-in-encoded-contract', 'wrong-domain negative missed')
    require(all(0 <= v <= 1 for raw in raw_pair for pixel in struct.iter_unpack('<4e', raw) for v in pixel[:3]),
            'wrong-domain control must remain numerically bounded')
    rows['linear_relabelled_encoded'] = dict(result=result, numerically_bounded=True,
        source_hashes=[sha(raw) for raw in raw_pair])
    require(binding(base, candidate, proof, 'LinearSdr') == 'declared-domain-mismatch', 'declared-domain negative missed')
    rows['wrong_declaration'] = dict(result='declared-domain-mismatch')
    invalid = bytearray(final)
    struct.pack_into('<3H', invalid, 0, 0x7e00, 0x7c00, 0xfc00)
    stats = summarize(invalid, width, height)
    require(stats['classification'] == 'nonfinite-output' and
            all(stats['counts'][key] == 1 for key in ['nan', 'positive_infinity', 'negative_infinity']), 'nonfinite negative missed')
    rows['nan_positive_negative_inf'] = dict(result=stats['classification'], sha256=sha(invalid),
        modified_half_words={0: '7e00', 1: '7c00', 2: 'fc00'}, counts=stats['counts'])
    finite = bytearray(final)
    struct.pack_into('<3e', finite, 0, -1/1024, 1+1/1024, 1.125)
    stats = summarize(finite, width, height)
    require(stats['classification'] == 'finite-fp16-excursion' and stats['finite_fp16_contract'] and
            not stats['bounded_sdr_rgb_contract'] and stats['excursion_bins']['over_1_over_64'] >= 1,
            'finite excursion incorrectly treated as nonfinite/storage failure')
    rows['small_and_larger_finite_excursions'] = dict(result=stats['classification'], sha256=sha(finite),
        modified_rgb_pixel0=[-1/1024, 1+1/1024, 1.125], finite_fp16_contract=True, bounded_sdr_rgb_contract=False)
    changed = bytearray(base)
    changed[0] ^= 1
    require(binding(changed, candidate, proof) == 'unverified-payload-domain', 'modified input accepted as verified domain')
    rows['unproved_payload'] = dict(result='unverified-payload-domain', sha256=sha(changed))
    tampered = bytearray(final)
    tampered[0] ^= 1
    try:
        verify_blob(tampered, dict(bytes=len(final), sha256=sha(final)))
    except ValueError as error:
        require(str(error) == 'evidence-identity-mismatch', 'unrelated integrity negative failure')
        rows['evidence_byte_changed'] = dict(result=str(error), sha256=sha(tampered))
    else:
        raise ValueError('tampered evidence accepted')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', required=True, type=Path)
    parser.add_argument('--storage-audit', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    require(not args.output.exists(), 'fresh output required; preserve earlier evidence')
    args.output.mkdir(parents=True)
    report = dict(schema='post-output-contract-classification-v1', result='running', cases={},
        gpu_executed=False, model_evaluations=0, output_pixels_changed=0, fallback_pixels_written=0,
        production_admission_changed=False, core_integration_ready=False,
        scope='offline classification of exactly five existing actual NR/private-SR samples; no rerender, model A/B, clipping or fallback',
        tool_sha256=sha(Path(__file__).read_bytes()),
        validation_sha256=sha(args.validation.read_bytes()), storage_audit_sha256=sha(args.storage_audit.read_bytes()))
    destination = args.output/'classification.json'
    def save():
        destination.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    try:
        source = json.loads(args.validation.read_text())
        audit = json.loads(args.storage_audit.read_text())
        require(source['schema'] == 'actual-post-residual-sr-v1' and source['sources_unchanged'] and
                source['result'] == 'completed-with-numerical-refusals', 'unexpected actual-run receipt')
        require(audit['schema'] == 'actual-post-residual-storage-audit-v1' and
                audit['validation_sha256'] == report['validation_sha256'], 'storage audit not bound to this run')
        report['upstream_commit'] = source['upstream_commit']
        for number in range(1, 6):
            name = 'nr-'+str(number)
            row = source['cases'][name]
            folder = args.validation.parent/name
            require(row['exit_code'] == 4 and row['metrics']['real_private_sr_evaluations'] == 1, 'sample is not the original refused actual SR run')
            # Hash every recorded file, including binaries. Nothing is loaded/executed.
            for filename, identity in row['files'].items():
                verify_file(folder/filename, identity)
            proof = json.loads((folder/'pair-proof.json').read_text())
            capture, validation = Path(proof['capture']), Path(proof['validation'])
            require(sha(validation.read_bytes()) == proof['validation_sha256'], 'producer receipt changed')
            out = args.output/name
            out.mkdir()
            # Reuse the audited original producer/transcode gate, not a new guessed encoding.
            regenerated = prepare.prepare(capture, validation, out, True)
            require(regenerated['result'] == 'prepared-offline-transcoded-pair' and
                    regenerated['package']['sha256'] == proof['package']['sha256'], 'source/transcode proof changed')
            pair = (folder/'pair.bin').read_bytes()
            require(pair == (out/'pair.bin').read_bytes(), 'pair reproduction mismatch')
            header = prepare.PACKAGE_HEADER.unpack(pair[:64])
            magic, version, width, height, nw, nh, kind, stream, epoch, sequence, domain, reserved = header
            require(magic == b'NRPOST1\0' and version == domain == 1 and not kind and not reserved and
                    (nw, nh, width, height) == tuple(row['metrics']['shape']), 'package header mismatch')
            require((stream, epoch, sequence) == tuple(proof['package']['source'][key] for key in ['stream', 'epoch', 'sequence']), 'source identity mismatch')
            length = width*height*8
            require(len(pair) == 64+2*length, 'pair length mismatch')
            base, candidate = pair[64:64+length], pair[64+length:]
            domain_result = binding(base, candidate, proof)
            require(domain_result == 'verified-explicit-srgb-transcode', 'positive domain gate refused')
            final = (folder/'final.fp16').read_bytes()
            stats = summarize(final, width, height)
            previous = audit['cases'][name]['output']
            require(stats['below_zero'] == previous['below_zero'] and stats['above_one'] == previous['above_one'] and
                    stats['minimum'] == previous['minimum'] and stats['maximum'] == previous['maximum'] and
                    stats['counts']['finite_excursion'] == row['metrics']['outside_sdr_channels'], 'independent stored range count mismatch')
            require((folder/'base-after.fp16').read_bytes() == base, 'base changed')
            require(final[6::8] == base[6::8] and final[7::8] == base[7::8], 'alpha changed')
            # The recorded real SR carrier is independently distinguished from final colour.
            carrier = summarize((folder/'sr-output.fp16').read_bytes(), width, height)
            require(carrier['counts']['finite_excursion'] == row['metrics']['outside_carrier_channels'], 'carrier count mismatch')
            negatives = negative_controls(final, width, height, base, candidate, proof, capture)
            report['cases'][name] = dict(shape=[nw, nh, width, height], domain_evidence=domain_result,
                input_pair_reproduced=True, final_sha256=sha(final), original_exit_code=4,
                output=stats, sr_carrier=carrier, base_unchanged=True, alpha_unchanged=True,
                negative_controls=negatives, spatial_controls_from_bound_audit=audit['cases'][name]['spatial_controls'],
                interpretation='finite stored excursions; no evidence here of wrong input encoding or nonfinite output; extended SDR destination semantics remain unapproved')
            print(name, stats['classification'], 'outside_channels='+str(stats['counts']['finite_excursion']),
                  'affected_pixels='+str(stats['affected_pixels']), flush=True)
            save()
        report.update(result='classification-completed', same_actual_samples=5,
                      negative_control_cases=30, unique_negative_control_mechanisms=6)
        save()
        return 0
    except Exception as error:
        report.update(result='failed', failure=str(error))
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())
