"""Evaluate *measured* paired NR GPU timings; never infer speed from launch count.
Input JSON: {"samples":[{"pair":"...", "identity":{...}, "variant":"fp8|candidate",
"nr_gpu_ms":number, "covered_fp8_ms":number (fp8 only), "warmup":false}]}
identity must include gpu, driver, runtime_sha256, shape, mode, settings.
covered_fp8_ms must be the UNION of disjoint replaced regions, not sums of
nested ranges. Measurements must exclude warmup and use identical GPU clocks.
"""
from __future__ import annotations
import argparse
import json
import math
import statistics
from pathlib import Path


def positive(value, name, allow_zero=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name}: expected number")
    if not math.isfinite(value) or (value < 0 if allow_zero else value <= 0):
        raise ValueError(f"{name}: invalid timing")
    return float(value)


def analyze(data):
    pairs = {}
    identity = None
    for sample in data['samples']:
        if sample.get('warmup') is not False:
            raise ValueError('every sample must explicitly be non-warmup')
        ident = sample['identity']
        for key in ('gpu', 'driver', 'runtime_sha256', 'shape', 'mode', 'settings'):
            if key not in ident or ident[key] in (None, ''):
                raise ValueError('missing identity ' + key)
        encoded = json.dumps(ident, sort_keys=True)
        if identity is not None and encoded != identity:
            raise ValueError('mixed GPU/driver/runtime/shape/settings; split reports')
        identity = encoded
        key, variant = sample['pair'], sample['variant']
        if variant not in ('fp8', 'candidate'):
            raise ValueError('invalid variant')
        pair = pairs.setdefault(key, {})
        if variant in pair:
            raise ValueError('duplicate pair variant')
        pair[variant] = sample
    if len(pairs) < 5:
        raise ValueError('need at least five paired runs, not correlated frames')
    savings, coverage, base_times, new_times = [], [], [], []
    for pair in pairs.values():
        if set(pair) != {'fp8', 'candidate'}:
            raise ValueError('incomplete pair')
        base = positive(pair['fp8']['nr_gpu_ms'], 'baseline')
        new = positive(pair['candidate']['nr_gpu_ms'], 'candidate')
        covered = positive(pair['fp8']['covered_fp8_ms'], 'coverage', True)
        if covered > base:
            raise ValueError('coverage exceeds NR time; overlapping ranges?')
        base_times.append(base); new_times.append(new)
        savings.append((base-new)/base)
        coverage.append(covered/base)
    med = statistics.median(savings)
    return {
        'scope': 'paired full NR GPU wall time; NOT game FPS or quality acceptance',
        'runs': len(pairs),
        'baseline_nr_ms_median': statistics.median(base_times),
        'candidate_nr_ms_median': statistics.median(new_times),
        'paired_fraction_saved_median': med,
        'paired_fraction_saved_min': min(savings),
        'paired_fraction_saved_max': max(savings),
        'covered_fraction_median': statistics.median(coverage),
        'ideal_fraction_saved_at_2x_zero_overhead': statistics.median(coverage)/2,
        'timing_only_all_pairs_improve': min(savings) > 0,
        'runtime_admission': False,
        'reason': 'quality, tensor semantics, lifetime and repeatability remain separate gates',
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(analyze(json.loads(args.input.read_text(encoding='utf-8'))), indent=2))
    except (ValueError, KeyError, TypeError, OSError) as e:
        parser.exit(2, f'input rejected: {e}\n')
